import test from 'node:test';
import assert from 'node:assert/strict';
import {remoteText,ControlQueue,type Grant} from '../src/features/remote_control/protocol.ts';
import {TextDelivery} from '../src/features/remote_control/text_delivery.ts';

test('Unicode text preserves whitespace, normalizes newlines and enforces scalar limits',()=>{
  assert.equal(remoteText(' 你好😀\r\n第二行\r\t '),' 你好😀\n第二行\n\t ');
  assert.equal(remoteText('😀'.repeat(256)).length,512);
  for(const invalid of ['', '😀'.repeat(257),'\ud800','\udfff','\x00','\x1b','\x7f','\x85'])assert.throws(()=>remoteText(invalid));
});
test('only a matching well-formed host acknowledgement completes explicit text delivery',()=>{
  const outcomes:any[]=[];const delivery=new TextDelivery((...result)=>outcomes.push(result));
  try{
    const first=delivery.begin('你好😀');assert.equal(delivery.busy,true);
    assert.throws(()=>delivery.begin('重复'));
    for(const invalid of [null,{}, {id:first.text_id+1,accepted:true,reason:''},{id:first.text_id,accepted:'true',reason:''}])delivery.receive(invalid);
    assert.equal(delivery.busy,true);assert.equal(outcomes.length,0);
    delivery.receive({id:first.text_id,accepted:true,reason:''});assert.equal(delivery.busy,false);assert.equal(outcomes[0][0],'accepted');
    const second=delivery.begin('第二次');assert(second.text_id>first.text_id);
    delivery.receive({id:first.text_id,accepted:true,reason:''});assert(delivery.busy);
    delivery.receive({id:second.text_id,accepted:false,reason:'input_failed'});assert.equal(outcomes[1][0],'rejected');assert.match(outcomes[1][1],/部分文字/);
  }finally{delivery.cancel();}
});
test('unknown delivery times out once without retry; disconnect cancels pending timeout',async()=>{
  const outcomes:any[]=[];const delivery=new TextDelivery((...result)=>outcomes.push(result),15);
  const request=delivery.begin('文字');await new Promise(r=>setTimeout(r,40));
  assert.equal(delivery.busy,false);assert.equal(outcomes.length,1);assert.equal(outcomes[0][0],'unknown');
  delivery.receive({id:request.text_id,accepted:true,reason:''});assert.equal(outcomes.length,1);
  delivery.begin('取消');assert.equal(delivery.cancel(),true);await new Promise(r=>setTimeout(r,40));assert.equal(outcomes.length,1);
});
test('reliable queue releases held input before text and stop discards unsent text',async()=>{
  const sent:any[]=[];let unblock:()=>void=()=>{};
  const queue=new ControlQueue({grant_id:'g',epoch:1} as Grant,async bytes=>{
    const value=JSON.parse(new TextDecoder().decode(bytes));sent.push(value);
    if(value.type==='key')await new Promise<void>(r=>unblock=r);
  },error=>{throw error;});
  queue.push({type:'release'});queue.push({type:'text',text:'你好',text_id:1,layout_version:1});
  await new Promise(r=>setTimeout(r,10));assert.deepEqual(sent.map(v=>v.type),['release','text']);
  queue.push({type:'key'});queue.push({type:'text',text:'不应发送',text_id:2});const done=queue.finish();unblock();await done;
  assert.deepEqual(sent.map(v=>v.type),['release','text','key','stop']);
});
