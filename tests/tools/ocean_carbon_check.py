"""Check the ocean carbon extension of the ocean CLI: atmospheric CO2 over land and sea, the ocean's CO2 uptake
and acidification, oxygen and dead zones, their output contract and budgets, the response to the atmosphere and
to river nitrate, and request validation."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/ocean-carbon-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/ocean-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/ocean-circulation-v1.schema.json').read_text())
except ImportError:  # the schemas are still exercised by the viewer contract tests
    jsonschema = None

MT = 44.01e-12   # Mt CO2 per mol


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def close(a, b, rel, absolute=0.0):
    return abs(a - b) <= max(rel * max(abs(a), abs(b)), absolute)


def short(days=20, start='2025-07-01T00:00:00Z', snapshot_hours=120):
    """The example basin over a few summer weeks without spin-up."""
    req = copy.deepcopy(example)
    req['time'] = {'start_time_utc': start, 'timestep_seconds': 3600.0, 'steps': days * 24, 'spinup_steps': 0}
    req['snapshot_every_steps'] = snapshot_hours
    return req


def run(req, label):
    schema_ok(RUN_SCHEMA, req, f'{label} request')
    with tempfile.TemporaryDirectory() as tmp:
        path, out = Path(tmp) / 'request.json', Path(tmp) / 'out.json'
        path.write_text(json.dumps(req))
        done = subprocess.run([cli, str(path), str(out)], capture_output=True, text=True)
        assert done.returncode == 0, f'{label}: {done.stderr[-400:]}'
        return json.loads(out.read_text())


def rejected(req, needle, label):
    with tempfile.TemporaryDirectory() as tmp:
        path, out = Path(tmp) / 'request.json', Path(tmp) / 'out.json'
        path.write_text(json.dumps(req))
        done = subprocess.run([cli, str(path), str(out)], capture_output=True, text=True)
        assert done.returncode != 0, f'{label} should be rejected'
        assert needle in done.stderr, f'{label}: expected "{needle}" in {done.stderr[-300:]}'


def check_contract(timeline, label):
    """Shapes, class identities and budgets of the carbon blocks."""
    o = timeline['ocean']
    schema_ok(OUT_SCHEMA, o, label)
    grid = timeline['frames'][0]['grid']
    count, area_km2 = grid['nx'] * grid['ny'], grid['spacing_east_m'] * grid['spacing_north_m'] / 1e6
    mask = o['ocean_mask']
    sea = [i for i in range(count) if mask[i]]
    assert {'biogeochemistry', 'atmosphere_co2', 'thermal_layers', 'air_sea'} <= set(o['processes'])
    top = o['carbon']
    L = len(top['levels_m'])
    assert top['method'] == 'z_level_npzd_lite_carbonate_wanninkhof_metabolic_index'
    assert len(top['seafloor_depth_m']) == count and len(top['emission_mol_m2_s']) == count
    assert all(top['seafloor_depth_m'][i] > 0 for i in sea) and all(top['seafloor_depth_m'][i] == 0 for i in range(count) if not mask[i])
    assert top['open_boundary']['cells'] > 0, 'the sponge is the open boundary of the chemistry'
    # The emissions add up to the sources.
    emitted = sum(top['emission_mol_m2_s']) * grid['spacing_east_m'] * grid['spacing_north_m'] * 365.25 * 86400 * MT
    assert close(emitted, sum(s['mt_co2_per_year'] for s in top['sources']), 1e-4), f'{label}: emissions {emitted}'
    hab = top['habitat']
    hypoxia, anoxia = hab['hypoxia_umol_kg'], hab['anoxia_umol_kg']
    initial_excess = None
    for k, f in enumerate(o['frames']):
        c = f['carbon']
        a, oc = c['atmosphere'], c['ocean']
        assert len(a['co2_ppm']) == count and all(v > 100 for v in a['co2_ppm'])
        for key in ('co2_flux_mol_m2_yr', 'surface_pco2_uatm', 'surface_ph', 'surface_omega_aragonite', 'bottom_omega_aragonite',
                    'bottom_oxygen_umol_kg', 'minimum_oxygen_umol_kg', 'minimum_oxygen_depth_m', 'habitable_fraction',
                    'production_g_c_m2_day', 'surface_nitrate_umol_kg', 'column_class'):
            assert len(oc[key]) == count, f'{label}: {key}'
        for key in ('level_oxygen_umol_kg', 'level_ph', 'level_omega_aragonite', 'level_class'):
            assert len(oc[key]) == L and all(len(row) == count for row in oc[key]), f'{label}: {key}'
        dead = set(oc['dead_zone_cells'])
        anoxic = corrosive = 0
        for i in range(count):
            if not mask[i]:
                assert oc['column_class'][i] == -1 and all(oc['level_class'][l][i] == -1 for l in range(L))
                continue
            levels = [l for l in range(L) if oc['level_class'][l][i] >= 0]
            assert levels == list(range(len(levels))) and levels, 'levels run from the surface to the seafloor'
            for l in levels:
                o2, cls = oc['level_oxygen_umol_kg'][l][i], oc['level_class'][l][i]
                if o2 < anoxia * 0.999:
                    assert cls == 3, f'{label}: anoxic level {l} of cell {i}'
                elif o2 < hypoxia * 0.999 and o2 > anoxia * 1.001:
                    assert cls == 2, f'{label}: hypoxic level {l} of cell {i}'
                elif o2 > hypoxia * 1.001:
                    assert cls <= 1, f'{label}: oxygenated level {l} of cell {i}'
                if o2 > hypoxia * 1.001 and oc['level_omega_aragonite'][l][i] < hab['corrosive_omega'] * 0.999:
                    assert cls == 1, f'{label}: corrosive water is stressed (level {l} of cell {i})'
                assert 6.5 < oc['level_ph'][l][i] < 8.8
            assert oc['column_class'][i] == max(oc['level_class'][l][i] for l in levels), 'the column takes its worst level'
            assert 0 <= oc['habitable_fraction'][i] <= 1
            bottom = oc['level_oxygen_umol_kg'][levels[-1]][i]
            assert close(oc['bottom_oxygen_umol_kg'][i], bottom, 5e-3, 0.01)
            assert oc['minimum_oxygen_umol_kg'][i] <= min(oc['level_oxygen_umol_kg'][l][i] for l in levels) * 1.005 + 0.01
            if bottom < hypoxia * 0.999:
                assert i in dead, f'{label}: hypoxic bottom water makes a dead zone'
            if i in dead:
                assert oc['column_class'][i] >= 2
            anoxic += oc['column_class'][i] == 3
            corrosive += oc['surface_omega_aragonite'][i] < hab['corrosive_omega'] or oc['bottom_omega_aragonite'][i] < hab['corrosive_omega']
        t = oc['totals']
        assert close(t['sea_area_km2'], len(sea) * area_km2, 1e-4)
        assert close(t['dead_zone_area_km2'], len(dead) * area_km2, 1e-4, 1e-6)
        assert close(t['anoxic_area_km2'], anoxic * area_km2, 1e-4, 1e-6)
        assert close(t['corrosive_area_km2'], corrosive * area_km2, 1e-4, 1e-6)
        assert 0 <= t['habitable_volume_pct'] <= 100 and close(t['dead_zone_pct'], t['dead_zone_area_km2'] / t['sea_area_km2'] * 100, 1e-3, 1e-6)
        assert close(t['mean_surface_ph'], sum(oc['surface_ph'][i] for i in sea) / len(sea), 1e-4)
        flux = sum(oc['co2_flux_mol_m2_yr'][i] for i in sea) * area_km2 * 1e6 * MT
        assert close(t['uptake_mt_co2_yr'], flux, 2e-3, 0.05), f'{label}: uptake {t["uptake_mt_co2_yr"]} vs map {flux}'
        # Ocean budgets (mol since the start): carbon, nitrogen.
        b = oc['budget']
        inventory = b['carbon_inventory_mol']
        carbon = b['air_sea_carbon_mol'] + b['river_carbon_mol'] - b['export_carbon_mol'] + b['ventilation_carbon_mol'] + b['boundary_carbon_mol'] + b['transport_carbon_mol']
        assert close(b['carbon_change_mol'], carbon, 1e-6, 1e-9 * inventory), f'{label}: carbon budget at frame {k}'
        assert abs(b['transport_carbon_mol']) <= 1e-8 * inventory, f'{label}: transport residual {b["transport_carbon_mol"]}'
        nitrogen = b['river_nitrogen_mol'] - b['export_nitrogen_mol'] + b['ventilation_nitrogen_mol'] + b['boundary_nitrogen_mol'] - b['denitrified_nitrogen_mol'] + b['transport_nitrogen_mol']
        assert close(b['nitrogen_change_mol'], nitrogen, 1e-6, 1e-9 * inventory), f'{label}: nitrogen budget at frame {k}'
        # Atmosphere budget (Mt CO2 since the start).
        if initial_excess is None:
            initial_excess = a['initial_excess_mt_co2']
        air = a['emitted_mt_co2'] - a['land_uptake_mt_co2'] - a['ocean_uptake_mt_co2'] - a['ventilation_mt_co2'] + a['transport_mt_co2'] + a['background_shift_mt_co2']
        scale = sum(abs(a[key]) for key in ('emitted_mt_co2', 'land_uptake_mt_co2', 'ocean_uptake_mt_co2', 'ventilation_mt_co2', 'transport_mt_co2', 'background_shift_mt_co2'))
        assert close(a['excess_mt_co2'] - initial_excess, air, 1e-3, 1e-4 * scale + 1e-3), f'{label}: air budget at frame {k}: {a["excess_mt_co2"] - initial_excess} vs {air}'
        # The air and the sea see the same exchange (the air one chemistry step behind).
        assert close(a['ocean_uptake_mt_co2'], b['air_sea_carbon_mol'] * MT, 0.05, 0.05), f'{label}: air-sea exchange {a["ocean_uptake_mt_co2"]} vs {b["air_sea_carbon_mol"] * MT}'
        if k == 0:
            assert all(b[key] == 0 for key in b if key.endswith('_mol') and 'inventory' not in key), 'the first frame starts the budgets'
    return o


def main():
    base = short()
    timeline = run(base, 'summer')
    o = check_contract(timeline, 'summer')
    frames = o['frames']
    grid = timeline['frames'][0]['grid']
    nx, count = grid['nx'], grid['nx'] * grid['ny']
    top = o['carbon']
    last = frames[-1]['carbon']
    # CO2 plumes: the air over the refinery belt is the highest in the region, above the background.
    refinery = next(s for s in top['sources'] if s['name'] == 'Bayou Refineries')
    ppm = last['atmosphere']['co2_ppm']
    assert ppm[refinery['peak_cell']] > last['atmosphere']['background_ppm'] + 1.0, 'the refineries raise the local CO2'
    assert ppm[refinery['peak_cell']] >= sorted(ppm)[-count // 20], 'the plume is among the highest 5 % of the region'
    # The river delta fertilises the sea: production and nitrate peak near the mouth.
    mask = o['ocean_mask']
    oc = last['ocean']
    sea = [i for i in range(count) if mask[i]]
    mouth = next(r for r in o['forcing_source']['rivers'] if r['name'] == 'Great Delta River')['mouth_cell']
    assert top['river_chemistry'][0]['nitrate_umol_kg'] == base['forcing']['rivers'][0]['chemistry']['nitrate_umol_kg']
    assert oc['surface_nitrate_umol_kg'][mouth] > 10 * sorted(oc['surface_nitrate_umol_kg'][i] for i in sea)[len(sea) // 2], 'river nitrate at the mouth'
    assert last['ocean']['budget']['river_nitrogen_mol'] > 0
    # No river nitrate: less production and less oxygen loss on the shelf.
    clean = copy.deepcopy(base)
    for r in clean['forcing']['rivers']:
        r['chemistry']['nitrate_umol_kg'] = 0.0
    clean_o = run(clean, 'clean rivers')['ocean']
    clean_last = clean_o['frames'][-1]['carbon']['ocean']
    assert clean_last['budget']['river_nitrogen_mol'] == 0
    assert clean_last['totals']['production_mt_c_yr'] < oc['totals']['production_mt_c_yr'], 'river nitrate raises production'
    shelf = [i for i in sea if top['seafloor_depth_m'][i] < 60]
    assert sum(clean_last['bottom_oxygen_umol_kg'][i] for i in shelf) > sum(oc['bottom_oxygen_umol_kg'][i] for i in shelf), 'river nitrate drains the shelf bottom water'
    # A high-CO2 atmosphere over the same ocean: more uptake, lower pH and saturation state.
    high = copy.deepcopy(base)
    high['biogeochemistry']['surface_dic_from_atmosphere'] = False
    low = copy.deepcopy(high)
    high['atmosphere_co2']['background_ppm'] = [[2020, 900.0], [2030, 900.0]]
    low['atmosphere_co2']['background_ppm'] = [[2020, 420.0], [2030, 420.0]]
    high_o, low_o = run(high, 'high CO2')['ocean'], run(low, 'low CO2')['ocean']
    for timeline_o, label in [(high_o, 'high CO2'), (low_o, 'low CO2')]:
        check_contract({'ocean': timeline_o, 'frames': timeline['frames']}, label)
    h, l = high_o['frames'][-1]['carbon'], low_o['frames'][-1]['carbon']
    assert h['ocean']['budget']['air_sea_carbon_mol'] > l['ocean']['budget']['air_sea_carbon_mol'] + 1e10, 'a higher atmosphere pushes more CO2 into the sea'
    assert h['ocean']['budget']['air_sea_carbon_mol'] > 0
    assert h['ocean']['totals']['mean_surface_ph'] < l['ocean']['totals']['mean_surface_ph'] - 0.01, 'the uptake acidifies the surface'
    assert h['ocean']['totals']['mean_surface_omega_aragonite'] < l['ocean']['totals']['mean_surface_omega_aragonite']
    assert h['atmosphere']['ocean_uptake_mt_co2'] > l['atmosphere']['ocean_uptake_mt_co2']

    # Request validation.
    bad = copy.deepcopy(base); del bad['atmosphere_co2']
    rejected(bad, 'go together', 'biogeochemistry without atmosphere_co2')
    bad = copy.deepcopy(base); del bad['biogeochemistry']
    rejected(bad, 'go together', 'atmosphere_co2 without biogeochemistry')
    bad = copy.deepcopy(base); del bad['physics']['thermal_layers']
    rejected(bad, 'thermal_layers', 'biogeochemistry without the layered column')
    bad = copy.deepcopy(base); bad['biogeochemistry']['levels_m'] = [10, 5, 20]
    rejected(bad, 'level bottoms must increase', 'levels that do not deepen')
    bad = copy.deepcopy(base); bad['atmosphere_co2']['sources'][0]['east_m'] = 5.0e7
    rejected(bad, 'outside the grid', 'a source outside the grid')
    bad = copy.deepcopy(base); bad['atmosphere_co2']['sources'][0]['mt_co2_per_year'] = -1.0
    rejected(bad, 'CO2 sources', 'a negative emission')
    bad = copy.deepcopy(base); bad['atmosphere_co2']['background_ppm'] = [[2030, 420.0], [2020, 430.0]]
    rejected(bad, 'increasing years', 'background years out of order')
    bad = copy.deepcopy(base); bad['forcing']['rivers'][0]['chemistry']['nitrate_umol_kg'] = -5.0
    rejected(bad, 'river chemistry', 'negative river nitrate')
    bad = copy.deepcopy(base); bad['biogeochemistry']['habitat']['anoxia_umol_kg'] = 80.0
    rejected(bad, 'anoxia_umol_kg', 'anoxia above hypoxia')
    print('ocean carbon CLI checks passed')


if __name__ == '__main__':
    main()
