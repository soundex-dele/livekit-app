// Real browser -> C++ SFU -> native SDK. The host publishes synthetic frames and
// records input to a file; this test never captures or injects the user desktop.
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { createServer } from 'node:net';
import { mkdir, writeFile, readFile, readdir } from 'node:fs/promises';
import { existsSync, createWriteStream } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash, createHmac } from 'node:crypto';
import assert from 'node:assert/strict';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../../../..');
const output=resolve(process.env.RC_RESULTS||resolve(root,'out/remote-control',new Date().toISOString().replace(/[:.]/g,'-')));
await mkdir(output,{recursive:true});
const mediaBin=process.env.RC_MEDIA_SERVER||resolve(root,'livekit-cpp/out/windows-x64/Release/livekit-server.exe');
const mediaKind=process.env.RC_MEDIA_SERVER_KIND||'cpp';assert(['cpp','official'].includes(mediaKind),'Unknown RC_MEDIA_SERVER_KIND');
const businessBin=process.env.RC_BUSINESS_SERVER||resolve(root,'livekit-server/out/livekit-business-api.exe');
const hostBin=process.env.RC_HOST_FIXTURE||resolve(root,'livekit-app/out/remote-control-desktop/Release/remote_host_fixture.exe');
for(const bin of [mediaBin,businessBin,hostBin])assert(existsSync(bin),'Missing binary: '+bin);
const children=[],logs=[],evidence={automated:true,synthetic_media:true,native_input:false,media_kind:mediaKind,scenarios:[],binaries:{},web:{}};
for(const bin of [mediaBin,businessBin,hostBin])evidence.binaries[bin]=createHash('sha256').update(await readFile(bin)).digest('hex');
const web=resolve(root,'livekit-app/apps/remote-control/web/dist');
for(const file of ['index.html',...(await readdir(resolve(web,'assets'))).map(name=>'assets/'+name)])evidence.web[file]=createHash('sha256').update(await readFile(resolve(web,file))).digest('hex');
function start(label,bin,args,env={}){
 const child=spawn(bin,args,{windowsHide:true,env:{...process.env,...env},stdio:['ignore','pipe','pipe']});children.push(child);
 for(const [stream,suffix] of [[child.stdout,'stdout'],[child.stderr,'stderr']]){const log=createWriteStream(resolve(output,`${label}.${suffix}.log`));logs.push(log);stream.pipe(log);}
 return child;
}
const freePort=()=>new Promise((resolve,reject)=>{const s=createServer();s.on('error',reject);s.listen(0,'127.0.0.1',()=>{const port=s.address().port;s.close(()=>resolve(port));});});
async function until(check,label,timeout=20000){const end=Date.now()+timeout;while(Date.now()<end){if(await check())return;await new Promise(r=>setTimeout(r,100));}throw Error('Timed out: '+label);}
const events=async file=>{try{return(await readFile(file,'utf8')).trim().split('\n').filter(Boolean).map(JSON.parse);}catch{return[];}};
let browser,metricsUrl;
try{
 const mediaPort=await freePort(),apiPort=await freePort();const base=`http://127.0.0.1:${apiPort}`;
 if(mediaKind==='cpp')metricsUrl=`http://127.0.0.1:${mediaPort}/metrics`;
 const configuration={port:mediaPort,keys:{rctest:'isolated-remote-control-secret-32-chars'},rtc:{port_range_start:51400,port_range_end:51500}};
 if(mediaKind==='official'){configuration.bind_addresses=['127.0.0.1'];Object.assign(configuration.rtc,{tcp_port:await freePort(),use_external_ip:false,node_ip:'127.0.0.1',enable_loopback_candidate:true});configuration.logging={level:'warn'};}
 else configuration.bind='127.0.0.1';
 const config=resolve(output,'media.json');await writeFile(config,JSON.stringify(configuration));
 const media=start('media',mediaBin,['--config',config]);
 const business=start('business',businessBin,['-listen',`127.0.0.1:${apiPort}`,'-db',resolve(output,'state.sqlite'),'-remote-control-web',resolve(root,'livekit-app/apps/remote-control/web/dist'),'-media-api',`http://127.0.0.1:${mediaPort}`,'-media-url',`ws://127.0.0.1:${mediaPort}`],{LIVEKIT_API_KEY:'rctest',LIVEKIT_API_SECRET:'isolated-remote-control-secret-32-chars'});
 for(const url of [base+'/healthz',`http://127.0.0.1:${mediaPort}`+(mediaKind==='official'?'/':'/healthz')])await until(async()=>{try{return(await fetch(url)).ok;}catch{return false;}},'health '+url);
 const executablePath=process.env.RC_CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
 browser=await chromium.launch({headless:true,...(executablePath?{executablePath}:{}),args:['--autoplay-policy=no-user-gesture-required']});
 async function connect(label,args=[],connectDelay=0){
   const file=resolve(output,label+'.inputs.jsonl'),host=start(label,hostBin,[base,file,...args]);let text='';host.stdout.on('data',b=>text+=b);
   await until(()=>/PAIRING (\d{10})/.test(text),label+' pairing');const code=text.match(/PAIRING (\d{10})/)[1];
   const context=await browser.newContext({viewport:{width:1280,height:1000}}),page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
   if(connectDelay)await page.route('**/api/v1/remote-control/control-grants/*/connect',async route=>{await new Promise(resolve=>setTimeout(resolve,connectDelay));await route.continue();});
   await page.addInitScript(()=>{
     window.__pcs=[];window.__channels=[];
     const monitor=(channel,direction)=>{
       const state={label:channel.label,direction,created:performance.now(),opened:0,received:0,sent:0,closed:0};window.__channels.push(state);
       channel.addEventListener('open',()=>state.opened=performance.now());channel.addEventListener('close',()=>state.closed=performance.now());
       channel.addEventListener('message',()=>state.received++);const send=channel.send.bind(channel);channel.send=(...args)=>{state.sent++;return send(...args);};
       return channel;
     };
     const Native=window.RTCPeerConnection;window.RTCPeerConnection=class extends Native{
       constructor(...args){super(...args);window.__pcs.push(this);this.addEventListener('datachannel',event=>monitor(event.channel,'remote'));}
       createDataChannel(...args){return monitor(super.createDataChannel(...args),'local');}
     };
   });
   await page.goto(base+'/apps/remote-control/');assert(await page.locator('#remote-text').isDisabled());assert(await page.locator('#send-text').isDisabled());await page.locator('#code').fill(code);await page.locator('#name').fill('Integration helper');await page.locator('#connect').click();
   try{
     await page.waitForFunction(()=>document.querySelector('#badge').textContent==='已连接',{},{timeout:30000});
     await page.waitForFunction(()=>{const v=document.querySelector('video');return v.videoWidth>0&&v.getVideoPlaybackQuality().totalVideoFrames>=5;},{},{timeout:30000});
   }
   catch(error){
     const diagnostic=await page.evaluate(async()=>({badge:document.querySelector('#badge').textContent,status:document.querySelector('#status').textContent,channels:window.__channels,video:{width:document.querySelector('video').videoWidth,frames:document.querySelector('video').getVideoPlaybackQuality().totalVideoFrames},stats:await Promise.all(window.__pcs.map(async pc=>({state:pc.connectionState,reports:[...(await pc.getStats()).values()]})))}));
     await writeFile(resolve(output,label+'.rtc.json'),JSON.stringify(diagnostic,null,2));throw error;
   }
   const frames=await page.locator('video').evaluate(v=>({width:v.videoWidth,height:v.videoHeight,frames:v.getVideoPlaybackQuality().totalVideoFrames}));
   assert.equal(errors.length,0,errors.join('\n'));return{host,context,page,file,frames,errors};
 }
 const a=await connect('host-a'),b=await connect('host-b');
 await a.page.locator('#remote-text').fill('需要先选择目标');assert(await a.page.locator('#send-text').isDisabled(),'text requires explicit target selection');
 await a.page.locator('#viewport').click();await a.page.keyboard.down('KeyA');await a.page.keyboard.up('KeyA');await a.page.mouse.wheel(0,120);
 await until(async()=> (await events(a.file)).some(e=>e.type==='key'&&e.code==='KeyA'&&e.down),'key reaches native SDK');
 assert(!(await events(b.file)).some(e=>e.type==='key'),'cross-session input leakage');
 await a.page.keyboard.down('Shift');await a.page.locator('#name').focus();
 await until(async()=>{const e=await events(a.file);return e.some(x=>x.code==='ShiftLeft'&&x.down)&&e.some(x=>x.code==='ShiftLeft'&&!x.down);},'blur releases Shift');
 await a.page.locator('#remote-text').fill('长'.repeat(257));await a.page.locator('#send-text').click();
 assert.match(await a.page.locator('#text-status').textContent(),/最多发送 256/);assert(!(await events(a.file)).some(e=>e.type==='text'));
 const unicode='你好，远程协助 😀\n第二行\t完成';
 await a.page.locator('#remote-text').fill(unicode);
 const waiting=await a.page.locator('#text-form').evaluate(form=>{form.requestSubmit();form.requestSubmit();return document.querySelector('#send-text').disabled&&document.querySelector('#remote-text').disabled&&form.getAttribute('aria-busy')==='true';});assert(waiting,'pending request disables duplicate submission and draft editing');
 await a.page.waitForFunction(()=>document.querySelector('#text-status').textContent.includes('已送入远端输入系统'));
 const textEvents=(await events(a.file)).filter(e=>e.type==='text');assert.equal(textEvents.length,1,'double submit must not duplicate text');assert.equal(textEvents[0].text,unicode);
 assert.equal(await a.page.locator('#remote-text').inputValue(),'','acknowledged text is cleared');
 assert(!(await events(b.file)).some(e=>e.type==='text'),'cross-session text leakage');
 await a.page.screenshot({path:resolve(output,'controller.png'),fullPage:true});
 await a.page.setViewportSize({width:390,height:844});
 assert(await a.page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth),'mobile layout must not scroll horizontally');
 await a.page.screenshot({path:resolve(output,'controller-mobile.png'),fullPage:true});
 await a.page.locator('#remote-text').fill('结束后应清除');
 await a.page.locator('#stop').click();await until(()=>a.host.exitCode!==null,'host stop');
 assert(await a.page.locator('#remote-text').isDisabled());assert(await a.page.locator('#send-text').isDisabled());assert.equal(await a.page.locator('#remote-text').inputValue(),'');
 await a.page.locator('#text-form').evaluate(form=>form.requestSubmit());assert.equal((await events(a.file)).filter(e=>e.type==='text').length,1,'stop blocks text');
 evidence.scenarios.push({name:'media-input-blur-stop-isolation',passed:true,frames:a.frames});
 evidence.scenarios.push({name:'unicode-text-ack-limit-double-submit-isolation-stop',passed:true});
 const legacy=await connect('host-legacy',['--no-text']);assert(await legacy.page.locator('#remote-text').isDisabled());assert(await legacy.page.locator('#send-text').isDisabled());
 assert.match(await legacy.page.locator('#text-status').textContent(),/不支持发送文字/);await legacy.page.locator('#stop').click();await until(()=>legacy.host.exitCode!==null,'legacy host stop');await legacy.context.close();
 evidence.scenarios.push({name:'text-capability-disabled',passed:true});
 const delayed=await connect('host-delayed',[],4500);await delayed.page.locator('#viewport').click();await delayed.page.keyboard.press('KeyD');
 await until(async()=>(await events(delayed.file)).some(e=>e.code==='KeyD'&&e.down),'delayed handshake accepts input after authentication');
 await delayed.page.locator('#stop').click();await until(()=>delayed.host.exitCode!==null,'delayed host stop');await delayed.context.close();
 evidence.scenarios.push({name:'delayed-initial-handshake',passed:true,controller_connect_delay_ms:4500,frames:delayed.frames});
 const c=await connect('host-c');await c.page.locator('#viewport').click();await c.page.keyboard.down('Shift');
 await until(async()=>(await events(c.file)).some(e=>e.code==='ShiftLeft'&&e.down),'held key before heartbeat loss');
 const cdp=await c.context.newCDPSession(c.page);const heartbeatLossAt=Date.now();
 await cdp.send('Emulation.setVirtualTimePolicy',{policy:'pause'});
 await until(async()=>(await events(c.file)).some(e=>e.code==='ShiftLeft'&&!e.down),'heartbeat timeout releases key',6000);
 evidence.scenarios.push({name:'heartbeat-loss-release',passed:true,elapsed_ms:Date.now()-heartbeatLossAt});
 await c.context.close();await until(()=>c.host.exitCode!==null,'host exits after heartbeat loss');
 await b.page.locator('#viewport').click();await b.page.keyboard.down('Control');
 await until(async()=>(await events(b.file)).some(e=>e.code==='ControlLeft'&&e.down),'held key before service outage');
 const outageAt=Date.now();business.kill();
 await until(async()=>(await events(b.file)).some(e=>e.code==='ControlLeft'&&!e.down),'business outage releases key',12000);
 evidence.scenarios.push({name:'business-outage-release',passed:true,elapsed_ms:Date.now()-outageAt,frames:b.frames});
 await until(()=>b.host.exitCode!==null,'host exits after service outage',15000);
 for(const client of [a,b,c,legacy,delayed])assert.equal(client.errors.length,0,client.errors.join('\n'));
 assert.equal(media.exitCode,null,'media process died');evidence.passed=true;
}catch(error){evidence.passed=false;evidence.error=String(error);process.exitCode=1;console.error(error);
 let i=0;for(const context of browser?.contexts()||[])for(const page of context.pages())try{await page.screenshot({path:resolve(output,`failure-${++i}.png`),fullPage:true,timeout:2000});}catch{}
}
finally{
 if(metricsUrl)try{
   const header=Buffer.from(JSON.stringify({alg:'HS256',typ:'JWT'})).toString('base64url'),payload=Buffer.from(JSON.stringify({iss:'rctest',sub:'test-metrics',exp:Math.floor(Date.now()/1000)+60,video:{roomList:true}})).toString('base64url');
   const token=header+'.'+payload+'.'+createHmac('sha256','isolated-remote-control-secret-32-chars').update(header+'.'+payload).digest('base64url');
   const response=await fetch(metricsUrl,{headers:{Authorization:'Bearer '+token},signal:AbortSignal.timeout(2000)});
   await writeFile(resolve(output,'media-metrics.txt'),`HTTP ${response.status}\n`+await response.text());
 }catch(error){await writeFile(resolve(output,'media-metrics-error.txt'),String(error));}
 if(browser)await browser.close();for(const child of children.reverse()){if(child.exitCode===null){child.kill();await Promise.race([new Promise(r=>child.once('exit',r)),new Promise(r=>setTimeout(r,3000))]);}}
 await writeFile(resolve(output,'evidence.json'),JSON.stringify(evidence,null,2));for(const log of logs)log.end();console.log('Evidence: '+output);
}
