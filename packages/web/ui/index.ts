export type ButtonVariant = 'secondary' | 'primary' | 'danger' | 'ghost';
export type FeedbackTone = 'neutral' | 'info' | 'success' | 'warning' | 'error';

let nextDialog = 0;

export function createButton(text: string, variant: ButtonVariant = 'secondary') {
  const button = document.createElement('button');
  button.type = 'button';
  button.textContent = text;
  button.className = `av-button av-button--${variant}`;
  return button;
}

export interface DialogOptions {
  id?: string;
  title: string;
  description?: string;
}

/** Uses the browser's modal focus containment. No business requests live here. */
export function createDialog(options: DialogOptions) {
  const element = document.createElement('dialog');
  element.id = options.id ?? `av-dialog-${++nextDialog}`;
  element.className = 'av-dialog';
  const heading = document.createElement('header');
  heading.className = 'av-dialog__heading';
  const titles = document.createElement('div');
  const title = document.createElement('h2');
  title.id = `${element.id}-title`;
  title.textContent = options.title;
  const description = document.createElement('p');
  description.textContent = options.description ?? '';
  description.hidden = !options.description;
  titles.append(title, description);
  const close = createButton('×', 'ghost');
  close.classList.add('av-dialog__close');
  close.setAttribute('aria-label', '关闭弹窗');
  heading.append(titles, close);
  const content = document.createElement('div');
  content.className = 'av-dialog__content';
  const error = document.createElement('p');
  error.className = 'av-alert';
  error.dataset.tone = 'error';
  error.setAttribute('role', 'alert');
  const actions = document.createElement('footer');
  actions.className = 'av-dialog__actions';
  element.setAttribute('aria-labelledby', title.id);
  element.append(heading, content, error, actions);
  let trigger: HTMLElement | null = null;
  let busy = false;
  close.onclick = () => element.close('cancel');
  element.addEventListener('cancel', event => { if (busy) event.preventDefault(); });
  element.addEventListener('keydown', event => {
    if (event.key !== 'Tab') return;
    const items = Array.from(element.querySelectorAll<HTMLElement>(
      'button:not([disabled]), a[href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])',
    )).filter(item => item.getClientRects().length > 0 && item.tabIndex >= 0);
    if (!items.length) { event.preventDefault(); return; }
    const first = items[0], last = items[items.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault(); last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault(); first.focus();
    }
  });
  element.addEventListener('close', () => {
    if (trigger?.isConnected) trigger.focus();
  });
  return {
    element, title, description, close, content, actions, error,
    open(initialFocus?: HTMLElement) {
      if (element.open) return;
      trigger = document.activeElement instanceof HTMLElement ? document.activeElement : null;
      error.textContent = '';
      element.showModal();
      (initialFocus ?? content.querySelector<HTMLElement>('input, select, button') ?? close).focus();
    },
    setBusy(value: boolean) {
      busy = value;
      element.setAttribute('aria-busy', String(value));
      close.disabled = value;
    },
  };
}

export async function confirmAction(title: string, description: string, confirmText = '确认') {
  const dialog = createDialog({title, description});
  const cancel = createButton('取消');
  const confirm = createButton(confirmText, 'danger');
  dialog.actions.append(cancel, confirm);
  cancel.onclick = () => dialog.element.close('cancel');
  confirm.onclick = () => dialog.element.close('confirm');
  document.body.append(dialog.element);
  return new Promise<boolean>(resolve => {
    dialog.element.addEventListener('close', () => {
      const accepted = dialog.element.returnValue === 'confirm';
      dialog.element.remove();
      resolve(accepted);
    }, {once: true});
    dialog.open(cancel);
  });
}

export function emptyState(title: string, description: string) {
  const element = document.createElement('div');
  element.className = 'av-empty';
  const mark = document.createElement('span');
  mark.className = 'av-empty__mark';
  mark.setAttribute('aria-hidden', 'true');
  mark.innerHTML = '<svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="14" rx="3"/><path d="M8 21h8m-4-3v3M8 10h8m-8 4h5"/></svg>';
  const heading = document.createElement('h3');
  heading.textContent = title;
  const text = document.createElement('p');
  text.textContent = description;
  element.append(mark, heading, text);
  return element;
}

/** Mirror a live status without replacing its accessibility or business owner. */
export function observeStatus(element: HTMLElement, classify: (text: string) => FeedbackTone) {
  const sync = () => { element.dataset.tone = classify(element.textContent?.trim() ?? ''); };
  const observer = new MutationObserver(sync);
  observer.observe(element, {subtree: true, childList: true, characterData: true});
  sync();
  return () => observer.disconnect();
}
