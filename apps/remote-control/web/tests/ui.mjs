import { chromium } from 'playwright';
import assert from 'node:assert/strict';
import { uiHarness, syntheticVideo } from '../../../../packages/web/ui/test-utils.mjs';

const ui = await uiHarness(chromium, 'remote-control');
try {
  for (const width of [390, 800, 1280, 1440]) {
    const page = await ui.browser.newPage({viewport:{width, height:900}});
    const errors = []; let fail = false;
    page.on('pageerror', error => errors.push(String(error)));
    await page.route('**/api/**', route => {
      const path = new URL(route.request().url()).pathname;
      assert(!path.includes('/auth/'), 'Connection-code flow must remain available without login');
      if (fail) return route.fulfill({status:404, json:{error:{code:'code_not_found'}}});
      if (path.endsWith('/redeem')) return route.fulfill({json:{resource_id:'fixture', credential:'fixture'}});
      const grant = {id:'grant', epoch:1, state:'pending', lease_remaining_ms:10000};
      if (path.endsWith('/request')) return route.fulfill({json:grant});
      return route.fulfill({json:{id:'fixture', control_grant:grant}});
    });
    await page.goto(ui.url);
    await ui.shot(page, `${width}-idle`);
    assert(await page.locator('#remote-text').isDisabled());
    await page.locator('#code').fill('0123456789');
    await page.locator('#name').fill('协助者小明');
    await page.locator('#connect').click();
    await page.waitForFunction(() => document.querySelector('#badge').textContent === '等待确认');
    assert(await page.locator('#connect').isDisabled());
    assert(await page.locator('#send-text').isDisabled());
    await ui.shot(page, `${width}-pending`);
    await page.locator('#stop').click();
    await page.waitForFunction(() => document.querySelector('#badge').textContent === '已结束');
    await page.waitForFunction(() => !document.querySelector('#connect').disabled);
    await ui.shot(page, `${width}-ended`);
    fail = true;
    await page.locator('#connect').click();
    await page.waitForFunction(() => document.querySelector('#status').textContent.includes('申请失败'));
    await ui.shot(page, `${width}-error`);
    await syntheticVideo(page, '#screen', 'Remote Assistance Preview');
    await page.evaluate(() => {
      document.querySelector('#placeholder').style.display = 'none';
      document.querySelector('#badge').textContent = '已连接';
      document.querySelector('#status').textContent = '已连接。点击画面启用键鼠操作。';
      document.querySelector('#stop').disabled = false;
    });
    await ui.shot(page, `${width}-connected`);
    assert.equal(await page.locator('#connection-details').evaluate(element => element.open), false);
    await page.locator('#viewport').scrollIntoViewIfNeeded();
    const viewport = await page.locator('#viewport').boundingBox();
    assert(await page.evaluate(({x,y}) => document.elementFromPoint(x,y)?.closest('#viewport') !== null, {x: viewport.x + viewport.width / 2, y: viewport.y + Math.min(viewport.height / 2, 100)}));
    assert.deepEqual(errors, []);
    await page.close();
  }
  console.log('PASS remote-control: 4 widths; no-login connection, pending, ended, failure and synthetic viewport. Screenshots:', ui.output);
} finally { await ui.close(); }
