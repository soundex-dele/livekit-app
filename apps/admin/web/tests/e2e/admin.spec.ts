import {expect, test, type Page as BrowserPage} from '@playwright/test';
import type {Member, Resource, ResourceDetail} from '../../src/types';

const now = Date.UTC(2026, 10, 6, 6);
const initial = (org: string): Resource => ({id: `meeting-${org}`, organization_id: org, business: 'conference', kind: 'meeting', title: org === 'org-a' ? '产品评审会议' : '研发同步会议', state: 'active', owner: 'host', version: 1, created: now, updated: now, module_enabled: true});
async function fixture(page: BrowserPage, admin = true) {
  let loggedIn = false;
  const resources = [initial('org-a'), initial('org-b')];
  const actions: {key: string; body: Record<string, unknown>; organization: string}[] = [];
  const members: Member[] = [{subject_id: 'host', name: '会议主持人', role: 'host', state: 'active', policy_version: 1, updated: now, muted: false, unmute_request: 0, stage: '', banned: false}, {subject_id: 'member', name: '测试成员', role: 'member', state: 'active', policy_version: 1, updated: now, muted: false, unmute_request: 0, stage: '', banned: false}];
  const organizationItems = ['org-a', 'org-b'].map((id, index) => ({id, data: {name: index === 0 ? '产品团队' : '研发团队', owner: 'host'}, scope: 'app', kind: 'organization', owner: 'host', version: 1, created: new Date(now).toISOString(), updated: new Date(now).toISOString()}));
  let failNextAction = false;
  let pauseA: Promise<void> | undefined;
  await page.route('**/api/v1/**', async route => {
    const request = route.request();
    const url = new URL(request.url());
    const pathname = url.pathname.replace('/api/v1', '');
    const respond = (body: unknown, status = 200) => route.fulfill({status, json: body});
    if (pathname === '/capabilities') return respond({modules: ['organizations', 'jobs', 'events', 'av'], registration: false, version: '0.2.0', organization_mode: 'optional'});
    if (pathname === '/auth/login') {loggedIn = true; return respond({});}
    if (pathname === '/auth/logout') {loggedIn = false; return respond({});}
    if (!loggedIn) return respond({error: {code: 'unauthorized'}, request_id: 'test-login'}, 401);
    const organization = /^\/admin\/organizations\/([^/]+)/.exec(pathname)?.[1] || request.headers()['x-organization-id'] || '';
    if (pathname.endsWith('/context')) return respond({user: {id: 'operator', name: admin ? '平台管理员' : '组织管理员', admin, disabled: false, tenant_id: ''}, scope: organization ? `org:${organization}` : 'app', organization, role: organization ? 'admin' : 'system-admin', permissions: admin || organization ? ['*'] : []});
    if (pathname === '/admin/organizations' || pathname === '/organizations') return respond({items: admin ? organizationItems : organizationItems.slice(0, 1), total: admin ? 2 : 1, limit: 50, offset: 0});
    if (pathname === '/av/capabilities') return respond({modules: ['conference', 'screen-cast', 'remote-control', 'live-studio'], organization_required: true, api_version: '1', admin_url: '/apps/admin/'});
    if (pathname.startsWith('/av/admin/')) {
      const org = /^\/av\/admin\/organizations\/([^/]+)/.exec(pathname)?.[1];
      const rows = resources.filter(resource => !org || resource.organization_id === org);
      if (!admin && !org) return respond({error: {code: 'forbidden'}}, 403);
      if (pathname.endsWith('/overview')) return respond({generated_at: now, resources: rows.map(resource => ({business: resource.business, state: resource.state, count: 1})), created_last_24h: rows.length, operations: [{state: 'queued', count: actions.length}]});
      if (pathname.endsWith('/resources')) {
        if (org === 'org-a' && pauseA) await pauseA;
        const q = url.searchParams.get('q');
        const business = url.searchParams.get('business');
        const items = rows.filter(row => (!q || row.title.includes(q) || row.id.includes(q)) && (!business || row.business === business));
        return respond({items, total: items.length, limit: 20, offset: 0});
      }
      if (pathname.endsWith('/actions')) {
        const key = request.headers()['idempotency-key'];
        const body = request.postDataJSON();
        actions.push({key, body, organization: org || ''});
        if (failNextAction) {failNextAction = false; return respond({error: {code: 'request_timeout'}, request_id: 'test-timeout'}, 504);}
        const resource = resources.find(row => pathname.includes('/' + row.id + '/'))!;
        if (body.action === 'end') resource.state = 'ended';
        if (body.action === 'mute') members[1].muted = true;
        resource.version++;
        return respond({accepted: true, id: resource.id, action_id: 'action-1', state: resource.state, version: resource.version, operation_ids: ['operation-1']});
      }
      if (/\/resources\/[^/]+$/.test(pathname)) {
        const resource = resources.find(row => pathname.endsWith('/' + row.id))!;
        const detail: ResourceDetail = {resource, members, members_truncated: false, bindings: [{id: 'binding-1', node_id: 'node-1', purpose: 'conference', state: 'active', generation: 1, created: now}], actions: [], details: {meeting: {number: '123456789', host: 'host', starts_at: now, duration_minutes: 60, waiting_room: true, locked: false, sharing_member: ''}}};
        return respond(detail);
      }
      if (pathname.endsWith('/operations')) return respond({items: actions.length ? [{id: 'operation-1', organization_id: org || 'org-a', resource_id: 'meeting-org-a', binding_id: 'binding-1', subject_id: '', action: 'DeleteRoom', state: 'queued', job_id: 'job-1', created: now, updated: now}] : [], total: actions.length ? 1 : 0, limit: 20, offset: 0});
      if (pathname.endsWith('/media-nodes')) return respond({items: [{id: 'node-1', incarnation: 'boot-1', configured: true, enabled: true, healthy: true, status: 'healthy', capacity: 100, bindings: 2, updated: now}], total: 1, limit: 100, offset: 0});
    }
    if (pathname.endsWith('/permissions')) return respond({catalog: ['av.manage', 'members.manage', 'jobs.manage'], effective: ['*']});
    return respond({items: [], total: 0, limit: 20, offset: 0});
  });
  return {actions, failNext: () => {failNextAction = true;}, pauseOrgA: (promise: Promise<void>) => {pauseA = promise;}};
}
async function login(page: BrowserPage) {
  await page.goto('/apps/admin/');
  await page.getByLabel('账号', {exact: true}).fill('operator');
  await page.getByLabel('密码', {exact: true}).fill('testing-password');
  await page.getByRole('button', {name: '登录', exact: true}).click();
  await expect(page.getByRole('combobox', {name: '管理范围'})).toBeVisible();
}
async function chooseOrganization(page: BrowserPage, name: string) {
  await page.getByRole('combobox', {name: '管理范围'}).focus();
  await page.getByRole('combobox', {name: '管理范围'}).press('ArrowDown');
  await page.getByTitle(name, {exact: true}).click();
}

