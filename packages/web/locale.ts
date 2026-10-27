const messages: Record<string, string> = {
  idle: '未连接', waiting: '等待连接', pending: '等待对方批准', approved: '已批准',
  paired: '已配对', streaming: '正在传屏', ended: '已结束', expired: '已过期',
  rejected: '对方已拒绝请求', revoked: '授权已结束', stopped: '连接已停止',
  code_unavailable: '连接码无效、已使用或已过期，请重新生成。',
  code_expired: '连接码已过期，请重新生成。',
  pairing_unavailable: '连接码无效、已使用或已过期，请重新生成。',
  grant_expired: '控制授权已过期，请重新申请。',
  session_inactive: '会话已结束，请重新连接。',
  grant_inactive: '控制授权已结束，请重新申请。',
  rate_limited: '操作过于频繁，请稍后重试。',
  media_unavailable: '媒体服务暂不可用，请稍后重试。',
  storage_error: '服务暂时无法保存状态，请稍后重试。',
  unauthorized: '连接凭证无效，请重新连接。', forbidden: '没有执行此操作的权限。',
  not_found: '会话不存在或已结束。',
  connect_timeout: '连接超时，请检查双方网络后重试。',
  heartbeat_timeout: '连接中断，请检查网络后重新连接。',
  server_restart: '服务已重启，请重新连接。',
};
export function statusText(value: string): string { return messages[value] ?? '连接已结束'; }
export function errorText(value: unknown): string {
  const raw = value instanceof Error ? value.message : typeof value === 'string' ? value : '';
  if (messages[raw]) return messages[raw];
  if (/timeout|timed out|aborted/i.test(raw)) return '请求超时，请检查网络后重试。';
  if (/fetch|network|connection/i.test(raw)) return '无法连接服务，请检查网络和服务地址。';
  // Known UI messages remain readable; never reflect arbitrary server payloads.
  if (/^[\u3400-\u9fff]/u.test(raw)) return raw;
  return '操作未完成，请检查连接后重试。';
}
