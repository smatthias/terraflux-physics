"""Check that a solar-cycle run drives the other models: the ocean's astronomical shortwave follows the run's TSI as
an anomaly (forcing.air_sea.shortwave.solar_cycle_path), and the polar-melt scenarios add its surface-temperature
response (solar_cycle section). Runs without these options are unchanged.

Usage: solar_coupling_check.py <terraflux-run-solar-cycle> <terraflux-run-ocean-circulation> <terraflux-run-polar-melt>"""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
solar_cli, ocean_cli, polar_cli = sys.argv[1:4]

try:
    import jsonschema
    OCEAN_RUN = json.loads((root / 'docs/weather/ocean-run-v1.schema.json').read_text())
    POLAR_RUN = json.loads((root / 'docs/weather/polar-melt-run-v1.schema.json').read_text())
    POLAR_OUT = json.loads((root / 'docs/weather/polar-melt-v1.schema.json').read_text())
except ImportError:
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def interpolate(years, values, at, outside):
    mids = [y + 0.5 for y in years]
    if at < mids[0] or at > mids[-1]:
        return outside
    k = next(i for i, m in enumerate(mids) if m >= at)
    if k == 0:
        return values[0]
    w = (at - mids[k - 1]) / (mids[k] - mids[k - 1])
    return values[k - 1] + (values[k] - values[k - 1]) * w


