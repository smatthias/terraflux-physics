#!/usr/bin/env python3
"""Audit the viewer's side-panel controls: does every selectable control change the 3D view?

For each example and every view it enables, every enabled control of the "01 …" sections of the side panel (the field
layer list and its toggles, the view's own layer section, the currents section) is changed and the 3D scene compared
before and after. The comparison is a fingerprint of the scene graph (the visible objects, their geometry, vertex
colours, materials and uniforms), so it needs a development build of the viewer, which exposes the scene
(window.__terrafluxScene) and can pause rendering (window.__terrafluxPauseRender) for speed:

    cd viewer && NODE_ENV=development npx vite build --mode development --outDir /tmp/audit-dist
    (cd /tmp/audit-dist && python3 -m http.server 5174) &
    python3 tools/audit_view_controls.py --url http://127.0.0.1:5174/ --examples loadHurricaneExample,loadBalticSailingExample --out audit.jsonl
    python3 tools/audit_view_controls.py --summary audit.jsonl

Requires Playwright for Python with Chromium. Each example is audited at a frame 60 % into its run (the first frame is
often a calm spin-up). In views other than the Field explorer three field layers are tried (they act alike there).
A control that changes nothing is listed; whether that is right (nothing to show in this run or frame) or a fault is
for the reader: docs/weather/viewer-control-audit.md records the findings and the fixes.
"""
import asyncio, json, sys
S='document.querySelector("#app").__vue_app__._instance.setupState'
FP = '''(() => { const scene = window.__terrafluxScene?.(); if (!scene) return null; let h = 0, n = 0
  const mix = v => { h = (h * 31 + Math.round(v * 1000)) % 2147483647 }
  scene.traverseVisible(o => { n++; mix(o.position.x); mix(o.position.y); mix(o.position.z); mix(o.scale.x); mix(o.rotation.y)
    const g = o.geometry; if (g) { for (const k of ['position', 'color']) { const a = g.attributes?.[k]; if (a) { mix(a.count); const arr = a.array, step = Math.max(1, Math.floor(arr.length / 400)); for (let i = 0; i < arr.length; i += step) mix(arr[i]) } } if (o.isInstancedMesh) { mix(o.count); const m = o.instanceMatrix.array, st = Math.max(1, Math.floor(m.length / 400)); for (let i = 0; i < m.length; i += st) mix(m[i]) } }
    const skip = new Set(['uTime', 'uHover', 'uHoverOn', 'uSel', 'uSelOn', 'uIso', 'uIsoOn'])
    for (const m of [].concat(o.material ?? [])) { if (m.color) mix(m.color.r + 2 * m.color.g + 3 * m.color.b); mix(m.opacity ?? 1); mix(Object.keys(m.defines ?? {}).length); if (m.map?.image?.width) mix(m.map.image.width)
      for (const uniforms of [m.uniforms, m.userData?.uniforms]) if (uniforms) for (const [k, u] of Object.entries(uniforms)) { if (skip.has(k)) continue; if (typeof u.value === 'number') mix(u.value); else if (u.value?.isVector2) { mix(u.value.x); mix(u.value.y) } else if (u.value?.isColor) mix(u.value.r + 2 * u.value.g + 3 * u.value.b) } } })
  return `${n}:${h}` })()'''
CONTROLS_JS = '''(() => {
  const secs = [...document.querySelectorAll('aside section')].filter(s => /^01/.test(s.querySelector('.section-label')?.textContent?.trim() ?? ''))
  const out = []
  secs.forEach((s, si) => s.querySelectorAll('input, select, button.layer').forEach((e, k) => {
    if (e.disabled || e.type === 'file') return
    const label = (e.closest('label')?.textContent ?? e.textContent ?? '').replace(/\\s+/g, ' ').trim().slice(0, 80)
    out.push({ si, k, tag: e.tagName, type: e.type, label, section: s.querySelector('.section-label').textContent.trim(), active: e.classList.contains('active'), value: e.value, options: e.tagName === 'SELECT' ? [...e.options].filter(o => !o.disabled).map(o => o.value) : null })
  }))
  return out })()'''
async def act(pg, c, mode, value=None):
    # Found again by its section, kind and label each time (controls appear and disappear as others change).
    return await pg.evaluate('''([section, tag, type, label, mode, value]) => {
      const norm = t => (t ?? '').replace(/\\s+/g, ' ').trim().slice(0, 80)
      // Numbers in labels change with the state (height scale 2x, 35 %): compared without them, and without the
      // "· none at this time" notes.
      const key = t => t.split(' · ')[0].replace(/[0-9][0-9.,]*/g, '#')
      const s = [...document.querySelectorAll('aside section')].find(x => x.querySelector('.section-label')?.textContent?.trim() === section); if (!s) return 'gone'
      const e = [...s.querySelectorAll('input, select, button.layer')].find(x => x.tagName === tag && (x.type || '') === (type || '') && key(norm(x.closest('label')?.textContent ?? x.textContent)) === key(label)); if (!e) return 'gone'
      if (mode === 'click') e.click()
      else { e.value = value; e.dispatchEvent(new Event('input', { bubbles: true })); e.dispatchEvent(new Event('change', { bubbles: true })) }
      return 'ok' }''', [c['section'], c['tag'], c['type'], c['label'], mode, value])
