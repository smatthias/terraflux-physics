import test from 'node:test'
import assert from 'node:assert/strict'
import * as THREE from 'three'
import { CLOUD_VERTEX, CLOUD_FRAGMENT, SHAFT_VERTEX, SHAFT_FRAGMENT } from './clouds3d.js'
import { surfaceMaterial, skirtMaterial } from './surface.js'
import { arrowField } from './flowArrows.js'
import { WATER_VERTEX, WATER_FRAGMENT } from './waterSurface.js'

// Node cannot compile GLSL, so this catches the mistake that slipped through once: a variable named after a word that
// GLSL ES 3.0 reserves (flat, smooth, ...) breaks the shader in the browser.
const RESERVED = ['flat', 'smooth', 'noperspective', 'centroid', 'sample', 'patch', 'input', 'output', 'filter', 'active', 'common', 'partition', 'namespace', 'using', 'class', 'union', 'template', 'this', 'goto', 'inline', 'noinline', 'volatile', 'public', 'static', 'extern', 'external', 'interface', 'long', 'short', 'double', 'half', 'fixed', 'unsigned', 'superp', 'cast', 'sizeof', 'typedef', 'enum', 'asm']
// Declared names: after a type keyword, the first identifier and every identifier after a comma at parenthesis depth 0,
// up to the end of the statement (initialisers may contain calls with commas of their own).
function declared(src) {
  const names = [], re = /\b(?:float|int|bool|vec[234]|mat[234])\s+([A-Za-z_]\w*)/g
  let m
  while ((m = re.exec(src))) {
    names.push(m[1])
    let depth = 0, k = m.index + m[0].length, expectName = false
    for (; k < src.length; k++) {
      const ch = src[k]
      if (ch === '(' || ch === '[') depth++
      else if (ch === ')' || ch === ']') { if (depth === 0) break; depth-- }
      else if ((ch === ';' || ch === '{') && depth === 0) break
      else if (ch === ',' && depth === 0) expectName = true
      else if (expectName && /[A-Za-z_]/.test(ch)) { const id = /^[A-Za-z_]\w*/.exec(src.slice(k))[0]; if (!/^(?:float|int|bool|vec[234]|mat[234]|in|out|inout|const|highp|mediump|lowp)$/.test(id)) names.push(id); k += id.length - 1; expectName = false }
    }
  }
  return names
}
const patched = material => { const sh = { uniforms: {}, vertexShader: THREE.ShaderLib.standard.vertexShader, fragmentShader: THREE.ShaderLib.standard.fragmentShader }; material.onBeforeCompile(sh); return sh.vertexShader + sh.fragmentShader }

test('the reserved-name check finds names after commas inside calls', () => {
  assert.deepEqual(declared('float a = step(1.0, x), flat = 2.0; vec3 b, c;'), ['a', 'flat', 'b', 'c'])
  assert.deepEqual(declared('void f(float x, float y){ }'), ['x', 'y'])
})

test('no custom shader declares a GLSL-reserved name', () => {
  const sources = { water: WATER_VERTEX + WATER_FRAGMENT, clouds: CLOUD_VERTEX + CLOUD_FRAGMENT, rain: SHAFT_VERTEX + SHAFT_FRAGMENT, surface: patched(surfaceMaterial({ nx: 2, ny: 2 })), terrain: patched(surfaceMaterial({ nx: 2, ny: 2, terrain: {} })),
    sides: patched(skirtMaterial({ terrain: true })), arrows: patched(arrowField([{ dir: new THREE.Vector3(1, 0, 0), origin: new THREE.Vector3(), length: 1, color: 1 }]).material) }
  for (const [name, src] of Object.entries(sources)) {
    const names = declared(src)
    assert.ok(names.length > 3, `${name}: declarations found`)
    const bad = names.filter(n => RESERVED.includes(n))
    assert.deepEqual(bad, [], `${name} declares reserved names`)
  }
})
