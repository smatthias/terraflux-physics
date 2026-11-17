import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import * as THREE from 'three'
import { windsockMarker, boatMarker, aircraftMarker, headingYaw, bearingVector, sockDroop, sheetAngle } from './stationMarkers.js'
import { sailPlan } from './sailTrim.js'
import { BOATS } from './polars.js'

const here = dirname(fileURLToPath(import.meta.url))
const app = readFileSync(join(here, 'App.vue'), 'utf8')
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg ?? ''} expected ${b} ± ${tol}, got ${a}`)
const world = (object, local) => { object.updateWorldMatrix(true, true); return new THREE.Vector3(...local).applyMatrix4(object.matrixWorld) }
// Horizontal unit direction (scene x, z) of b - a.
const flat = (a, b) => { const d = new THREE.Vector2(b.x - a.x, b.z - a.z); return d.normalize() }
const byName = (group, name) => { let hit = null; group.traverse(o => { if (o.name === name) hit = o }); return hit }

test('scene bearings: north is -z, east is +x; headings turn a north-pointing model', () => {
  const [ex, ez] = bearingVector(90); near(ex, 1, 1e-12); near(ez, 0, 1e-12)
  const [nx, nz] = bearingVector(0); near(nx, 0, 1e-12); near(nz, -1, 1e-12)
  for (const h of [0, 45, 90, 200, 315]) {
    const v = new THREE.Vector3(0, 0, -1).applyAxisAngle(new THREE.Vector3(0, 1, 0), headingYaw(h)), [bx, bz] = bearingVector(h)
    near(v.x, bx, 1e-12, `heading ${h}`); near(v.z, bz, 1e-12)
  }
})

test('the windsock points downwind and droops in light wind; the runway lies along its heading', () => {
  for (const from of [0, 90, 225, 300]) {
    const m = windsockMarker({ fromDeg: from, kt: 20, runwayDeg: 120 }), sock = byName(m, 'sock')
    const root = world(sock, [0, 0, 0]), tip = world(sock, [0, 0, 1]), d = flat(root, tip), [bx, bz] = bearingVector(from + 180)
    near(d.x, bx, 1e-9, `wind from ${from}: sock toward ${from + 180}`); near(d.y, bz, 1e-9)
    near(tip.y, root.y, 1e-9, 'fully extended from 15 kt')
  }
  const calm = windsockMarker({ fromDeg: 0, kt: 0 }), sock = byName(calm, 'sock')
  assert.ok(world(sock, [0, 0, 1]).y < world(sock, [0, 0, 0]).y - 0.99, 'hanging down in calm air')
  near(sockDroop(7.5), 44, 1e-9); assert.equal(sockDroop(30), 0)
  const rw = byName(windsockMarker({ runwayDeg: 120 }), 'runway'), a = world(rw, [0, 0, -1]), b = world(rw, [0, 0, 1]), d = flat(b, a), [bx, bz] = bearingVector(120)
  near(Math.abs(d.x * bx + d.y * bz), 1, 1e-9, 'runway parallel to 120/300')
  assert.equal(windsockMarker({ colour: '#e040fb' }).children[0].material.color.getHexString(), 'e040fb', 'the ring takes the flight-category colour')
})

test('the boat points along its heading and sets its sail to leeward of the apparent wind', () => {
  for (const heading of [0, 60, 135, 270]) {
    for (const awa of [40, -40, 150, -150]) {
      const m = boatMarker({ headingDeg: heading, awa, awdFrom: heading + awa }), boat = byName(m, 'boat'), sail = byName(m, 'sail')
      const bow = flat(world(boat, [0, 0, 0]), world(boat, [0, 0, -1])), [hx, hz] = bearingVector(heading)
      near(bow.x, hx, 1e-9, `bow along ${heading}`); near(bow.y, hz, 1e-9)
      // The clew (boom end, +z of the sail pivot) lies on the side away from the wind.
      const clew = flat(world(sail, [0, 0, 0]), world(sail, [0, 0, 1])), [sx, sz] = bearingVector(heading + 90)
      const toStarboard = clew.x * sx + clew.y * sz
      if (awa > 0) assert.ok(toStarboard < 0, `wind over starboard (awa ${awa}): sail to port`)
      else assert.ok(toStarboard > 0, `wind over port (awa ${awa}): sail to starboard`)
      assert.equal(m.userData.leeward, awa > 0 ? 'port' : 'starboard')
    }
  }
  near(sheetAngle(40), 20, 1e-12); assert.equal(sheetAngle(5), 8, 'close-hauled limit'); assert.equal(sheetAngle(180), 85, 'squared out')
})

test('the masthead arrow points into the apparent wind', () => {
  const m = boatMarker({ headingDeg: 0, awa: 60, awdFrom: 60 }), arrow = byName(m, 'apparent-wind')
  const tip = world(arrow, [0, 1, 0]), base = world(arrow, [0, 0, 0]), d = flat(base, tip), [bx, bz] = bearingVector(60)
  near(d.x, bx, 1e-6); near(d.y, bz, 1e-6)
})

test('the aircraft points its nose along the heading, pitches up in a climb, and shows red left / green right', () => {
  for (const h of [0, 75, 230]) {
    const a = aircraftMarker({ size: 1 }); a.userData.set(new THREE.Vector3(1, 2, 3), h, 0)
    const nose = flat(world(a, [0, 0, 0]), world(a, [0, 0, -1])), [bx, bz] = bearingVector(h)
    near(nose.x, bx, 1e-9, `heading ${h}`); near(nose.y, bz, 1e-9)
    assert.deepEqual(world(a, [0, 0, 0]).toArray().map(v => +v.toFixed(9)), [1, 2, 3])
  }
  const climb = aircraftMarker({ size: 1 }); climb.userData.set(new THREE.Vector3(), 0, 5)
  assert.ok(world(climb, [0, 0, -1]).y > 0.08, 'nose up')
  const lights = []; aircraftMarker({ size: 1 }).traverse(o => { if (o.material?.color && o.geometry?.type === 'SphereGeometry') lights.push([o.position.x, o.material.color.getHexString()]) })
  assert.ok(lights.some(([x, c]) => x < 0 && c === 'ff3b30') && lights.some(([x, c]) => x > 0 && c === '34c759'), 'red on the left wing (looking forward, -x), green on the right')
})

test('markers animate without throwing and the boat rides the sea', () => {
  const sock = windsockMarker({ fromDeg: 90, kt: 12 }), boat = boatMarker({ hs: 2, tp: 7 })
  for (const t of [0, 0.5, 1.7, 10]) { sock.userData.update(t); boat.userData.update(t) }
  const hull = byName(boat, 'boat'); boat.userData.update(1); const y1 = hull.position.y; boat.userData.update(2); assert.notEqual(hull.position.y, y1, 'heave')
  assert.ok(Math.abs(hull.rotation.z) <= 12 * Math.PI / 180 + 1e-12, 'roll stays bounded')
})

test('App.vue shows both as full views: view buttons, layer sections, scene layers, fixed aerodromes with weather cards, the route and reports below the scene', () => {
  for (const needle of ["@click=\"view='flight'\" title=", '>Flight weather</button>', "@click=\"view='sail'\" title=", '>Sailing weather</button>', '01b / FLIGHT WEATHER LAYERS', '01b / SAILING WEATHER LAYERS',
    '<FlightWeather v-if="flightData"', '<SailingWeather v-if="sailData"', ' drawWeatherViews(g,dx,dz,surface)', ' drawStations()\n}', 'watch(stationMarkerKey,()=>drawStations())',
    'for(const update of stationUpdates)update(glow.time)', '@seek="seek"', 'seaY:wavesShown&&!windGlobe', 'flightLayers,sailLayers],draw', 'inspecting.value=!stationView.value',
    '<FlightPlan v-if="flightOn&&airports.length>=2"', '@select-airport="selectAirport"', 'if(i!==null&&flightOn.value){if(pickingWaypoints.value){addWaypointAt(i);return}const a=nearestAirport(airports.value,grid.value,i);if(a)selectAirport(a.id);return}', 'waypoints:w.points', 'new THREE.InstancedMesh(box,', "fm.name='layer-freezing'", 'applyFlightPreset(key)',
    'for(const {airport,report,symbol} of aps){', 'runwayDeg:airport.runwayDeg', 'airportCardSprite(report,airport,selectedOne', 'new THREE.TubeGeometry(curve',
    'renderer.render(scene,camera);renderFlightInset();', ' advanceFlight(dt)\n', '<FlyOverlay v-if="flyState&&!fallback"', 'class="fly-launch"', '@fly="openFly"', 'sprite.layers.set(LABEL_LAYER)',
    'camera.layers.enable(LABEL_LAYER);camera.layers.enable(ROUTE_LAYER);', 'if(cockpit)flyCam.layers.disable(ROUTE_LAYER)', 'if(corridor&&!corridor.has(i))continue', 'renderer.setScissorTest(false);renderer.setViewport(0,0,W,H)'])
    assert.ok(app.includes(needle), `App.vue: ${needle}`)
  for (const gone of ['station-toggle', 'station-reports', 'FlightReport', 'SailingReport']) assert.ok(!app.includes(gone), `no overlay left: ${gone}`)
  assert.ok(app.indexOf('const flightLayers=') < app.indexOf('watch([data,layer'), 'the layers are declared before the rebuild watch reads them')
  assert.ok(app.indexOf('const waveInfo=computed') < app.indexOf('const sailData=computed'), 'the sailing report reads the frame\'s wave field')
})

test('App.vue sailing view: a click reads a point and leaves the boat where it is; the return trip is wired into the plan', () => {
  for (const needle of ['const boatCell=ref(null)', 'boatCell.value!==null&&boatCell.value<g.nx*g.ny?boatCell.value:centreCell(g)', 'pointReadout(data.value,sailProbe.value,{sea:sailSea.value,waves:waveInfo.value,boat:sailData.value.station',
    '@move-boat="moveBoatToProbe"', '@steer-here="steerToProbe"', 'drawProbeCard(canvas.getContext', 'new THREE.LineDashedMaterial', 'probeCardContent(sailProbeData.value)',
    "CLICK THE MAP TO READ A POINT (THE BOAT STAYS)", ':return-data="returnData"', ':windows="planWindows"', ':rules="planRules"'])
    assert.ok(app.includes(needle), `App.vue: ${needle}`)
  assert.ok(!/sailCell=computed\(\(\)=>[^\n]*selected/.test(app), 'the boat does not follow the selection')
})

test('the boat carries its sail plan: reefs, headsail kinds, storm sails, heel to leeward, and poses for a turn', () => {
  const B = BOATS.cruiser40, plan = (aws, awa, tws = aws, twa = awa) => sailPlan({ tws, twa, aws, awa }, B)
  const reefed = boatMarker({ headingDeg: 0, awa: 30, awdFrom: 30, sails: plan(24, 30) })
  assert.ok(byName(reefed, 'mainsail') && byName(reefed, 'reef-bundle'), 'a reefed main with the reef bundled on the boom')
  assert.ok(byName(reefed, 'jib')); assert.equal(byName(reefed, 'gennaker'), null)
  // Heeled to leeward: wind over starboard, the masthead to port (-x at heading 0).
  const top = world(byName(reefed, 'heel'), [0, 1, 0]), base = world(byName(reefed, 'heel'), [0, 0, 0]); assert.ok(top.x < base.x - 0.05, 'heeled to port')
  const full = boatMarker({ headingDeg: 0, awa: 30, sails: plan(10, 30) }); assert.equal(byName(full, 'reef-bundle'), null)
  const main = byName(full, 'mainsail'), mainR = byName(reefed, 'mainsail'); main.geometry.computeBoundingBox(); mainR.geometry.computeBoundingBox()
  assert.ok(mainR.geometry.boundingBox.max.y < main.geometry.boundingBox.max.y * 0.9, 'the reefed main is lower')
  assert.ok(byName(boatMarker({ awa: 120, sails: plan(8, 120, 12, 140) }), 'gennaker'), 'the gennaker off the wind')
  const gale = boatMarker({ awa: 40, sails: plan(36, 40, 34, 50) }); assert.ok(byName(gale, 'trysail')); assert.equal(byName(gale, 'mainsail'), null)
  const bare = boatMarker({ awa: 40, sails: plan(50, 40, 50, 50) }); assert.equal(byName(bare, 'trysail') ?? byName(bare, 'mainsail') ?? byName(bare, 'jib'), null, 'bare poles')
  // A pose: the boom across to starboard, heeled to starboard, on a new heading.
  reefed.userData.set({ headingDeg: 90, boomDeg: 30, jibDeg: 20, heelDeg: 15 })
  const sail = byName(reefed, 'sail'), clew = flat(world(sail, [0, 0, 0]), world(sail, [0, 0, 1])), [sx, sz] = bearingVector(90 + 90)
  assert.ok(clew.x * sx + clew.y * sz > 0.4, 'the boom to starboard of a boat heading east (towards the south)')
  const t2 = world(byName(reefed, 'heel'), [0, 1, 0]), b2 = world(byName(reefed, 'heel'), [0, 0, 0]); assert.ok(t2.z > b2.z + 0.05, 'heeled to starboard: the masthead to the south')
  for (const t of [0, 0.3, 1]) reefed.userData.update(t)
})

test('App.vue: route weather for flights; sails, turns, route planning and the sail-through for sailing', () => {
  for (const needle of ['routeWeather(flightPlanData.value,visibleFrames.value)', ':route-weather="flightRouteWeather"', 'drawWeatherIcon(canvas.getContext(\'2d\'),st.symbol',
    'sailPlanOf({tws:r.wind.kt,twa:r.boat.twa,aws:r.boat.aws,awa:r.boat.awa}', 'simulateManoeuvre({conditions:{twdFrom:r.wind.fromDeg', 'stationBoat.userData.set(turnState.value)', 'sails:sailTrimNow.value',
    'sailRoute({frames:visibleFrames.value,startIndex:sailDepartureIndex()', 'departureScan({frames,indices:[indices[k]]', 'sailStateAt(sailRouteData.value,sailFly.value.hours)', 'renderFlightInset();renderSailInset();',
    "ribbon.name='sail-route'", '<SailTrim v-if="sailData?.available&&sailTrimNow"', '<SailPlan v-if="sailData?.available"', '<SailOverlay v-if="sailFlyState&&!fallback"', '@route-here="routeToProbe"',
    'if(i!==null&&sailOn.value&&sailPicking.value){addSailWaypointAt(i);return}', 'countrySea.value', 'async function loadBalticSailingExample(){', 'polar:(tws,twa)=>polarSpeed(b,tws,twa)'])
    assert.ok(app.includes(needle), `App.vue: ${needle}`)
})

test('App.vue: currents in the sailing view; every panel control acts on the view shown', () => {
  for (const needle of ['currentModel(visibleFrames.value,{sea:sailSea.value,tables:currentTables.value.map(x=>x.table),settings:currentSettings.value})', 'currentAt:currentModelNow.value?currentAtCell:null',
    'currents:currentModelNow.value,settings:sailRouteOptions.value', "sailLayers.value.ground==='current'&&currentModelNow.value", "gem.name='current-station'", '01c / CURRENTS', '@change="loadCurrentTable"',
    "await loadCurrentExample('baltic-straits-example.csv'", '@click="selectLayer(l.key)"', 'if(groundFromField.value)return fieldColor(values.value[i]??null)', 'watch(fieldGround,()=>draw(),{deep:true})',
    'v-model="mapLayers.relief"', ':disabled="!runHas.fronts"', 'barbStrides[d]===barbStrides[flightLayers.density]', ":disabled=\"!sailRouteData?.ok\"",
    "const noneNow=k=>frameHas.value[k]?'':' · none at this time'", 'for(const [v,get] of Object.entries(OWN_GROUND))watch(get,', 'const dustScales=computed(', 'Labels of the GAFOR areas', 'Chart window (charts below)',
    "if(import.meta.env.MODE!=='production')window.__terrafluxScene=()=>scene", "if(import.meta.env.MODE!=='production'&&window.__terrafluxPauseRender)return;renderer.render(scene,camera)"])
    assert.ok(app.includes(needle), `App.vue: ${needle}`)
  assert.ok(!app.includes('@click="layer=l.key"'), 'a field layer picked in another view acts on that view')
})
