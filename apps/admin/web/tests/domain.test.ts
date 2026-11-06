import {describe, expect, it} from 'vitest';
import {canRetryMediaTask, memberActions, orgPath, resourceActions, scopeKey, scopePath} from '../src/domain';
import type {Member, Resource, ResourceDetail} from '../src/types';
const resource: Resource = {id: 'meeting', organization_id: 'org-a', business: 'conference', kind: 'meeting', title: '会议', state: 'active', owner: 'host', version: 1, created: 1, updated: 1, module_enabled: true};
const host: Member = {subject_id: 'host', name: '主持人', role: 'host', state: 'active', policy_version: 1, updated: 1, muted: false, unmute_request: 0, stage: '', banned: false};
const detail: ResourceDetail = {resource, members: [host], members_truncated: false, actions: [], bindings: [], details: {meeting: {number: '123456789', host: 'host', starts_at: 1, duration_minutes: 60, waiting_room: false, locked: false, sharing_member: ''}}};
describe('management boundaries', () => {
  it('requires both cross-owner management and write permissions to retry media tasks', () => {expect(canRetryMediaTask(['*'])).toBe(true); expect(canRetryMediaTask(['jobs.manage', 'jobs.write'])).toBe(true); expect(canRetryMediaTask(['jobs.manage'])).toBe(false); expect(canRetryMediaTask(['jobs.write'])).toBe(false); expect(canRetryMediaTask([])).toBe(false);});
  it('keeps global and explicit organization API paths distinct', () => {expect(scopePath('')).toBe('/av/admin/platform'); expect(orgPath('a/b')).toBe('/av/admin/organizations/a%2Fb'); expect(scopeKey('org-a', true)).not.toEqual(scopeKey('org-a', false));});
  it('does not offer removal of the current host', () => {expect(memberActions(detail, host)).toEqual(['mute']); expect(memberActions(detail, {...host, subject_id: 'member', muted: true})).toEqual(['request-unmute', 'kick']);});
  it('offers cancellation for a scheduled meeting and no action on terminal resources', () => {expect(resourceActions({...resource, state: 'scheduled'})).toEqual(['cancel']); expect(resourceActions({...resource, state: 'ended'})).toEqual([]); expect(memberActions({...detail, resource: {...resource, state: 'ended'}}, host)).toEqual([]);});
  it('remote control only exposes revocation and ending', () => {expect(resourceActions({...resource, business: 'remote-control'})).toEqual(['revoke-control', 'end']);});
  it('does not offer removal of the live host, but permits guest moderation', () => {const live: ResourceDetail = {...detail, resource: {...resource, business: 'live-studio'}, details: {live: {host: 'host', room_id: 'room', pk_id: '', started: 1}}}; expect(memberActions(live, host)).toEqual([]); expect(memberActions(live, {...host, subject_id: 'guest', stage: 'approved'})).toEqual(['chat-mute', 'kick', 'stage-remove']);});
});
