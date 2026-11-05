"""Check terraflux-run-earth-forecast on a short, small ensemble: the output contract, the probabilities (multiples of
one member), the warnings recomputed from the probabilities and the warning areas, the daily and city products, the
verification against the truth member, the hurricane tracks, the sensitivity run without the Sun, that the output
does not depend on the number of threads, and that invalid requests produce no output."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/earth-forecast-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/earth-forecast-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/earth-forecast-v1.schema.json').read_text())
except ImportError:
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


schema_ok(RUN_SCHEMA, example, 'example request')
fast = copy.deepcopy(example)
fast['time'].update(days=3, detail_hours=48)
fast['ensemble'].update(members=4, threads=2)
fast['sensitivity'] = {'runs': ['no_sun'], 'days': 1}
fast['cities'] = [c for c in example['cities'] if c['name'] in ('Miami', 'New Orleans', 'New York', 'London', 'Madrid', 'Reykjavik')]
schema_ok(RUN_SCHEMA, fast, 'fast request')
members = fast['ensemble']['members']
nx, ny = fast['grid']['nx'], fast['grid']['ny']
n = nx * ny

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)

    def run(doc, name):
        path = directory / f'{name}-request.json'
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    result, output = run(fast, 'fast')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    f = timeline['earth_forecast']
    schema_ok(OUT_SCHEMA, f, 'output')
    assert timeline['schema_version'] and f['members'] == members and f['forecast_days'] == 3

    # ---- Frames and static fields.
    assert f['frame_hours'] == list(range(0, 49, 3)) + [60, 72], f['frame_hours']
    assert len(f['frames']) == len(timeline['frames']) == len(f['frame_hours'])
    assert [fr['hour'] for fr in f['frames']] == f['frame_hours']
    st = f['static']
    assert all(len(st[k]) == n for k in ('land_mask', 'latitude_deg', 'longitude_deg', 'elevation_m', 'sea_surface_temperature_c', 'soil_moisture_initial'))
    mask_rows = fast['surface']['land_mask']
    for j in range(ny):
        for i in range(nx):
            assert st['land_mask'][j * nx + i] == (mask_rows[ny - 1 - j][i] == '#'), 'land mask rows are north first'
    assert all((sst is None) == land for sst, land in zip(st['sea_surface_temperature_c'], st['land_mask'])), 'SST over the sea only'
    assert max(st['elevation_m']) > 1500, 'the terrain ridges are there'
    step = 100 // members
    for fr in f['frames']:
        for key in ('p_precip_1mm', 'p_gust_orange', 'p_thunder', 'p_tornado'):
            assert len(fr[key]) == n and all(p % step == 0 and 0 <= p <= 100 for p in fr[key]), f'{key}: multiples of one member'
        assert all(900 < p < 1060 for p in fr['pressure_hpa']), 'plausible sea-level pressure'
        assert all(x >= 0 for x in fr['precip_mm'])
    for key in ('air_temperature', 'wind_east', 'wind_north'):
        assert all(len(fr['fields'][key]['values']) == n for fr in timeline['frames'])

    # ---- Days: products and warnings recomputed from the probabilities.
    thresholds = f['warnings']['thresholds']
    pmin, pred = 100 * thresholds['min_probability'], 100 * max(thresholds['min_probability'], thresholds['red_min_probability'])
    # Warnings are issued on land and on the sea next to it.
    warn = []
    for k in range(n):
        i, j = k % nx, k // nx
        warn.append(any(0 <= i + di < nx and 0 <= j + dj < ny and st['land_mask'][(j + dj) * nx + i + di] for di in (-1, 0, 1) for dj in (-1, 0, 1)))

    def levels_from(p3, minimum=pmin):
        out = []
        for k in range(n):
            level = 0
            for lv in (3, 2, 1):
                if p3[lv - 1][k] >= minimum - 1e-9:
                    level = lv
                    break
            if level == 3 and p3[2][k] < pred - 1e-9:
                level = 2
            out.append(level if warn[k] else 0)
        return out

    assert len(f['days']) == 3
    regions = f['warnings']['regions']
    for day in f['days']:
        d = day['day']
        assert all(len(day[k]) == n for k in ('tmax_c', 'tmin_c', 'precip_mm', 'gust_m_s', 'symbol', 'p_thunder', 'runoff_72h_mm', 'surge_m'))
        assert all(a >= b - 0.05 for a, b in zip(day['tmax_c'], day['tmin_c'])), 'Tmax >= Tmin'
        assert all(p1 >= p10 for p1, p10 in zip(day['p_precip_1mm'], day['p_precip_10mm'])), 'P(>= 1 mm) >= P(>= 10 mm)'
        for p3 in (day['p_gust'], day['p_rain']):
            assert all(a >= b >= c for a, b, c in zip(*p3)), 'exceedance probabilities fall with the threshold'
        assert day['warnings']['wind'] == levels_from(day['p_gust']), f'day {d}: wind warnings follow the gust probabilities'
        assert day['warnings']['rain'] == levels_from(day['p_rain']), f'day {d}: rain warnings follow the rain probabilities'
        for hazard, lv in day['warnings'].items():
            assert all(level == 0 or warn[k] for k, level in enumerate(lv)), f'{hazard}: warnings only on land and the coast'
            listed = [r for r in regions if r['day'] == d and r['hazard'] == hazard]
            assert sum(r['cells'] for r in listed) == sum(1 for x in lv if x >= 1), f'day {d} {hazard}: the warning areas cover the warned cells'
            for r in listed:
                assert lv[r['peak_cell']] == r['level'], 'a region peaks at its highest level'
        assert all(s in range(8) for s in day['symbol'])
        assert day['verification']['tmax_rmse_k'] >= 0

    # ---- Cities: daily table and hourly wind.
    assert len(f['cities']) == len(fast['cities'])
    for city in f['cities']:
        assert city['inside'], city['name']
        assert abs(city['cell_latitude_deg'] - city['latitude_deg']) < 2.5 and len(city['days']) == 3
        h = city['hourly']
        assert len(h['hour']) == 72 and h['hour'][0] == 1
        assert all(len(h[k]) == 72 for k in ('wind_m_s', 'wind_p10_m_s', 'wind_p90_m_s', 'gust_m_s', 'gust_p90_m_s', 'direction_deg', 'temperature_c', 'precip_mm', 'truth_wind_m_s'))
        assert all(lo <= hi + 1e-9 for lo, hi in zip(h['wind_p10_m_s'], h['wind_p90_m_s']))
        assert all(g >= w - 1e-9 for g, w in zip(h['gust_m_s'], h['wind_m_s'])), 'gusts exceed the mean wind'
        assert all(0 <= x < 360 for x in h['direction_deg'])
        for row in city['days']:
            assert row['tmax_p10_c'] <= row['tmax_p90_c'] + 1e-9 and row['tmax_c'] >= row['tmin_c'] - 0.05
            assert set(row['warnings']) == set(f['warnings']['hazards'])

    # ---- Verification: spread from the start, errors growing with lead time.
    frames = f['verification']['frames']
    assert frames[0]['pressure_hpa']['spread'] > 0 and frames[0]['rain_1mm'] is None
    by_hour = {fr['hour']: fr for fr in frames}
    assert by_hour[72]['pressure_hpa']['rmse'] > by_hour[24]['pressure_hpa']['rmse'], 'pressure errors grow'
    assert by_hour[72]['pressure_hpa']['spread'] > by_hour[24]['pressure_hpa']['spread'], 'and so does the spread'
    assert frames[0]['pressure_hpa']['anomaly_correlation'] > 0.95 and by_hour[24]['pressure_hpa']['anomaly_correlation'] > 0.6, 'the first day is skilful'
    assert by_hour[24]['pressure_hpa']['skill_vs_climatology'] > 0, 'better than the climatology on day 1'
    lead = {x['day']: x for x in f['verification']['lead_days']}
    assert set(lead) == {1, 3}, 'lead days within the forecast'
    assert f['verification']['useful_hours'] is None or f['verification']['useful_hours'] >= 24

    # ---- Hurricane tracks: one per member plus the truth, starting near the seed, moving.
    seed = fast['hurricanes']['seeds'][0]
    assert len(f['tracks']['members']) == members and f['tracks']['truth']
    starts = [m[0]['points'][0] for m in f['tracks']['members']]
    assert all(abs(p[1] - seed['latitude_deg']) < 2 and abs(p[2] - seed['longitude_deg']) < 2 for p in starts), 'tracks start near the seed (with the position spread)'
    for m in f['tracks']['members']:
        pts = m[0]['points']
        assert pts[-1][0] > pts[0][0] and (abs(pts[-1][1] - pts[0][1]) + abs(pts[-1][2] - pts[0][2])) > 2, 'the storm moves'
        assert all(p[3] > 0 for p in pts)
    assert len({(round(m[0]['points'][-1][1], 1), round(m[0]['points'][-1][2], 1)) for m in f['tracks']['members']}) > 1, 'the members spread'

    # ---- Couplings: the sensitivity run without the Sun narrows the diurnal range.
    runs = {x['run']: x for x in f['couplings']['runs']}
    assert set(runs) == {'no_sun'}
    assert runs['no_sun']['tmax_land_change_k'] < 0 < runs['no_sun']['tmin_land_change_k'], runs['no_sun']
    sources = f['couplings']['control']['precip_sources']
    assert all(v >= 0 for v in sources.values()) and sources['tropical_cyclone'] > 0 and sources['layered'] > 0

    # ---- Threads do not change the result.
    single = copy.deepcopy(fast)
    single['ensemble']['threads'] = 1
    result, output1 = run(single, 'single')
    assert result.returncode == 0, result.stderr
    other = json.loads(output1.read_text())
    other['earth_forecast']['request']['ensemble']['threads'] = 2
    assert other == timeline, 'the output does not depend on the number of threads'

    # ---- Invalid requests produce no output.
    def rejected(mutate, label):
        doc = copy.deepcopy(fast)
        mutate(doc)
        result, out = run(doc, 'bad')
        assert result.returncode != 0 and not out.exists(), f'{label} must be rejected'

    rejected(lambda d: d['ensemble'].update(truth_seed=d['ensemble']['seed']), 'a truth equal to a member')
    rejected(lambda d: d['surface'].update(land_mask=d['surface']['land_mask'][1:]), 'a land mask with a missing row')
    rejected(lambda d: d.update(extra=1), 'an unknown key')
    rejected(lambda d: d['sensitivity'].update(days=5), 'a sensitivity run beyond the forecast')
    rejected(lambda d: d['precipitation'].update(hail_full_freezing_level_m=5000), 'inverted hail levels')
    rejected(lambda d: d['warnings'].update(gust_m_s=[30, 25, 40]), 'decreasing warning thresholds')
    rejected(lambda d: d['synoptic']['systems'][0].update(anomaly_hpa=-5), 'a high with a negative anomaly')

print('earth forecast checks passed')
