import { control, ControlError, type ResourceEvent } from '../../../../packages/web/control';
import type { LiveEvent, LiveRoom, Snapshot } from './state';

const credentials = new Map<string, string>();
const subjects = new Map<string, string>();
export function resourceCredential(id: string) { return credentials.get(id) ?? ''; }
export function clearVisitors() { credentials.clear(); subjects.clear(); }
export function eventView(event: ResourceEvent): LiveEvent {
  const kind = event.type.replace(/^live\./, '');
  return {seq: event.sequence, kind, created: event.created,
    payload: {...event.payload, name: event.payload.name ?? event.payload.subject_id ?? event.payload.sender ?? '',
      text: event.payload.content ?? '', points: event.payload.amount ?? 0,
      identity: event.payload.target ?? event.payload.subject_id ?? ''}};
}
export function roomView(value: any): LiveRoom {
  return {...value, broadcast: value.broadcast_id ?? value.broadcast ?? '', state: value.state ?? 'offline'};
}
export function snapshotView(value: any): Snapshot {
  const stage = value.stage_members ?? [];
  const viewers = value.members ?? value.viewers ?? stage;
  const pk = value.pk;
  return {
    broadcast: {...value.broadcast, owner: value.host, room: value.room_id},
    online: value.online ?? viewers.length,
    viewers: viewers.map((member: any) => ({identity: member.subject_id, user_id: member.subject_id,
      name: member.name ?? member.subject_id, muted: member.muted === true})),
    guests: stage.map((member: any) => ({user_id: member.subject_id, name: member.name ?? member.subject_id, state: member.stage})),
    pk: pk ? {id: pk.id, a: pk.left_id, b: pk.right_id, state: pk.state === 'pending' ? 'invited' : pk.state,
      deadline: Math.floor(pk.expires / 1000), score_a: pk.left_score, score_b: pk.right_score, winner: pk.winner} : null,
    leaderboard: (value.leaderboard ?? []).map((entry: any) => ({name: entry.name ?? entry.subject_id, points: entry.points})),
    seq: value.sequence ?? 0,
  };
}
export async function watch(id: string) {
  const result = await control.request(`live-studio/broadcasts/${id}/watch`, {}, {credential: resourceCredential(id)});
  if (result.credential) credentials.set(id, result.credential);
  subjects.set(id, result.subject_id);
  return result;
}
export async function api<T = any>(path: string, body?: any): Promise<T> {
  if (path.startsWith('/api/')) return control.request<T>(path, body);
  if (path === 'rooms') {
    if (!control.organization) return [] as T;
    const value = await control.request('live-studio/rooms');
    return value.items.map(roomView) as T;
  }
  if (path === 'leaderboard') {
    if (!control.organization) return [] as T;
    const value = await control.request('live-studio/board');
    return value.items.map((item: any) => ({name: item.subject_id, points: item.points})) as T;
  }
  if (path === 'wallet') {
    const value = await control.request('live-studio/wallet');
    return {remaining: value.balance} as T;
  }
  if (path === 'admin/users') return (await control.request('/api/v1/users')).items as T;
  if (path === 'admin/disable') return control.request(`/api/v1/users/${body.user}/disable`, {disabled: body.disabled});
  const match = /^broadcasts\/([^/]+)\/(.+)$/.exec(path);
  if (!match) return control.request<T>('live-studio/' + path, body);
  const [, id, original] = match;
  const options = {credential: resourceCredential(id)};
  if (original === 'snapshot' || original === 'heartbeat') {
    if (original === 'heartbeat') await control.request(`live-studio/broadcasts/${id}/heartbeat`, {}, options);
    return snapshotView(await control.request(`live-studio/broadcasts/${id}`, undefined, options)) as T;
  }
  if (original === 'connect') {
    const access = await control.request(`live-studio/broadcasts/${id}/connect`, {stage: body?.stage === true}, options);
    return control.credentials(access.grant_id, options) as Promise<T>;
  }
  const actions: Record<string, string> = {'stop': 'end', 'guest/request': 'stage-request', 'guest/approve': 'stage-approve',
    'guest/reject': 'stage-reject', 'guest/end': body?.target === subjects.get(id) ? 'stage-leave' : 'stage-remove'};
  const action = actions[original] ?? original;
  const payload = original === 'chat' ? {content: body.text} : original === 'gift' ? {amount: body.points} : body;
  return control.request(`live-studio/broadcasts/${id}/${action}`, payload ?? {}, {
    ...options, idempotencyKey: original === 'gift' ? body.request_id : undefined,
  });
}
export { control, ControlError };
