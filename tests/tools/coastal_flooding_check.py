"""Check terraflux-run-coastal-flooding: the output contract and its identities (maps against the totals, the
regions and the events), the sea-level budget, the expected behaviour of the example coast (islands drown,
dikes are overtopped, cliffs hold, the sea reaches further up the rivers), a run on prescribed per-cell data
with explicit scenarios, and that invalid requests produce no output."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/coastal-flooding-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/coastal-flooding-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/coastal-flooding-v1.schema.json').read_text())
except ImportError:
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def close(a, b, rel, absolute=0.0):
    return abs(a - b) <= max(rel * max(abs(a), abs(b)), absolute)


def check_contract(timeline, label):
    c = timeline['coast']
    schema_ok(OUT_SCHEMA, c, label)
    grid = timeline['frames'][0]['grid']
    nx, ny = grid['nx'], grid['ny']
    count, km2 = nx * ny, grid['spacing_east_m'] * grid['spacing_north_m'] / 1e6
    sea = set(c['sea_cells'])
    assert c['sea_cells'] == sorted(sea), f'{label}: sea cells sorted and unique'
    z = timeline['terrain']['values']
    assert all(z[i] < 0 for i in sea), f'{label}: the sea lies below today\'s mean sea level'
    pop = c['population']
    assert all(pop[i] == 0 for i in sea) and all(p >= 0 for p in pop), f'{label}: people live on land'
    assert all(-1 <= r < len(c['region_names']) for r in c['region'])
    years, map_years = c['output_years'], c['map_years']
    assert years[0] == c['reference_year'] and set(map_years) <= set(years)
    frames = timeline['frames']
    assert len(frames) == len(map_years) == len(c['map_frames'])
    for f, m in zip(frames, c['map_frames']):
        assert close(f['elapsed_seconds'], (m['year'] - c['reference_year']) * 365.25 * 86400, 1e-12, 1e-6)
    names = [s['name'] for s in c['scenarios']]
    assert c['map_scenario'] in names
    for s in c['scenarios']:
        sl = s['sea_level']
        for k in range(len(years)):
            parts = sl['thermal_expansion_m'][k] + sl['glaciers_m'][k] + sl['antarctic_m'][k] + sl['greenland_m'][k] + sl['land_water_m'][k]
            assert close(parts, sl['global_m'][k], 2e-3, 2e-4), f'{label}/{s["name"]}: the global rise is the sum of its parts in {years[k]}'
        assert sl['global_m'][0] == 0 and sl['regional_m'][0] == 0, 'counted from today'
        assert all(b >= a - 1e-4 for a, b in zip(sl['global_m'], sl['global_m'][1:])), f'{label}/{s["name"]}: the sea keeps rising (committed rise)'
        for block in s['regions']:
            for key, values in block['series'].items():
                assert len(values) == len(years), f'{label}: region series {key} per output year'
            assert close(block['series']['design_flood_return_years'][0], c['design_storm']['return_period_years'], 1e-6), 'today the design flood keeps its return period'
            ev = block['events']
            if ev['design_flood_every_year'] is not None:
                later = [k for k, y in enumerate(years) if y >= ev['design_flood_every_year']]
                assert all(block['series']['design_flood_return_years'][k] <= 1.0 + 1e-6 for k in later[:1]), 'annual once the event has happened'
        for r in s['rivers']:
            assert all(len(v) == len(years) for v in r['series'].values())
        for isl in s['islands']:
            assert len(isl['lost_fraction']) == len(years) and all(0 <= v <= 1 for v in isl['lost_fraction'])
        for i, y in s['year_lost']:
            assert 0 <= i < count and i not in sea and y >= c['reference_year']
    # The map scenario's maps against its totals and regions.
    s = next(x for x in c['scenarios'] if x['name'] == c['map_scenario'])
    region = c['region']
    lost_year = dict((i, y) for i, y in s['year_lost'])
    for m in c['map_frames']:
        k = years.index(m['year'])
        lost, storm, people = m['lost_fraction'], m['storm_depth_m'], 0.0
        assert all(lost[i] == 0 and storm[i] == 0 for i in sea), 'no land lost or flooded at sea'
        assert all(0 <= v <= 1 for v in lost) and all(v >= 0 for v in storm)
        assert all(m['inundation_depth_m'][i] == 0 or lost[i] == 1 for i in range(count)), 'inundated land is wholly lost'
        total_lost = sum(v * km2 for v in lost)
        assert close(total_lost, s['totals']['lost_km2'][k], 2e-3, 0.5), f'{label}: lost land from the map in {m["year"]}'
        assert close(sum(km2 for v in storm if v > 0), s['totals']['storm_flooded_km2'][k], 1e-6, 0.5), f'{label}: storm-flooded land from the map in {m["year"]}'
        people = sum(p * v for p, v in zip(pop, lost))
        assert close(people, s['totals']['people_on_lost_land'][k], 5e-3, 5.0), f'{label}: people on lost land in {m["year"]}'
        for r, block in enumerate(s['regions']):
            cells = [i for i in range(count) if region[i] == r]
            assert close(sum(lost[i] * km2 for i in cells), block['series']['lost_km2'][k], 2e-3, 0.5), f'{label}: {block["name"]} lost land in {m["year"]}'
            assert close(sum(pop[i] for i in cells if storm[i] > 0), block['series']['people_storm_flooded'][k], 5e-3, 5.0), f'{label}: {block["name"]} people under the storm in {m["year"]}'
        protected = set(m['protected_low_cells'])
        assert not (protected & sea) and all(lost[i] < 1 for i in protected), 'protected land is dry land'
        for i in range(count):
            if lost[i] >= 0.5:
                assert i in lost_year and lost_year[i] <= m['year'], f'{label}: cell {i} lost by {m["year"]} carries its year'
    return c


with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)

    def run(doc, name):
        path = directory / f'{name}-request.json'
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    # 1. The example: five coast types under the three warming scenarios of the polar-melt example.
    schema_ok(RUN_SCHEMA, example, 'example request')
    result, output = run(example, 'example')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    c = check_contract(timeline, 'example')
    years = c['output_years']
    k2100, kend = years.index(2100), len(years) - 1
    scen = {s['name'].split(' ')[0]: s for s in c['scenarios']}
    assert set(scen) == {'low', 'intermediate', 'high'}, 'the polar run\'s warming scenarios (its control is left out)'
    rise = lambda n, k: scen[n]['sea_level']['global_m'][k]
    assert rise('low', k2100) < rise('intermediate', k2100) < rise('high', k2100), 'more warming, more rise'
    # The budget against IPCC AR6 medians for 2100 (about 0.32 / 0.50 / 0.71 m from 2020), generously.
    assert 0.2 < rise('low', k2100) < 0.5 and 0.35 < rise('intermediate', k2100) < 0.65 and 0.5 < rise('high', k2100) < 0.9, 'global rise by 2100 within the AR6 likely ranges'
    assert rise('high', kend) > 2.0, 'committed rise: metres by 2300 in the high scenario'
    # Greenland from the polar run: far-field fingerprint 0.6 for this north-west European coast.
    hi = scen['high']['sea_level']
    assert close(hi['regional_m'][kend] - hi['global_m'][kend], -0.4 * hi['greenland_m'][kend] + 0.1 * hi['antarctic_m'][kend], 5e-3, 5e-3), 'regional = global + fingerprint corrections'
    region = {b['name']: b for b in scen['high']['regions']}
    lost_pct = lambda n, k: region[n]['series']['lost_pct'][k]
    assert lost_pct('Cliff Coast', kend) == 0, 'cliffs hold'
    assert lost_pct('Low Islands', k2100) > 25 and region['Low Islands']['events']['island_half_lost'] is not None, 'the low islands drown first'
    assert region['Delta Lowlands']['events']['defences_overtopped'] is not None and region['Delta Lowlands']['events']['defences_overtopped'] < 2200, 'the delta\'s dikes are overtopped'
    assert region['Delta Lowlands']['series']['protected_low_km2'][k2100] > region['Delta Lowlands']['series']['protected_low_km2'][0], 'more land depends on the defences as the sea rises'
    assert region['Delta Lowlands']['series']['design_flood_return_years'][k2100] < region['Cliff Coast']['series']['design_flood_return_years'][k2100], 'the sinking delta sees its design flood more often'
    low = {b['name']: b for b in scen['low']['regions']}
    for name in ['Low Islands', 'Barrier Coast']:
        a, b = low[name]['events']['island_half_lost'], region[name]['events']['island_half_lost']
        assert b is not None and (a is None or a >= b), f'{name}: more warming, earlier loss'
    rivers = {r['name']: r['series'] for r in scen['high']['rivers']}
    for name, s in rivers.items():
        assert s['surge_reach_km'][kend] >= s['surge_reach_km'][0] and s['salt_intrusion_km'][kend] > s['salt_intrusion_km'][0], f'{name}: the sea pushes further up the river'
        assert s['surge_reach_km'][0] >= s['tidal_reach_km'][0], f'{name}: the storm reaches beyond the tide'
    totals = scen['high']['totals']
    assert totals['max_setup_m'][0] > 1.0 and totals['mean_coastal_setup_m'][0] > 0.3, 'the onshore storm piles water up over the shallow shelf'
    assert totals['people_storm_flooded'][kend] > totals['people_storm_flooded'][0] * 5, 'far more people under the design storm'

    # 2. Prescribed per-cell data and explicit scenarios: a coast with an enclosed depression behind a ridge.
    nx, ny = 30, 10
    z = []
    for y in range(ny):
        for x in range(nx):
            if x < 6:
                z.append(-5.0 - x)
            elif x == 12:
                z.append(4.0)   # ridge
            elif 13 <= x <= 16 and 3 <= y <= 6:
                z.append(-1.5)  # enclosed depression: land, not sea
            else:
                z.append(0.8 + 0.05 * x)
    cells = {'schema_version': 'terraflux.coastal.flooding-run/v1',
             'time': {'reference_year': 2020, 'output_years': [2020, 2060, 2100]},
             'sea_level': {'scenarios': [{'name': 'steady', 'global_warming_k': [[1850, 0.0], [2020, 1.2]]},
                                         {'name': 'warming', 'global_warming_k': [[1850, 0.0], [2020, 1.2], [2100, 5.0]], 'greenland_m': [[2020, 0.0], [2100, 0.3]]}]},
             'map_scenario': 'warming',
             'grid': {'nx': nx, 'ny': ny, 'spacing_m': 1000.0},
             'cells': {'elevation_m': z, 'population': [0.0 if v < 0 and i % nx < 6 else 10.0 for i, v in enumerate(z)], 'region': [0 if i % nx >= 6 else -1 for i in range(nx * ny)],
                       'region_names': ['Plain'], 'source': 'test grid'},
             'rivers': [{'name': 'creek', 'path': [[6500.0, 5500.0], [29500.0, 5500.0]], 'discharge_m3_s': 50.0, 'width_m': 40.0, 'channel_depth_m': 3.0}],
             'coast': {'high_tide_m': 1.0, 'storm_wind_speed_m_s': 20.0, 'storm_wind_toward_deg': 90.0}}
    schema_ok(RUN_SCHEMA, cells, 'cells request')
    result, output = run(cells, 'cells')
    assert result.returncode == 0, result.stderr
    t2 = json.loads(output.read_text())
    c2 = check_contract(t2, 'cells')
    sea = set(c2['sea_cells'])
    depression = [y * nx + x for y in range(3, 7) for x in range(13, 17)]
    assert not (set(depression) & sea), 'an enclosed depression below sea level is land, not sea'
    steady = next(s for s in c2['scenarios'] if s['name'] == 'steady')
    assert steady['sea_level']['global_m'][-1] > 0, 'constant warming still raises the sea (committed expansion, glaciers, Antarctica)'
    first = c2['map_frames'][0]
    assert all(first['lost_fraction'][i] == 0 for i in depression), 'today the ridge keeps the sea out of the depression'
    assert c2['coast_source']['kind'] == 'prescribed_cells' and c2['rivers'][0]['cells'][0] % nx == 6, 'the creek starts at the coast'

    # 3. Invalid requests produce no output.
    def without_polar(d):
        d['sea_level']['polar_timeline_path'] = 'missing/polar.json'

    mutations = [
        ('schema', lambda d: d.update(schema_version='other'), 'schema_version'),
        ('reference', lambda d: d['time'].update(output_years=[2030, 2040]), 'output_years'),
        ('map-years', lambda d: d['time'].update(map_years=[2020, 2045]), 'map_years'),
        ('map-scenario', lambda d: d.update(map_scenario='nope'), 'map_scenario'),
        ('polar-missing', without_polar, 'polar timeline'),
        ('fingerprint', lambda d: d['sea_level']['fingerprints'].update(greenland=5.0), 'greenland_fingerprint'),
        ('feature', lambda d: d['synthetic_coast']['features'].append({'kind': 'volcano', 'name': 'x'}), 'feature kind'),
        ('river-off-grid', lambda d: d['rivers'][0].update(path=[[85000.0, 70000.0], [85000.0, 900000.0]]), 'leaves the grid'),
        ('river-no-sea', lambda d: d['rivers'][0].update(path=[[85000.0, 200000.0], [85000.0, 240000.0]]), 'never reaches the sea'),
        ('surge-scale', lambda d: d['coast'].update(surge_scale_m=0.0), 'surge_scale_m'),
        ('island-height', lambda d: d['synthetic_coast']['features'][4]['islands'][0].update(height_m=1.0), 'shore elevation'),
    ]
    for name, mutate, message in mutations:
        invalid = copy.deepcopy(example)
        mutate(invalid)
        result, output = run(invalid, f'invalid-{name}')
        assert result.returncode != 0 and message in result.stderr, f'{name}: {result.stderr[-200:]}'
        assert not output.exists(), name

print('coastal flooding CLI checks passed' + ('' if jsonschema else ' (jsonschema not installed: schema validation skipped)'))
