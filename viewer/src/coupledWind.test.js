import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { defaultCoupledForm, formToRequest, formFromRequest, checkCoupledRequest, requestFromCoupledAtmosphere, validateCoupledAtmosphere } from './coupledWind.js'
import { validateSequence } from './data.js'

const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
const exampleRequest = () => JSON.parse(readFileSync(join(root, 'examples', 'coupled-wind-request.json'), 'utf8'))
const fixture = () => JSON.parse(readFileSync(join(root, 'viewer', 'public', 'coupled', 'engine-timeline.json'), 'utf8'))

test('default form reproduces the committed example request exactly', () => {
  assert.deepEqual(formToRequest(defaultCoupledForm()), exampleRequest())
})

test('form and request convert both ways', () => {
  const form = defaultCoupledForm()
  form.centers[1].temperature_anomaly_k = 6.5
  form.momentum_diffusivity_m2_s = 0
  form.column_enabled = false
  assert.equal(formToRequest(form).column, undefined)
  form.column_enabled = true
  form.column_lapse_rate_k_m = 0.007
  form.coriolis_mode = 'prescribed'
  form.coriolis_parameter_s = -9e-5
  assert.deepEqual(formFromRequest(formToRequest(form)), form)
  const perCell = exampleRequest()
  perCell.initial_fields.air_temperature_k = { values: [] }
  assert.throws(() => formFromRequest(perCell), /only represents/)
})

test('request check mirrors the solver bounds', () => {
  const ok = checkCoupledRequest(exampleRequest())
  assert.deepEqual(ok.errors, [])
  assert.ok(ok.estimates.coriolis_friction_number < 1 && ok.estimates.momentum_diffusion_number < 1)
  assert.ok(ok.estimates.coriolis_parameter_s < 0, 'Southern Hemisphere f is negative')
  assert.ok(Math.abs(ok.estimates.inertial_period_h - 19.5) < 0.5)
  assert.ok(ok.estimates.cross_isobar_angle_deg > 10 && ok.estimates.cross_isobar_angle_deg < 15)
  assert.ok(ok.estimates.scalar_stability_estimate < 1)
  assert.ok(ok.warnings.some(w => /numerical diffusion/.test(w)))

  const slow = exampleRequest(); slow.time.timestep_seconds = 20000
  assert.match(checkCoupledRequest(slow).errors.join(' '), /Timestep × \(\|f\| \+ k\)/)
  const viscous = exampleRequest(); viscous.diffusion.momentum_m2_s = 5e6
  assert.match(checkCoupledRequest(viscous).errors.join(' '), /Momentum diffusion number/)
  const negative = exampleRequest(); negative.diffusion.humidity_m2_s = -1
  assert.match(checkCoupledRequest(negative).errors.join(' '), /humidity_m2_s/)
  const wrongLow = exampleRequest(); wrongLow.pressure.pressure_systems.centers[1].pressure_anomaly_pa = 300
  assert.match(checkCoupledRequest(wrongLow).errors.join(' '), /low needs a negative/)
  const dry = exampleRequest(); dry.initial_fields.specific_humidity_kg_kg.anomalies[0].amplitude = -0.009
  assert.match(checkCoupledRequest(dry).errors.join(' '), /Specific humidity/)
  const unresolved = exampleRequest(); unresolved.time.timestep_seconds = 3000; unresolved.grid.spacing_east_m = unresolved.grid.spacing_north_m = 10000
  assert.ok(checkCoupledRequest(unresolved).warnings.some(w => /spin-up peak/.test(w)))
  const frozen = exampleRequest(); frozen.column.environment_lapse_rate_k_m = 0.03
  assert.match(checkCoupledRequest(frozen).errors.join(' '), /below 0 K/)
  const superadiabatic = exampleRequest(); superadiabatic.column.environment_lapse_rate_k_m = 0.011
  assert.ok(checkCoupledRequest(superadiabatic).warnings.some(w => /dry adiabatic/.test(w)))
  assert.equal(ok.estimates.column_top_m, 12000)
  const noRotation = exampleRequest(); noRotation.atmosphere.coriolis_mode = 'none'; noRotation.atmosphere.surface_friction_coefficient_s = 0
  assert.ok(checkCoupledRequest(noRotation).warnings.some(w => /grows without bound/.test(w)))
})

