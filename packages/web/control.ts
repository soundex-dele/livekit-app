import { createButton, createDialog, confirmAction } from './ui';

export interface RequestOptions {
  credential?: string;
  keepalive?: boolean;
  signal?: AbortSignal;
  idempotencyKey?: string;
}
export interface MediaCredentials {
  media_url: string; media_token: string; participant_identity: string;
  expires_at: string; room?: string; policy_version?: number;
}
export interface Organization { id: string; data: { name: string } }
export interface ResourceEvent {
  id: string; resource_id: string; sequence: number; type: string;
  payload: Record<string, unknown>; created: number;
}
export class ControlError extends Error {
  constructor(public readonly code: string, public readonly status: number, public readonly retryable = false) { super(code); }
}

// Account cookies remain HttpOnly. Resource credentials live only in the
// caller's session state and never replace the account authentication token.
export class ControlClient {
  private organizationValue = '';
  private generation = 0;
  private pending = new Set<AbortController>();
  constructor(private readonly fetcher: typeof fetch = (...args) => fetch(...args)) {}
  get organization() { return this.organizationValue; }
  resetAccount() {
    this.organizationValue = '';
    this.cancelPending();
  }
  selectOrganization(id: string) {
    if (id === this.organizationValue) return;
    this.organizationValue = id;
    this.cancelPending();
  }
  private cancelPending() {
    this.generation++;
    for (const request of this.pending) request.abort();
    this.pending.clear();
  }
  async request<T = any>(path: string, body?: unknown, options: RequestOptions = {}): Promise<T> {
    const generation = this.generation;
    const controller = new AbortController();
    this.pending.add(controller);
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (body !== undefined) headers['X-Foundation-Request'] = '1';
    const businessRequest = !path.startsWith('/api/') || path.startsWith('/api/v1/av/');
    if (businessRequest && this.organizationValue && !options.credential) headers['X-Organization-ID'] = this.organizationValue;
    if (options.credential) headers['X-AV-Resource-Credential'] = options.credential;
    if (options.idempotencyKey) headers['Idempotency-Key'] = options.idempotencyKey;
    const signals = [controller.signal, AbortSignal.timeout(5000)];
    if (options.signal) signals.push(options.signal);
    try {
      const response = await this.fetcher(path.startsWith('/api/') ? path : '/api/v1/av/' + path, {
        method: body === undefined ? 'GET' : 'POST', credentials: 'same-origin', headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        signal: AbortSignal.any(signals), keepalive: options.keepalive,
      });
      const value = await response.json().catch(() => ({}));
      if (generation !== this.generation) throw new ControlError('organization_changed', 409);
      if (!response.ok) throw new ControlError(value.error?.code || (typeof value.error === 'string' ? value.error : `http_${response.status}`), response.status, value.error?.retryable === true);
      return value as T;
    } finally { this.pending.delete(controller); }
  }
  async credentials(grant: string, options: RequestOptions = {}): Promise<MediaCredentials> {
    const deadline = Date.now() + 12000;
    const generation = this.generation;
    while (true) {
      try { return await this.request<MediaCredentials>(`rtc-grants/${encodeURIComponent(grant)}/credentials`, {}, {...options, idempotencyKey: undefined}); }
      catch (error) {
        if (!(error instanceof ControlError) || error.code !== 'media_preparing' || Date.now() >= deadline || options.signal?.aborted || generation !== this.generation) throw error;
        await new Promise(resolve => setTimeout(resolve, 250));
      }
    }
  }
  async organizations(): Promise<Organization[]> {
    const value = await this.request<{items: Organization[]}>('/api/v1/organizations');
    return value.items;
  }
  async selectDefaultOrganization(): Promise<Organization[]> {
    const organizations = await this.organizations();
    if (!organizations.some(org => org.id === this.organization)) this.selectOrganization(organizations.length === 1 ? organizations[0].id : '');
    return organizations;
  }
  async eventSocket(resource: string, credential = '', after = 0): Promise<WebSocket> {
    const ticket = await this.request<{ticket: string}>('realtime/tickets', {resource_id: resource}, {credential});
    const url = new URL('/api/v1/av/realtime/ws', location.origin);
    url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    url.searchParams.set('ticket', ticket.ticket);
    url.searchParams.set('after', String(after));
    return new WebSocket(url);
  }
}

