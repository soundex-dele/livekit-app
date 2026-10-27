// Native synthetic sender -> C++ SFU -> headless browser. Never captures the desktop.
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { createServer } from 'node:net';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { existsSync, createWriteStream } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../../../..');
const output=resolve(process.env.SC_RESULTS||resolve(root,'out/screen-cast',new Date().toISOString().replace(/[:.]/g,'-')));
await mkdir(output,{recursive:true});
const mediaBin=process.env.SC_MEDIA_SERVER||resolve(root,'livekit-cpp/out/windows-x64/Release/livekit-server.exe');
const businessBin=process.env.SC_BUSINESS_SERVER||resolve(root,'livekit-server/out/livekit-business-api.exe');
const hostBin=process.env.SC_HOST_FIXTURE||resolve(root,'livekit-app/out/screen-cast-desktop/Release/screen_cast_fixture.exe');
const children=[],logs=[],evidence={automated:true,synthetic_media:true,native_input:false,scenarios:[],binaries:{}};
function start(label,bin,args,env={}){
 const child=spawn(bin,args,{windowsHide:true,env:{...process.env,...env},stdio:['ignore','pipe','pipe']});children.push(child);
 for(const [stream,suffix] of [[child.stdout,'stdout'],[child.stderr,'stderr']]){const log=createWriteStream(resolve(output,`${label}.${suffix}.log`));logs.push(log);stream.pipe(log);}
 return child;
}
const freePort=()=>new Promise((resolve,reject)=>{const s=createServer();s.on('error',reject);s.listen(0,'127.0.0.1',()=>{const port=s.address().port;s.close(()=>resolve(port));});});
async function until(check,label,timeout=20000){const end=Date.now()+timeout;while(Date.now()<end){if(await check())return;await new Promise(r=>setTimeout(r,100));}throw Error('Timed out: '+label);}
const events=async file=>{try{return(await readFile(file,'utf8')).trim().split('\n').filter(Boolean).map(JSON.parse);}catch{return[];}};
let browser;
try{
 for(const bin of [mediaBin,businessBin,hostBin]){assert(existsSync(bin),'Missing binary: '+bin);evidence.binaries[bin]=createHash('sha256').update(await readFile(bin)).digest('hex');}
 const mediaPort=await freePort(),apiPort=await freePort();const base=`http://127.0.0.1:${apiPort}`;
 const config=resolve(output,'media.json');await writeFile(config,JSON.stringify({bind:'127.0.0.1',port:mediaPort,keys:{sctest:'isolated-screen-cast-secret-32-chars'},rtc:{port_range_start:51400,port_range_end:51500}}));
 const media=start('media',mediaBin,['--config',config]);
 const business=start('business',businessBin,['-listen',`127.0.0.1:${apiPort}`,'-db',resolve(output,'state.sqlite'),'-screen-cast-web',resolve(root,'livekit-app/apps/screen-cast/web/dist'),'-media-api',`http://127.0.0.1:${mediaPort}`,'-media-url',`ws://127.0.0.1:${mediaPort}`],{LIVEKIT_API_KEY:'sctest',LIVEKIT_API_SECRET:'isolated-screen-cast-secret-32-chars'});
 for(const url of [base,`http://127.0.0.1:${mediaPort}`])await until(async()=>{try{return(await fetch(url+'/healthz')).ok;}catch{return false;}},'health '+url);
 const executablePath=process.env.SC_CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
 browser=await chromium.launch({headless:true,...(executablePath?{executablePath}:{}),args:['--autoplay-policy=no-user-gesture-required']});
 async function connect(label,stopAfter){
   const context=await browser.newContext({viewport:{width:1200,height:900}}),page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
   await page.addInitScript(()=>{window.__pcs=[];const Native=window.RTCPeerConnection;window.RTCPeerConnection=class extends Native{constructor(...args){super(...args);window.__pcs.push(this);}};});
   let session;page.on('response',async r=>{if(r.url()===base+'/api/v1/screen-cast/sessions'&&r.status()===201)session=await r.json();});
   await page.goto(base+'/apps/screen-cast/');await page.locator('#receive').click();
   await until(()=>session,label+' session');const code=session.code;
   const host=start(label,hostBin,[base,code,...(stopAfter?[String(stopAfter)]:[])]);let stdout='';host.stdout.on('data',b=>stdout+=b);
   try{
   await page.waitForFunction(()=>document.querySelector('#badge').textContent==='正在播放',{},{timeout:30000});
   await page.waitForFunction(()=>{const v=document.querySelector('video');return v.videoWidth>0&&v.getVideoPlaybackQuality().totalVideoFrames>=80;},{},{timeout:30000});
   }catch(e){
    const diagnostic=await page.evaluate(async()=>({badge:document.querySelector('#badge').textContent,error:document.querySelector('#error').textContent,video:{width:document.querySelector('video').videoWidth,frames:document.querySelector('video').getVideoPlaybackQuality().totalVideoFrames},stats:await Promise.all(window.__pcs.map(async pc=>({state:pc.connectionState,reports:[...(await pc.getStats()).values()]})))}));
    evidence.scenarios.push({name:label,passed:false,diagnostic});await page.screenshot({path:resolve(output,label+'-failure.png'),fullPage:true});throw e;
   }
   const frames=await page.locator('video').evaluate(v=>({width:v.videoWidth,height:v.videoHeight,frames:v.getVideoPlaybackQuality().totalVideoFrames}));
   assert.equal(errors.length,0,errors.join('\n'));assert.match(stdout,/PREVIEW 1280x720/);assert.equal(frames.width,1280);assert.equal(frames.height,720);
   return {host,context,page,frames,session};
 }
 async function ended(c){await until(async()=>{const r=await fetch(`${base}/api/v1/screen-cast/sessions/${c.session.id}`,{headers:{Authorization:'Bearer '+c.session.credential}});return (await r.json()).state==='ended';},'persisted ended',40000);await c.page.waitForFunction(()=>document.querySelector('#badge').textContent==='已结束');}
 const a=await connect('sender-a'),b=await connect('sender-b');
 assert.notEqual(a.session.id,b.session.id);assert.equal((await fetch(base+'/api/v1/screen-cast/redeem',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({code:a.session.code})})).status,404);
 assert.equal((await fetch(`${base}/api/v1/screen-cast/sessions/${b.session.id}`,{headers:{Authorization:'Bearer '+a.session.credential}})).status,401);
 await a.page.screenshot({path:resolve(output,'receiver-playing.png'),fullPage:true});
 await a.page.locator('#stop').click();await until(()=>a.host.exitCode!==null,'native exits after receiver stop');await ended(a);
 const before=await b.page.locator('video').evaluate(v=>v.getVideoPlaybackQuality().totalVideoFrames);
 await b.page.waitForFunction(n=>document.querySelector('video').getVideoPlaybackQuality().totalVideoFrames>=n+30,before);
 evidence.scenarios.push({name:'pairing-decoding-preview-stop-isolation',passed:true,frames:[a.frames,b.frames]});
 b.host.kill();await ended(b);evidence.scenarios.push({name:'sender-crash',passed:true});
 const c=await connect('sender-local-stop',24);await until(()=>c.host.exitCode!==null,'local stop');await ended(c);assert.equal(c.host.exitCode,0);evidence.scenarios.push({name:'sender-local-stop',passed:true,frames:c.frames});
 const cancelPage=await browser.newPage();let pending;await cancelPage.route('**/api/v1/screen-cast/sessions',async route=>{const response=await route.fetch();pending=await response.json();await new Promise(r=>setTimeout(r,1000));await route.fulfill({response});});
 await cancelPage.goto(base+'/apps/screen-cast/');await cancelPage.locator('#receive').click();await until(()=>pending,'delayed create');await cancelPage.locator('#stop').click();
 await until(async()=>{const r=await fetch(`${base}/api/v1/screen-cast/sessions/${pending.id}`,{headers:{Authorization:'Bearer '+pending.credential}});return (await r.json()).state==='ended';},'cancelled create cleanup');await cancelPage.close();
 evidence.scenarios.push({name:'cancel-during-create',passed:true});
 const lost=await connect('sender-media-loss');media.kill();await until(()=>lost.host.exitCode!==null,'media outage stops sender',20000);await ended(lost);evidence.scenarios.push({name:'media-outage',passed:true,frames:lost.frames});
 const restarted=start('media-restarted',mediaBin,['--config',config]);await until(async()=>{try{return(await fetch(`http://127.0.0.1:${mediaPort}/healthz`)).ok;}catch{return false;}},'media restart');
 const d=await connect('sender-business-loss');business.kill();await until(()=>d.host.exitCode!==null,'business outage releases native sender',15000);await d.page.waitForFunction(()=>document.querySelector('#badge').textContent==='已结束');
 evidence.scenarios.push({name:'business-outage',passed:true,frames:d.frames});assert.equal(restarted.exitCode,null,'media process died');evidence.passed=true;
}catch(error){evidence.passed=false;evidence.error=String(error);process.exitCode=1;console.error(error);}
finally{
 if(browser)await browser.close();for(const child of children.reverse()){if(child.exitCode===null){child.kill();await Promise.race([new Promise(r=>child.once('exit',r)),new Promise(r=>setTimeout(r,3000))]);}}
 await writeFile(resolve(output,'evidence.json'),JSON.stringify(evidence,null,2));for(const log of logs)log.end();console.log('Evidence: '+output);
}
