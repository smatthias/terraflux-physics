"""Check the polar processes of the ocean CLI: sea ice, a coupled land ice sheet, climate warming and meltwater
hosing. Output contract and budgets (volume with sea-ice and glacial water, heat with the ice), the identities
between maps and totals, the response of the overturning to hosing against a control run, and request
validation. Runs a one-year variant of examples/polar-ocean-request.json twice (about 30 s)."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request = json.loads((root / 'examples/polar-ocean-request.json').read_text())
cli = sys.argv[1]
YEAR = 365.25 * 86400.0
ICE_TO_WATER, OCEAN_AREA = 0.917, 3.618e14

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
    """Shapes, identities and budgets of a polar run."""
    o = timeline['ocean']
    schema_ok(OUT_SCHEMA, o, label)
    grid = timeline['frames'][0]['grid']
    count, area = grid['nx'] * grid['ny'], grid['spacing_east_m'] * grid['spacing_north_m']
    mask = o['ocean_mask']
    sea = [i for i in range(count) if mask[i]]
    processes = o['processes']
    sheet = o.get('ice_sheet')
    if 'ice_sheet' in processes:
        assert any(sheet['domain']) and all(not (d and m) for d, m in zip(sheet['domain'], mask)), f'{label}: the ice sheet lives on land'
        assert close(sheet['initial_sea_level_equivalent_m'], sheet['initial_volume_m3'] * ICE_TO_WATER / OCEAN_AREA, 1e-3)
    hosing = o['forcing_source'].get('hosing')
    previous = None
    for k, f in enumerate(o['frames']):
        c = f['cumulative_m3']
        # Volume: the slab and rain terms plus the water sea ice gives (melt - freezing) and the glacial water.
        expected = (o['initial_volume_m3'] + c['river'] + c['net_precipitation'] + c['sponge'] + c['rain_sea'] + c['coastal_runoff'] - c['evaporation']
                    + c['ice_water'] + c['glacial'])
        assert close(f['volume_m3'], expected, 1e-9), f'{label}: volume budget at frame {k}'
        assert abs(f['volume_balance_error_m3']) <= 1e-9 * f['volume_m3'] and abs(f['salt_balance_error_kg']) <= 1e-9 * f['salt_kg'], f'{label}: step budgets at frame {k}'
        assert abs(f['heat_budget']['heat_balance_error_j']) <= 1e-12 * f['volume_m3'] * 1025 * 3990 * 300, f'{label}: heat budget at frame {k}'
        if k == 0:
            assert c['ice_water'] == 0 and c['glacial'] == 0 and f['glacial']['cumulative_m3'] == 0, 'the first frame is the initial state'
        g = f['glacial']
        assert g['meltwater_m3_s'] >= 0 and g['ice_m3_s'] >= 0 and close(g['cumulative_m3'], c['glacial'], 1e-12, 1e-3)
        if previous is not None:
            assert c['glacial'] >= previous['cumulative_m3']['glacial'] - 1e-3, 'glacial water only enters the sea'
        # Sea ice: maps and totals, land free of ice, the surface at most melting, heat identity.
        si = f['sea_ice']
        a, h, ts, to_ice = si['ice_concentration'], si['ice_thickness_m'], si['ice_surface_temperature_k'], si['ocean_to_ice_heat_w_m2']
        assert all(a[i] == 0 and h[i] == 0 and ts[i] == 0 and to_ice[i] == 0 for i in range(count) if not mask[i]), f'{label}: land has no sea ice'
        assert all(0 <= a[i] <= 1 and h[i] >= 0 for i in sea)
        assert all(180 < ts[i] <= 273.2 for i in sea if a[i] > 0), f'{label}: ice surface at most melting'
        assert close(si['ice_area_km2'] * 1e6, sum(a[i] for i in sea) * area, 2e-3, area * 1e-3), f'{label}: sea-ice area at frame {k}'
        assert close(si['ice_volume_km3'] * 1e9, sum(a[i] * h[i] for i in sea) * area, 2e-3, area * 1e-2), f'{label}: sea-ice volume at frame {k}'
        assert close(si['ice_extent_km2'] * 1e6, sum(area for i in sea if a[i] > 0.15), 1e-3, 1.0), f'{label}: sea-ice extent at frame {k}'
        assert close(si['cumulative_ice_water_m3'], c['ice_water'], 1e-12, 1e-3)
        airsea = f['air_sea']
        for i in sea:
            parts = airsea['shortwave_w_m2'][i] + airsea['longwave_w_m2'][i] + airsea['sensible_w_m2'][i] + airsea['latent_w_m2'][i] - to_ice[i]
            scale = sum(abs(airsea[key][i]) for key in ['shortwave_w_m2', 'longwave_w_m2', 'sensible_w_m2', 'latent_w_m2']) + abs(to_ice[i])
            assert abs(parts - airsea['net_heat_w_m2'][i]) <= 0.01 * scale + 2, f'{label}: net heat = open-water fluxes - heat to the ice (frame {k}, cell {i})'
        assert all(to_ice[i] == 0 for i in sea if a[i] == 0 and (previous is None or previous['sea_ice']['ice_concentration'][i] == 0)), 'no ice, no heat to the ice'
        # Oxygen capacity: cold water holds more.
        o2, sst = f['thermal']['surface_oxygen_umol_kg'], f['thermal']['sst_k']
        assert all(o2[i] == 0 for i in range(count) if not mask[i]) and all(200 < o2[i] < 500 for i in sea)
        coldest, warmest = min(sea, key=lambda i: sst[i]), max(sea, key=lambda i: sst[i])
        assert o2[coldest] > o2[warmest] + 50, f'{label}: oxygen solubility falls with temperature'
        # Hosing follows its ramp (the rate of the last step, taken at its middle).
        if hosing and k > 0:
            t = f['elapsed_seconds'] - 0.5 * o['request']['time']['timestep_seconds']
            expected_sv = min(hosing['max_sv'], max(0.0, hosing['ramp_sv_per_year'] * (t - hosing['start_seconds']) / YEAR))
            assert close(f['hosing_sv'], expected_sv, 1e-4, 1e-6), f'{label}: hosing ramp at frame {k}'
        # The ice sheet: ice only on its domain, surface = bed + ice, totals from the maps, sea level from the ice lost.
        if 'ice_sheet' in processes:
            s = f['ice_sheet']
            thick = s['ice_thickness_m']
            assert all(thick[i] == 0 for i in range(count) if not sheet['domain'][i]) and all(v >= 0 for v in thick)
            assert all(abs(s['surface_elevation_m'][i] - sheet['bed_elevation_m'][i] - thick[i]) <= 1.0 + 1e-3 * thick[i] for i in range(count) if sheet['domain'][i])
            assert close(s['volume_m3'], sum(thick[i] for i in range(count) if sheet['domain'][i]) * area, 2e-3, area), f'{label}: ice volume at frame {k}'
            smb = sum(s['smb_m_ice_yr'][i] for i in range(count) if sheet['domain'][i]) * area * ICE_TO_WATER / 1e9
            assert close(s['smb_gt_yr'], smb, 0.01, 0.5), f'{label}: surface mass balance at frame {k}'
            assert close(s['sea_level_rise_m'], (sheet['initial_volume_m3'] - s['volume_m3']) * ICE_TO_WATER / OCEAN_AREA, 1e-3, 1e-7), f'{label}: sea level at frame {k}'
            assert all(v >= 0 for v in s['melt_m_ice_yr'])
            # Its discharge enters the sea as ice (icebergs + frontal melt), its runoff as meltwater (with the hosing).
            if k > 0:
                assert close(g['ice_m3_s'], s['calving_m3_s'] + s['frontal_ablation_m3_s'], 2e-3, 1.0), f'{label}: glacial ice = calving + frontal melt (frame {k})'
                assert close(g['meltwater_m3_s'], s['runoff_m3_s'] + (f['hosing_sv'] * 1e6 if hosing else 0.0), 2e-3, 1.0), f'{label}: glacial meltwater = runoff + hosing (frame {k})'
        for lens in f['surface_lenses']:
            assert 'glacial' in lens['source_shares'], 'polar runs report the glacial share of each lens'
        previous = f
    return o


with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)

    def run(doc, name):
        path = directory / f'{name}-request.json'
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    schema_ok(RUN_SCHEMA, request, 'example request')

    # 1. One year from January, no spin-up; quick hosing (0.5 Sv within six months) and +2 K of warming.
    hosed = copy.deepcopy(request)
    hosed['time'].update(steps=11688, spinup_steps=0)
    hosed['snapshot_every_steps'] = 2922
    hosed['forcing']['hosing'].update(ramp_sv_per_year=1.0, max_sv=0.5)
    hosed['forcing']['warming'].update(rate_k_per_year=2.0, max_k=2.0)
    control = copy.deepcopy(hosed)
    del control['forcing']['hosing']
    del control['forcing']['warming']
    for name, doc in [('hosed', hosed), ('control', control)]:
        schema_ok(RUN_SCHEMA, doc, f'{name} request')
    result, output = run(hosed, 'hosed')
    assert result.returncode == 0, result.stderr
    h = check_contract(json.loads(output.read_text()), 'hosed')
    result, output = run(control, 'control')
    assert result.returncode == 0, result.stderr
    c = check_contract(json.loads(output.read_text()), 'control')
    assert h['processes'] == ['air_sea', 'thermal_layers', 'sea_ice', 'ice_sheet', 'warming', 'hosing'] and c['processes'] == ['air_sea', 'thermal_layers', 'sea_ice', 'ice_sheet']
    assert len(h['frames']) == 5 and h['frames'][-1]['elapsed_seconds'] == 11688 * 2700.0
    assert h['request']['physics']['sea_ice']['initial_thickness_m'] == 1.5 and h['forcing_source']['warming']['amplification_north'] == 2.5
    mask = h['ocean_mask']
    grid = h['request']['grid']
    nx, ny = grid['nx'], grid['ny']
    sea = [i for i in range(nx * ny) if mask[i]]
    north = [i for i in sea if i // nx >= int(0.7 * ny)]
    south = [i for i in sea if i // nx < int(0.3 * ny)]
    mean = lambda values, cells: sum(values[i] for i in cells) / len(cells)
    H, C = h['frames'], c['frames']
    # Seasons: sea ice in spring, (almost) none at the end of the summer; the sinking is a winter process.
    assert C[1]['sea_ice']['ice_area_km2'] > 10 * max(1.0, C[3]['sea_ice']['ice_area_km2']) or C[3]['sea_ice']['ice_area_km2'] < 3e4, 'seasonal sea ice'
    assert C[4]['sea_ice']['ice_area_km2'] > 3e5, 'the ice is back in January'
    assert C[2]['overturning_sv'] < 0.3 * C[1]['overturning_sv'], 'little sinking in summer'
    assert C[4]['ice_sheet']['smb_gt_yr'] > 0 and C[3]['ice_sheet']['runoff_m3_s'] + C[2]['ice_sheet']['runoff_m3_s'] > 10, 'the ice sheet gains snow and melts in summer'
    # Hosing: the fresh cap over the sinking region weakens the next winter's overturning.
    assert H[-1]['overturning_sv'] < 0.8 * C[-1]['overturning_sv'], f"hosing weakens the overturning ({H[-1]['overturning_sv']:.1f} vs {C[-1]['overturning_sv']:.1f} Sv)"
    assert mean(H[-1]['surface_salinity_psu'], north) < mean(C[-1]['surface_salinity_psu'], north) - 0.2, 'a fresher northern surface'
    assert H[-1]['cumulative_m3']['glacial'] > 20 * C[-1]['cumulative_m3']['glacial'], 'hosing dominates the glacial water'
    assert any(l['kind'] == 'glacial' and l['source_shares']['glacial'] > 0.5 for f in H[2:] for l in f['surface_lenses']), 'a glacial meltwater lens'
    # Warming: the south (amplification 1) warms, and its water holds less oxygen.
    assert mean(H[-1]['thermal']['sst_k'], south) > mean(C[-1]['thermal']['sst_k'], south) + 0.5, 'the warmed south'
    assert mean(H[-1]['thermal']['surface_oxygen_umol_kg'], south) < mean(C[-1]['thermal']['surface_oxygen_umol_kg'], south), 'less oxygen in warmer water'
    assert H[-1]['ice_sheet']['sea_level_rise_m'] > 0 and H[-1]['ice_sheet']['volume_m3'] < h['ice_sheet']['initial_volume_m3'], 'the ice sheet loses mass'

    # 2. Invalid requests produce no output.
    def no_layers(d):
        del d['physics']['thermal_layers']
    def no_air_sea(d):
        del d['forcing']['air_sea']
    mutations = [
        ('ice-without-layers', no_layers, 'layered column'),
        ('ice-without-air-sea', no_air_sea, 'air-sea forcing'),
        ('ice-concentration', lambda d: d['physics']['sea_ice'].update(initial_concentration=1.2), 'initial_concentration'),
        ('ice-conductivity', lambda d: d['physics']['sea_ice'].update(conductivity_w_m_k=50.0), 'conductivity'),
        ('warming-rate', lambda d: d['forcing']['warming'].update(rate_k_per_year=5.0), 'forcing.warming'),
        ('hosing-no-sea', lambda d: d['forcing']['hosing'].update(center_east_m=-5.0e6, radius_m=1000.0), 'no sea cell'),
        ('hosing-max', lambda d: d['forcing']['hosing'].update(max_sv=50.0), 'max_sv'),
        ('sheet-no-land', lambda d: d['ice_sheet']['region'].update(center_east_m=1700000.0, center_north_m=1500000.0, radius_east_m=150000.0, radius_north_m=150000.0), 'no land cell'),
        ('sheet-refreezing', lambda d: d['ice_sheet'].update(physics={'refreezing_fraction': 1.5}), 'refreezing_fraction'),
        ('sheet-step-every', lambda d: d['ice_sheet'].update(step_every=0), 'step_every'),
    ]
    for name, mutate, message in mutations:
        invalid = copy.deepcopy(hosed)
        invalid['time'].update(steps=4, spinup_steps=0)
        invalid['snapshot_every_steps'] = 4
        mutate(invalid)
        result, output = run(invalid, f'invalid-{name}')
        assert result.returncode != 0 and message in result.stderr, f'{name}: {result.stderr[-200:]}'
        assert not output.exists(), name

print('polar ocean CLI checks passed' + ('' if jsonschema else ' (jsonschema not installed: schema validation skipped)'))
