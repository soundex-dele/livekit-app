import {ApiError} from '@platform/sdk';
import type {GovernanceAction, Member, Resource, ResourceDetail} from './types';

export const businessNames = {'conference': '会议', 'screen-cast': '投屏', 'remote-control': '远控', 'live-studio': '直播'} as const;
export function businessName(value: string) {return businessNames[value as keyof typeof businessNames] || value;}
export const actionNames: Record<GovernanceAction, string> = {end: '结束会话', cancel: '取消预约', 'revoke-control': '撤销控制授权', kick: '移出成员', mute: '静音', 'request-unmute': '邀请开麦', 'chat-mute': '禁言', 'chat-unmute': '解除禁言', 'stage-remove': '嘉宾下麦'};
export const stateNames: Record<string, string> = {active: '进行中', live: '直播中', accepted: '已接受', paired: '已配对', waiting: '等待中', scheduled: '已预约', ended: '已结束', cancelled: '已取消', expired: '已过期', created: '已创建', pending: '待处理', approved: '已批准', rejected: '已拒绝', revoked: '已撤销', removed: '已移出', left: '已离开', queued: '排队中', retrying: '重试中', succeeded: '已完成', superseded: '已被新状态取代', failed: '失败', healthy: '健康', unhealthy: '异常', stale: '状态已过期', unknown: '未知'};
export const terminalResource = (state: string) => ['ended', 'cancelled', 'expired', 'failed'].includes(state);
export const pendingOperation = (state: string) => ['queued', 'retrying'].includes(state);
export const canRetryMediaTask = (permissions: readonly string[]) => permissions.includes('*') || (permissions.includes('jobs.manage') && permissions.includes('jobs.write'));
export const orgPath = (organization: string) => '/av/admin/organizations/' + encodeURIComponent(organization);
export const scopePath = (organization: string) => organization ? orgPath(organization) : '/av/admin/platform';
export const scopeKey = (organization: string, management: boolean) => ['scope', management ? 'management' : 'member', organization || 'platform', 'av-admin'] as const;
export function resourceActions(resource: Resource): GovernanceAction[] {
  if (terminalResource(resource.state)) return [];
  if (resource.business === 'conference' && resource.state === 'scheduled') return ['cancel'];
  return resource.business === 'remote-control' ? ['revoke-control', 'end'] : ['end'];
}
export function memberActions(detail: ResourceDetail, member: Member): GovernanceAction[] {
  if (terminalResource(detail.resource.state) || !['active', 'accepted', 'waiting'].includes(member.state)) return [];
  if (detail.resource.business === 'conference') {
    const actions: GovernanceAction[] = member.state === 'waiting' ? [] : [member.muted ? 'request-unmute' : 'mute'];
    if (member.subject_id !== detail.details.meeting?.host) actions.push('kick');
    return actions;
  }
  if (detail.resource.business === 'live-studio' && member.subject_id !== detail.details.live?.host) {
    const actions: GovernanceAction[] = [member.muted ? 'chat-unmute' : 'chat-mute', 'kick'];
    if (member.stage && !['rejected', 'pending'].includes(member.stage)) actions.push('stage-remove');
    return actions;
  }
  return [];
}
export function errorText(error: unknown): string {
  if (!(error instanceof ApiError)) return error instanceof Error ? error.message : '请求失败，请重试。';
  const descriptions: Record<string, string> = {forbidden: '当前账号无权执行此操作。', unauthorized: '登录已过期，请重新登录。', stale_version: '资源状态已变化，请刷新详情后重试。', version_conflict: '资源状态已变化，请刷新详情后重试。', module_disabled: '该业务当前已关闭。', request_timeout: '请求超时。操作结果可能尚未返回，请先刷新状态。', network_error: '网络连接失败，请检查连接后重试。', organization_required: '请先选择一个组织。', invalid_reason: '请填写操作原因。', cannot_remove_host: '不能移出当前主持人或主播，可使用结束会话。'};
  return `${descriptions[error.code] || error.code}${error.requestId ? '（请求编号：' + error.requestId + '）' : ''}`;
}
export function formatTime(value: number | undefined) {return value ? new Date(value).toLocaleString('zh-CN', {hour12: false}) : '—';}
