import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ControlClient, ControlError} from '../../../../packages/web/control';
import {eventView, roomView, snapshotView} from '../src/api';

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {status, headers: {'Content-Type': 'application/json'}});
}

test('account organization and visitor credentials remain separate', async () => {
  const calls: {url: string; options?: RequestInit}[] = [];
  const client = new ControlClient(async (url, options) => {
    calls.push({url: String(url), options});
    return json({ok: true});
  });
  client.selectOrganization('org-one');
  await client.request('live-studio/rooms', {title: 'test'});
  await client.request('live-studio/broadcasts/b/connect', {}, {credential: 'visitor-secret'});
  await client.request('/api/v1/users');
  const accountHeaders = new Headers(calls[0].options?.headers);
  assert.equal(accountHeaders.get('X-Organization-ID'), 'org-one');
  assert.equal(accountHeaders.get('X-Foundation-Request'), '1');
  const visitorHeaders = new Headers(calls[1].options?.headers);
  assert.equal(visitorHeaders.get('X-Organization-ID'), null);
  assert.equal(visitorHeaders.get('Authorization'), null);
  assert.equal(visitorHeaders.get('X-AV-Resource-Credential'), 'visitor-secret');
  assert.ok(calls.every(call => !call.url.includes('visitor-secret')));
  assert.equal(new Headers(calls[2].options?.headers).get('X-Organization-ID'), null);
});

test('organization switching aborts pending requests and rejects stale responses', async () => {
  let complete!: (value: Response) => void;
  let signal: AbortSignal | null | undefined;
  const client = new ControlClient(async (_url, options) => {
    signal = options?.signal;
    return new Promise<Response>(resolve => { complete = resolve; });
  });
  client.selectOrganization('one');
  const pending = client.request('live-studio/rooms');
  client.selectOrganization('two');
  assert.equal(signal?.aborted, true);
  complete(json({items: [{id: 'private-to-one'}]}));
  await assert.rejects(pending, (error: unknown) => error instanceof ControlError && error.code === 'organization_changed');
});

test('media provisioning retries only preparing without a token replay key', async () => {
  let attempts = 0;
  const client = new ControlClient(async (url, options) => {
    assert.equal(String(url), '/api/v1/av/rtc-grants/grant-one/credentials');
    assert.equal(new Headers(options?.headers).get('Idempotency-Key'), null);
    assert.equal(new Headers(options?.headers).get('X-AV-Resource-Credential'), 'visitor');
    if (++attempts === 1) return json({error: {code: 'media_preparing', retryable: true}}, 409);
    return json({media_url: 'wss://media.test', media_token: 'one-use', participant_identity: 'p'});
  });
  assert.equal((await client.credentials('grant-one', {credential: 'visitor', idempotencyKey: 'do-not-replay'})).media_token, 'one-use');
  assert.equal(attempts, 2);
  const denied = new ControlClient(async () => json({error: {code: 'grant_revoked'}}, 403));
  await assert.rejects(denied.credentials('grant-one'), /grant_revoked/);
});

test('live resource snapshots and individual realtime events match the UI model', () => {
  assert.equal(roomView({id: 'r', broadcast_id: 'b'}).broadcast, 'b');
  const state = snapshotView({broadcast: {id: 'b', state: 'live'}, host: 'host', room_id: 'r',
    members: [{subject_id: 'viewer', name: 'Visitor'}],
    stage_members: [{subject_id: 'guest', stage: 'approved'}],
    pk: {id: 'pk', left_id: 'b', right_id: 'other', state: 'pending', expires: 123000, left_score: 1, right_score: 2}});
  assert.equal(state.broadcast.owner, 'host');
  assert.equal(state.viewers[0].user_id, 'viewer');
  assert.equal(state.guests[0].state, 'approved');
  assert.equal(state.pk?.deadline, 123);
  assert.equal(state.pk?.state, 'invited');
  const event = eventView({id: 'e', resource_id: 'b', sequence: 9, type: 'live.gift',
    payload: {sender: 'viewer', amount: 10}, created: 123000});
  assert.deepEqual([event.seq, event.kind, event.payload.name, event.payload.points], [9, 'gift', 'viewer', 10]);
});
