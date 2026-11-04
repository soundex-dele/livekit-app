import '../style.css';
import '../features/remote_control/controller';

// The controller owns #badge text. Mirror it onto a data attribute so the
// status pill can be styled without touching the connection state machine.
const badge = document.getElementById('badge');
if (badge) {
  const sync = () => {
    const text = badge.textContent?.trim() ?? '';
    badge.dataset.state = text === '已连接' ? 'connected'
      : text === '等待确认' ? 'pending'
      : text === '已结束' ? 'ended' : 'idle';
  };
  new MutationObserver(sync).observe(badge, { childList: true, characterData: true, subtree: true });
  sync();
}

const navItems = Array.from(document.querySelectorAll<HTMLAnchorElement>('.nav-item[data-nav]'));
for (const item of navItems) {
  item.addEventListener('click', () => {
    for (const other of navItems) other.classList.toggle('active', other === item);
  });
}
