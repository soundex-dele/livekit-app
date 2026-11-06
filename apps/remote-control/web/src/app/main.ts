import '../style.css';
import '../features/remote_control/controller';
import { observeStatus } from '../../../../../packages/web/ui';

const badge = document.getElementById('badge')!;
const details = document.getElementById('connection-details') as HTMLDetailsElement;
let connected = false;
observeStatus(badge, text => {
  const nowConnected = text === '已连接';
  if (nowConnected && !connected) details.open = false;
  if (text === '已结束') details.open = true;
  connected = nowConnected;
  return nowConnected ? 'success' : text === '等待确认' ? 'warning' : text === '已结束' ? 'neutral' : 'info';
});
