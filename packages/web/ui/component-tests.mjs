import { readFile } from 'node:fs/promises';
import assert from 'node:assert/strict';

/** Exercise the real shared DOM source in isolation from media/business state. */
export async function componentChecks(ui, typescript) {
  const css = await readFile(new URL('./style.css', import.meta.url), 'utf8');
  const source = await readFile(new URL('./index.ts', import.meta.url), 'utf8');
  const module = typescript.transpileModule(source, {compilerOptions:{target:typescript.ScriptTarget.ES2022, module:typescript.ModuleKind.ES2022}}).outputText;
  const moduleUrl = 'data:text/javascript;base64,' + Buffer.from(module).toString('base64');
  for (const theme of ['light','dark']) {
    for (const scale of [1,2]) {
      const page = await ui.browser.newPage({viewport:{width:1000,height:900},deviceScaleFactor:scale});
      const errors = [];
      page.on('pageerror',error => errors.push(String(error)));
      await page.setContent('<html lang="zh-CN"><head><meta charset="utf-8"></head><body></body></html>');
      await page.addStyleTag({content:css});
      await page.evaluate(async ({moduleUrl,theme}) => {
        const kit = await import(moduleUrl);
        document.documentElement.dataset.theme = theme;
        document.body.style.cssText='background:var(--av-bg);color:var(--av-ink);padding:32px';
        document.body.innerHTML='<div class="av-page-header"><div><h1>通用 UI 组件</h1><p>按钮、表单、状态与弹窗 · '+theme+'</p></div></div><div class="av-card" id="controls"><h2>操作与反馈</h2><div id="buttons" style="display:flex;gap:12px;flex-wrap:wrap"></div><div style="display:grid;grid-template-columns:1fr 1fr;gap:24px;margin-top:24px"><label>输入框<input placeholder="请输入内容"></label><label>当前组织<select><option>设计工作室</option></select></label></div><p class="av-alert" data-tone="error" style="margin:24px 0 0">保存失败，请检查网络连接后重试。已填写的内容会为你保留。</p></div><div id="empty" style="margin-top:24px"></div>';
        for (const [label,variant] of [['主要操作','primary'],['次要操作','secondary'],['危险操作','danger'],['文字操作','ghost']]) {
          const button=kit.createButton(label,variant);document.querySelector('#buttons').append(button);
        }
        const disabled=kit.createButton('暂不可用');disabled.disabled=true;document.querySelector('#buttons').append(disabled);
        document.querySelector('#empty').append(kit.emptyState('还没有内容','创建第一项内容后，它会出现在这里。'));
        const open=kit.createButton('打开设置','primary');open.id='open-dialog';document.querySelector('#buttons').append(open);
        const dialog=kit.createDialog({title:'统一设置弹窗',description:'较长的中文说明会自然换行，内容区域与操作区域保持明确的层级。'});
        dialog.content.innerHTML='<label>显示名称<input id="dialog-name" value="一个很长的中文显示名称，用于检查输入和布局"></label>';
        const cancel=kit.createButton('取消');cancel.onclick=()=>dialog.element.close('cancel');
        const save=kit.createButton('保存','primary');save.onclick=()=>dialog.element.close('save');
        dialog.actions.append(cancel,save);document.body.append(dialog.element);
        open.onclick=()=>dialog.open();
        const danger=document.querySelector('.av-button--danger');
        danger.onclick=async()=>{window.confirmed=await kit.confirmAction('结束当前会话？','结束后将停止所有参与者的当前连接，此操作需要明确确认。','结束会话');};
      },{moduleUrl,theme});
      const ratio=await page.locator('#open-dialog').evaluate(button=>{
        const color=getComputedStyle(button);
        const luminance=value=>value.match(/[\d.]+/g).slice(0,3).map(Number).map(x=>{x/=255;return x<=.04045?x/12.92:((x+.055)/1.055)**2.4;}).reduce((n,x,i)=>n+x*[.2126,.7152,.0722][i],0);
        const a=luminance(color.color),b=luminance(color.backgroundColor);
        return (Math.max(a,b)+.05)/(Math.min(a,b)+.05);
      });
      assert(ratio>=4.5,`${theme} primary text contrast ${ratio}`);
      await ui.shot(page,`components-${theme}-${scale}x`);
      await page.locator('#open-dialog').click();
      assert.equal(await page.evaluate(()=>document.activeElement.id),'dialog-name');
      for(let i=0;i<6;i++){
        await page.keyboard.press('Tab');
        assert(await page.evaluate(()=>!!document.activeElement.closest('dialog')),'modal must contain keyboard focus');
      }
      await ui.shot(page,`components-${theme}-${scale}x-dialog`);
      await page.keyboard.press('Escape');
      assert.equal(await page.evaluate(()=>document.activeElement.id),'open-dialog');
      await page.locator('#buttons .av-button--danger').click();
      assert.equal(await page.evaluate(()=>document.activeElement.textContent),'取消');
      await page.keyboard.press('Enter');
      await page.waitForFunction(()=>window.confirmed===false);
      assert.deepEqual(errors,[]);
      await page.close();
    }
  }
  console.log('PASS shared components: light/dark, 1x/2x, contrast, modal focus containment/restoration and safe default cancellation');
}