test('platform governance uses explicit organization and separates business from cleanup', async ({page}) => {
  const mock = await fixture(page); await login(page);
  await chooseOrganization(page, '产品团队');
  await page.getByRole('menuitem', {name: '会议管理', exact: true}).click();
  await expect(page.getByRole('button', {name: '产品评审会议', exact: true})).toBeVisible();
  await expect(page.getByText('研发同步会议', {exact: true})).toHaveCount(0);
  await page.getByRole('button', {name: '查看详情', exact: true}).first().click();
  await page.getByRole('button', {name: '结束会话', exact: true}).click();
  await page.getByRole('button', {name: '确认结束会话'}).click();
  await expect(page.getByText('请填写操作原因。', {exact: true})).toBeVisible();
  await page.getByLabel('操作原因').fill('本次会议已完成，清理遗留会话');
  await page.getByRole('button', {name: '确认结束会话'}).click();
  await expect(page.getByText('业务状态：已结束')).toBeVisible();
  await page.getByRole('tab', {name: '媒体操作', exact: true}).click();
  await expect(page.getByText('排队中', {exact: true}).first()).toBeVisible();
  expect(mock.actions).toHaveLength(1);
  expect(mock.actions[0].body).toMatchObject({action: 'end', reason: '本次会议已完成，清理遗留会话', version: 1});
  expect(mock.actions[0].organization).toBe('org-a'); expect(mock.actions[0].key).toBeTruthy();
});

