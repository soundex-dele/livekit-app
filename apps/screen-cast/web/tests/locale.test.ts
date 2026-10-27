import { test } from 'node:test';
import assert from 'node:assert/strict';
import { errorText, statusText } from '../../../../packages/web/locale';

test('connection failures and terminal states have Chinese user messages', () => {
  for (const state of ['expired', 'rejected', 'revoked', 'heartbeat_timeout', 'server_restart'])
    assert.match(statusText(state), /[\u3400-\u9fff]/u);
  assert.match(errorText(new Error('session_inactive')), /会话已结束/);
  assert.match(errorText(new TypeError('Failed to fetch')), /检查网络/);
  assert.match(errorText(new Error('The operation was aborted')), /超时/);
  assert.doesNotMatch(errorText(new Error('unknown server payload: secret')), /secret/);
});
