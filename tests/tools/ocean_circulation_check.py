"""Check the ocean circulation CLI: request validation, output contract, budgets and couplings."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request = json.loads((root / 'examples/atlantic-streams-request.json').read_text())
cli = sys.argv[1]

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    path = directory / 'request.json'

    def run(doc, name):
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    # A short run on a coarser basin: half a year of spin-up, two months recorded, with a river pulse.
    short = copy.deepcopy(request)
    short['grid'].update(nx=30, ny=24, spacing_east_m=100000.0, spacing_north_m=100000.0)
    short['time'].update(timestep_seconds=3600.0, steps=1440, spinup_steps=4320)
    short['snapshot_every_steps'] = 240
    short['forcing']['rivers'] = [
        {'name': 'river', 'east_m': 300000.0, 'north_m': 700000.0, 'discharge_m3_s': 30000.0, 'temperature_k': 285.15},
        {'name': 'pulse', 'east_m': 700000.0, 'north_m': 2100000.0, 'discharge_m3_s': 0.0, 'temperature_k': 274.15,
         'events': [{'start_seconds': 0.0, 'duration_seconds': 5184000.0, 'discharge_m3_s': 400000.0}]},
        {'name': 'alpine', 'east_m': 300000.0, 'north_m': 1300000.0, 'discharge_m3_s': 0.0, 'catchment_timeline_path': 'viewer/public/flood/engine-timeline.json', 'catchment_scale': 500.0},
    ]
    result, output = run(short, 'short')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    assert timeline['schema_version'] == 'terraflux.weather.timeline/v1'
    assert len(timeline['frames']) == 7 and timeline['frames'][-1]['elapsed_seconds'] == 1440 * 3600
    assert timeline['terrain']['schema_version'] == 'terraflux.terrain.grid/v1' and len(timeline['terrain']['values']) == 720
    o = timeline['ocean']
    assert o['schema_version'] == 'terraflux.ocean.circulation/v1' and o['method'] == 'reduced_gravity_c_grid_sverdrup_stommel_formation'
    assert o['basin_source']['kind'] == 'synthetic_ridges_and_basins' and o['basin_source']['sponge']['south_rows'] == 4
    assert o['forcing_source']['wind']['kind'] == 'gyre' and len(o['forcing_source']['rivers']) == 3
    assert o['forcing_source']['rivers'][2]['catchment']['kind'] == 'catchment_timeline' and o['forcing_source']['rivers'][2]['catchment']['scale'] == 500.0
    count = 720
    mask = o['ocean_mask']
    assert len(mask) == count and sum(mask) > 400 and len(o['sponge']) == count
    frames = o['frames']
    assert len(frames) == 7 and frames[0]['cumulative_m3']['river'] == 0
    for i, f in enumerate(frames):
        assert f['elapsed_seconds'] == timeline['frames'][i]['elapsed_seconds']
        for key in ['u_m_s', 'v_m_s', 'thickness_m', 'salinity_psu', 'surface_salinity_psu', 'temperature_k', 'sinking_m_s', 'front', 'stream_id']:
            assert len(f[key]) == count, key
        for j in range(count):
            if not mask[j]:
                assert f['u_m_s'][j] == 0 and f['thickness_m'][j] == 0 and f['stream_id'][j] == -1
            assert f['surface_salinity_psu'][j] <= f['salinity_psu'][j] + 1e-6
        c = f['cumulative_m3']
        assert abs(f['volume_m3'] - o['initial_volume_m3'] - c['river'] - c['net_precipitation'] - c['sponge']) <= 1e-6 * o['initial_volume_m3'], 'volume budget'
        assert abs(f['volume_balance_error_m3']) <= 1e-6 * o['initial_volume_m3'] and abs(f['salt_balance_error_kg']) <= 1e-6 * o['initial_salt_kg']
        assert f['formation_cells'] == sum(1 for v in f['sinking_m_s'] if v > 0)
        assert f['stream_count'] == len(f['streams']) and all(s['cell_count'] >= 2 and len(s['path']) >= 1 for s in f['streams'])
        assert len(f['exchange_points']) == 3 and all(p['plume_area_km2'] >= 0 for p in f['exchange_points'])
    last = frames[-1]
    assert last['river_input_m3_s'] > 430000, 'base rivers + pulse + catchment-fed discharge'
    pulse = [p for p in last['exchange_points'] if p['name'] == 'pulse'][0]
    assert pulse['plume_area_km2'] > 0 and pulse['minimum_salinity_psu'] < 33, 'the pulse produces a fresh plume'
    assert last['max_speed_m_s'] > 0.05 and last['stream_count'] >= 1, 'currents and streams develop'
    assert last['cumulative_m3']['river'] > 0 and last['cumulative_m3']['sinking'] >= 0

    # Coupled wind forcing from a synthetic coupled-wind timeline covering the basin.
    coupled = {'schema_version': 'terraflux.weather.timeline/v1', 'frames': [], 'coupled_atmosphere': {'schema_version': 'terraflux.weather.coupled_atmosphere/v1'}}
    grid = {'nx': 6, 'ny': 5, 'origin_east_m': 0.0, 'origin_north_m': 0.0, 'spacing_east_m': 500000.0, 'spacing_north_m': 500000.0,
            'geographic_anchor_longitude_deg': -45.0, 'geographic_anchor_latitude_deg': 38.0, 'coordinate_system': 'local_azimuthal_equidistant_m', 'array_order': 'row_major_y_then_x'}
    for k, elapsed in enumerate([0.0, 86400.0]):
        coupled['frames'].append({'schema_version': 'terraflux.weather.forecast/v1', 'start_time_utc': short['time']['start_time_utc'], 'elapsed_seconds': elapsed, 'grid': grid, 'observation_updates': [],
                                  'fields': {'air_temperature': {'units': 'K', 'values': [288.0] * 30}, 'wind_east': {'units': 'm/s', 'values': [8.0 if k == 0 else -8.0] * 30}, 'wind_north': {'units': 'm/s', 'values': [0.0] * 30}}})
    coupled_path = directory / 'coupled.json'
    coupled_path.write_text(json.dumps(coupled))
    driven = copy.deepcopy(short)
    driven['time'].update(steps=48, spinup_steps=0)
    driven['snapshot_every_steps'] = 24
    driven['forcing']['wind'] = {'kind': 'coupled_timeline', 'path': str(coupled_path), 'air_density_kg_m3': 1.2, 'drag_coefficient': 1.3e-3}
    driven['forcing']['rivers'] = []
    result, output = run(driven, 'driven')
    assert result.returncode == 0, result.stderr
    driven_out = json.loads(output.read_text())
    assert driven_out['ocean']['forcing_source']['wind']['kind'] == 'coupled_timeline'
    wind = driven_out['frames'][1]['fields']['wind_east']['values']
    water = [i for i, m in enumerate(driven_out['ocean']['ocean_mask']) if m]
    assert abs(wind[water[0]] - 8.0) < 1e-6, 'the forecast frames carry the coupled wind (first coupled frame applies during the first day)'
    assert abs(driven_out['frames'][2]['fields']['wind_east']['values'][water[0]] + 8.0) < 1e-6, 'the second coupled frame applies afterwards'

    # Invalid requests produce no output.
    mutations = [
        ('schema', lambda d: d.update(schema_version='other')),
        ('cfl', lambda d: d['time'].update(timestep_seconds=40000.0)),
        ('no-water', lambda d: d['basin']['terrain'].update(base_elevation_m=1000.0)),
        ('salinity', lambda d: d['ocean'].update(initial_salinity_psu=60.0)),
        ('wind-kind', lambda d: d['forcing'].update(wind={'kind': 'tornado'})),
        ('river-range', lambda d: d['forcing']['rivers'][0].update(discharge_m3_s=-1.0)),
        ('frames', lambda d: (d['time'].update(steps=400), d.update(snapshot_every_steps=1))),
        ('mask-size', lambda d: d.update(basin={'ocean_mask': [1, 0, 1]})),
        ('coriolis', lambda d: d['physics'].update(coriolis_mode='spiral')),
        ('catchment-missing', lambda d: d['forcing']['rivers'][2].update(catchment_timeline_path=str(directory / 'missing.json'))),
    ]
    for name, mutate in mutations:
        invalid = copy.deepcopy(short)
        invalid['time'].update(steps=24, spinup_steps=0)
        invalid['snapshot_every_steps'] = 24
        mutate(invalid)
        result, output = run(invalid, f'invalid-{name}')
        assert result.returncode != 0, name
        assert not output.exists(), name

print('ocean circulation CLI checks passed')