export const control = new ControlClient();

export interface AccountPanelOptions {
  registration?: boolean;
  trigger?: HTMLButtonElement;
  dialogId?: string;
}

/** Shared account/organization UI. Credentials stay in HttpOnly cookies. */
export function mountAccountPanel(host: HTMLElement, changed: () => void = () => {}, options: AccountPanelOptions = {}) {
  const trigger = options.trigger ?? createButton('账号与组织');
  if (!options.trigger) host.append(trigger);
  const dialog = createDialog({id: options.dialogId ?? 'account-dialog', title: '账号与组织', description: '管理登录状态与当前工作组织。'});
  dialog.title.id = 'auth-title';
  dialog.element.setAttribute('aria-labelledby', 'auth-title');
  dialog.close.id = 'close-auth';
  dialog.error.id = 'auth-error';
  dialog.content.innerHTML = `<form id="auth-form" class="av-account-login">
    <label for="username">用户名<input id="username" name="name" autocomplete="username" maxlength="32" required></label>
    <p class="hint" id="username-hint" hidden>3–32 位字母、数字或下划线</p>
    <label for="password">密码<input id="password" name="password" type="password" autocomplete="current-password" maxlength="128" required></label>
    <p class="hint" id="password-hint" hidden>至少 10 个字符</p>
    <button class="av-button--primary" id="submit-auth" type="submit">登录</button>
    <button class="av-button--ghost" id="toggle-auth" type="button" ${options.registration ? '' : 'hidden'}>没有账号？注册</button>
  </form>
  <section class="av-account-session" hidden>
    <p class="av-account-status av-alert" role="status" aria-live="polite"></p>
    <label>当前组织<select aria-label="当前组织"><option value="">选择组织</option></select></label>
    <form class="av-organization-create"><label>创建新组织<input name="organization" maxlength="100" placeholder="组织名称" required></label><button type="submit">创建组织</button></form>
    <button class="av-account-logout av-button--danger" type="button">退出账号</button>
  </section>`;
  const login = dialog.content.querySelector<HTMLFormElement>('.av-account-login')!;
  const session = dialog.content.querySelector<HTMLElement>('.av-account-session')!;
  const status = session.querySelector<HTMLElement>('.av-account-status')!;
  const select = session.querySelector<HTMLSelectElement>('select')!;
  const create = session.querySelector<HTMLFormElement>('.av-organization-create')!;
  const logout = session.querySelector<HTMLButtonElement>('.av-account-logout')!;
  const username = login.querySelector<HTMLInputElement>('#username')!;
  const password = login.querySelector<HTMLInputElement>('#password')!;
  const submit = login.querySelector<HTMLButtonElement>('#submit-auth')!;
  const toggle = login.querySelector<HTMLButtonElement>('#toggle-auth')!;
  const done = createButton('完成');
  done.onclick = () => dialog.element.close();
  dialog.actions.append(done);
  document.body.append(dialog.element);
  let busy = false, registered = false, loggedIn = false;
  function registrationMode(value: boolean) {
    registered = value;
    dialog.error.textContent = '';
    dialog.title.textContent = value ? '创建账号' : '欢迎回来';
    submit.textContent = value ? '注册并登录' : '登录';
    toggle.textContent = value ? '已有账号？登录' : '没有账号？注册';
    username.minLength = value ? 3 : 1;
    if (value) username.pattern = '[A-Za-z0-9_]+'; else username.removeAttribute('pattern');
    password.minLength = value ? 10 : 1;
    password.autocomplete = value ? 'new-password' : 'current-password';
    login.querySelector<HTMLElement>('#username-hint')!.hidden = !value;
    login.querySelector<HTMLElement>('#password-hint')!.hidden = !value;
  }
  function showError(error: unknown) {
    dialog.error.textContent = error instanceof ControlError && error.status === 401 ? '账号或密码不正确，请重试。'
      : error instanceof ControlError && error.status === 409 ? '账号或组织已存在，请换一个名称。'
      : error instanceof ControlError && error.status >= 500 ? '服务暂时不可用，请稍后重试。'
      : error instanceof Error ? error.message : '请求失败，请重试。';
  }
  async function refresh() {
    try {
      const user = await control.request<{name: string}>('/api/v1/auth/me');
      const organizations = await control.selectDefaultOrganization();
      select.replaceChildren(new Option('选择组织', ''), ...organizations.map(org => new Option(org.data.name, org.id)));
      select.value = control.organization;
      const organization = organizations.find(org => org.id === control.organization);
      trigger.textContent = `${user.name}${organization ? ' · ' + organization.data.name : ' · 选择组织'}`;
      trigger.setAttribute('aria-label', '账号与组织');
      status.textContent = `已登录为 ${user.name}。${organization ? '当前组织：' + organization.data.name : '请选择或创建组织。'}`;
      login.hidden = true; session.hidden = false; loggedIn = true;
      dialog.title.textContent = '账号与组织';
      dialog.description.textContent = '组织切换将结束当前组织下的活动会话。';
      done.textContent = '完成';
      return true;
    } catch (error) {
      if (!(error instanceof ControlError) || error.status !== 401) {
        showError(error);
        return false;
      }
      login.hidden = false; session.hidden = true; loggedIn = false;
      trigger.textContent = options.registration ? '登录 / 注册' : '账号与组织';
      trigger.removeAttribute('aria-label');
      select.replaceChildren(new Option('请先登录', ''));
      dialog.description.textContent = options.registration ? '登录后参与聊天、送礼和连麦。' : '创建资源前请登录并选择组织。';
      registrationMode(registered);
      done.textContent = '取消';
      return true;
    }
  }
  async function run(action: () => Promise<void>, closeOnSuccess = false) {
    if (busy) return;
    busy = true; dialog.setBusy(true); dialog.error.textContent = '';
    dialog.element.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('input, select, button').forEach(item => item.disabled = true);
    try {
      await action();
      if (!await refresh()) return;
      changed();
      if (closeOnSuccess) dialog.element.close('success');
    } catch (error) { showError(error); }
    finally {
      busy = false; dialog.setBusy(false);
      dialog.element.querySelectorAll<HTMLInputElement | HTMLButtonElement | HTMLSelectElement>('input, select, button').forEach(item => item.disabled = false);
    }
  }
  login.onsubmit = event => {
    event.preventDefault();
    void run(async () => {
      await control.request(`/api/v1/auth/${registered ? 'register' : 'login'}`, {name: username.value.trim(), password: password.value});
      control.resetAccount(); password.value = '';
    }, true);
  };
  toggle.onclick = () => registrationMode(!registered);
  create.onsubmit = event => {
    event.preventDefault(); const data = new FormData(create);
    void run(async () => {
      const organization = await control.request<{id: string}>('/api/v1/organizations', {name: data.get('organization')});
      control.selectOrganization(organization.id);
      await control.request('organizations/initialize', {}); create.reset();
    });
  };
  select.onchange = () => { control.selectOrganization(select.value); changed(); void refresh(); };
  logout.onclick = async () => {
    if (!await confirmAction('退出账号？', '当前活动会话将结束，你可以稍后重新登录。', '退出账号')) return;
    void run(async () => { await control.request('/api/v1/auth/logout', {}); control.resetAccount(); registered = false; });
  };
  const open = () => { dialog.open(loggedIn ? select : username); if (!busy) void refresh(); };
  trigger.onclick = open;
  void refresh().then(changed);
  return {element: dialog.element, open, refresh};
}
