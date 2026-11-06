import {readFile, writeFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import openapiTS, {astToString} from 'openapi-typescript';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = path.resolve(web, '../../../../livekit-server/api/openapi.json');
const target = path.join(web, 'src/generated-contract.ts');
const contract = JSON.parse(await readFile(source, 'utf8'));
contract.paths = Object.fromEntries(Object.entries(contract.paths).filter(([name]) => name.startsWith('/api/v1/av/admin/')));
if (!Object.keys(contract.paths).length) throw new Error('AV management OpenAPI paths missing. Generate the combined backend contract first.');
const output = astToString(await openapiTS(contract));
if (process.argv.includes('--check')) {
  if (await readFile(target, 'utf8').catch(() => '') !== output) throw new Error('AV admin contract types are stale. Run npm run generate.');
  console.log('AV management contract types match.');
} else {await writeFile(target, output); console.log('Generated AV management API contract types.');}
