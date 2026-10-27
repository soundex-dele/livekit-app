import assert from 'node:assert/strict';
import {spawn,spawnSync} from 'node:child_process';
import {randomBytes} from 'node:crypto';
import {mkdirSync,existsSync,writeFileSync,openSync,closeSync,readFileSync} from 'node:fs';
import {resolve,join} from 'node:path';
const pkg=resolve(process.argv[2]);const output=resolve(process.argv[3]);mkdirSync(output,{recursive:true});
const apiPort=19781,mediaPort=19780;const pids=[];
const report={checks:[],result:'running'};
try{
 const stdoutPath=join(output,'launcher-output.json'),stderrPath=join(output,'launcher-error.log');const stdout=openSync(stdoutPath,'w'),stderr=openSync(stderrPath,'w');
 const start=spawnSync(process.env.STUDIO_PWSH||'pwsh',['-NoProfile','-File',join(pkg,'start-services.ps1'),'-ApiPort',String(apiPort),'-MediaPort',String(mediaPort),'-DataDirectory',join(output,'data')],{cwd:pkg,windowsHide:true,encoding:'utf8',stdio:['ignore',stdout,stderr],env:{...process.env,LIVEKIT_API_KEY:'package-smoke',LIVEKIT_API_SECRET:randomBytes(24).toString('hex'),LIVE_STUDIO_ADMISSION_SECRET:randomBytes(24).toString('hex')}});
 closeSync(stdout);closeSync(stderr);assert.equal(start.status,0,readFileSync(stderrPath,'utf8')||String(start.error));const processes=JSON.parse(readFileSync(stdoutPath,'utf8'));pids.push(processes.media_pid,processes.business_pid);
 for(const port of [apiPort,mediaPort]){let ready=false;for(let i=0;i<50&&!ready;i++){try{ready=(await fetch(`http://127.0.0.1:${port}/healthz`)).ok;}catch{}if(!ready)await new Promise(r=>setTimeout(r,100));}assert.ok(ready,`service ${port} health`);}
 report.checks.push('packaged service deployment script and health endpoints');
 const web=await fetch(`http://127.0.0.1:${apiPort}/apps/live-studio/`);assert.ok(web.ok&&(await web.text()).includes('<html'));report.checks.push('packaged Web assets');
 const registration=await fetch(`http://127.0.0.1:${apiPort}/api/v1/auth/register`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:'package_check',password:randomBytes(16).toString('hex')})});assert.equal(registration.status,200);report.checks.push('packaged identity and SQLite migrations');
 const code=await new Promise((resolve,reject)=>{const child=spawn(join(pkg,'av_live_studio.exe'),['--hidden','--frames','16','--settings',join(output,'client'),'--screenshot',join(output,'studio.bmp')],{cwd:pkg,windowsHide:true,stdio:'pipe',env:{...process.env,SDL_VIDEODRIVER:'dummy',SDL_RENDER_DRIVER:'software'}});child.on('error',reject);child.on('exit',resolve);});assert.equal(code,0);assert.ok(existsSync(join(output,'studio.bmp')));report.checks.push('packaged desktop DLL loading and hidden UI render');report.result='passed';
}catch(error){report.result='failed';report.error=String(error.stack||error);process.exitCode=1;}finally{for(const pid of pids){try{process.kill(pid);}catch{}}writeFileSync(join(output,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));}
