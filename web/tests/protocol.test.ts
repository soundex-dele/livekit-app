import test from 'node:test';import assert from 'node:assert/strict';
import {point,ControlQueue,type Grant} from '../src/protocol.ts';
import {readFileSync} from 'node:fs';
test('shared C++ / browser contract messages use the same envelope',async()=>{
 const fixture=JSON.parse(readFileSync(new URL('../../../docs/remote-control/messages.json',import.meta.url),'utf8'));
 const sent:any[]=[];const q=new ControlQueue({grant_id:'example',epoch:1} as Grant,async b=>{sent.push(JSON.parse(new TextDecoder().decode(b)));},e=>{throw e});
 for(const message of fixture.controller_messages){const {v,grant_id,epoch,seq,...event}=message;q.push(event);}
 await new Promise(r=>setTimeout(r,20));assert.deepEqual(sent,fixture.controller_messages);q.close();
});
test('letterbox mapping rejects bars and maps content',()=>{
 const rect={left:10,top:20,width:1000,height:1000};assert.equal(point(rect,1920,1080,500,30),null);assert.deepEqual(point(rect,1920,1080,510,520),{x:.5,y:.5});assert.equal(point(rect,0,1080,510,520),null);
});
test('queue coalesces movement and preserves button ordering',async()=>{
 const sent:any[]=[];const q=new ControlQueue({grant_id:'g',epoch:2} as Grant,async b=>{sent.push(JSON.parse(new TextDecoder().decode(b)));},e=>{throw e});
 q.push({type:'move',x:.1});q.push({type:'move',x:.9});q.push({type:'button',down:true});q.push({type:'button',down:false});await new Promise(r=>setTimeout(r,20));
 assert.deepEqual(sent.map(x=>x.type),['move','button','button']);assert.equal(sent[0].x,.9);assert.deepEqual(sent.map(x=>x.seq),[1,2,3]);q.close();q.push({type:'key'});assert.equal(sent.length,3);
});
test('stop discards queued input but waits for reliable stop before closing',async()=>{
 const sent:string[]=[];let unblock:()=>void=()=>{};
 const q=new ControlQueue({grant_id:'g',epoch:1} as Grant,async bytes=>{const v=JSON.parse(new TextDecoder().decode(bytes));sent.push(v.type);if(v.type==='key')await new Promise<void>(r=>{unblock=r;});},e=>{throw e});
 q.push({type:'key',code:'KeyA',down:true});q.push({type:'button',down:true});const finished=q.finish();unblock();await finished;assert.deepEqual(sent,['key','stop']);
});
