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

export function mountAccountPanel(host: HTMLElement, changed: () => void = () => {}) {
  const panel = document.createElement('details');
  panel.className = 'av-account-panel';
  panel.innerHTML = `<summary>账号与组织</summary><form class="av-account-login"><label>用户名<input name="name" autocomplete="username" required></label><label>密码<input name="password" type="password" autocomplete="current-password" required></label><button type="submit">登录</button></form><label>当前组织<select aria-label="当前组织"><option value="">请先登录</option></select></label><form class="av-organization-create"><label>新组织名称<input name="organization" maxlength="100" required></label><button type="submit">创建组织</button></form><button class="av-account-logout" type="button">退出账号</button><p role="status" aria-live="polite"></p>`;
  const status = panel.querySelector<HTMLParagraphElement>('p')!;
  const select = panel.querySelector<HTMLSelectElement>('select')!;
  const login = panel.querySelector<HTMLFormElement>('.av-account-login')!;
  const create = panel.querySelector<HTMLFormElement>('.av-organization-create')!;
  const logout = panel.querySelector<HTMLButtonElement>('.av-account-logout')!;
  let busy = false;
  async function refresh() {
    try {
      const user = await control.request<{name: string}>('/api/v1/auth/me');
      const organizations = await control.selectDefaultOrganization();
      select.replaceChildren(new Option('选择组织', ''), ...organizations.map(org => new Option(org.data.name, org.id)));
      select.value = control.organization;
      status.textContent = `${user.name} · ${control.organization ? '组织已选择' : '请选择或创建组织'}`;
      login.hidden = true; create.hidden = false; logout.hidden = false;
    } catch {
      login.hidden = false; create.hidden = true; logout.hidden = true;
      select.replaceChildren(new Option('请先登录', '')); status.textContent = '创建资源前请登录并选择组织。';
    }
  }
  async function run(action: () => Promise<void>) {
    if (busy) return; busy = true; panel.setAttribute('aria-busy', 'true');
    panel.querySelectorAll<HTMLButtonElement>('button').forEach(button => button.disabled = true);
    try { await action(); await refresh(); changed(); }
    catch (error) { status.textContent = error instanceof ControlError && error.status === 401 ? '账号或密码不正确，请重试。' : error instanceof Error ? error.message : '请求失败，请重试。'; }
    finally { busy = false; panel.removeAttribute('aria-busy'); panel.querySelectorAll<HTMLButtonElement>('button').forEach(button => button.disabled = false); }
  }
  login.onsubmit = event => { event.preventDefault(); const data = new FormData(login); void run(async () => { await control.request('/api/v1/auth/login', {name: data.get('name'), password: data.get('password')}); control.resetAccount(); (login.elements.namedItem('password') as HTMLInputElement).value = ''; }); };
  create.onsubmit = event => { event.preventDefault(); const data = new FormData(create); void run(async () => { const organization = await control.request<{id: string}>('/api/v1/organizations', {name: data.get('organization')}); control.selectOrganization(organization.id); await control.request('organizations/initialize', {}); create.reset(); }); };
  select.onchange = () => { control.selectOrganization(select.value); changed(); };
  logout.onclick = () => void run(async () => { await control.request('/api/v1/auth/logout', {}); control.resetAccount(); });
  panel.addEventListener('toggle', () => { if (panel.open && !busy) void refresh(); });
  host.append(panel); void refresh().then(changed);
  return panel;
}
