import test from 'node:test'
import assert from 'node:assert/strict'
import { typePlan, typedParts, lineStart, lineEnd, typingRate } from './typewriter.js'

test('every character gets one offset across lines and tokens', () => {
  const plan = typePlan([['Warm · ', { v: '82 %', tone: 'class' }], [], ['ok']])
  assert.equal(plan.total, 7 + 4 + 2)
  assert.deepEqual(plan.lines[0].map(t => [t.text, t.value, t.start, t.end, t.tone]), [['Warm · ', false, 0, 7, null], ['82 %', true, 7, 11, 'class']])
  assert.deepEqual(plan.lines[1], [])
  assert.equal(plan.lines[2][0].start, 11)
  assert.equal(lineStart(plan.lines[2]), 11); assert.equal(lineEnd(plan.lines[0]), 11)
  assert.equal(lineStart([]), Infinity); assert.equal(lineEnd([]), -Infinity)
  assert.equal(typePlan([[{ v: 3 }]]).lines[0][0].text, '3', 'numbers become text')
})

test('tokens split into typed text and a ghost that keeps the place', () => {
  const [token] = typePlan([['', { v: '21.4' }]]).lines[0].slice(1)
  assert.deepEqual(typedParts(token, 0), { shown: '', ghost: '21.4', done: false, active: false })
  assert.deepEqual(typedParts(token, 2.7), { shown: '21', ghost: '.4', done: false, active: true })
  assert.deepEqual(typedParts(token, 99), { shown: '21.4', ghost: '', done: true, active: false })
})

test('long explanations type in about two seconds, short ones at a brisk pace', () => {
  assert.equal(typingRate(1900), 1000)
  assert.equal(typingRate(40), 90)
})
