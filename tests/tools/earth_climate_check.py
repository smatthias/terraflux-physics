"""Check terraflux-run-earth-climate: the output contract, identities recomputed from the output (the CO2 forcing from
the concentration, the drivers adding up to the warming because the response is linear, ECS from the feedbacks, the
carbon budget of the CO2 map, the zero-mean concentration anomaly), the ordering of the scenarios and the impacts, the
map frames, the couplings with and without the linked runs, and that invalid requests produce no output."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/earth-climate-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/earth-climate-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/earth-climate-v1.schema.json').read_text())
except ImportError:
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def close(a, b, rel, label, absolute=0.0):
    assert abs(a - b) <= rel * max(abs(a), abs(b)) + absolute, f'{label}: {a} vs {b}'


schema_ok(RUN_SCHEMA, example, 'example request')

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)

    def run(doc, name):
        path = directory / f'{name}-request.json'
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    result, output = run(example, 'example')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    c = timeline['earth_climate']
    schema_ok(OUT_SCHEMA, c, 'output')
    years = c['years']
    cfg = example['years']
    assert years == list(range(cfg['first'], cfg['last'] + 1)) and c['today'] == cfg['today']
    today = years.index(c['today'])
    reference = [k for k, y in enumerate(years) if cfg['reference_first'] <= y <= cfg['reference_last']]
    keys = [s['key'] for s in example['scenarios']]
    assert sorted(c['series']) == sorted(keys) and sorted(c['drivers']) == sorted(keys)

    # ---- Forcing and response.
    fb = example['climate']['feedbacks']
    net = fb['planck'] - fb['water_vapour_lapse_rate'] - fb['surface_albedo'] - fb['clouds'] - fb['other']
    close(c['climate']['feedback_w_m2_k'], net, 1e-3, 'net feedback')
    close(c['climate']['ecs_k'], example['climate']['doubling_forcing_w_m2'] / net, 5e-3, 'ECS = F2x / lambda')
    assert 1.4 < c['climate']['tcr_k'] < c['climate']['ecs_k'], 'the transient response lags the equilibrium'
    shares = c['climate']['feedback_shares_today']
    close(sum(shares.values()), c['climate']['warming_today_k'], 0.02, 'the feedback shares add up to the warming', 0.01)
    for key in keys:
        s = c['series'][key]
        assert all(len(v) == len(years) for k, v in s.items() if k != 'agents')
        assert all(len(a['forcing_w_m2']) == len(years) for a in s['agents'].values())
        # The CO2 forcing against the reference-period concentration.
        c0 = sum(s['co2_ppm'][k] for k in reference) / len(reference)
        for k in (today, len(years) - 1):
            close(s['agents']['co2']['forcing_w_m2'][k], 5.35 * math.log(s['co2_ppm'][k] / c0), 0.02, f'{key}: CO2 forcing 5.35 ln(C/C0)', 0.01)
            close(s['total_forcing_w_m2'][k], sum(a['forcing_w_m2'][k] for a in s['agents'].values()), 0.01, f'{key}: total forcing is the sum of the agents', 0.02)
        # Low and high climate sensitivity bracket the central path once the greenhouse warming dominates (after eruptions
        # the low sensitivity cools less, and in between the different response times may cross).
        modern = years.index(1960)
        assert all(lo <= mid <= hi for lo, mid, hi in zip(s['global_low_k'][modern:], s['global_k'][modern:], s['global_high_k'][modern:])), 'the sensitivity range brackets the central path'
        assert s['global_low_k'][today] < s['global_k'][today] < s['global_high_k'][today]
        assert s['land_k'][today] > s['global_k'][today] and s['arctic_k'][today] > s['land_k'][today], 'land and Arctic amplification'
        # Linear response: the warming by driver adds up to the warming (the space drivers are bounds around zero).
        for period in ('today', 'y2050', 'y2100'):
            rows = c['drivers'][key][period]
            index = today if period == 'today' else years.index(2050 if period == 'y2050' else 2100)
            close(sum(r['warming_k'] for r in rows), s['global_k'][index], 0.03, f'{key} {period}: the drivers add up to the warming', 0.02)
            assert [abs(r['warming_k']) for r in rows] == sorted((abs(r['warming_k']) for r in rows), reverse=True), 'drivers ranked by their effect'
        assert c['drivers'][key]['today'][0]['key'] == 'co2', 'CO2 drives most of the warming today'
        assert s['ph'][today] < s['ph'][reference[0]], 'the ocean acidifies'
    central = c['series'][c['central_scenario']]
    assert 1.1 < central['global_k'][today] < 1.6, 'present-day warming close to the observed 1.2-1.5 K'
    end = {k: c['series'][k]['global_k'][-1] for k in keys}
    assert end['ssp126'] < end['ssp245'] < end['ssp585'], 'more emissions, more warming'
    aerosols = next(r for r in c['drivers'][c['central_scenario']]['today'] if r['key'] == 'aerosols')
    assert aerosols['warming_k'] < 0 and aerosols['warming_low_k'] < aerosols['warming_k'] < aerosols['warming_high_k'], 'aerosols cool, with a wide range'

    # ---- Impacts: monotonic in the warming of the scenario.
    impacts = {i['key']: i for i in c['impacts']}
    for key in ('heat_50yr_factor', 'heat_10yr_factor', 'heavy_rain_factor', 'ocean_heat_zj'):
        i = impacts[key]
        assert i['today'] > i['preindustrial'] and i['scenarios']['ssp585']['y2100'] > i['scenarios']['ssp126']['y2100'], key
    assert impacts['heat_50yr_factor']['today'] > impacts['heat_10yr_factor']['today'] > 1, 'rarer heat extremes grow faster'
    assert impacts['september_ice_mkm2']['scenarios']['ssp585']['y2100'] < impacts['september_ice_mkm2']['today']
    assert impacts['ph']['scenarios']['ssp585']['y2100'] < impacts['ph']['today'] < impacts['ph']['preindustrial']

    # ---- Map: the pattern averages to one over the area; frames per decade.
    m = c['map']
    nx, ny = example['map']['cells_east'], example['map']['cells_north']
    assert len(m['pattern']) == len(m['land_mask']) == nx * ny
    dlat = 180.0 / ny
    weights = [math.sin(math.radians(-90 + (j + 1) * dlat)) - math.sin(math.radians(-90 + j * dlat)) for j in range(ny) for _ in range(nx)]
    close(sum(p * w for p, w in zip(m['pattern'], weights)) / sum(weights), 1.0, 0.01, 'area-mean pattern')
    assert [f['year'] for f in m['frames']] == list(range(cfg['first'], cfg['last'] + 1, cfg['frame_every']))
    assert len(timeline['frames']) == len(m['frames'])
    last = m['frames'][-1]
    for key in keys:
        area_mean = sum(x * w for x, w in zip(last['local_warming_k'][key], weights)) / sum(weights)
        close(area_mean, c['series'][key]['global_k'][years.index(last['year'])], 0.05, f'{key}: the map averages to the global warming', 0.05)
    # Regional terms: aerosols cool most where the air is polluted; a weaker overturning cools the North Atlantic.
    aerosol_frame = next(fr for fr in m['frames'] if fr['year'] == 1980)
    assert min(aerosol_frame['aerosol_k']) < 1.3 * (sum(x * w for x, w in zip(aerosol_frame['aerosol_k'], weights)) / sum(weights)) < 0, 'aerosol cooling concentrated in the polluted regions'
    assert all(x <= 0 for x in last['overturning_k']) and min(last['overturning_k']) < 0, 'the overturning term only cools'

    # ---- CO2 map: emissions, sinks and the atmosphere close the budget.
    co2 = c['co2']
    b = co2['budget']
    area = [w * 2 * math.pi * 6371.0 ** 2 / nx for w in weights]   # km2 per cell
    emitted = sum(e * a for e, a in zip(co2['emissions_t_km2_yr'], area)) / 1e9
    sunk = sum(e * a for e, a in zip(co2['sinks_t_km2_yr'], area)) / 1e9
    close(emitted, b['fossil_gt_co2_yr'] + b['land_use_gt_co2_yr'], 0.02, 'mapped emissions')
    close(sunk, b['land_sink_gt_co2_yr'] + b['ocean_sink_gt_co2_yr'], 0.02, 'mapped sinks')
    close(emitted + sunk, b['atmosphere_gt_co2_yr'], 0.03, 'what stays in the air')
    close(b['airborne_fraction'], b['atmosphere_gt_co2_yr'] / (b['fossil_gt_co2_yr'] + b['land_use_gt_co2_yr']), 0.01, 'airborne fraction')
    close(b['growth_ppm_yr'], b['atmosphere_gt_co2_yr'] / 7.82, 0.02, 'growth: 7.82 Gt CO2 per ppm')
    anomaly_mean = sum(x * w for x, w in zip(co2['surface_ppm_anomaly'], weights)) / sum(weights)
    assert abs(anomaly_mean) < 0.01 and b['north_minus_south_ppm'] > 1, 'a zero-mean anomaly, higher in the north (the emissions)'
    ranked = co2['regions']
    assert ranked[0]['share_of_annual'] >= ranked[-1]['share_of_annual'] and sum(r['share_of_annual'] for r in ranked) <= 1.0 + 1e-6
    assert all(r['latitude_deg'] is not None for r in ranked), 'every region has a centre on the map'
    assert co2['hotspots'] == sorted(co2['hotspots'], key=lambda h: -h['score'])

    # ---- Couplings: the links add the other models' numbers.
    couplings = c['couplings']
    assert any(x['from'] == 'carbon' and x['strength'] == 'dominant' for x in couplings)
    assert {'solar_cycle', 'cosmic_dust', 'solar_wind', 'polar_melt', 'forecast'} <= set(c['links'])
    unlinked = copy.deepcopy(example)
    unlinked.pop('links')
    result, output2 = run(unlinked, 'unlinked')
    assert result.returncode == 0, result.stderr
    u = json.loads(output2.read_text())['earth_climate']
    schema_ok(OUT_SCHEMA, u, 'unlinked output')
    assert not any(x['to'] == 'weather' for x in u['couplings']), 'no forecast couplings without the forecast run'
    assert len(u['couplings']) < len(couplings)

    # ---- Invalid requests produce no output.
    def rejected(mutate, label):
        doc = copy.deepcopy(example)
        mutate(doc)
        result, out = run(doc, 'bad')
        assert result.returncode != 0 and not out.exists(), f'{label} must be rejected'

    rejected(lambda d: d['agents'].update(unicorns={'value_2019_w_m2': 1}), 'an unknown agent')
    rejected(lambda d: d.update(central_scenario='ssp999'), 'an unknown central scenario')
    rejected(lambda d: d['scenarios'].append(copy.deepcopy(d['scenarios'][0])), 'duplicate scenario keys')
    rejected(lambda d: d['map'].update(land_mask=d['map']['land_mask'][:-1]), 'a land mask with a missing row')
    rejected(lambda d: d['climate']['feedbacks'].update(clouds=1.9), 'a runaway feedback')
    rejected(lambda d: d['years'].update(today=1990), 'today before the reference')
    rejected(lambda d: d['links'].update(forecast_path='viewer/public/missing/engine-timeline.json'), 'a missing linked run')

print('earth climate checks passed')
