import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateProject} from './project.mjs';
const {cases}=JSON.parse(await readFile(new URL('../fixtures/project-compatibility.json',import.meta.url),'utf8'));
for(const fixture of cases)test(`schema ${fixture.project.schemaVersion} matches frozen compatibility contract without mutation`,()=>{
  const before=structuredClone(fixture.project);
  const actual=validateProject(fixture.project);
  assert.deepEqual(actual,fixture.expected);
  assert.deepEqual(fixture.project,before);
  assert.deepEqual(validateProject(actual),fixture.expected);
});
test('unsupported versions are rejected rather than interpreted as current',()=>{
  for(const schemaVersion of [0,8,'7',null])assert.throws(()=>validateProject({...cases[0].project,schemaVersion}),/schemaVersion/);
});
