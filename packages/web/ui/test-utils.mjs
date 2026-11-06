// Shared headless UI harness. Synthetic fixtures never capture user devices.
import { createServer } from 'node:http';
import { readFile, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { resolve, relative, extname } from 'node:path';
import assert from 'node:assert/strict';

export async function uiHarness(chromium, app) {
  const dist = resolve('dist');
  const output = resolve(`../../../out/ui-refresh/web/${app}`);
  await mkdir(output, {recursive: true});
  const prefix = `/apps/${app}`;
  const server = createServer(async (req, res) => {
    try {
      let path = new URL(req.url, 'http://localhost').pathname;
      if (path.startsWith(prefix + '/')) path = path.slice(prefix.length);
      const file = resolve(dist, '.' + (path === '/' ? '/index.html' : path));
      const local = relative(dist, file);
      if (local.startsWith('..') || local.includes(':')) { res.writeHead(403); res.end(); return; }
      const data = await readFile(file);
      res.setHeader('Content-Type', ({'.html':'text/html; charset=utf-8', '.js':'text/javascript', '.css':'text/css'})[extname(file)] || 'application/octet-stream');
      res.end(data);
    } catch { res.writeHead(404); res.end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const executablePath = process.env.UI_CHROME || process.env.SC_CHROME || [
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  ].find(existsSync);
  let browser;
  try { browser = await chromium.launch({headless: true, ...(executablePath ? {executablePath} : {})}); }
  catch (error) { await new Promise(resolve => server.close(resolve)); throw error; }
  return {
    browser, output,
    url: `http://127.0.0.1:${server.address().port}${prefix}/`,
    async close() { await browser.close(); await new Promise(resolve => server.close(resolve)); },
    async shot(page, name) {
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${app}/${name}: horizontal overflow`);
      const modal = page.locator('dialog[open]');
      if (await modal.count()) {
        const rect = await modal.last().boundingBox();
        const viewport = page.viewportSize();
        assert(rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= viewport.width + 1 && rect.y + rect.height <= viewport.height + 1, `${name}: dialog exceeds viewport`);
      }
      await page.screenshot({path: resolve(output, `${name}.png`), fullPage: true});
    },
  };
}

export async function syntheticVideo(page, selector, title) {
  await page.evaluate(({selector, title}) => {
    const canvas = document.createElement('canvas');
    canvas.width = 1280; canvas.height = 720;
    const context = canvas.getContext('2d');
    context.fillStyle = '#17365e'; context.fillRect(0, 0, 1280, 720);
    context.fillStyle = '#284d7b'; context.fillRect(48, 48, 1184, 624);
    context.fillStyle = '#edf4ff'; context.font = '48px sans-serif'; context.fillText(title, 96, 180);
    context.font = '24px sans-serif'; context.fillStyle = '#b9cde9';
    context.fillText('Synthetic UI fixture · no device capture', 96, 240);
    const video = document.querySelector(selector);
    video.srcObject = canvas.captureStream(1); video.muted = true; void video.play();
  }, {selector, title});
  await page.waitForFunction(selector => document.querySelector(selector).videoWidth === 1280, selector);
}
