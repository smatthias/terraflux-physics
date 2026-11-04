"""Check terraflux-run-polar-melt: the output contract, the identities and the physics of melting polar ice
and the overturning, from the files alone, and that invalid requests produce no output."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/polar-melt-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/polar-melt-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/polar-melt-v1.schema.json').read_text())
except ImportError:
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


schema_ok(RUN_SCHEMA, example, 'example request')

# A fast variant: a coarse Greenland, a short spin-up, and forcing strong enough for every event to happen.
fast = copy.deepcopy(example)
fast['time'].update(start_year=1850, end_year=2150, output_every_years=10)
fast['greenland']['grid'].update(nx=13, ny=28, spacing_m=100000)
fast['greenland']['spinup_years'] = 400
fast['arctic_sea_ice']['spinup_years'] = 40
fast['scenarios'] = [
    {'name': 'steady', 'global_warming_k': [[1850, 0.0]]},
    {'name': 'strong', 'global_warming_k': [[1850, 0.0], [1950, 6.0], [2150, 6.0]]},
]
fast['map_scenario'] = 'strong'
fast['ensemble'] = {'salinity_difference_psu': [0.8, 1.2], 'greenland_warming_amplification': [1.6]}
schema_ok(RUN_SCHEMA, fast, 'fast request')

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    path = directory / 'request.json'

    def run(doc, name):
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    result, output = run(fast, 'fast')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    p = timeline['polar']
    schema_ok(OUT_SCHEMA, p, 'output')
    years = p['output_years']
    assert years[0] == 1850 and years[-1] == 2149 and len(timeline['frames']) == len(years) == len(p['greenland_frames'])
    grid = timeline['frames'][0]['grid']
    count = grid['nx'] * grid['ny']
    assert len(p['land_mask']) == count and len(timeline['terrain']['values']) == count
    for frame, maps in zip(timeline['frames'], p['greenland_frames']):
        assert frame['elapsed_seconds'] == maps['elapsed_seconds']
        for key in ['ice_thickness_m', 'surface_elevation_m', 'smb_m_ice_yr', 'melt_m_ice_yr']:
            assert len(maps[key]) == count, key
        assert all(h == 0 for h, land in zip(maps['ice_thickness_m'], p['land_mask']) if not land), 'no ice at sea'
        assert all(h >= 0 for h in maps['ice_thickness_m'])
    # The sea level rises by the ice lost, as water over the global ocean.
    v0, area = p['greenland_frames'][0]['volume_m3'], 3.618e14
    first_sle = p['greenland_frames'][0]['sea_level_rise_m']
    for maps in p['greenland_frames']:
        assert abs((maps['sea_level_rise_m'] - first_sle) - (v0 - maps['volume_m3']) * 0.917 / area) < 1e-3 + 1e-3 * abs(maps['sea_level_rise_m']), 'sea-level rise = ice lost'

    names = [s['name'] for s in p['scenarios']]
    assert names[0].startswith('control') and p['scenarios'][0]['control'] and 'ensemble' not in p['scenarios'][0]
    by_name = {s['name']: s for s in p['scenarios']}
    for s in p['scenarios']:
        assert all(len(v) == len(years) for v in s['series'].values())
    # Control: the start climate held; the overturning stays at its present strength.
    control = p['scenarios'][0]['series']
    # (the fast check's short spin-up leaves some drift; it must stay small next to the forced change)
    drift = max(abs(q - 17.0) for q in control['overturning_sv'])
    assert drift < 0.3, f'control overturning steady ({drift:.3f} Sv drift)'
    assert all(v == 0 for v in control['sea_level_rise_vs_control_m'])
    steady = by_name['steady']
    assert all(e is None for e in steady['events'].values()), 'a flat scenario triggers nothing'
    # Strong warming: every event happens, in a physical order.
    strong = by_name['strong']
    s, ev = strong['series'], strong['events']
    assert all(ev[k] is not None for k in ev), f'every event happens under strong warming: {ev}'
    assert ev['greenland_negative_smb']['year'] < ev['overturning_collapsed']['year'] and ev['threshold_crossed']['year'] <= ev['overturning_collapsed']['year']
    assert ev['overturning_weakened_25pct']['year'] <= ev['overturning_weakened_50pct']['year'] <= ev['overturning_collapsed']['year']
    k = years.index(ev['overturning_collapsed']['year'] // 10 * 10 + 10) if ev['overturning_collapsed']['year'] // 10 * 10 + 10 in years else len(years) - 1
    assert s['overturning_sv'][k] < 0.2 * strong['initial_overturning_sv'] + 1.0, 'collapsed'
    assert s['threshold_sv'][-1] < s['threshold_sv'][0], 'polar amplification lowers the threshold'
    assert strong['initial_overturning_sv'] - min(s['overturning_sv']) > 20 * drift, 'the forced change dwarfs the drift'
    assert s['freshwater_greenland_sv'][-1] > 0.01 and s['sea_level_rise_vs_control_m'][-1] > 0.05 and s['greenland_volume_m3'][-1] < s['greenland_volume_m3'][0], 'Greenland melts'
    assert s['arctic_ice_september_m'][-1] == 0 and s['arctic_ice_free_days'][-1] > 60, 'ice-free summers'
    assert s['north_oxygen_change_pct'][5] < 0, 'warmer water holds less oxygen'
    # The north's climate target warms more than the tropics (polar amplification); the overturning's heat keeps
    # the north close to it. After the collapse that heat is gone: the north falls well short of its target
    # (the cold blob), while before the collapse the shortfall is small.
    amplification = fast['overturning']['north_amplification'] - fast['overturning']['south_amplification']
    def shortfall(k):
        return amplification * s['global_warming_k'][k] - (s['north_atlantic_temperature_change_k'][k] - s['tropical_atlantic_temperature_change_k'][k])
    early = years.index(1890)
    assert shortfall(len(years) - 1) > shortfall(early) + 1.5, f'the north falls short of its warming after the collapse ({shortfall(early):.2f} -> {shortfall(len(years) - 1):.2f} K)'
    before = years.index(1950)
    assert s['heat_transport_pw'][-1] < 0.3 * s['heat_transport_pw'][0], 'the northward heat transport is lost'
    # The fronts feel the subpolar ocean: thermal forcing = start value + per_north_k x north-box warming (+ per_k x
    # global warming), so it follows both the climate and the overturning's heat transport and counts neither twice.
    clim = fast['greenland']['climate']
    for k in range(len(years)):
        expected = clim['thermal_forcing_k'] + clim['thermal_forcing_per_north_k'] * s['north_atlantic_temperature_change_k'][k] + clim['thermal_forcing_per_k'] * s['global_warming_k'][k]
        assert abs(s['ocean_thermal_forcing_k'][k] - expected) < 2e-3 + 1e-3 * abs(expected), f'thermal forcing at the fronts in {years[k]}'
    # Recovery time grows toward the threshold (critical slowing down) and is undefined past it.
    finite = [v for v in s['recovery_time_years'][:before] if v is not None]
    assert finite[-1] > finite[0] and s['recovery_time_years'][-1] is None
    # Ensemble summary consistency.
    e = strong['ensemble']
    collapsed = [m['events']['overturning_collapsed']['year'] for m in e['members'] if m['events']['overturning_collapsed']]
    assert e['member_count'] == 2 and e['collapsed_members'] == len(collapsed)
    if collapsed:
        assert e['earliest_collapse'] == min(collapsed) and e['latest_collapse'] == max(collapsed)
    fresher = [m for m in e['members'] if m['salinity_difference_psu'] == 1.2][0]
    saltier = [m for m in e['members'] if m['salinity_difference_psu'] == 0.8][0]
    assert fresher['events']['threshold_crossed']['year'] <= saltier['events']['threshold_crossed']['year'], 'closer to the threshold, earlier crossing'
    # Equilibrium diagram: the strong branch ends at the threshold, the reversed branch flows backwards.
    d = p['equilibrium_diagram']['start']
    assert abs(max(f for f, _ in d['stable_strong']) - d['threshold_sv']) < 0.05 * d['threshold_sv']
    assert all(q < 0 for _, q in d['reversed']) and all(q > 0 for _, q in d['unstable'])
    assert p['equilibrium_diagram']['warmest']['threshold_sv'] < d['threshold_sv']
    assert abs(p['calibration']['present_freshwater_sv'] - 17.0 * 1.0 / 35.0) < 1e-4

    # Statistical early-warning indicators with a noisy freshwater forcing.
    noisy = copy.deepcopy(fast)
    noisy['scenarios'] = [fast['scenarios'][1]]
    noisy['control'] = False
    noisy.pop('ensemble')
    noisy['overturning'].update(noise_sv=0.03, seed=5, early_warning_window_years=30)
    result, output = run(noisy, 'noisy')
    assert result.returncode == 0, result.stderr
    ews = json.loads(output.read_text())['polar']['scenarios'][0]['early_warning']
    assert ews['window_years'] == 30 and 'caveat' in ews and -1 <= ews['kendall_tau_variance'] <= 1

    # Invalid requests produce no output.
    mutations = [
        ('schema', lambda d: d.update(schema_version='other')),
        ('years', lambda d: d['time'].update(end_year=1800)),
        ('step', lambda d: d['time'].update(timestep_days=0)),
        ('warming', lambda d: d['scenarios'][1].update(global_warming_k=[[1850, 0.0], [1900, 40.0]])),
        ('order', lambda d: d['scenarios'][1].update(global_warming_k=[[1900, 0.0], [1850, 1.0]])),
        ('map', lambda d: d.update(map_scenario='missing')),
        ('radius', lambda d: d['greenland']['land'].update(radius_east_m=-1.0)),
        ('unstable-present', lambda d: d['overturning']['present'].update(salinity_difference_psu=3.0)),
        ('ensemble', lambda d: d['ensemble'].update(salinity_difference_psu=[0.8, 4.0])),
        ('arctic', lambda d: d['arctic_sea_ice'].update(relative_humidity=2.0)),
        ('no-greenland', lambda d: d.pop('greenland')),
        ('thermal-forcing', lambda d: d['greenland']['climate'].update(thermal_forcing_per_north_k=5.0)),
    ]
    for name, mutate in mutations:
        invalid = copy.deepcopy(fast)
        invalid['greenland']['spinup_years'] = 1
        invalid['time'].update(end_year=1870)
        mutate(invalid)
        result, output = run(invalid, f'invalid-{name}')
        assert result.returncode != 0, name
        assert not output.exists(), name

print('polar melt CLI checks passed' + ('' if jsonschema else ' (jsonschema not installed: schema validation skipped)'))
