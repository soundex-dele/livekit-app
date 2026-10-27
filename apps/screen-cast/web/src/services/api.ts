export interface SessionInfo { id: string; state: 'waiting'|'paired'|'streaming'|'ended'|'expired'; reason: string; expires_at: string; credential?: string; code?: string; }
export const terminal = (s: SessionInfo) => s.state === 'ended' || s.state === 'expired';
export async function api<T>(path: string, credential = '', body?: object, keepalive = false): Promise<T> {
 const r = await fetch('/api/v1/screen-cast/' + path, { method: body ? 'POST' : 'GET', headers: { 'Content-Type': 'application/json', ...(credential ? {Authorization: 'Bearer ' + credential} : {}) }, body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(5000), keepalive });
 if (!r.ok) { const data = await r.json().catch(() => ({})); throw Error(data.error?.code || data.error || `服务请求失败 (${r.status})`); }
 return r.json() as Promise<T>;
}
