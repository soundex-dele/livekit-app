import { control } from '../../../../../packages/web/control';
export interface SessionInfo { id: string; state: 'waiting'|'paired'|'streaming'|'ended'|'expired'; reason: string; expires_at: string; credential?: string; code?: string; pairing_code?: string; }
export const terminal = (s: SessionInfo) => s.state === 'ended' || s.state === 'expired';
export async function api<T>(path: string, credential = '', body?: object, keepalive = false): Promise<T> {
 const value = await control.request<any>('screen-cast/' + path, body, {credential, keepalive});
 if (path.endsWith('/connect')) return await control.credentials(value.grant_id, {credential}) as T;
 if (value.pairing_code) value.code = value.pairing_code;
 return value as T;
}
