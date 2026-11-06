import { chromium } from 'playwright';
import assert from 'node:assert/strict';
import { uiHarness, syntheticVideo } from '../../../../packages/web/ui/test-utils.mjs';

const ui = await uiHarness(chromium, 'screen-cast');
try {
  for (const width of [390, 800, 1280, 1440]) {
    const page = await ui.browser.newPage({viewport: {width, height: 900}});
    let state = 'waiting', fail = false;
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    await page.route('**/api/**', route => {
      const path = new URL(route.request().url()).pathname;
      if (path === '/api/v1/auth/me') return route.fulfill({json: {id:'fixture-user', name:'体验用户'}});
      if (path === '/api/v1/organizations') return route.fulfill({json: {items:[{id:'fixture-org', data:{name:'设计工作室'}}]}});
      if (!path.startsWith('/api/v1/av/screen-cast/')) throw new Error(`Unexpected request: ${path}`);
      if (fail) return route.fulfill({status:503, json:{error:{code:'media_unavailable'}}});
      return route.fulfill({json: {id:'fixture', credential:'fixture', pairing_code:'0123456789', state, reason:'', expires_at:new Date(Date.now()+300000).toISOString()}});
    });
    await page.goto(ui.url);
    await page.getByRole('button', {name:'账号与组织', exact:true}).waitFor();
    await page.waitForFunction(() => document.querySelector('.av-account-session select').value === 'fixture-org');
    await ui.shot(page, `${width}-idle`);
    const account = page.getByRole('button', {name:'账号与组织', exact:true});
    await account.click();
    await page.locator('#account-dialog[open]').waitFor();
    await ui.shot(page, `${width}-account`);
    assert.equal(await page.evaluate(() => document.activeElement.tagName), 'SELECT');
    await page.keyboard.press('Escape');
    assert(await account.evaluate(element => document.activeElement === element));
    await page.locator('#receive').click();
    await page.waitForFunction(() => document.querySelector('#code').textContent === '0123456789');
    assert(await page.locator('#receive').isDisabled());
    await ui.shot(page, `${width}-waiting`);
    state = 'expired';
    await page.waitForFunction(() => document.querySelector('#badge').textContent === '投屏码已过期');
    await ui.shot(page, `${width}-expired`);
    fail = true;
    await page.locator('#receive').click();
    await page.waitForFunction(() => document.querySelector('#error').textContent.length > 0);
    await ui.shot(page, `${width}-error`);
    await syntheticVideo(page, 'video', 'Screen Cast Preview');
    await page.evaluate(() => {
      document.body.dataset.playing = 'true';
      document.querySelector('#placeholder').hidden = true;
      document.querySelector('#badge').textContent = '正在播放';
      document.querySelector('#code').textContent = '已配对';
      document.querySelector('#error').textContent = '';
      document.querySelector('#fullscreen').disabled = false;
    });
    await ui.shot(page, `${width}-playing`);
    assert.deepEqual(errors, []);
    await page.close();
  }
  console.log('PASS screen-cast: 4 widths; account focus, waiting, expired, failure and synthetic playback. Screenshots:', ui.output);
} finally { await ui.close(); }
