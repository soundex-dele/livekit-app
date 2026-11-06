import {createHash} from 'node:crypto';
import {mkdir, readFile, readdir, writeFile, access} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {argument, npm} from './tooling.mjs';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const foundation = path.resolve(argument('--foundation-root', path.resolve(web, '../../../../../platform-foundation')));
const update = process.argv.includes('--update-lock');
const target = path.join(web, 'foundation-web.lock.json');
const output = path.join(web, 'out/foundation');
const sha = value => createHash('sha256').update(value).digest('hex');
async function filesAt(directory) {
  const entries = await readdir(path.join(foundation, directory), {withFileTypes: true});
  return (await Promise.all(entries.map(entry => entry.isDirectory() ? filesAt(path.posix.join(directory, entry.name)) : [path.posix.join(directory, entry.name)]))).flat();
}
async function sourceLock() {
  const files = ['package.json', 'package-lock.json', 'tools/build-packages.mjs'];
  for (const name of ['sdk', 'react', 'ui', 'admin']) {
    const directory = `packages/${name}`;
    // npm includes root metadata such as README and LICENSE even with files:["dist"].
    // Lock every root file so those inputs and packaging rules cannot drift unnoticed.
    const entries = await readdir(path.join(foundation, directory), {withFileTypes: true});
    files.push(...entries.filter(entry => entry.isFile()).map(entry => `${directory}/${entry.name}`), ...await filesAt(`${directory}/src`));
  }
  files.sort();
  return Object.fromEntries(await Promise.all(files.map(async file => [file, sha((await readFile(path.join(foundation, file), 'utf8')).replaceAll('\r\n', '\n'))])));
}
const before = await sourceLock();
let pinned;
try {pinned = JSON.parse(await readFile(target, 'utf8'));} catch (error) {if (!update) throw new Error('Foundation web source lock missing. Review source and run with --update-lock.', {cause: error});}
if (!update && JSON.stringify(pinned.files) !== JSON.stringify(before)) throw new Error('Foundation web source drift. Review changes before updating the lock.');
try {await access(path.join(foundation, 'node_modules/typescript/bin/tsc'));} catch {npm(['ci', '--legacy-peer-deps', '--no-audit'], foundation);}
npm(['run', 'build:packages'], foundation);
await mkdir(output, {recursive: true});
const packages = {};
for (const name of ['sdk', 'react', 'ui', 'admin']) {
  const info = JSON.parse(npm(['pack', `./packages/${name}`, '--pack-destination', output, '--json'], foundation, true))[0];
  if (info.files.some(file => file.path.startsWith('src/'))) throw new Error('Private source leaked into Foundation tarball.');
  packages[name] = {file: info.filename, integrity: info.integrity, sha256: sha(await readFile(path.join(output, info.filename)))};
}
const after = await sourceLock();
if (JSON.stringify(before) !== JSON.stringify(after)) throw new Error('Foundation source changed while building. Retry after concurrent edits finish.');
const lock = {version: 1, source: '../../../../../platform-foundation', files: after, packages};
if (update) await writeFile(target, JSON.stringify(lock, null, 2) + '\n');
else if (JSON.stringify(pinned.packages) !== JSON.stringify(packages)) throw new Error('Foundation package integrity differs from the reviewed lock.');
console.log('Foundation public package sources and tarballs verified.');
