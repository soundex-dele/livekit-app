import {execFileSync} from 'node:child_process';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {argument, npm} from '../apps/admin/web/scripts/tooling.mjs';

const app = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const web = path.join(app, 'apps/admin/web');
const foundation = argument('--foundation-root', path.resolve(app, '../../platform-foundation'));
const update = process.argv.includes('--update-foundation-lock');
const args = [path.join(web, 'scripts/prepare-foundation.mjs'), '--foundation-root', foundation];
if (update) args.push('--update-lock');
execFileSync(process.execPath, args, {cwd: web, stdio: 'inherit'});
if (update) npm(['install', '--package-lock-only', '--legacy-peer-deps', '--ignore-scripts', '--no-audit', ...['sdk', 'react', 'ui', 'admin'].map(name => `@platform/${name}@file:out/foundation/platform-${name}-0.2.0.tgz`)], web);
if (!process.argv.includes('--skip-install')) {
  npm(['ci', '--legacy-peer-deps', '--no-audit'], web);
}
npm(['run', 'verify'], web);
console.log('AV admin built:', path.join(web, 'dist'));
