"""Check the catchment hydrology CLI: request validation, output contract and water budget."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request = json.loads((root / 'examples/alpine-flood-request.json').read_text())
cli = sys.argv[1]

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    path = directory / 'request.json'

    def run(doc, name):
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True)
        return result, output

    # A shortened run: one day of the example with the cloudburst moved forward.
    short = copy.deepcopy(request)
    short['time'].update(steps=144, spinup_steps=36)
    short['snapshot_every_steps'] = 12
    short['forcing']['rainfall']['events'] = [
        {'start_seconds': 3600.0, 'duration_seconds': 21600.0, 'rate_mm_h': 50.0, 'center_east_m': 0.0, 'center_north_m': 3000.0, 'radius_m': 11000.0, 'orographic_enhancement_per_km': 0.3},
    ]
    result, output = run(short, 'short')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    assert timeline['schema_version'] == 'terraflux.weather.timeline/v1'
    assert len(timeline['frames']) == 13 and timeline['frames'][-1]['elapsed_seconds'] == 144 * 600
    assert timeline['terrain']['schema_version'] == 'terraflux.terrain.grid/v1' and len(timeline['terrain']['values']) == 1200
    h = timeline['hydrology']
    assert h['schema_version'] == 'terraflux.hydrology.catchment/v1'
    assert h['method'] == 'priority_flood_d8_kinematic_cascade_darcy_infinite_slope_stream_power'
    assert h['request']['time']['steps'] == 144 and h['request']['time']['spinup_steps'] == 36
    assert h['terrain_source']['kind'] == 'synthetic_ridges_and_basins' and len(h['terrain_source']['ridges']) == 3
    assert h['forcing_source']['coupled']['kind'] == 'none' and len(h['forcing_source']['rainfall_events']) == 1
    n = h['network']
    count = 1200
    assert all(len(n[k]) == count for k in ['downstream', 'channel', 'lake_id', 'basin_id', 'upstream_area_km2', 'strahler_order', 'ocean', 'terminal'])
    assert sum(n['ocean']) > 0 and sum(n['channel']) > 100 and len(n['basins']) > 5 and len(n['lakes']) > 0
    # Every land cell reaches the sea or the edge along strictly descending receivers.
    for i in range(count):
        if n['ocean'][i]:
            assert n['downstream'][i] == -1 and n['basin_id'][i] == -1
            continue
        seen, current = 0, i
        while n['downstream'][current] >= 0 and not n['ocean'][n['downstream'][current]]:
            current = n['downstream'][current]
            seen += 1
            assert seen <= count
        assert n['terminal'][current] in ('ocean', 'edge')
    assert [b['area_km2'] for b in n['basins']] == sorted((b['area_km2'] for b in n['basins']), reverse=True)
    frames = h['frames']
    assert len(frames) == 13 and frames[0]['cumulative_m3']['precipitation'] == 0
    initial = h['initial_storage_m3']
    peak = 0.0
    for i, f in enumerate(frames):
        assert f['elapsed_seconds'] == timeline['frames'][i]['elapsed_seconds']
        s, c = f['storage_m3'], f['cumulative_m3']
        scale = max(1.0, initial, c['precipitation'])
        assert abs(s['snow'] + s['surface'] + s['soil'] + s['groundwater'] + s['channels'] + s['lakes'] - s['total']) <= 1e-6 * scale
        assert abs(s['total'] + c['evapotranspiration'] + c['ocean_discharge'] + c['edge_outflow'] - c['precipitation'] - initial) <= 1e-6 * scale, 'water budget'
        assert abs(f['balance_error_m3']) <= 1e-6 * scale
        assert all(len(f[k]) == count for k in ['rainfall_rate_mm_h', 'surface_water_mm', 'discharge_m3_s', 'flood_class', 'erosion_mm', 'factor_of_safety', 'landslide'])
        assert all(q == 0 or n['channel'][j] for j, q in enumerate(f['discharge_m3_s']))
        assert sum(1 for v in f['flood_class'] if v > 0) * 1.0 == f['flooded_area_km2']
        assert f['landslide_cells'] == sum(f['landslide'])
        assert len(f['lake_level_m']) == len(n['lakes'])
        assert all(v <= lake['capacity_m3'] * (1 + 1e-9) + 1e-6 for v, lake in zip(f['lake_volume_m3'], n['lakes']))
        peak = max(peak, f['max_discharge_m3_s'])
    assert peak > 50 and frames[-1]['cumulative_m3']['ocean_discharge'] > 0, 'the cloudburst produces a flood wave that reaches the sea'
    assert frames[-1]['sediment_yield_kg'] > 0 and max(frames[-1]['erosion_mm']) > 0
    assert all(e['kind'] == 'landslide' and 0 <= e['cell'] < count and e['factor_of_safety'] < 1 for e in h['events'])

    # Coupled-timeline forcing: rain and temperature sampled from a coupled-wind run on the same local frame.
    coupled = {
        'schema_version': 'terraflux.weather.timeline/v1',
        'frames': [],
        'coupled_atmosphere': {'schema_version': 'terraflux.weather.coupled_atmosphere/v1', 'orography': {'frames': []}},
    }
    grid = {'nx': 4, 'ny': 3, 'origin_east_m': -20000.0, 'origin_north_m': -15000.0, 'spacing_east_m': 10000.0, 'spacing_north_m': 10000.0,
            'geographic_anchor_longitude_deg': 10.5, 'geographic_anchor_latitude_deg': 46.8, 'coordinate_system': 'local_azimuthal_equidistant_m', 'array_order': 'row_major_y_then_x'}
    for k, elapsed in enumerate([0.0, 3600.0, 7200.0]):
        coupled['frames'].append({'schema_version': 'terraflux.weather.forecast/v1', 'start_time_utc': short['time']['start_time_utc'], 'elapsed_seconds': elapsed, 'grid': grid,
                                  'observation_updates': [], 'fields': {'air_temperature': {'units': 'K', 'values': [280.0 + k] * 12}, 'wind_east': {'units': 'm/s', 'values': [0.0] * 12}, 'wind_north': {'units': 'm/s', 'values': [0.0] * 12}}})
        coupled['coupled_atmosphere']['orography']['frames'].append({'elapsed_seconds': elapsed, 'orographic_precipitation_rate_mm_h': [20.0 if k == 1 else 0.0] * 12})
    coupled_path = directory / 'coupled.json'
    coupled_path.write_text(json.dumps(coupled))
    driven = copy.deepcopy(short)
    driven['time'].update(steps=18, spinup_steps=0)
    driven['snapshot_every_steps'] = 6
    driven['forcing']['rainfall'] = {'coupled_timeline_path': str(coupled_path)}
    result, output = run(driven, 'driven')
    assert result.returncode == 0, result.stderr
    driven_out = json.loads(output.read_text())
    assert driven_out['hydrology']['forcing_source']['coupled']['kind'] == 'coupled_timeline'
    rates = [max(f['rainfall_rate_mm_h']) for f in driven_out['hydrology']['frames']]
    assert rates[0] == 0 and rates[2] == 20.0 and rates[3] == 0, rates  # 20 mm/h between the second and third coupled frames
    assert abs(driven_out['frames'][2]['fields']['air_temperature']['values'][0] - 281.0) < 1e-9

    # Precipitation outcomes: rate_mm_h is the intensity while it rains, so the catchment receives the cell-mean
    # rate x duration_fraction (accumulation = rate x duration x interval), here 40 mm/h x 0.25 = 10 mm/h.
    showers = copy.deepcopy(coupled)
    showers['coupled_atmosphere'] = {'schema_version': 'terraflux.weather.coupled_atmosphere/v1', 'precipitation': {'frames': []}}
    for k, elapsed in enumerate([0.0, 3600.0, 7200.0]):
        showers['coupled_atmosphere']['precipitation']['frames'].append({'elapsed_seconds': elapsed, 'rate_mm_h': [40.0 if k == 1 else 0.0] * 12, 'duration_fraction': [0.25 if k == 1 else 0.0] * 12})
    showers_path = directory / 'showers.json'
    showers_path.write_text(json.dumps(showers))
    driven['forcing']['rainfall'] = {'coupled_timeline_path': str(showers_path)}
    result, output = run(driven, 'showers')
    assert result.returncode == 0, result.stderr
    showers_out = json.loads(output.read_text())
    rates = [max(f['rainfall_rate_mm_h']) for f in showers_out['hydrology']['frames']]
    assert rates[0] == 0 and abs(rates[2] - 10.0) < 1e-9 and rates[3] == 0, rates
    assert 'rate_mm_h x duration_fraction' in ' '.join(showers_out['hydrology']['forcing_source']['coupled']['sources'])

    # Invalid requests produce no output.
    mutations = [
        ('schema', lambda d: d.update(schema_version='other')),
        ('dt', lambda d: d['time'].update(timestep_seconds=7200.0)),
        ('no-drain', lambda d: (d['domain'].update(open_edges=False), d['terrain'].update(base_elevation_m=500.0))),
        ('no-rain', lambda d: d['forcing'].update(rainfall={})),
        ('negative-rate', lambda d: d['forcing']['rainfall']['events'][0].update(rate_mm_h=-1.0)),
        ('porosity', lambda d: d['soil'].update(porosity=1.5)),
        ('darcy', lambda d: d['groundwater'].update(hydraulic_conductivity_m_s=10.0)),
        ('frames', lambda d: (d['time'].update(steps=400), d.update(snapshot_every_steps=1))),
        ('terrain-size', lambda d: d.update(terrain={'values': [1.0, 2.0]})),
        ('coupled-missing', lambda d: d['forcing'].update(rainfall={'coupled_timeline_path': str(directory / 'missing.json')})),
    ]
    for name, mutate in mutations:
        invalid = copy.deepcopy(short)
        mutate(invalid)
        result, output = run(invalid, f'invalid-{name}')
        assert result.returncode != 0, name
        assert not output.exists(), name

print('catchment hydrology CLI checks passed')