with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)

    def run(cli, doc, name):
        request = directory / f'{name}-request.json'
        request.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(request), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    # ---- A small solar run -----------------------------------------------------------------------------------
    solar = json.loads((root / 'examples/solar-cycle-request.json').read_text())
    solar['forecast']['members'] = 40
    solar['disk'].update(first_year=2020.5, last_year=2022.5, every_months=24, cells=16)
    result, solar_path = run(solar_cli, solar, 'solar')
    assert result.returncode == 0, result.stderr
    export = json.loads(solar_path.read_text())['solar']['forcing_export']
    years, tsi, surface, reference = export['years'], export['tsi_w_m2'], export['surface_k'], export['reference_tsi_w_m2']
    assert any(abs(t - reference) > 0.3 for t in tsi), 'the exported TSI carries the cycle'

    # ---- Ocean: the shortwave follows the TSI anomaly at the model date -----------------------------------------
    ocean = json.loads((root / 'examples/ocean-heat-rain-request.json').read_text())
    ocean['time'].update(steps=48, spinup_steps=0, start_time_utc='2000-06-01T00:00:00Z')   # near the maximum of cycle 23
    ocean['snapshot_every_steps'] = 24
    same = copy.deepcopy(ocean)
    same['forcing']['air_sea']['shortwave']['solar_constant_w_m2'] = 1361.0
    brighter = copy.deepcopy(ocean)
    brighter['forcing']['air_sea']['shortwave']['solar_constant_w_m2'] = 1371.0
    cycle = copy.deepcopy(ocean)
    cycle['forcing']['air_sea']['shortwave']['solar_cycle_path'] = str(solar_path)
    for doc, label in [(same, 'constant'), (cycle, 'solar cycle')]:
        schema_ok(OCEAN_RUN, doc, f'ocean request with a {label}')
    outputs = {}
    for doc, name in [(ocean, 'base'), (same, 'same'), (brighter, 'brighter'), (cycle, 'cycle')]:
        result, path = run(ocean_cli, doc, f'ocean-{name}')
        assert result.returncode == 0, result.stderr
        outputs[name] = json.loads(path.read_text())['ocean']
    base = [f['air_sea']['mean_shortwave_w_m2'] for f in outputs['base']['frames'] if f.get('air_sea')]
    assert base and max(base) > 100, 'daylight in the run'
    assert outputs['same']['frames'] == outputs['base']['frames'], 'a solar constant of 1361 W/m2 is the default'
    year = 2000 + 152.5 / 366   # the run spans one day from 1 June (2000 is a leap year)
    anomaly = interpolate(years, tsi, year, reference) - reference
    assert anomaly > 0.2, f'June 2000 lies near a strong maximum: the TSI is above the mean ({anomaly:.3f} W/m2)'
    for ratio_name, expected in [('brighter', 1371.0 / 1361.0), ('cycle', (1361.0 + anomaly) / 1361.0)]:
        for f0, f1 in zip(outputs['base']['frames'], outputs[ratio_name]['frames']):
            a, b = f0.get('air_sea'), f1.get('air_sea')
            if a and a['mean_shortwave_w_m2'] > 100:
                assert abs(b['mean_shortwave_w_m2'] / a['mean_shortwave_w_m2'] - expected) < 3e-5, f'{ratio_name}: shortwave ratio {b["mean_shortwave_w_m2"] / a["mean_shortwave_w_m2"]} vs {expected}'
    provenance = json.dumps(outputs['cycle'])
    assert '"solar_cycle"' in provenance and str(solar_path) in provenance, 'the provenance names the solar run'
    bad = copy.deepcopy(cycle)
    bad['forcing']['air_sea']['shortwave']['solar_constant_w_m2'] = 1361.0
    result, path = run(ocean_cli, bad, 'ocean-bad')
    assert result.returncode != 0 and not path.exists(), 'a solar constant and a solar cycle together are rejected'
    bad['forcing']['air_sea']['shortwave'] = {'kind': 'astronomical', 'solar_cycle_path': str(directory / 'missing.json')}
    result, path = run(ocean_cli, bad, 'ocean-missing')
    assert result.returncode != 0 and 'solar' in result.stderr, 'a missing solar run is reported'

    # ---- Polar melt: every scenario warms by the solar response ----------------------------------------------
    polar = json.loads((root / 'examples/polar-melt-request.json').read_text())
    polar['time'].update(start_year=1950, end_year=2040, output_every_years=10)
    polar['greenland']['grid'].update(nx=13, ny=28, spacing_m=100000)
    polar['greenland']['spinup_years'] = 300
    polar['arctic_sea_ice']['spinup_years'] = 30
    polar['scenarios'] = [{'name': 'steady', 'global_warming_k': [[1950, 0.0]]}, {'name': 'warming', 'global_warming_k': [[1950, 0.0], [2040, 2.0]]}]
    polar['map_scenario'] = 'warming'
    polar.pop('ensemble', None)
    with_sun = copy.deepcopy(polar)
    with_sun['solar_cycle'] = {'timeline_path': str(solar_path)}
    schema_ok(POLAR_RUN, with_sun, 'polar request with a solar cycle')
    result, path = run(polar_cli, polar, 'polar-base')
    assert result.returncode == 0, result.stderr
    plain = json.loads(path.read_text())['polar']
    result, path = run(polar_cli, with_sun, 'polar-sun')
    assert result.returncode == 0, result.stderr
    sunny = json.loads(path.read_text())['polar']
    schema_ok(POLAR_OUT, sunny, 'polar output with a solar cycle')
    assert 'solar_cycle' not in plain and sunny['solar_cycle']['warming'] and sunny['solar_cycle']['arctic_shortwave']
    assert abs(sunny['solar_cycle']['max_abs_warming_k'] - max(abs(v) for v in surface)) < 1e-3
    names = [s['name'] for s in sunny['scenarios']]
    assert names == [s['name'] for s in plain['scenarios']] and plain['scenarios'][0]['control'], 'the control run is there too'
    for s0, s1 in zip(plain['scenarios'], sunny['scenarios']):
        for y, w0, w1 in zip(sunny['output_years'], s0['series']['global_warming_k'], s1['series']['global_warming_k']):
            # The annual mean of the interpolated response over the year's steps.
            expected = sum(interpolate(years, surface, y + (k + 0.5) / 36.525, 0.0) for k in range(37)) / 37
            assert abs((w1 - w0) - expected) < 2e-3, f'{s1["name"]} {y}: the warming rises by the solar response ({w1 - w0:.4f} vs {expected:.4f})'
    steady = next(s for s in sunny['scenarios'] if s['name'] == 'steady')['series']['global_warming_k']
    assert max(steady) - min(steady) > 0.01, 'the steady scenario now carries the solar cycle'
    for change, label in [(lambda d: d['solar_cycle'].update(timeline_path=str(directory / 'missing.json')), 'a missing solar run'),
                          (lambda d: d['solar_cycle'].update(shortwave=True), 'an unknown solar_cycle field'),
                          (lambda d: d['solar_cycle'].update(warming=False, arctic_shortwave=False), 'a solar cycle that applies nothing')]:
        doc = copy.deepcopy(with_sun)
        change(doc)
        result, path = run(polar_cli, doc, 'polar-bad')
        assert result.returncode != 0 and not path.exists(), f'{label} must be rejected'

print('solar coupling check passed')
