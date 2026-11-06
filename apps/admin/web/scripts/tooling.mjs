import {existsSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import path from 'node:path';

const candidates = [process.env.npm_execpath, path.join(path.dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js'), path.resolve(path.dirname(process.execPath), '../lib/node_modules/npm/bin/npm-cli.js'), '/usr/share/nodejs/npm/bin/npm-cli.js'];
export function npm(args, cwd, capture = false) {
  const cli = candidates.find(candidate => candidate && existsSync(candidate));
  if (!cli) throw new Error('Cannot locate npm-cli.js. Run this command through npm, or install Node.js with npm.');
  return execFileSync(process.execPath, [cli, ...args], {cwd, stdio: capture ? 'pipe' : 'inherit', encoding: 'utf8', maxBuffer: 16 * 1024 * 1024});
}
export function argument(name, fallback) {const index = process.argv.indexOf(name); return index < 0 ? fallback : process.argv[index + 1];}
