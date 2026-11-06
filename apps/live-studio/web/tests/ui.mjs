import { chromium } from 'playwright';
import assert from 'node:assert/strict';
import { uiHarness, syntheticVideo } from '../../../../packages/web/ui/test-utils.mjs';
import { componentChecks } from '../../../../packages/web/ui/component-tests.mjs';
import typescript from 'typescript';

const ui = await uiHarness(chromium, 'live-studio');
try {
  await componentChecks(ui, typescript);
  for (const width of [390, 800, 1280, 1440]) {
    const page = await ui.browser.newPage({viewport:{width, height:1000}});
    const errors = [];
    let authenticated = false, failAuth = true, authCalls = 0, disableCalls = 0;
    const user = {id:'viewer', name:'体验用户', admin:true};
    const rooms = Array.from({length:6}, (_, index) => ({id:`room-${index}`, title:['一起聊聊声音与画面','周五创作现场','从零开始的直播课堂'][index % 3], state:index === 2 ? 'offline' : 'live', broadcast_id:index === 2 ? '' : `broadcast-${index}`}));
    page.on('pageerror', error => errors.push(String(error)));
    await page.routeWebSocket('**/api/v1/av/realtime/ws**', socket => { socket.onMessage(() => {}); });
    await page.route('**/api/**', async route => {
      const path = new URL(route.request().url()).pathname;
      if (path === '/api/v1/auth/me') return route.fulfill(authenticated ? {json:user} : {status:401, json:{error:{code:'unauthorized'}}});
      if (path === '/api/v1/auth/login' || path === '/api/v1/auth/register') {
        authCalls++;
        await new Promise(resolve => setTimeout(resolve, 150));
        if (failAuth) return route.fulfill({status:401, json:{error:{code:'unauthorized'}}});
        authenticated = true; return route.fulfill({json:{user}});
      }
      if (path === '/api/v1/organizations') return route.fulfill({json:{items:[{id:'organization',data:{name:'设计工作室'}}]}});
      if (path === '/api/v1/users') return route.fulfill({json:{items:[user,{id:'other',name:'创作者小林',admin:false,disabled:false}]}});
      if (path.endsWith('/disable')) { disableCalls++; return route.fulfill({json:{}}); }
      if (path.endsWith('/password-reset')) return route.fulfill({json:{token:'fixture-reset-token-with-a-long-value-for-wrapping'}});
      if (path === '/api/v1/av/live-studio/rooms') return route.fulfill({json:{items:rooms}});
      if (path === '/api/v1/av/live-studio/board') return route.fulfill({json:{items:[{subject_id:'山间电台',points:128},{subject_id:'小林的创作间',points:96},{subject_id:'设计工作室',points:72}]}});
      if (path.includes('/live-studio/rooms/')) return route.fulfill({json:rooms[0]});
      if (path.endsWith('/watch')) return route.fulfill({json:{subject_id:'viewer',credential:'fixture'}});
      if (path.endsWith('/connect')) return route.fulfill({json:{grant_id:'fixture'}});
      if (path.endsWith('/credentials')) return route.fulfill({status:503,json:{error:{code:'media_unavailable'}}});
      if (path.endsWith('/realtime/tickets')) return route.fulfill({json:{ticket:'fixture'}});
      if (path.endsWith('/wallet')) return route.fulfill({json:{balance:100}});
      if (/\/broadcasts\/[^/]+$/.test(path)) return route.fulfill({json:{broadcast:{id:'broadcast-0',state:'live'},host:'host',room_id:'room-0',online:12,members:[],stage_members:[],leaderboard:[{subject_id:'山间电台',points:12}],sequence:0}});
      return route.fulfill({json:{}});
    });
    await page.goto(ui.url);
    await page.getByRole('button',{name:'登录 / 注册',exact:true}).waitFor();
    await page.locator('.av-empty').waitFor();
    await ui.shot(page, `${width}-empty`);
    const account = page.locator('#account');
    await account.click();
    await page.locator('#auth-dialog[open]').waitFor();
    assert.equal(await page.evaluate(() => document.activeElement.id), 'username');
    await ui.shot(page, `${width}-login`);
    await page.keyboard.press('Escape');
    assert(await account.evaluate(element => document.activeElement === element));
    await account.click();
    await page.locator('#username').fill('viewer');
    await page.locator('#password').fill('wrong-password');
    await page.locator('#submit-auth').click();
    await page.waitForFunction(() => document.querySelector('#auth-error').textContent.includes('不正确'));
    assert.equal(await page.locator('#username').inputValue(),'viewer');
    await ui.shot(page, `${width}-login-error`);
    failAuth = false;
    await page.locator('#toggle-auth').click();
    await page.locator('#password').fill('fixture-password');
    await ui.shot(page, `${width}-register`);
    await page.locator('#submit-auth').click();
    await page.locator('#auth-form').evaluate(form => { form.requestSubmit(); form.requestSubmit(); });
    await page.waitForFunction(() => !document.querySelector('#auth-dialog').open);
    assert.equal(authCalls,2,'busy form must suppress duplicate registration');
    await page.waitForFunction(() => document.querySelectorAll('.room-card').length === 6);
    await ui.shot(page, `${width}-lobby`);
    await account.click();
    await ui.shot(page, `${width}-organization`);
    await page.keyboard.press('Escape');
    await page.locator('#admin').click();
    await page.locator('#users .entry').nth(1).waitFor();
    await ui.shot(page, `${width}-management`);
    await page.locator('#users .entry').nth(1).getByRole('button',{name:'禁用',exact:true}).click();
    await page.locator('dialog[open]').waitFor();
    assert.equal(await page.evaluate(() => document.activeElement.textContent),'取消');
    await ui.shot(page, `${width}-confirm`);
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => !document.querySelector('dialog[open]'));
    assert.equal(disableCalls,0,'Enter on default cancel must not disable an account');
    await page.evaluate(() => { location.hash='room/room-0'; });
    await page.waitForFunction(() => document.querySelector('#status').textContent.length > 0);
    await page.waitForFunction(() => document.querySelector('#notice').textContent.includes('媒体服务暂时不可用'));
    await ui.shot(page, `${width}-watch-error`);
    await page.evaluate(() => {
      const video=document.createElement('video');video.playsInline=true;video.autoplay=true;document.querySelector('#video-stage').append(video);
      document.querySelector('#empty-video').hidden=true;document.querySelector('#notice').textContent='';
      const line=document.createElement('div');line.className='chat-message';line.textContent='创作者小林：欢迎来到直播间，今天一起聊聊创作。';document.querySelector('#messages').append(line);
    });
    await syntheticVideo(page, '#video-stage video', 'Live Studio Preview');
    await ui.shot(page, `${width}-watch`);
    assert.deepEqual(errors,[]);
    await page.close();
  }
  console.log('PASS live-studio: 4 widths; login/register/focus/busy/error, lobby, admin cancellation and synthetic viewer. Screenshots:',ui.output);
} finally { await ui.close(); }
