import { test } from 'node:test';
import assert from 'node:assert/strict';
import { api } from '../src/services/api';
test('API reports business errors and never puts credentials in URLs',async()=>{
 const original=globalThis.fetch;const calls: {url: string; options?: RequestInit}[]=[];
 try {
  globalThis.fetch=async(url,options)=>{calls.push({url:String(url),options});return new Response(JSON.stringify({error:{code:'session_inactive'}}),{status:409});};
  await assert.rejects(api('sessions/one/connect','receiver-secret',{}),/session_inactive/);
  assert.equal(calls[0].url,'/api/v1/screen-cast/sessions/one/connect');
  assert.equal(new Headers(calls[0].options?.headers).get('Authorization'),'Bearer receiver-secret');
  assert.equal(calls[0].options?.method,'POST');assert.ok(calls[0].options?.signal);
 }finally{globalThis.fetch=original;}
});
