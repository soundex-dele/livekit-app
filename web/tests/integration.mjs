// Real browser -> C++ SFU -> native SDK. The host publishes synthetic frames and
// records input to a file; this test never captures or injects the user desktop.
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { createServer } from 'node:net';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { existsSync, createWriteStream } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../..');
const output=resolve(process.env.RC_RESULTS||resolve(root,'out/remote-control',new Date().toISOString().replace(/[:.]/g,'-')));
await mkdir(output,{recursive:true});
const mediaBin=process.env.RC_MEDIA_SERVER||resolve(root,'livekit-cpp/out/windows-x64/Release/livekit-server.exe');
const businessBin=process.env.RC_BUSINESS_SERVER||resolve(root,'livekit-server/out/livekit-business-api.exe');
const hostBin=process.env.RC_HOST_FIXTURE||resolve(root,'livekit-sdk-cpp/out/windows-x64/Release/remote_host_fixture.exe');
for(const bin of [mediaBin,businessBin,hostBin])assert(existsSync(bin),'Missing binary: '+bin);
const children=[],logs=[],evidence={automated:true,synthetic_media:true,native_input:false,scenarios:[],binaries:{}};
for(const bin of [mediaBin,businessBin,hostBin])evidence.binaries[bin]=createHash('sha256').update(await readFile(bin)).digest('hex');
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
 const mediaPort=await freePort(),apiPort=await freePort();const base=`http://127.0.0.1:${apiPort}`;
 const config=resolve(output,'media.json');await writeFile(config,JSON.stringify({bind:'127.0.0.1',port:mediaPort,keys:{rctest:'isolated-remote-control-secret-32-chars'},rtc:{port_range_start:51400,port_range_end:51500}}));
 const media=start('media',mediaBin,['--config',config]);
 const business=start('business',businessBin,['-listen',`127.0.0.1:${apiPort}`,'-db',resolve(output,'state.sqlite'),'-web',resolve(root,'livekit-app/web/dist'),'-media-api',`http://127.0.0.1:${mediaPort}`,'-media-url',`ws://127.0.0.1:${mediaPort}`],{LIVEKIT_API_KEY:'rctest',LIVEKIT_API_SECRET:'isolated-remote-control-secret-32-chars'});
 for(const url of [base,`http://127.0.0.1:${mediaPort}`])await until(async()=>{try{return(await fetch(url+'/healthz')).ok;}catch{return false;}},'health '+url);
 const executablePath=process.env.RC_CHROME||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
 browser=await chromium.launch({headless:true,...(executablePath?{executablePath}:{}),args:['--autoplay-policy=no-user-gesture-required']});
 async function connect(label){
   const file=resolve(output,label+'.inputs.jsonl'),host=start(label,hostBin,[base,file]);let text='';host.stdout.on('data',b=>text+=b);
   await until(()=>/PAIRING (\d{10})/.test(text),label+' pairing');const code=text.match(/PAIRING (\d{10})/)[1];
   const context=await browser.newContext({viewport:{width:1280,height:1000}}),page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(String(e)));
   await page.goto(base);await page.locator('#code').fill(code);await page.locator('#name').fill('Integration helper');await page.locator('#connect').click();
   await page.waitForFunction(()=>document.querySelector('#badge').textContent==='已连接',{},{timeout:30000});
   await page.waitForFunction(()=>{const v=document.querySelector('video');return v.videoWidth>0&&v.getVideoPlaybackQuality().totalVideoFrames>=5;},{},{timeout:30000});
   const frames=await page.locator('video').evaluate(v=>({width:v.videoWidth,height:v.videoHeight,frames:v.getVideoPlaybackQuality().totalVideoFrames}));
   assert.equal(errors.length,0,errors.join('\n'));return{host,context,page,file,frames};
 }
 const a=await connect('host-a'),b=await connect('host-b');
 await a.page.locator('#viewport').click();await a.page.keyboard.down('KeyA');await a.page.keyboard.up('KeyA');await a.page.mouse.wheel(0,120);
 await until(async()=> (await events(a.file)).some(e=>e.type==='key'&&e.code==='KeyA'&&e.down),'key reaches native SDK');
 assert(!(await events(b.file)).some(e=>e.type==='key'),'cross-session input leakage');
 await a.page.keyboard.down('Shift');await a.page.locator('#name').focus();
 await until(async()=>{const e=await events(a.file);return e.some(x=>x.code==='ShiftLeft'&&x.down)&&e.some(x=>x.code==='ShiftLeft'&&!x.down);},'blur releases Shift');
 await a.page.screenshot({path:resolve(output,'controller.png'),fullPage:true});
 await a.page.locator('#stop').click();await until(()=>a.host.exitCode!==null,'host stop');
 evidence.scenarios.push({name:'media-input-blur-stop-isolation',passed:true,frames:a.frames});
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
 assert.equal(media.exitCode,null,'media process died');evidence.passed=true;
}catch(error){evidence.passed=false;evidence.error=String(error);process.exitCode=1;console.error(error);}
finally{
 if(browser)await browser.close();for(const child of children.reverse()){if(child.exitCode===null){child.kill();await Promise.race([new Promise(r=>child.once('exit',r)),new Promise(r=>setTimeout(r,3000))]);}}
 await writeFile(resolve(output,'evidence.json'),JSON.stringify(evidence,null,2));for(const log of logs)log.end();console.log('Evidence: '+output);
}
