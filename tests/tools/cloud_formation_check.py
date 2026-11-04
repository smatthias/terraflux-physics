"""Run the coupled wind CLI on the cloud-formation example, compare it with
the committed viewer fixture and check the cloud/front output format and
physics; pass the built terraflux-run-coupled-wind executable."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request_path = root / 'examples' / 'cloud-formation-request.json'
fixture_path = root / 'viewer' / 'public' / 'clouds' / 'engine-timeline.json'
request = json.loads(request_path.read_text())

with tempfile.TemporaryDirectory() as temporary:
    output_path = Path(temporary) / 'clouds.json'
    subprocess.run([sys.argv[1], str(request_path), str(output_path)], check=True)
    output = json.loads(output_path.read_text())


def equivalent(a, b):
    if type(a) in (float, int) and type(b) in (float, int) and not isinstance(a, bool) and not isinstance(b, bool):
        return abs(a - b) <= 1e-9 * max(1, abs(a), abs(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equivalent(x, y) for x, y in zip(a, b))
    return a == b


assert equivalent(output, json.loads(fixture_path.read_text())), \
    'Cloud formation output differs from the committed fixture. Regenerate viewer/public/clouds/engine-timeline.json.'

frames = output['frames']
clouds = output['coupled_atmosphere']['clouds']
grid = request['grid']
count = grid['nx'] * grid['ny']
TYPES = ['none', 'cumulus', 'towering_cumulus', 'cumulonimbus', 'stratocumulus', 'stratus', 'nimbostratus',
         'altocumulus', 'altostratus', 'cirrus', 'cirrostratus']
FRACTION = dict(none=0, cumulus=.3, towering_cumulus=.5, cumulonimbus=.9, stratocumulus=.6, stratus=.9,
                nimbostratus=1, altocumulus=.5, altostratus=.9, cirrus=.3, cirrostratus=.7)
MECHANISMS = ['none', 'convective', 'frontal', 'frontal_convective']
TRIGGERS = ['none', 'free', 'thermals', 'frontal', 'convergence', 'overrunning']
FRONTS = ['none', 'cold', 'warm', 'stationary']

assert clouds['schema_version'] == 'terraflux.weather.cloud_formation/v1'
assert clouds['method'] == 'frontal_convergence_overrunning_heated_parcel_convection'
assert clouds['source_kind'] == 'engine_output'
cfg = clouds['config']
for key, value in request['cloud_formation'].items():
    assert cfg[key] == value, key
assert cfg['warm_front_slope'] < cfg['cold_front_slope']
assert len(clouds['frames']) == len(frames)
amplitude, phase = cfg['surface_heating']['amplitude_k'], cfg['surface_heating']['phase_seconds']
H, tau = cfg['boundary_layer_depth_m'], cfg['lifting_time_scale_s']

night_frontal, day_storms = 0, 0
for frame, cloud in zip(frames, clouds['frames']):
    t = frame['elapsed_seconds']
    assert cloud['elapsed_seconds'] == t
    assert abs(cloud['surface_heating_k'] - amplitude * max(0.0, math.sin(2 * math.pi * (t - phase) / 86400))) < 1e-9
    for key in ('cloud_type', 'mechanism', 'trigger', 'base_m', 'top_m', 'cloud_fraction', 'thunderstorm', 'lifting_depth_m',
                'heated_cape_j_kg', 'heated_cin_j_kg', 'front_type', 'temperature_gradient_k_m', 'temperature_advection_k_s',
                'divergence_s', 'distance_to_front_m', 'side'):
        assert len(cloud[key]) == count, key
    cloudy = storms = fronts = 0
    for j in range(count):
        kind, mech, trig = cloud['cloud_type'][j], cloud['mechanism'][j], cloud['trigger'][j]
        assert kind in TYPES and mech in MECHANISMS and trig in TRIGGERS
        base, top = cloud['base_m'][j], cloud['top_m'][j]
        if kind == 'none':
            assert mech == 'none' and trig == 'none' and base is None and top is None and cloud['cloud_fraction'][j] == 0
        else:
            cloudy += 1
            assert mech != 'none' and trig != 'none' and base is not None and top is not None and 0 <= base < top <= cfg['max_cloud_top_m']
            assert abs(cloud['cloud_fraction'][j] - FRACTION[kind]) < 1e-12
            convective = kind in ('cumulus', 'towering_cumulus', 'cumulonimbus')
            assert convective == (mech in ('convective', 'frontal_convective'))
            assert (trig in ('free', 'thermals', 'convergence') or mech == 'frontal_convective') or mech == 'frontal'
        assert cloud['thunderstorm'][j] == (kind == 'cumulonimbus')
        storms += cloud['thunderstorm'][j]
        assert abs(cloud['lifting_depth_m'][j] - max(0.0, -H * cloud['divergence_s'][j]) * tau) < 1e-6
        assert cloud['heated_cape_j_kg'][j] >= 0 and cloud['heated_cin_j_kg'][j] >= 0
        assert cloud['front_type'][j] in FRONTS and cloud['side'][j] in (-1, 0, 1)
        if cloud['front_type'][j] != 'none':
            fronts += 1
            assert cloud['temperature_gradient_k_m'][j] >= cfg['frontal_gradient_threshold_k_m'] and cloud['side'][j] == 0
            if cloud['front_type'][j] == 'cold':
                assert cloud['temperature_advection_k_s'][j] <= -cfg['advection_threshold_k_s']
            elif cloud['front_type'][j] == 'warm':
                assert cloud['temperature_advection_k_s'][j] >= cfg['advection_threshold_k_s']
        assert (cloud['distance_to_front_m'][j] is None) == (cloud['front_cells'] == 0)
    assert cloud['cloudy_cells'] == cloudy and cloud['thunderstorm_cells'] == storms and cloud['front_cells'] == fronts
    if cloud['surface_heating_k'] == 0:
        assert 'thermals' not in cloud['trigger'], 'thermal cumulus needs surface heating'
        night_frontal += sum(1 for m in cloud['mechanism'] if m == 'frontal')
    else:
        day_storms += storms
assert night_frontal > 0, 'frontal layered cloud forms along the fronts at night'
assert day_storms > 0, 'daytime heating over the warm, moist sector produces thunderstorms'
spun_up = clouds['frames'][1]
assert 'cold' in spun_up['front_type'] and 'warm' in spun_up['front_type'], 'both cold and warm fronts once the circulation exists'
assert 'frontal_convective' in spun_up['mechanism'], 'combined frontal lifting and convection occurs'
print('cloud formation CLI and fixture checks passed')

# Precipitation outcomes: format, internal consistency and independent checks.
precip = output['coupled_atmosphere']['precipitation']
assert precip['schema_version'] == 'terraflux.weather.precipitation_outcomes/v1'
assert precip['method'] == 'genus_condensation_rate_wet_bulb_phase_updraft_hail' and precip['source_kind'] == 'engine_output'
pcfg = precip['config']
for key, value in request['precipitation'].items():
    assert pcfg[key] == value, key
assert len(precip['frames']) == len(frames)
PTYPES = ['none', 'drizzle', 'rain', 'sleet', 'snow', 'rain_showers', 'sleet_showers', 'snow_showers']
CHARACTER = {'stratus': 'drizzle', 'stratocumulus': 'intermittent', 'altostratus': 'intermittent', 'nimbostratus': 'continuous',
             'towering_cumulus': 'showers', 'cumulonimbus': 'showers'}
classify = lambda r: 'none' if r < 0.05 else 'light' if r < 2.5 else 'moderate' if r < 10 else 'heavy' if r < 50 else 'violent'
es = lambda t: 610.94 * math.exp(17.625 * (t - 273.15) / (t - 273.15 + 243.04))
def wet_bulb(T, q, p):
    e = q * p / (0.622 + 0.378 * q); gamma = 1004.6 * p / (0.622 * 2.501e6); lo, hi = T - 60, T
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if es(mid) - e - gamma * (T - mid) > 0: hi = mid
        else: lo = mid
    return 0.5 * (lo + hi)
lapse, trop = request['column']['environment_lapse_rate_k_m'], request['column']['tropopause_height_m']
day_lightning = night_lightning = 0
for frame, cloud, pf in zip(frames, clouds['frames'], precip['frames']):
    assert pf['elapsed_seconds'] == frame['elapsed_seconds']
    T = frame['fields']['air_temperature']['values']
    q = output['coupled_atmosphere']['specific_humidity_kg_kg_by_frame'][frames.index(frame)]
    P = output['coupled_atmosphere']['pressure_pa']
    for key in ('precipitation_type', 'phase', 'character', 'intensity', 'rate_mm_h', 'duration_fraction', 'accumulation_mm',
                'hail_max_diameter_cm', 'lightning', 'virga', 'updraft_m_s', 'freezing_level_m', 'surface_wet_bulb_k',
                'precipitable_water_mm', 'cloud_base_temperature_k', 'cloud_top_temperature_k', 'hazards'):
        assert len(pf[key]) == count, key
    precipitating = lightning = hail = snow = 0
    total = 0.0
    for j in range(count):
        kind, genus, rate = pf['precipitation_type'][j], cloud['cloud_type'][j], pf['rate_mm_h'][j]
        assert kind in PTYPES and rate >= 0 and pf['intensity'][j] == classify(rate)
        if kind == 'none':
            assert rate == 0 and pf['phase'][j] == 'none' and pf['character'][j] == 'none' and pf['accumulation_mm'][j] == 0
            assert pf['lightning'][j] is False and pf['hail_max_diameter_cm'][j] == 0
            assert genus not in ('nimbostratus',) or pf['virga'][j], 'nimbostratus always precipitates unless virga'
        else:
            precipitating += 1
            assert rate >= pcfg['minimum_rate_mm_h'] and pf['character'][j] == CHARACTER[genus]
            assert abs(pf['accumulation_mm'][j] - rate * pf['duration_fraction'][j] * pcfg['interval_seconds'] / 3600) < 1e-9
            assert pf['duration_fraction'][j] == {'drizzle': 1.0, 'continuous': 1.0, 'intermittent': 0.5, 'showers': 0.2}[pf['character'][j]]
            tw = pf['surface_wet_bulb_k'][j]
            expected_phase = 'liquid' if pf['cloud_top_temperature_k'][j] > 273.15 or tw > pcfg['sleet_wet_bulb_k'] else 'mixed' if tw > pcfg['snow_wet_bulb_k'] else 'solid'
            assert pf['phase'][j] == expected_phase
            total += pf['accumulation_mm'][j]
        if pf['lightning'][j]:
            lightning += 1
            assert genus == 'cumulonimbus' and pf['cloud_top_temperature_k'][j] <= pcfg['lightning_top_temperature_k'] and 'thunderstorm' in pf['hazards'][j]
        if pf['hail_max_diameter_cm'][j] > 0:
            hail += 1
            assert pf['lightning'][j] and pcfg['hail_min_diameter_cm'] <= pf['hail_max_diameter_cm'][j] <= pcfg['hail_max_diameter_cm'] and 'hail' in pf['hazards'][j]
        if pf['phase'][j] == 'solid': snow += 1
        assert genus in ('cirrus', 'cirrostratus', 'altocumulus', 'cumulus', 'none') or kind != 'none' or pf['virga'][j] or rate < pcfg['minimum_rate_mm_h']
        # Independent thermodynamics: wet-bulb by bisection and the freezing level of the linear profile.
        assert abs(pf['surface_wet_bulb_k'][j] - wet_bulb(T[j], q[j], P[j])) < 1e-6
        fz = pf['freezing_level_m'][j]
        expected_fz = (T[j] - 273.15) / lapse if T[j] > 273.15 else 0.0
        if expected_fz <= trop:
            assert fz is not None and abs(fz - expected_fz) < 1.0, (fz, expected_fz)
        assert pf['precipitable_water_mm'][j] > 0
        if genus != 'none':
            assert pf['cloud_base_temperature_k'][j] is not None and pf['cloud_top_temperature_k'][j] < pf['cloud_base_temperature_k'][j]
    assert pf['precipitating_cells'] == precipitating and pf['lightning_cells'] == lightning and pf['hail_cells'] == hail and pf['snow_cells'] == snow
    assert abs(pf['total_accumulation_mm'] - total) < 1e-6 and pf['max_rate_mm_h'] == max(pf['rate_mm_h'])
    if cloud['surface_heating_k'] > 0: day_lightning += lightning
    else: night_lightning += lightning
assert day_lightning > 0 and night_lightning == 0, 'thunderstorms with hail in the heated afternoon only'
assert any(h == 'prolonged_rain' for pf in precip['frames'] for hs in pf['hazards'] for h in hs), 'nimbostratus gives prolonged rain along the fronts'
print('precipitation outcome checks passed')