test('cloud formation example round-trips through the editor with air masses and the full cloud config', () => {
  const request = JSON.parse(readFileSync(join(root, 'examples', 'cloud-formation-request.json'), 'utf8'))
  const form = formFromRequest(request)
  assert.equal(form.airmasses.length, 2)
  assert.equal(form.cloud_enabled, true)
  assert.equal(form.cloud.warm_front_slope, 1 / 200)
  assert.equal(form.precip_enabled, true)
  assert.equal(form.precip.snow_wet_bulb_k, 273.65)
  assert.deepEqual(formToRequest(form), request)
  const badSnow = JSON.parse(JSON.stringify(request)); badSnow.precipitation.snow_wet_bulb_k = 280
  assert.match(checkCoupledRequest(badSnow).errors.join(' '), /Snow wet-bulb/)
  const report = checkCoupledRequest(request)
  assert.deepEqual(report.errors, [])
  const noColumn = JSON.parse(JSON.stringify(request)); delete noColumn.column
  assert.match(checkCoupledRequest(noColumn).errors.join(' '), /requires the vertical column/)
  const steepWarm = JSON.parse(JSON.stringify(request)); steepWarm.cloud_formation.warm_front_slope = 1 / 50
  assert.match(checkCoupledRequest(steepWarm).errors.join(' '), /gentler/)
  const fixture = JSON.parse(readFileSync(join(root, 'viewer', 'public', 'clouds', 'engine-timeline.json'), 'utf8'))
  const frames = validateSequence(fixture)
  assert.deepEqual(requestFromCoupledAtmosphere(fixture.coupled_atmosphere, frames), request)
})

test('engine fixture validates as a coupled timeline and round-trips its request', () => {
  const frames = validateSequence(fixture())
  assert.equal(frames.length, 7)
  const count = frames[0].forecast.grid.nx * frames[0].forecast.grid.ny
  for (const frame of frames) {
    assert.equal(frame.coupled.pressure.length, count)
    assert.equal(frame.coupled.density.length, count)
    assert.equal(frame.coupled.humidity.length, count)
    assert.equal(frame.coupled.specificHumidity.length, count)
    assert.equal(frame.coupled.supersaturated, 0)
    assert.ok(frame.coupled.stability <= 1)
  }
  assert.equal(frames[0].coupled.request.diffusion.temperature_m2_s, 5000)
  assert.equal(frames[0].coupled.columns.lcl.length, count)
  assert.ok(Math.max(...frames[0].coupled.columns.cape) > 500 && frames[0].coupled.columns.potential.includes('weak'))
  assert.ok(Math.max(...frames.at(-1).coupled.columns.cape) < Math.max(...frames[0].coupled.columns.cape))
  assert.equal(frames[0].coupled.columns.config.layer_count, 24)
  const request = requestFromCoupledAtmosphere(fixture().coupled_atmosphere, frames)
  assert.deepEqual(request, exampleRequest())
  assert.deepEqual(formFromRequest(request), defaultCoupledForm())
})

test('invalid coupled extensions are rejected', () => {
  const broken = fixture(); broken.coupled_atmosphere.air_density_kg_m3_by_frame[2][5] = -1
  assert.throws(() => validateSequence(broken), /air density/)
  const short = fixture(); short.coupled_atmosphere.relative_humidity_pct_by_frame.pop()
  assert.throws(() => validateSequence(short), /frame count/)
  const mixed = fixture(); mixed.atmosphere = {}
  assert.throws(() => validateSequence(mixed), /cannot carry both/)
  const badLevel = fixture(); badLevel.coupled_atmosphere.columns.cape_j_kg_by_frame[0][3] = -1
  assert.throws(() => validateSequence(badLevel), /CAPE/)
  const badOrder = fixture(); const cols = badOrder.coupled_atmosphere.columns; const j = cols.lfc_height_m_by_frame[0].findIndex(v => v !== null); cols.lfc_height_m_by_frame[0][j] = cols.equilibrium_level_m_by_frame[0][j] + 1
  assert.throws(() => validateSequence(badOrder), /ordered/)
  const wrongGrid = fixture(); wrongGrid.coupled_atmosphere.request.grid.nx = 5
  assert.throws(() => validateCoupledAtmosphere(wrongGrid.coupled_atmosphere, validateSequence({ schema_version: 'terraflux.weather.timeline/v1', frames: wrongGrid.frames })), /grid must match/)
})