test('failed actions keep one idempotency key and host removal is unavailable', async ({page}) => {
  const mock = await fixture(page); await login(page); await chooseOrganization(page, '产品团队');
  await page.getByRole('menuitem', {name: '会议管理', exact: true}).click();
  await page.getByRole('button', {name: '查看详情', exact: true}).first().click();
  await page.getByRole('tab', {name: '成员（2）'}).click();
  const hostRow = page.getByRole('row').filter({hasText: '会议主持人'});
  await expect(hostRow.getByRole('button', {name: '移出成员'})).toHaveCount(0);
  const memberRow = page.getByRole('row').filter({hasText: '测试成员'});
  await memberRow.getByRole('button', {name: '静音', exact: true}).click();
  await page.getByLabel('操作原因').fill('处理持续背景噪声'); mock.failNext();
  await page.getByRole('button', {name: '确认静音'}).click();
  await expect(page.getByText('再次提交将重试同一请求。', {exact: false})).toBeVisible();
  await page.getByRole('button', {name: '确认静音'}).click();
  await expect(page.getByText('业务操作已受理。')).toBeVisible();
  expect(mock.actions).toHaveLength(2); expect(mock.actions[0].key).toBe(mock.actions[1].key); expect(mock.actions[0].body).toEqual(mock.actions[1].body);
});

test('organization change discards a delayed previous organization response', async ({page}) => {
  const mock = await fixture(page); await login(page);
  let release!: () => void; const delayed = new Promise<void>(resolve => {release = resolve;}); mock.pauseOrgA(delayed);
  await chooseOrganization(page, '产品团队');
  await page.getByRole('menuitem', {name: '会议管理', exact: true}).click();
  await chooseOrganization(page, '研发团队');
  await expect(page.getByRole('button', {name: '研发同步会议', exact: true})).toBeVisible();
  release();
  await expect(page.getByRole('button', {name: '产品评审会议', exact: true})).toHaveCount(0);
  await expect(page.getByRole('menuitem', {name: '媒体节点', exact: true})).toHaveCount(0);
});

test('organization administrators see only available organizations and cannot access global nodes', async ({page}) => {
  await fixture(page, false); await login(page); await chooseOrganization(page, '产品团队');
  await expect(page.getByRole('menuitem', {name: '会议管理', exact: true})).toBeVisible();
  await expect(page.getByRole('menuitem', {name: '媒体节点', exact: true})).toHaveCount(0);
  await page.getByRole('combobox', {name: '管理范围'}).focus();
  await page.getByRole('combobox', {name: '管理范围'}).press('ArrowDown');
  await expect(page.getByTitle('研发团队', {exact: true})).toHaveCount(0);
});

test('responsive overview and resource layouts render without document overflow', async ({page}) => {
  await fixture(page); await login(page);
  for (const width of [1440, 800, 390]) {
    await page.setViewportSize({width, height: 960});
    await expect(page.getByRole('heading', {name: '运营概览', exact: true})).toBeVisible();
    await page.screenshot({path: `out/screenshots/overview-${width}.png`, fullPage: true});
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
  }
  await page.setViewportSize({width: 1440, height: 960});
  await page.getByRole('switch').click();
  await page.screenshot({path: 'out/screenshots/overview-dark.png', fullPage: true});
  await page.getByRole('menuitem', {name: '媒体节点', exact: true}).click();
  await expect(page.getByRole('cell', {name: 'node-1', exact: true})).toBeVisible();
  await page.screenshot({path: 'out/screenshots/nodes.png', fullPage: true});
});
