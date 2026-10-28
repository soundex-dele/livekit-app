import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdirSync,writeFileSync,readFileSync,existsSync} from 'node:fs';
import {resolve,dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {randomBytes} from 'node:crypto';
import os from 'node:os';
import {chromium} from 'playwright';

const here=dirname(fileURLToPath(import.meta.url));const root=resolve(here,'../../../../..');
const output=resolve(root,'livekit-app/out/live-studio/integration-'+Date.now());mkdirSync(output,{recursive:true});
const mediaPort=Number(process.env.STUDIO_MEDIA_PORT||19080),apiPort=Number(process.env.STUDIO_API_PORT||19081);
const base=`http://127.0.0.1:${apiPort}`,media=`http://127.0.0.1:${mediaPort}`;
const secret=randomBytes(24).toString('hex'),admission=randomBytes(24).toString('hex'),password=randomBytes(16).toString('hex');
const config={bind:'127.0.0.1',port:mediaPort,keys:{studio:secret},max_connections:256,rtc:{port_range_start:51000,port_range_end:51999}};
writeFileSync(resolve(output,'media.json'),JSON.stringify(config));
const children=[],timers=[];let browser;const report={checks:[],result:'running'};
function launch(exe,args,name,env={}){const child=spawn(exe,args,{cwd:root,windowsHide:true,env:{...process.env,...env}});let log='';child.stdout?.on('data',b=>{log+=b;});child.stderr?.on('data',b=>{log+=b;});child.on('error',e=>{log+=String(e);});children.push({child,name,log:()=>log});return child;}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn,label,timeout=30000){const deadline=Date.now()+timeout;let last;while(Date.now()<deadline){try{const v=await fn();if(v)return v;}catch(e){last=e;}await sleep(200);}throw new Error(`Timeout: ${label}: ${last??''}`);}
async function api(path,body,token){const response=await fetch(base+path,{method:body===undefined?'GET':'POST',headers:{'Content-Type':'application/json','X-AV-Client':'desktop',...(token?{Authorization:`Bearer ${token}`}:{})},body:body===undefined?undefined:JSON.stringify(body)});const result=await response.json();if(!response.ok)throw new Error(`${response.status}: ${JSON.stringify(result)}`);return result;}
function passed(name){report.checks.push(name);console.log('PASS',name);}
const sdkPath=resolve(here,'../node_modules/livekit-client/dist/livekit-client.umd.js');
async function publisher(name){
 if(process.env.STUDIO_NATIVE==='1'){
  const child=launch(resolve(root,'livekit-app/out/live-studio-qt6/Release/studio_publisher_fixture.exe'),['--url',base,'--name',name,'--duration',String(Number(process.env.STUDIO_DURATION_SECONDS||180)+300),'--settings',resolve(output,name+'-settings'),'--record',resolve(output,name+'.mp4')],name,{STUDIO_FIXTURE_PASSWORD:password});
  const entry=children.at(-1);
  const ready=await until(()=>{for(const line of entry.log().split('\n')){try{const value=JSON.parse(line);if(value.event==='ready')return value;}catch{}}if(child.exitCode!==null)throw new Error(entry.log());},'native publisher '+name,60000);
  const account=await api('/api/v1/auth/login',{name,password});return {token:account.session_token,account,broadcast:{id:ready.broadcast,room:ready.room},prefix:`/api/v1/live-studio/broadcasts/${ready.broadcast}/`,child};
 }

 const account=await api('/api/v1/auth/register',{name,password});const token=account.session_token;const broadcast=await api('/api/v1/live-studio/start',{title:`直播 ${name}`},token);const prefix=`/api/v1/live-studio/broadcasts/${broadcast.id}/`;
 const credentials=await api(prefix+'connect',{},token);const page=await browser.newPage();await page.goto(base+'/apps/live-studio/');await page.addScriptTag({path:sdkPath});
 await page.evaluate(async c=>{const lk=window.LivekitClient;const canvas=document.createElement('canvas');canvas.width=1280;canvas.height=720;const ctx=canvas.getContext('2d');let index=0;window.paint=setInterval(()=>{ctx.fillStyle=`hsl(${index%360},55%,30%)`;ctx.fillRect(0,0,1280,720);ctx.fillStyle='white';ctx.font='60px sans-serif';ctx.fillText(`FRAME ${index++}`,80,180);ctx.fillRect((index*8)%1100,400,150,150);},33);const audio=new AudioContext();const oscillator=audio.createOscillator();oscillator.frequency.value=440;const gain=audio.createGain();gain.gain.value=.15;const dest=audio.createMediaStreamDestination();oscillator.connect(gain).connect(dest);oscillator.start();await audio.resume();window.audioContext=audio;window.hostRoom=new lk.Room();await window.hostRoom.connect(c.media_url,c.media_token);await window.hostRoom.localParticipant.publishTrack(canvas.captureStream(30).getVideoTracks()[0],{videoCodec:'vp8'});await window.hostRoom.localParticipant.publishTrack(dest.stream.getAudioTracks()[0]);},credentials);
 await api(prefix+'heartbeat',{connected:true},token);const heartbeat=setInterval(()=>api(prefix+'heartbeat',{connected:true},token).catch(()=>{}),3000);timers.push(heartbeat);return {page,token,account,broadcast,prefix,heartbeat};
}
async function capacity(){
 const count=Number(process.env.STUDIO_ROOM_COUNT||5),viewers=Number(process.env.STUDIO_VIEWERS_PER_ROOM||20),seconds=Number(process.env.STUDIO_DURATION_SECONDS||1800);
 assert.ok(count>=1&&count<=5&&viewers>=1&&viewers<=20&&seconds>=10);
 report.capacity={rooms:count,viewers_per_room:viewers,duration_seconds:seconds,hardware:{cpu:os.cpus()[0]?.model,logical_processors:os.cpus().length,memory_bytes:os.totalmem(),network:'loopback; publishers, SFU and all decoders share this machine'},samples:[]};
 const pages=[];
 for(let room=0;room<count;room++){
  const host=await publisher('capacity_'+room);
  const page=await browser.newPage({viewport:{width:1280,height:720}});await page.goto(base+'/apps/live-studio/');await page.addScriptTag({path:sdkPath});
  await page.evaluate(()=>{document.body.replaceChildren();window.receivers=[];});
  for(let i=0;i<viewers;i++){
   const cookie='av_visitor='+randomBytes(24).toString('hex');
   const command=action=>fetch(base+host.prefix+action,{method:'POST',headers:{'Content-Type':'application/json',Cookie:cookie},body:'{}'});
   const response=await command('connect');assert.equal(response.status,200);const credentials=await response.json();
   timers.push(setInterval(()=>command('presence').catch(()=>{}),15000));
   await page.evaluate(async c=>{const r=new window.LivekitClient.Room({adaptiveStream:false});r.on('trackSubscribed',track=>{const el=track.attach();el.autoplay=true;el.muted=true;el.style.width='200px';document.body.append(el);void el.play();});await r.connect(c.media_url,c.media_token,{autoSubscribe:true});window.receivers.push(r);},credentials);
  }
  pages.push(page);console.log(`CAPACITY connected room ${room+1}/${count}: ${viewers} receivers`);
 }
 const sample=async()=>Promise.all(pages.map(page=>page.evaluate(async()=>{const result=[];for(const room of window.receivers){for(const participant of room.remoteParticipants.values())for(const publication of participant.trackPublications.values()){const track=publication.track;if(!track)continue;const stats=await track.getRTCStatsReport();stats?.forEach(s=>{if(s.type==='inbound-rtp')result.push({kind:s.kind,frames:s.framesDecoded??0,bytes:s.bytesReceived??0,timestamp:s.timestamp,playout:s.estimatedPlayoutTimestamp??null,width:s.frameWidth,height:s.frameHeight,packetsLost:s.packetsLost??0});});}}return result;})));
 await until(async()=>{const all=await sample();return all.every(rows=>rows.filter(s=>s.kind==='video'&&s.frames>10).length===viewers);},'all capacity receivers decode',90000);
 const cpuTimes=()=>os.cpus().reduce((a,c)=>({idle:a.idle+c.times.idle,total:a.total+Object.values(c.times).reduce((n,t)=>n+t,0)}),{idle:0,total:0});const initial=await sample(),started=Date.now();let previous=initial,cpuPrevious=cpuTimes();
 while(Date.now()-started<seconds*1000){await sleep(Math.min(10000,seconds*1000-(Date.now()-started)));const next=await sample();const elapsed=(Date.now()-started)/1000;let minimum=10000,bandwidth=0,decoded=0;for(let room=0;room<count;room++){const video=next[room].filter(s=>s.kind==='video'),old=previous[room].filter(s=>s.kind==='video');for(let i=0;i<video.length;i++){if(old[i])minimum=Math.min(minimum,(video[i].frames-old[i].frames)/((video[i].timestamp-old[i].timestamp)/1000));decoded++;}for(let i=0;i<next[room].length;i++){const v=next[room][i],p=previous[room][i];if(p)bandwidth+=(v.bytes-p.bytes)*8/((v.timestamp-p.timestamp)/1000);}}
  const cpuNow=cpuTimes(),cpuUsage=100*(1-(cpuNow.idle-cpuPrevious.idle)/(cpuNow.total-cpuPrevious.total));cpuPrevious=cpuNow;report.capacity.samples.push({elapsed,minimum_receiver_fps:minimum,decoded_receivers:decoded,total_received_bps:bandwidth,system_cpu_percent:cpuUsage,system_memory_used_bytes:os.totalmem()-os.freemem()});previous=next;writeFileSync(resolve(output,'capacity-progress.json'),JSON.stringify(report.capacity,null,2));console.log(`CAPACITY ${Math.round(elapsed)}s: receivers=${decoded}, min fps=${minimum.toFixed(1)}, ${(bandwidth/1e6).toFixed(1)} Mbps`);
 }
 const final=await sample();const averages=[];for(let r=0;r<count;r++){const a=initial[r].filter(s=>s.kind==='video'),b=final[r].filter(s=>s.kind==='video');for(let i=0;i<b.length;i++)averages.push((b[i].frames-a[i].frames)/((b[i].timestamp-a[i].timestamp)/1000));}
 report.capacity.average_fps=averages;report.capacity.minimum_average_fps=Math.min(...averages);report.capacity.av_sync='not established by RTP counters; requires timestamped audiovisual marker measurement';
 assert.equal(averages.length,count*viewers);assert.ok(report.capacity.minimum_average_fps>=27,`Receiver average below 27 fps: ${report.capacity.minimum_average_fps}`);passed('capacity decoded frame-rate target');
}
try{
 launch(resolve(root,'livekit-cpp/out/windows-x64/Release/livekit-server.exe'),['--config',resolve(output,'media.json')],'media',{LIVE_STUDIO_ADMISSION_SECRET:admission,LIVE_STUDIO_ADMISSION_PORT:String(apiPort)});
 launch(resolve(root,'livekit-server/out/live-studio/business-api.exe'),['-listen',`127.0.0.1:${apiPort}`,'-db',resolve(output,'studio.sqlite'),'-live-studio','-live-studio-web',resolve(here,'../dist'),'-remote-control-web','','-screen-cast-web','','-media-api',media,'-media-url',`ws://127.0.0.1:${mediaPort}`],'business',{LIVEKIT_API_KEY:'studio',LIVEKIT_API_SECRET:secret,LIVE_STUDIO_ADMISSION_SECRET:admission});
 await until(async()=> (await fetch(base+'/healthz')).ok,'business service');await until(async()=> (await fetch(media+'/healthz')).ok,'media service');
 browser=await chromium.launch({channel:process.env.STUDIO_BROWSER_CHANNEL||'chrome',headless:true,args:['--autoplay-policy=no-user-gesture-required','--use-fake-device-for-media-stream','--use-fake-ui-for-media-stream','--disable-background-timer-throttling','--disable-renderer-backgrounding','--disable-backgrounding-occluded-windows']});
 if(process.env.STUDIO_CAPACITY==='1'){await capacity();report.result='passed';}else{
 const a=await publisher('host_a'),b=await publisher('host_b');passed(process.env.STUDIO_NATIVE==='1'?'two native Studio publishers through admission hook':'two real browser publishers through admission hook');
 const viewer=await browser.newPage({viewport:{width:1440,height:1000}});await viewer.goto(base+'/apps/live-studio/');await viewer.getByRole('button',{name:'登录 / 注册',exact:true}).click();await viewer.locator('#toggle-auth').click();await viewer.locator('#username').fill('viewer_1');await viewer.locator('#password').fill(password);await viewer.locator('#submit-auth').click();await until(()=>viewer.locator('#auth-dialog').evaluate(el=>!el.open),'registration');await viewer.goto(base+`/apps/live-studio/#room/${a.broadcast.room}`);await until(()=>viewer.locator('#video-stage video').evaluateAll(v=>v.some(x=>x.videoWidth>0&&x.getVideoPlaybackQuality().totalVideoFrames>30)),'actual decoded video',45000);passed('registration, viewer admission and decoded video');
 if(process.env.STUDIO_NATIVE==='1'){
  report.native_av=await viewer.evaluate(async()=>{
   const video=document.querySelector('#video-stage video'),audio=document.querySelector('#video-stage audio');
   if(!audio?.srcObject)throw new Error('Missing received audio stream');
   const ac=new AudioContext(),source=ac.createMediaStreamSource(audio.srcObject),analyser=ac.createAnalyser();analyser.fftSize=512;source.connect(analyser);await ac.resume();
   const canvas=document.createElement('canvas');canvas.width=canvas.height=4;const context=canvas.getContext('2d',{willReadFrequently:true}),buffer=new Float32Array(analyser.fftSize);let previousAudio=false,previousVideo=false;const a=[],v=[],start=performance.now();
   await new Promise(resolve=>{function sample(){const now=performance.now();analyser.getFloatTimeDomainData(buffer);const rms=Math.sqrt(buffer.reduce((n,x)=>n+x*x,0)/buffer.length);const loud=rms>.025;context.drawImage(video,0,0,video.videoWidth*.08,video.videoHeight*.08,0,0,4,4);const pixels=context.getImageData(0,0,4,4).data;const white=pixels[0]>180&&pixels[1]>180;if(loud&&!previousAudio)a.push(now);if(white&&!previousVideo)v.push(now);previousAudio=loud;previousVideo=white;if(now-start<8500)requestAnimationFrame(sample);else resolve();}sample();});
   await ac.close();const offsets=v.map(t=>a.reduce((best,x)=>Math.abs(x-t)<Math.abs(best)?x-t:best,Infinity)).filter(x=>Math.abs(x)<600);return {method:'received flash/beep onset via canvas and WebAudio, headless loopback, RAF sampling',video_edges:v.length,audio_edges:a.length,offsets_ms:offsets,max_abs_offset_ms:Math.max(...offsets.map(Math.abs))};
  });
  assert.ok(report.native_av.audio_edges>=3,'actual non-silent decoded audio');report.native_av.target_met=report.native_av.offsets_ms.length>=3&&report.native_av.max_abs_offset_ms<=150;
  passed('native decoded flash and audio markers measured');
 }

 await viewer.locator('#message').fill('你好，直播间');await viewer.locator('#chat-form button').click();await until(()=>viewer.locator('#messages').textContent().then(t=>t.includes('你好，直播间')),'chat roundtrip');passed('ordered WebSocket chat');
 await viewer.locator('[data-gift="10"]').click();await until(()=>viewer.locator('#wallet').textContent().then(t=>t.includes('90')),'gift debit');passed('gift debit and balance refresh');
 await viewer.locator('#request-guest').click();const login=await api('/api/v1/auth/login',{name:'viewer_1',password});await api(a.prefix+'guest/approve',{target:login.user.id},a.token);await until(()=>viewer.locator('#guest-join').count().then(n=>n===1),'guest approval');await viewer.locator('#guest-join').click();await until(()=>viewer.locator('#guest-status').textContent().then(t=>t.includes('连麦中')),'real guest publication',45000);passed('approved guest publishes camera/microphone fixtures');
 if(process.env.STUDIO_NATIVE==='1'){
  await until(()=>viewer.evaluate(()=>{const v=document.querySelector('#video-stage video'),c=document.createElement('canvas');c.width=c.height=1;const x=c.getContext('2d');x.drawImage(v,v.videoWidth*.75,v.videoHeight*.8,1,1,0,0,1,1);const p=x.getImageData(0,0,1,1).data;return Math.abs(p[0]-25)+Math.abs(p[1]-38)+Math.abs(p[2]-76)>65;}),'guest appears in native program composition',30000);passed('native host composites guest return video');
 }

 const stageGrant=await api(a.prefix+'connect',{stage:true},login.session_token);await api(a.prefix+'guest/end',{target:login.user.id},a.token);const denied=await fetch(media+'/rtc/validate?access_token='+encodeURIComponent(stageGrant.media_token));assert.equal(denied.status,401);passed('SFU rejects revoked stage grant');
 await api(a.prefix+'pk/invite',{target:b.broadcast.id},a.token);await api(b.prefix+'pk/accept',{},b.token);
 if(process.env.STUDIO_NATIVE==='1'){
  await until(()=>viewer.evaluate(()=>{const v=document.querySelector('#video-stage video'),c=document.createElement('canvas');c.width=2;c.height=1;const x=c.getContext('2d');x.drawImage(v,v.videoWidth*.25,v.videoHeight*.8,1,1,0,0,1,1);x.drawImage(v,v.videoWidth*.75,v.videoHeight*.8,1,1,1,0,1,1);const p=x.getImageData(0,0,2,1).data;return p[2]>p[0]*1.5&&p[4]>p[6]*2;}),'native PK local blue and remote red composition',30000);passed('two native hosts exchange clean stage video for PK');
 }
await viewer.locator('[data-gift="1"]').click();await until(async()=>{const s=await api(a.prefix+'snapshot');return s.pk?.state==='active'&&s.pk.score_a===1;},'PK authoritative score');await api(b.prefix+'pk/leave',{},b.token);const settled=await api(a.prefix+'snapshot');assert.equal(settled.pk.winner,a.broadcast.id);passed('PK invite, consent, scoring and forfeit');
 await viewer.screenshot({path:resolve(output,'viewer.png'),fullPage:true});await viewer.setViewportSize({width:390,height:844});await viewer.screenshot({path:resolve(output,'viewer-mobile.png'),fullPage:true});assert.equal(await viewer.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);passed('responsive viewer screenshots');
 await api(a.prefix+'mute',{target:'u-'+login.user.id,enabled:true},a.token);await assert.rejects(()=>api(a.prefix+'chat',{text:'blocked'},login.session_token),/403/);passed('server-authoritative moderation');
 clearInterval(a.heartbeat);clearInterval(b.heartbeat);await api(a.prefix+'stop',{},a.token);await api(b.prefix+'stop',{},b.token);await until(()=>viewer.locator('#status').textContent().then(t=>t==='直播已结束'),'ended viewer');passed('stop revokes room and ends viewer');
 if(process.env.STUDIO_NATIVE==='1'){
  await until(()=>a.child.exitCode!==null&&b.child.exitCode!==null,'native recorder finalization',30000);
  assert.equal(a.child.exitCode,0);assert.equal(b.child.exitCode,0);
  assert.ok(existsSync(resolve(output,'host_a.mp4')));assert.ok(existsSync(resolve(output,'host_b.mp4')));passed('native recording finalization');
 }
 report.result=process.env.STUDIO_NATIVE==='1'&&!report.native_av?.target_met?'functional_pass_timing_failed':'passed';if(report.result!=='passed')process.exitCode=1;
 }
}catch(e){report.result='failed';report.error=String(e.stack??e);console.error(e);process.exitCode=1;}finally{
 timers.forEach(clearInterval);await browser?.close();for(const {child}of children)child.kill();for(const item of children)writeFileSync(resolve(output,item.name+'.log'),item.log());writeFileSync(resolve(output,'result.json'),JSON.stringify(report,null,2));console.log('Results:',output);
}
