"""Run the coupled wind CLI on the hurricane-landfall and tornado-outbreak
examples; compare the hurricane output with the committed fixture and check
the storm, severe-convection and surface formats and physics; pass the built
terraflux-run-coupled-wind executable."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]


def run(name):
    request = json.loads((root / 'examples' / name).read_text())
    with tempfile.TemporaryDirectory() as temporary:
        output_path = Path(temporary) / 'out.json'
        subprocess.run([sys.argv[1], str(root / 'examples' / name), str(output_path)], check=True)
        return request, json.loads(output_path.read_text())


def equivalent(a, b):
    if type(a) in (float, int) and type(b) in (float, int) and not isinstance(a, bool) and not isinstance(b, bool):
        return abs(a - b) <= 1e-9 * max(1, abs(a), abs(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equivalent(x, y) for x, y in zip(a, b))
    return a == b


# ----------------------------------------------------------------- hurricane
request, output = run('hurricane-landfall-request.json')
fixture = json.loads((root / 'viewer' / 'public' / 'hurricane' / 'engine-timeline.json').read_text())
assert equivalent(output, fixture), 'Hurricane output differs from the committed fixture. Regenerate viewer/public/hurricane/engine-timeline.json.'
grid = request['grid']
count = grid['nx'] * grid['ny']
frames = output['frames']
atm = output['coupled_atmosphere']
surface, storms, severe = atm['surface'], atm['storms'], atm['severe']
assert surface['schema_version'] == 'terraflux.weather.surface/v1' and len(surface['land']) == count and len(surface['sea_surface_temperature_k']) == count
assert all(v in (0, 1) for v in surface['land']) and surface['source']['land']['kind'] == 'straight_coast'
coast = request['surface']['land']['coast_east_m']
for j in range(count):
    east = grid['origin_east_m'] + (j % grid['nx'] + 0.5) * grid['spacing_east_m']
    assert surface['land'][j] == (1 if east < coast else 0)
assert storms['schema_version'] == 'terraflux.weather.storm_systems/v1' and storms['source_kind'] == 'engine_output'
cfg = storms['config']
assert cfg['seeds'] == [dict(s, radius_max_wind_m=s.get('radius_max_wind_m', 40000.0)) for s in request['storms']['seeds']]
assert len(storms['frames']) == len(frames)
CATEGORIES = ['depression', 'tropical_storm', 'category_1', 'category_2', 'category_3', 'category_4', 'category_5']
category = lambda v: CATEGORIES[0 if v < 17 else 1 if v < 33 else 2 if v < 43 else 3 if v < 50 else 4 if v < 58 else 5 if v < 70 else 6]
seen_ids = set()
for frame, sf in zip(frames, storms['frames']):
    assert sf['elapsed_seconds'] == frame['elapsed_seconds'] and sf['storm_count'] == len(sf['storms'])
    for key in ('potential_intensity_m_s', 'genesis_potential_index', 'vertical_shear_m_s', 'relative_vorticity_s', 'mid_level_relative_humidity',
                'outflow_temperature_k', 'enthalpy_disequilibrium_j_kg', 'storm_wind_speed_m_s', 'storm_pressure_anomaly_pa', 'storm_rain_rate_mm_h', 'storm_surge_m'):
        assert len(sf[key]) == count and all(math.isfinite(v) for v in sf[key]), key
    for j in range(count):
        if surface['land'][j]:
            assert sf['potential_intensity_m_s'][j] == 0 and sf['genesis_potential_index'][j] == 0, 'no potential intensity over land'
        else:
            assert 60 < sf['potential_intensity_m_s'][j] < 90, 'a 29.4 C ocean supports category-5 potential intensity'
        assert 0 <= sf['mid_level_relative_humidity'][j] <= 1 and sf['outflow_temperature_k'][j] < 230
        assert sf['storm_wind_speed_m_s'][j] >= 0 and sf['storm_pressure_anomaly_pa'][j] <= 0 and sf['storm_rain_rate_mm_h'][j] >= 0 and sf['storm_surge_m'][j] >= 0
        assert sf['storm_surge_m'][j] == 0 or surface['land'][j] == 1, 'surge only on land'
    for s in sf['storms']:
        seen_ids.add(s['id'])
        assert s['category'] == category(s['max_wind_m_s']) and s['central_pressure_pa'] < s['environment_pressure_pa']
        rho = 1.15
        drop = s['environment_pressure_pa'] - s['central_pressure_pa']
        assert drop > 0 and abs(drop - math.e * s['max_wind_m_s'] ** 2 / cfg['holland_b'] * (drop / (math.e * s['max_wind_m_s'] ** 2 / cfg['holland_b']))) < 1e-6
        assert abs(s['surge_estimate_m'] - (drop / (1025 * 9.80665) + cfg['surge_wind_coefficient_s2_m'] * s['max_wind_m_s'] ** 2)) < 1e-9
        assert abs(s['core_rain_rate_mm_h'] - cfg['rain_core_factor_mm_h_per_m_s'] * s['max_wind_m_s']) < 1e-9
        assert isinstance(s['over_land'], bool) and len(s['track']) >= 1 and all(len(p) == 2 for p in s['track'])
        if s['over_land']:
            assert s['potential_intensity_m_s'] == 0 and s['time_over_land_seconds'] > 0
        if sf['storms']:
            peak = max(sf['storm_wind_speed_m_s'])
            assert peak <= max(t['max_wind_m_s'] for t in sf['storms']) + 1e-9
events = storms['events']
kinds = [e['kind'] for e in events]
assert kinds[:2] == ['seed', 'seed'] and 'merger' in kinds and 'landfall' in kinds and 'genesis' in kinds, 'seeds, a merger, genesis and landfall all occur'
merger = next(e for e in events if e['kind'] == 'merger')
assert merger['storm_id'] == 1 and merger['other_id'] == 2 and merger['elapsed_seconds'] < 3600, 'the two seeded storms merge early'
landfall = next(e for e in events if e['kind'] == 'landfall')
landfall_cell = int((landfall['north_m'] - grid['origin_north_m']) // grid['spacing_north_m']) * grid['nx'] + int((landfall['east_m'] - grid['origin_east_m']) // grid['spacing_east_m'])
assert surface['land'][landfall_cell] == 1 and landfall['east_m'] < coast + grid['spacing_east_m'] and landfall['surge_m'] > 2 and landfall['category'] in ('category_2', 'category_3'), 'landfall on the western coast as a major storm with surge'
assert all(e['elapsed_seconds'] <= frames[-1]['elapsed_seconds'] + 1e-6 for e in events)
assert all(e['kind'] in ('seed', 'genesis', 'landfall', 'merger', 'dissipation', 'exit') for e in events)
# Kaplan-DeMaria inland decay between the landfall event and the next frame of that storm.
for sf in storms['frames']:
    for s in sf['storms']:
        if s['id'] == landfall['storm_id'] and s['over_land']:
            expected = cfg['decay_background_wind_m_s'] + (landfall['max_wind_m_s'] - cfg['decay_background_wind_m_s']) * math.exp(-cfg['decay_alpha_per_s'] * s['time_over_land_seconds'])
            assert abs(s['max_wind_m_s'] - expected) < 1e-6, 'inland decay follows Kaplan-DeMaria'
# Severe block present but quiet in the tropics (weak shear).
assert severe['schema_version'] == 'terraflux.weather.severe_convection/v1' and len(severe['frames']) == len(frames)
for sv in severe['frames']:
    for key in ('low_level_shear_m_s', 'deep_shear_m_s', 'storm_motion_east_m_s', 'storm_motion_north_m_s', 'helicity_m2_s2', 'significant_tornado_parameter',
                'heated_cape_j_kg', 'heated_cin_j_kg', 'cap_strength_j_kg', 'lcl_height_m', 'capped', 'cap_broken', 'tornado_risk', 'tornado_possible'):
        assert len(sv[key]) == count, key
    assert sv['tornado_possible_cells'] == sum(sv['tornado_possible']) and sv['capped_cells'] == sum(sv['capped']) and sv['cap_broken_cells'] == sum(sv['cap_broken'])
    assert abs(sv['max_stp'] - max(sv['significant_tornado_parameter'])) < 1e-9
    assert all(r in ('none', 'low', 'moderate', 'significant') for r in sv['tornado_risk'])
print('hurricane landfall checks passed')

# ------------------------------------------------------------------- tornado
request, output = run('tornado-outbreak-request.json')
atm = output['coupled_atmosphere']
severe, clouds = atm['severe'], atm['clouds']
assert atm['columns']['config']['inversion_strength_k'] == 1.0 and atm['columns']['config']['lapse_rate_above_inversion_k_m'] == 0.0085
heating = [f['surface_heating_k'] for f in clouds['frames']]
peak = max(range(len(heating)), key=lambda i: heating[i])
day, night = severe['frames'][peak], severe['frames'][heating.index(0.0, 1)]  # first night after the spin-up frame
assert night['cap_broken_cells'] == 0 and night['capped_cells'] > 0, 'at night the inversion caps the stored energy'
assert day['cap_broken_cells'] > 0, 'afternoon heating breaks the cap'
assert day['max_stp'] > night['max_stp'] and day['max_stp'] >= 1.0, 'the low-level jet under the heated warm sector reaches at least moderate tornado potential'
assert day['tornado_possible_cells'] > 0, 'tornadoes possible where cumulonimbus form under shear'
for j in range(len(day['tornado_possible'])):
    if day['tornado_possible'][j]:
        assert clouds['frames'][peak]['cloud_type'][j] == 'cumulonimbus' and day['tornado_risk'][j] in ('moderate', 'significant')
    assert day['deep_shear_m_s'][j] >= 0 and math.isfinite(day['helicity_m2_s2'][j])
assert max(day['low_level_shear_m_s']) > 10, 'friction turning under a 30 m/s jet gives strong low-level shear'
assert max(day['helicity_m2_s2']) > 50, 'veering hodograph gives helicity'
print('tornado outbreak checks passed')
