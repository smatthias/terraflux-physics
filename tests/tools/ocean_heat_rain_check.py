"""Check the ocean CLI extensions: rain on the sea and the coast, evaporation and air-sea heat exchange, the
layered temperature/salinity column, their output contract and budgets, coupling to a coupled-wind
(hurricane) run, and request validation."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request = json.loads((root / 'examples/ocean-heat-rain-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/ocean-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/ocean-circulation-v1.schema.json').read_text())
except ImportError:  # the schemas are still exercised by the viewer contract tests
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def close(a, b, rel, absolute=0.0):
    return abs(a - b) <= max(rel * max(abs(a), abs(b)), absolute)


def check_contract(timeline, label):
    """Per-frame shapes, identities and budgets of the extension blocks."""
    o = timeline['ocean']
    grid = timeline['frames'][0]['grid']
    count, area = grid['nx'] * grid['ny'], grid['spacing_east_m'] * grid['spacing_north_m']
    mask = o['ocean_mask']
    sea = [i for i in range(count) if mask[i]]
    schema_ok(OUT_SCHEMA, o, label)
    processes = o['processes']
    layers = o['thermal_layers']['count'] if 'thermal_layers' in processes else 0
    sigma = o['thermal_layers']['fractions'] if layers else []
    assert not layers or abs(sum(sigma) - 1) < 1e-12
    receiver = o['coastal']['receiver'] if 'rain' in processes else [-1] * count
    band = o['coastal']['band_m'] if 'rain' in processes else 0.0
    for i in range(count):
        r = receiver[i]
        if mask[i]:
            assert r == -1, 'sea cells have no receiver'
        elif r >= 0:
            assert mask[r], 'receivers are sea cells'
            dx = (r % grid['nx'] - i % grid['nx']) * grid['spacing_east_m']
            dy = (r // grid['nx'] - i // grid['nx']) * grid['spacing_north_m']
            assert math.hypot(dx, dy) <= band + 1e-6, 'receivers lie within the coastal band'
    heat0 = o['initial_heat_content_j']
    store0 = o['coastal']['initial_storage_m3'] if 'rain' in processes else 0.0
    for k, f in enumerate(o['frames']):
        c = f['cumulative_m3']
        # Volume: rivers, prescribed P - E, sponge, rain on the sea and coastal runoff in; evaporation out.
        expected = o['initial_volume_m3'] + c['river'] + c['net_precipitation'] + c['sponge'] + c['rain_sea'] + c['coastal_runoff'] - c['evaporation']
        assert close(f['volume_m3'], expected, 1e-9), f'{label}: volume budget at frame {k}'
        assert abs(f['volume_balance_error_m3']) <= 1e-9 * f['volume_m3'] and abs(f['salt_balance_error_kg']) <= 1e-9 * f['salt_kg']
        hb = f['heat_budget']
        assert abs(hb['heat_balance_error_j']) <= 1e-12 * f['volume_m3'] * 1025 * 3990 * 300, f'{label}: heat budget at frame {k}'
        if k == 0:
            assert hb['heat_content_j'] == heat0 and all(v == 0 for v in c.values()), 'the first frame is the initial state'
        if 'rain' in processes:
            r = f['rain']
            # Coastal land: rain = runoff + land loss + storage change.
            assert close(c['rain_coast'], c['coastal_runoff'] + c['coastal_loss'] + r['coastal_storage_m3'] - store0, 1e-9, 1.0), f'{label}: coastal budget at frame {k}'
            assert abs(r['coastal_balance_error_m3']) <= 1e-6 * max(1.0, c['rain_coast'])
            rate = r['rate_mm_h']
            on_sea = sum(rate[i] for i in sea) * area / 3.6e6
            on_coast = sum(rate[i] for i in range(count) if not mask[i] and receiver[i] >= 0) * area / 3.6e6
            on_inland = sum(rate[i] for i in range(count) if not mask[i] and receiver[i] < 0) * area / 3.6e6
            if k > 0:
                assert close(on_sea, r['rain_on_sea_m3_s'], 1e-3, 1e-3), f'{label}: rain on the sea matches the rate map'
                assert close(on_coast, r['rain_on_coast_m3_s'], 1e-3, 1e-3) and close(on_inland, r['rain_inland_m3_s'], 1e-3, 1e-3), f'{label}: land rain split'
                assert close(sum(r['coastal_inflow_m3_s']), r['coastal_runoff_m3_s'], 1e-3, 1e-3), 'the runoff arrives at the receivers'
            assert all(r['coastal_inflow_m3_s'][i] == 0 for i in range(count) if not mask[i]) and all(r['coastal_store_mm'][i] == 0 for i in sea)
        if 'air_sea' in processes and k > 0:
            a = f['air_sea']
            for i in sea:
                parts = a['shortwave_w_m2'][i] + a['longwave_w_m2'][i] + a['sensible_w_m2'][i] + a['latent_w_m2'][i]
                assert abs(parts - a['net_heat_w_m2'][i]) <= 0.01 * sum(abs(a[key][i]) for key in ['shortwave_w_m2', 'longwave_w_m2', 'sensible_w_m2', 'latent_w_m2']) + 2.0, 'net = SW + LW + SH + LH'
                sst = f['thermal']['sst_k'][i] if layers else f['temperature_k'][i]
                evaporation = -a['latent_w_m2'][i] / (2.501e6 - 2370 * (sst - 273.15)) / 1000.0 * 8.64e7
                assert abs(evaporation - a['evaporation_mm_day'][i]) <= 0.02 * abs(evaporation) + 0.02, 'evaporation = latent heat / (L_v rho_w)'
            total = sum(a['evaporation_mm_day'][i] for i in sea) * area / 8.64e7
            assert close(total, a['evaporation_m3_s'], 0.01, 1.0), 'evaporation total matches the map'
            assert close(sum(a['net_heat_w_m2'][i] for i in sea) / len(sea), a['mean_net_heat_w_m2'], 0.01, 0.5)
        if layers:
            th = f['thermal']
            assert len(th['layer_temperature_k']) == layers and all(len(v) == count for v in th['layer_temperature_k'])
            barrier = 0
            for i in sea:
                column_t = [th['layer_temperature_k'][j][i] for j in range(layers)]
                column_s = [th['layer_salinity_psu'][j][i] for j in range(layers)]
                # The slab output rounds temperature to 4 significant digits (0.1 K); the kernel test checks 1e-9.
                assert abs(sum(s * t for s, t in zip(sigma, column_t)) - f['temperature_k'][i]) <= 0.06, 'bulk temperature = column mean'
                assert abs(sum(s * v for s, v in zip(sigma, column_s)) - f['salinity_psu'][i]) <= 0.003, 'bulk salinity = column mean'
                assert abs(column_s[0] - f['surface_salinity_psu'][i]) <= 0.002 and abs(column_t[0] - th['sst_k'][i]) <= 0.01, 'the surface is the top layer'
                assert 0 < th['mixed_layer_depth_m'][i] <= f['thickness_m'][i] + 0.05 and 0 < th['isothermal_layer_depth_m'][i] <= f['thickness_m'][i] + 0.05
                barrier += th['isothermal_layer_depth_m'][i] > th['mixed_layer_depth_m'][i] + 1.0
            assert abs(barrier - th['barrier_layer_cells']) <= max(2, 0.02 * len(sea)), 'barrier-layer count'
            assert all(th['layer_temperature_k'][j][i] == 0 for j in range(layers) for i in range(count) if not mask[i])
            assert abs(th['mean_sst_k'] - sum(th['sst_k'][i] for i in sea) / len(sea)) <= 0.006, 'mean SST (5 significant digits)'
        lenses = f['surface_lenses']
        assert all(mask[l['freshest_cell']] for l in lenses) and all(lenses[j]['area_km2'] >= lenses[j + 1]['area_km2'] for j in range(len(lenses) - 1))
    return o


with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    path = directory / 'request.json'

    def run(doc, name):
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    schema_ok(RUN_SCHEMA, request, 'example request')

    # 1. A short run of the example with re-timed events: a storm over the open sea, a storm on the coast,
    #    and a cold, dry outbreak, all within the first day.
    short = copy.deepcopy(request)
    short['time'].update(steps=96, spinup_steps=48)
    short['snapshot_every_steps'] = 24
    short['forcing']['rain']['events'] = [
        {'start_seconds': 0.0, 'duration_seconds': 86400.0, 'rate_mm_h': 20.0, 'center_east_m': 1300000.0, 'center_north_m': 600000.0, 'radius_m': 150000.0},
        {'start_seconds': 0.0, 'duration_seconds': 86400.0, 'rate_mm_h': 15.0, 'center_east_m': 250000.0, 'center_north_m': 600000.0, 'radius_m': 110000.0},
    ]
    short['forcing']['air_sea']['air_masses'] = [
        {'start_seconds': 0.0, 'duration_seconds': 172800.0, 'center_east_m': 1200000.0, 'center_north_m': 1250000.0, 'radius_m': 300000.0, 'temperature_anomaly_k': -10.0, 'relative_humidity': 0.4}]
    schema_ok(RUN_SCHEMA, short, 'short request')
    result, output = run(short, 'short')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    assert len(timeline['frames']) == 5 and timeline['frames'][-1]['elapsed_seconds'] == 96 * 1800
    o = check_contract(timeline, 'example')
    assert o['processes'] == ['rain', 'coastal_runoff', 'air_sea', 'thermal_layers']
    assert o['request']['physics']['thermal_layers']['count'] == 10 and o['request']['coastal']['band_m'] == 110000.0 and 'air_sea' in o['request']
    assert o['forcing_source']['air_sea']['shortwave']['kind'] == 'astronomical' and len(o['forcing_source']['rain']['events']) == 2
    grid = timeline['frames'][0]['grid']
    nx, count = grid['nx'], grid['nx'] * grid['ny']
    cell = lambda east, north: int(north // grid['spacing_north_m']) * nx + int(east // grid['spacing_east_m'])
    mask = o['ocean_mask']
    day = o['frames'][2]          # t = 24 h: both storms have rained a full day
    storm, quiet, outbreak = cell(1300000, 600000), cell(1300000, 250000), cell(1200000, 1250000)
    assert day['surface_salinity_psu'][storm] < day['salinity_psu'][storm] - 0.3, 'a fresh rain lens under the open-sea storm'
    assert any(l['kind'] == 'rain' and l['area_km2'] > 10000 for l in day['surface_lenses']), 'it is reported as a rain lens'
    assert day['air_sea']['evaporation_mm_day'][storm] < day['air_sea']['evaporation_mm_day'][quiet], 'rain-saturated air suppresses evaporation'
    assert day['rain']['coastal_runoff_m3_s'] > 100 and day['cumulative_m3']['rain_coast'] > 0 and day['cumulative_m3']['rain_inland'] > 0, 'the coastal storm rains on the coast and inland'
    inflow = [i for i in range(count) if day['rain']['coastal_inflow_m3_s'][i] > 0]
    assert inflow and all(mask[i] for i in inflow), 'runoff enters the nearshore sea'
    # A storm centred on the coast rains straight onto the nearshore sea too: while it rains the nearshore lens
    # is a mixture; the land keeps draining afterwards, so the lens becomes runoff-dominated.
    assert any(l['source_shares']['coastal_runoff'] > 0.2 for l in day['surface_lenses']), 'the nearshore lens carries coastal runoff'
    last = o['frames'][-1]
    assert last['rain']['coastal_runoff_m3_s'] > 0 and last['rain']['rain_on_coast_m3_s'] == 0, 'the coast still drains after the rain'
    assert any(l['kind'] == 'coastal_runoff' for l in last['surface_lenses']), 'the nearshore lens is runoff-dominated after the storm'
    assert all(abs(sum(l['source_shares'].values()) - 1) < 0.01 or l['kind'] == 'other' for l in last['surface_lenses'])
    assert last['air_sea']['evaporation_mm_day'][outbreak] > 2 * last['air_sea']['evaporation_mm_day'][quiet], 'the dry outbreak evaporates strongly'
    assert last['air_sea']['net_heat_w_m2'][outbreak] < -300, 'and draws heat out of the sea'
    assert last['thermal']['mixed_layer_depth_m'][outbreak] > last['thermal']['mixed_layer_depth_m'][quiet], 'the outbreak deepens the mixed layer'
    assert last['surface_salinity_psu'][outbreak] > o['frames'][0]['surface_salinity_psu'][outbreak], 'evaporation makes the surface saltier'

    # 2. The ocean under the engine's hurricane run: wind, storm wind, humidity, cloud, pressure and the
    #    cell-mean precipitation with its wet-bulb temperature, linear in time.
    hurricane_path = root / 'viewer/public/hurricane/engine-timeline.json'
    hurricane = json.loads(hurricane_path.read_text())
    hgrid = hurricane['frames'][0]['grid']
    atmosphere = hurricane['coupled_atmosphere']
    storm_wind = [max(f['storm_wind_speed_m_s'][i] for f in atmosphere['storms']['frames']) for i in range(hgrid['nx'] * hgrid['ny'])]
    coupled = {
        'schema_version': 'terraflux.ocean.circulation-run/v1',
        'time': {'start_time_utc': hurricane['frames'][0]['start_time_utc'], 'timestep_seconds': 1800.0, 'steps': 144, 'spinup_steps': 0},
        'grid': {k: hgrid[k] for k in ['nx', 'ny', 'origin_east_m', 'origin_north_m', 'spacing_east_m', 'spacing_north_m', 'geographic_anchor_longitude_deg', 'geographic_anchor_latitude_deg']},
        'basin': {'ocean_mask': [0 if land else 1 for land in atmosphere['surface']['land']]},
        'ocean': {'initial_thickness_m': 150.0, 'initial_salinity_psu': 35.0, 'initial_temperature_k': 302.15},
        'physics': {'coriolis_mode': 'beta_plane', 'thermal_layers': {'count': 10, 'surface_layer_m': 3.0, 'initial_mixed_layer_depth_m': 25.0, 'initial_thermocline_scale_m': 60.0}},
        'forcing': {
            'wind': {'kind': 'coupled_timeline', 'path': str(hurricane_path), 'interpolation': 'linear'},
            'surface_temperature': {'south_k': 302.15, 'north_k': 302.15},
            'net_precipitation': {'south_m_s': 0.0, 'north_m_s': 0.0},
            'air_sea': {'coupled_timeline_path': str(hurricane_path), 'interpolation': 'linear'},
            'rain': {'coupled_timeline_path': str(hurricane_path), 'interpolation': 'linear', 'coastal_runoff': {'band_m': 90000.0}},
            'rivers': [],
        },
        'snapshot_every_steps': 24,
    }
    schema_ok(RUN_SCHEMA, coupled, 'hurricane request')
    result, output = run(coupled, 'hurricane')
    assert result.returncode == 0, result.stderr
    driven = json.loads(output.read_text())
    o = check_contract(driven, 'hurricane')
    provenance = o['forcing_source']
    assert provenance['air_sea']['coupled']['sampling'] == 'nearest_cell_linear_in_time' and provenance['wind']['sampling'] == 'nearest_cell_linear_in_time'
    sources = ' '.join(provenance['rain']['coupled']['sources'])
    assert 'rate_mm_h x duration_fraction' in sources and 'storm_wind_speed_m_s' in ' '.join(provenance['air_sea']['coupled']['sources'])
    mask = o['ocean_mask']
    sea = [i for i in range(len(mask)) if mask[i]]
    frames = o['frames']
    peak_outcome = max(max(r * d for r, d in zip(f['rate_mm_h'], f['duration_fraction'])) for f in atmosphere['precipitation']['frames'])
    assert all(max(f['rain']['rate_mm_h']) <= peak_outcome + 1e-6 for f in frames), 'the ocean receives the cell-mean rate, not the shower intensity'
    assert frames[-1]['cumulative_m3']['rain_sea'] > 0 and frames[-1]['cumulative_m3']['coastal_runoff'] > 0, 'the hurricane rains on the sea and the coast drains into it'
    windy = [i for i in sea if storm_wind[i] > 30.0]
    calm = [i for i in sea if storm_wind[i] < 15.0]
    assert windy and calm
    cooling = lambda cells: sum(frames[0]['thermal']['sst_k'][i] - frames[-1]['thermal']['sst_k'][i] for i in cells) / len(cells)
    deepening = lambda cells: sum(frames[-1]['thermal']['mixed_layer_depth_m'][i] - frames[0]['thermal']['mixed_layer_depth_m'][i] for i in cells) / len(cells)
    assert cooling(windy) > cooling(calm) + 0.1, f'a cold wake under the hurricane winds ({cooling(windy):.2f} vs {cooling(calm):.2f} K)'
    assert deepening(windy) > deepening(calm), 'the storm deepens the mixed layer'
    assert max(max(f['air_sea']['evaporation_mm_day']) for f in frames[1:]) > 10, 'storm winds evaporate more than 10 mm/day'

    # 3. Invalid requests produce no output.
    mutations = [
        ('one-layer', lambda d: d['physics']['thermal_layers'].update(count=1)),
        ('initial-sst', lambda d: d['physics']['thermal_layers'].update(initial_surface_temperature='hot')),
        ('shear', lambda d: d['physics']['thermal_layers'].update(bulk_richardson_number=-1.0)),
        ('rain-rate', lambda d: d['forcing']['rain']['events'][0].update(rate_mm_h=600.0)),
        ('rain-empty', lambda d: d['forcing'].update(rain={'coastal_runoff': {'band_m': 50000.0}})),
        ('runoff-coefficient', lambda d: d['forcing']['rain']['coastal_runoff'].update(runoff_coefficient=1.5)),
        ('air-mass-humidity', lambda d: d['forcing']['air_sea']['air_masses'][0].update(relative_humidity=1.5)),
        ('shortwave-kind', lambda d: d['forcing']['air_sea'].update(shortwave={'kind': 'moonlight'})),
        ('coefficient', lambda d: d['forcing']['air_sea'].update(coefficients={'moisture_transfer_coefficient': 0.5})),
        ('humidity', lambda d: d['forcing']['air_sea'].update(relative_humidity_south=1.4)),
        ('interpolation', lambda d: d['forcing']['air_sea'].update(coupled_timeline_path=str(hurricane_path), interpolation='cubic')),
        ('coupled-missing', lambda d: d['forcing']['rain'].update(coupled_timeline_path=str(directory / 'missing.json'))),
    ]
    for name, mutate in mutations:
        invalid = copy.deepcopy(short)
        invalid['time'].update(steps=4, spinup_steps=0)
        invalid['snapshot_every_steps'] = 4
        mutate(invalid)
        result, output = run(invalid, f'invalid-{name}')
        assert result.returncode != 0, name
        assert not output.exists(), name

print('ocean heat and rain CLI checks passed' + ('' if jsonschema else ' (jsonschema not installed: schema validation skipped)'))
