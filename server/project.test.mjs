import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { makeServer } from './index.mjs';
import { validateProject } from './project.mjs';

const project = { schemaVersion: 1, units: 'm', name: 'Studio', room: { width: 6, depth: 4, height: 2.8 } };
test('project contract rejects unsupported formats and invalid dimensions', () => {
  for (const change of [{ schemaVersion: 7 }, { units: 'mm' }, { name: ' ' }, { room: { width: -1, depth: 4, height: 3 } }, { room: { width: '6', depth: 4, height: 3 } }])
    assert.throws(() => validateProject({ ...project, ...change }));
});
test('native geometry computes quantities and rejects malformed dimensions', () => {
  const binary = new URL('../build/terratec-core', import.meta.url).pathname;
  const actual = JSON.parse(execFileSync(binary, ['6', '4', '2.8'], { encoding: 'utf8' }));
  assert.deepEqual(actual, { area: 24, volume: 67.2, perimeter: 20 });
  for (const value of ['-1', 'nan', '6oops', '1001', '0'])
    assert.throws(() => execFileSync(binary, [value, '4', '2.8'], { stdio: 'pipe' }));
});
test('API persists across restarts, preserves saved data after invalid writes, and calls core', async t => {
  const dataDir = await mkdtemp(join(tmpdir(), 'terratec-'));
  let server;
  const start = async () => {
    server = makeServer({ dataDir });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    return `http://127.0.0.1:${server.address().port}`;
  };
  const stop = () => new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  t.after(async () => { if (server.listening) await stop(); await rm(dataDir, { recursive: true, force: true }); });
  let base = await start();
  const send = (path, method, value) => fetch(base + path, { method, headers: { 'content-type': 'application/json' }, body: JSON.stringify(value) });
  assert.equal((await fetch(base + '/api/project')).status, 404);
  assert.equal((await send('/api/project', 'PUT', project)).status, 200);
  assert.equal((await send('/api/project', 'PUT', { ...project, units: 'ft' })).status, 400);
  const quantities = await send('/api/geometry', 'POST', project);
  assert.equal(quantities.status, 200);
  assert.deepEqual(await quantities.json(), { area: 24, volume: 67.2, perimeter: 20 });
  assert.equal((await send('/api/project', 'PUT', { ...project, name: 'x'.repeat(70000) })).status, 413);
  await stop(); base = await start();
  assert.deepEqual(await (await fetch(base + '/api/project')).json(), validateProject(project));
});
