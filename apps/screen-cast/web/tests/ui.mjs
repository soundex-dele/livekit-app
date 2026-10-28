// Layout/state fixtures only. Real media decoding is covered by integration.mjs.
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { resolve, extname } from 'node:path';
import assert from 'node:assert/strict';
const dist=resolve('dist'), output=resolve('../../../out/screen-cast-web-ui');await mkdir(output,{recursive:true});
const server=createServer(async(req,res)=>{try{const path=req.url.split('?')[0].replace(/^\/apps\/screen-cast/, '')||'/';const file=resolve(dist,'.'+(path==='/'?'/index.html':path));if(!file.startsWith(dist+'/')){res.writeHead(403);res.end();return;}const data=await readFile(file);res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css'})[extname(file)]||'application/octet-stream');res.end(data);}catch{res.writeHead(404);res.end();}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const executablePath=process.env.SC_CHROME||['/Applications/Google Chrome.app/Contents/MacOS/Google Chrome','C:/Program Files/Google/Chrome/Application/chrome.exe'].find(existsSync);
let browser;
try{
 browser=await chromium.launch({headless:true,...(executablePath?{executablePath}:{})});
 for(const width of [390,800,1280]){
  const page=await browser.newPage({viewport:{width,height:900}});let state='waiting',fail=false;
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  await page.route('**/api/v1/screen-cast/**',route=>{if(fail)return route.fulfill({status:503,json:{error:'media_unavailable'}});return route.fulfill({status:route.request().method()==='POST'?201:200,json:{id:'fixture',credential:'fixture',code:'0123456789',state,reason:'',expires_at:new Date(Date.now()+300000).toISOString()}});});
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  const shot=async name=>{assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`horizontal overflow at ${width}`);await page.screenshot({path:resolve(output,`receiver-${width}-${name}.png`),fullPage:true});};
  await shot('idle');await page.locator('#receive').click();await page.waitForFunction(()=>document.querySelector('#code').textContent==='0123456789');assert.equal(await page.locator('#receive').isDisabled(),true);await shot('waiting');
  state='expired';await page.waitForFunction(()=>document.querySelector('#badge').textContent==='投屏码已过期');await shot('expired');
  fail=true;await page.locator('#receive').click();await page.waitForFunction(()=>document.querySelector('#error').textContent.length>0);await shot('error');
  // Pure synthetic video illustration for reviewing the playing layout, no desktop capture.
  await page.evaluate(()=>{const canvas=document.createElement('canvas');canvas.width=1280;canvas.height=720;const c=canvas.getContext('2d');c.fillStyle='#173976';c.fillRect(0,0,1280,720);c.fillStyle='#fff';c.font='48px sans-serif';c.fillText('Screen Cast · Synthetic Preview',80,160);const v=document.querySelector('video');v.srcObject=canvas.captureStream(1);void v.play();document.body.dataset.playing='true';document.querySelector('#placeholder').hidden=true;document.querySelector('#badge').textContent='正在播放';document.querySelector('#code').textContent='已配对';document.querySelector('#error').textContent='';document.querySelector('#fullscreen').disabled=false;});
  await page.waitForFunction(()=>document.querySelector('video').videoWidth===1280);await shot('playing');assert.deepEqual(errors,[]);await page.close();
 }
 console.log('Receiver UI: desktop/mobile layout and waiting/expired/error/playing fixtures passed');
}finally{await browser?.close();await new Promise(r=>server.close(r));}
