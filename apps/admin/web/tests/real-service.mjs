// Real API + deployed production bundle. Uses isolated synthetic accounts only.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {dirname, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {chromium, expect} from '@playwright/test';

const here = dirname(fileURLToPath(import.meta.url));
const base = process.env.AV_ADMIN_TEST_URL;
const name = process.env.AV_ADMIN_TEST_NAME;
const password = process.env.AV_ADMIN_TEST_PASSWORD;
const organization = process.env.AV_ADMIN_TEST_ORGANIZATION;
const resource = process.env.AV_ADMIN_TEST_RESOURCE;
assert.ok(base && name && password && organization && resource, 'Use the isolated control-plane integration runner');
const directory = resolve(here, '../out/real-service');
const output = process.env.AV_ADMIN_TEST_OUTPUT || resolve(directory, 'report.json');
await mkdir(directory, {recursive: true});
const report = {passed: false, real_api: true, production_bundle: true, checks: [], screenshots: [], page_errors: [], asset_errors: []};
const browser = await chromium.launch({headless: true, ...(process.env.AV_CHROME_PATH ? {executablePath: process.env.AV_CHROME_PATH} : {})});
report.browser = browser.version();
const context = await browser.newContext({viewport: {width: 1440, height: 1000}, reducedMotion: 'reduce'});
report.reduced_motion = true;
const page = await context.newPage();
page.on('pageerror', error => report.page_errors.push(error.message));
const unexpected = [];
page.on('response', response => {
  if (new URL(response.url()).pathname.startsWith('/apps/admin/') && response.status() >= 400) {
    report.asset_errors.push({path: new URL(response.url()).pathname, status: response.status()});
  }
  if (response.url().includes('/api/') && response.status() >= 400 && response.status() !== 401) {
    unexpected.push({path: new URL(response.url()).pathname, status: response.status()});
  }
});
async function screenshot(label) {
  const path = resolve(directory, label + '.png');
  await page.screenshot({path, fullPage: true});
  report.screenshots.push(path);
}
async function saveReport() {
  await mkdir(dirname(output), {recursive: true});
  await writeFile(output, JSON.stringify(report, null, 2) + '\n');
}
async function route(hash, heading) {
  await page.evaluate(value => {location.hash = value;}, hash);
  await expect(page.getByRole('heading', {name: heading, exact: true})).toBeVisible();
}
async function selectScope(label, target) {
  const picker = page.getByRole('combobox', {name: '管理范围'});
  await page.locator('.ant-select').filter({has: picker}).locator('.ant-select-selector').click();
  await expect(picker).toHaveAttribute('aria-expanded', 'true');
  const option = page.locator('.ant-select-dropdown:not(.ant-select-dropdown-hidden)').getByTitle(label, {exact: true});
  await expect(option).toBeVisible();
  const selection = {label, opened: true, option_visible: true};
  (report.scope_selections ??= []).push(selection);
  const ready = page.waitForResponse(response => response.request().method() === 'GET' &&
    new URL(response.url()).pathname === (target ? `/api/v1/admin/organizations/${target}/context` : '/api/v1/context') && response.status() === 200)
    .then(() => null, error => error);
  try {await option.click();} catch (error) {
    selection.layout = await option.evaluate(element => {
      const popup = element.closest('.ant-select-dropdown');
      const style = getComputedStyle(popup);
      return {option: element.getBoundingClientRect().toJSON(), popup: popup.getBoundingClientRect().toJSON(),
        left: style.left, top: style.top, transform: style.transform, opacity: style.opacity,
        viewport: {width: innerWidth, height: innerHeight}, scroll: {x: scrollX, y: scrollY},
        popupClass: popup.className, popupStyle: popup.getAttribute('style'), mode: document.compatMode};
    }).catch(() => null);
    throw error;
  }
  selection.click_completed = true;
  selection.display_after_click = await page.locator('.ant-select').filter({has: picker}).textContent();
  await saveReport();
  const error = await ready;
  if (error) throw error;
  await expect(page.getByRole('combobox', {name: '管理范围'})).toBeVisible();
}
try {
  const document = await page.goto(base + '/apps/admin/');
  assert.equal(document.status(), 200);
  assert.ok((await document.text()).includes('type="module"'), 'The deployed entry must reference the built JavaScript bundle');
  await page.getByLabel('账号', {exact: true}).fill(name);
  await page.getByLabel('密码', {exact: true}).fill(password);
  await page.getByRole('button', {name: '登录', exact: true}).click();
  await expect(page.getByRole('heading', {name: '运营概览', exact: true})).toBeVisible();
  await expect(page.getByRole('combobox', {name: '管理范围'})).toBeVisible();
  await screenshot('platform-overview');
  report.checks.push('Real login and platform overview');

  await route('av-nodes', '媒体节点');
  await expect(page.getByRole('cell', {name: 'integration-node', exact: true})).toBeVisible();
  await screenshot('media-nodes');
  report.checks.push('Read-only live node status from actual SFU reconciliation');

  await selectScope('Managed outside platform membership', organization);
  await route('av-overview', '运营概览');
  await expect(page.getByRole('menuitem', {name: '媒体节点', exact: true})).toHaveCount(0);
  for (const [hash, heading] of [['members', '成员'], ['roles', '角色与权限'], ['audit', '审计'], ['jobs', '任务中心']]) {
    await route(hash, heading);
    await expect(page.getByText('加载失败', {exact: true})).toHaveCount(0);
  }
  report.checks.push('Non-member platform administrator enters organization and reuses Foundation management pages');

  await route('av-conference', '会议管理');
  await page.getByLabel('名称或编号', {exact: true}).fill(resource);
  const listed = page.waitForResponse(response => new URL(response.url()).pathname.endsWith('/resources') && response.url().includes(encodeURIComponent(resource)));
  await page.getByRole('button', {name: '查询', exact: true}).click();
  await listed;
  await page.getByRole('button', {name: 'Admin UI governance meeting', exact: true}).click();
  await page.getByRole('button', {name: '结束会话', exact: true}).click();
  const modal = page.getByRole('dialog', {name: '结束会话', exact: true});
  await modal.getByLabel('操作原因', {exact: true}).fill('Headless real-service administrative acceptance');
  const changed = page.waitForResponse(response => response.request().method() === 'POST' && new URL(response.url()).pathname.endsWith(`/resources/${resource}/actions`));
  await modal.getByRole('button', {name: '确认结束会话', exact: true}).click();
  const response = await changed;
  assert.equal(response.status(), 200);
  const result = await response.json();
  assert.ok(result.accepted && result.action_id);
  assert.equal(result.state, 'ended');
  await expect(modal).toHaveCount(0);
  await expect(page.getByText('已结束', {exact: true}).first()).toBeVisible();
  await screenshot('governance-result');
  await page.locator('.ant-drawer-close').click();
  report.checks.push('Resource search, detail, reason confirmation and actual meeting termination');

  await route('av-operations', '媒体操作');
  await screenshot('media-operations');
  await selectScope('全部组织', '');
  await route('av-overview', '运营概览');
  await expect(page.getByRole('menuitem', {name: '媒体节点', exact: true})).toBeVisible();
  await page.getByRole('switch').click();
  await screenshot('platform-dark');
  for (const width of [800, 390]) {
    await page.setViewportSize({width, height: 1000});
    await screenshot(`platform-${width}`);
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), `Unexpected horizontal overflow at ${width}px`);
  }
  report.checks.push('Scope reset, dark theme and 800/390px responsive layout');
  assert.deepEqual(report.page_errors, []);
  assert.deepEqual(report.asset_errors, []);
  assert.deepEqual(unexpected, []);
  report.passed = true;
} catch (error) {
  report.error = String(error).replaceAll(password, '[redacted]');
  await screenshot('failure').catch(() => {});
  process.exitCode = 1;
} finally {
  report.ui_assertions_passed = report.passed;
  report.passed = false;
  await saveReport();
  try {
    await context.close();
    await browser.close();
    report.browser_cleanup_completed = true;
    report.passed = report.ui_assertions_passed;
  } catch (error) {report.cleanup_error = String(error); report.passed = false;}
  if (!report.passed) process.exitCode = 1;
  await saveReport();
  console.log(JSON.stringify(report, null, 2));
}