async def settle(pg, ms=700): await pg.wait_for_timeout(ms)
async def main(examples, out):
    from playwright.async_api import async_playwright  # only needed to run the audit
    async with async_playwright() as p:
        b = await p.chromium.launch(args=["--use-gl=swiftshader","--enable-webgl","--ignore-gpu-blocklist","--enable-unsafe-swiftshader"])
        pg = await b.new_page(viewport={"width":1500,"height":1000})
        await pg.goto(URL, wait_until="load", timeout=120000); await pg.wait_for_timeout(4000)
        await pg.emulate_media(reduced_motion="reduce"); await pg.evaluate("window.__terrafluxPauseRender=true")
        for ex in examples:
            await pg.evaluate(f'{S}.runExample("{ex}")'); await pg.wait_for_timeout(9000)
            # A frame well into the run (the first is often a calm spin-up): 60 % of the way.
            await pg.evaluate(f'(()=>{{const s={S};const n=s.snapshots.length;if(n>1)s.seek(Math.floor(0.6*(n-1)))}})()'); await pg.wait_for_timeout(2500)
            views = await pg.evaluate('[...document.querySelectorAll(".view-switch button")].filter(b=>!b.disabled).map(b=>b.textContent.trim())')
            for vname in views:
                await pg.evaluate(f'[...document.querySelectorAll(".view-switch button")].find(b=>b.textContent.trim()==={json.dumps(vname)}).click()'); await pg.wait_for_timeout(4000)
                controls = await pg.evaluate(CONTROLS_JS)
                # Field layer buttons: all of them in the Field explorer; elsewhere three (they act alike in a view).
                if vname != 'Field explorer':
                    kept, nb = [], 0
                    for c in controls:
                        if c['tag'] == 'BUTTON' and not c['active']:
                            nb += 1
                            if nb > 3: continue
                        kept.append(c)
                    controls = kept
                active = next((c for c in controls if c['tag'] == 'BUTTON' and c['active']), None)
                base = await pg.evaluate(FP)
                if base is None: out.write(json.dumps({'example': ex, 'view': vname, 'error': 'no scene'}) + '\n'); continue
                for c in controls:
                    tests = []
                    try:
                        if c['tag'] == 'BUTTON':
                            if c['active']: continue
                            await act(pg, c, 'click'); await settle(pg); tests.append(('select layer', await pg.evaluate(FP)))
                            if active: await act(pg, active, 'click')
                            # Picking a layer in another view lays it on that view's ground: take it off again.
                            await pg.evaluate(f'(()=>{{const s={S};s.fieldGround={{}}}})()')
                        elif c['type'] == 'checkbox':
                            await act(pg, c, 'click'); await settle(pg); tests.append(('toggle', await pg.evaluate(FP))); await act(pg, c, 'click')
                        elif c['tag'] == 'SELECT':
                            for o in c['options']:
                                if o == c['value']: continue
                                await act(pg, c, 'set', o); await settle(pg); tests.append((f'option {o}', await pg.evaluate(FP)))
                            await act(pg, c, 'set', c['value'])
                        elif c['type'] in ('range', 'number'):
                            v = float(c['value'] or 0); nv = str(round(v * 1.5 + 1, 3))
                            await act(pg, c, 'set', nv); await settle(pg); tests.append((f'value {nv}', await pg.evaluate(FP))); await act(pg, c, 'set', c['value'])
                        else: continue
                        await settle(pg, 500)
                        after = await pg.evaluate(FP)
                        for what, fp in tests:
                            out.write(json.dumps({'example': ex, 'view': vname, 'section': c['section'], 'label': c['label'], 'type': c['type'] or c['tag'], 'what': what, 'changed': fp != base, 'restored': after == base}) + '\n'); out.flush()
                        base = after
                    except Exception as err:
                        out.write(json.dumps({'example': ex, 'view': vname, 'label': c['label'], 'error': str(err)[:200]}) + '\n'); out.flush()
        await b.close()
def summary(path):
    rows = [json.loads(l) for l in open(path)]
    for r in rows:
        if 'error' in r:
            print(f"ERROR | {r['example']} | {r['view']} | {r.get('label', '')} | {r['error'][:120]}")
        elif not r['changed']:
            print(f"NO CHANGE | {r['example']} | {r['view']} | {r['section']} | {r['label']} | {r['what']}")


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--url', default='http://127.0.0.1:5174/')
    p.add_argument('--examples', default='loadHurricaneExample')
    p.add_argument('--out', default='audit.jsonl')
    p.add_argument('--summary', help='print the controls without effect from an audit file and stop')
    a = p.parse_args()
    if a.summary:
        summary(a.summary)
    else:
        URL = a.url
        with open(a.out, 'a') as f:
            asyncio.run(main(a.examples.split(','), f))
