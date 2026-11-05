"""Check terraflux-run-solar-cycle: the output contract, the identities between its series (smoothing, proxies,
UV index, flare rates, the energy balance, the butterfly diagram, the disk), the forecast's ensemble, that a
monthly record reproduces the synthesised one, and that invalid requests produce no output."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/solar-cycle-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/solar-cycle-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/solar-cycle-v1.schema.json').read_text())
except ImportError:
    jsonschema = None


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def close(a, b, tol, label):
    assert abs(a - b) <= tol, f'{label}: {a} vs {b}'


schema_ok(RUN_SCHEMA, example, 'example request')

# A faster variant: fewer members, fewer and smaller disk frames.
fast = copy.deepcopy(example)
fast['forecast']['members'] = 60
fast['disk'].update(first_year=2012.5, last_year=2032.5, every_months=24, cells=32)
schema_ok(RUN_SCHEMA, fast, 'fast request')

with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    path = directory / 'request.json'

    def run(doc, name):
        path.write_text(json.dumps(doc))
        output = directory / f'{name}.json'
        result = subprocess.run([cli, str(path), str(output)], capture_output=True, text=True, cwd=root)
        return result, output

    result, output = run(fast, 'fast')
    assert result.returncode == 0, result.stderr
    timeline = json.loads(output.read_text())
    s = timeline['solar']
    schema_ok(OUT_SCHEMA, s, 'output')
    assert s['source_kind'] == 'engine_output' and s['record_kind'] == 'cycle_table' and 'not observations' in s['data_origin']

    # ---- Record and smoothing --------------------------------------------------------------------------------
    rec = s['record']
    n = rec['months']
    issue_month = math.floor(fast['forecast']['issue_year'] * 12 + 1e-9)
    first = int(rec['first_month'][:4]) * 12 + int(rec['first_month'][5:]) - 1
    assert first + n == issue_month, 'the record ends before the issue month'
    for key in ['sunspot_number', 'smoothed', 'model', 'f107_sfu', 'tsi_w_m2', 'facular_index', 'ozone_change_pct', 'stratosphere_k', 'uv_index_change_pct', 'forcing_w_m2', 'surface_k', 'deep_k']:
        assert len(rec[key]) == n, key
    sn, sm = rec['sunspot_number'], rec['smoothed']
    assert all(v >= 0 for v in sn) and all(v is None for v in sm[:6] + sm[-6:])
    for k in range(6, n - 6, 37):
        expected = (0.5 * sn[k - 6] + sum(sn[k - 5:k + 6]) + 0.5 * sn[k + 6]) / 12
        close(sm[k], expected, 0.051, 'SILSO 13-month smoothing')

    # ---- Cycles: the synthetic record reproduces the table ----------------------------------------------------
    cycles = s['cycles']
    assert cycles[0]['number'] == fast['record']['first_cycle_number']
    assert all(b['number'] == a['number'] + 1 and b['minimum_year'] > a['minimum_year'] for a, b in zip(cycles, cycles[1:]))
    complete = [c for c in cycles if c['complete']]
    assert len(complete) == len(cycles) - 1 and not cycles[-1]['complete']
    for c in complete:
        close(c['length_years'], c['end_year'] - c['minimum_year'], 0.006, 'length = next minimum - minimum (2 vs 3 decimals)')
        assert c['maximum_year'] > c['minimum_year'] and c['maximum'] > c['minimum']
    errors = sorted(abs(c['minimum_year'] - c['table']['minimum_year']) for c in cycles)
    assert errors[len(errors) // 2] < 0.7, f'detected minima follow the table (median error {errors[len(errors) // 2]})'
    ratios = [c['maximum'] / c['table']['maximum'] for c in cycles if c['maximum'] is not None]
    assert all(0.7 < r < 1.35 for r in ratios), f'detected maxima follow the table (noise) {ratios}'
    assert all('fit' in c for c in cycles) and cycles[-1]['fit']['in_progress']
    st = s['statistics']
    assert st['complete_cycles'] == len(complete) and 9.5 < st['mean_period_years'] < 12.5 and 0 <= st['amplitude_autocorrelation'] <= 0.8

    # ---- Proxies, UV and atmosphere -----------------------------------------------------------------------------
    p = s['irradiance']['proxies']
    bands = {b['key']: b for b in s['irradiance']['bands']}
    ref_sn = p['reference_sunspot_number']
    f_ref = p['f107_base_sfu'] + p['f107_per_sunspot'] * ref_sn + p['f107_per_sunspot_squared'] * ref_sn ** 2
    atm = s['atmosphere']
    for k in range(0, n, 53):
        f = p['f107_base_sfu'] + p['f107_per_sunspot'] * sn[k] + p['f107_per_sunspot_squared'] * sn[k] ** 2
        close(rec['f107_sfu'][k], f, 0.051, 'F10.7 from the sunspot number')
        x = (f - p['f107_base_sfu']) / (f_ref - p['f107_base_sfu'])
        close(rec['facular_index'][k], x, 0.0006, 'facular index')
        close(rec['tsi_w_m2'][k], p['tsi_quiet_w_m2'] + p['facular_brightening_w_m2'] * x - p['spot_darkening_w_m2_per_sunspot'] * sn[k], 0.0006, 'TSI = quiet + faculae - spots')
        for key, b in bands.items():
            close(rec['uv_change_pct'][key][k], 100 * b['variability'] * x, 1e-3 * max(1, abs(100 * b['variability'] * x)) + 5e-4, f'UV band {key}')
        close(rec['ozone_change_pct'][k], 100 * atm['ozone_change_at_reference'] * x, 6e-4, 'ozone')
        uvi = 100 * ((1 + atm['erythemal_variability'] * x) * (1 + atm['ozone_change_at_reference'] * x) ** -atm['radiation_amplification_factor'] - 1)
        close(rec['uv_index_change_pct'][k], uvi, 6e-4, 'UV-index change')
    keyed = sorted(bands.values(), key=lambda b: b['from_nm'])
    assert bands['lyman_alpha']['variability'] > bands['o2_herzberg']['variability'] > bands['uv_b']['variability'] > bands['uv_a']['variability'], 'shorter wavelengths vary more'
    ref = s['irradiance']['reference']
    assert 0.3 < ref['uv_share_of_tsi_change'] < 0.9 and ref['tsi_change_w_m2'] > 0.5
    for site in atm['sites']:
        quiet, maxi = site['quiet_noon_uv_index_by_month'], site['reference_noon_uv_index_by_month']
        assert len(quiet) == 12 and all(q >= 0 for q in quiet)
        for a, b in zip(quiet, maxi):
            if a > 1:
                close(b / a - 1, atm['uv_index_change_at_reference_pct'] / 100, 0.006, 'reference UV index = quiet x (1 + change)')
        if site['latitude_deg'] > 30:
            assert quiet[5] > 3 * quiet[11], 'northern summer noon UV far above winter'
        if site['latitude_deg'] < -30:
            assert quiet[11] > 3 * quiet[5], 'southern summer in December'

    # ---- Flares -----------------------------------------------------------------------------------------------------
    fl = s['flares']
    cfg = fl['config']

    def rate(sunspots, threshold):
        if sunspots <= 0:
            return 0.0
        e = 1 - cfg['size_index']
        q, qmax = (threshold / 1e-4) ** e, (cfg['max_peak_flux_w_m2'] / 1e-4) ** e
        return cfg['x_rate_per_year'] * (sunspots / cfg['reference_sunspot_number']) ** cfg['rate_exponent'] * (q - qmax) / (1 - qmax)

    for k in range(0, n, 61):
        close(rec['flares']['x1_expected'][k], rate(sn[k], 1e-4) / 12, 5e-3 * rate(sn[k], 1e-4) / 12 + 1e-9, 'expected X flares per month')
        close(rec['flares']['m1_expected'][k], rate(sn[k], 1e-5) / 12, 5e-3 * rate(sn[k], 1e-5) / 12 + 1e-9, 'expected M flares per month')
    counts, expected = sum(rec['flares']['x1_count']), sum(rec['flares']['x1_expected'])
    assert abs(counts - expected) < 5 * math.sqrt(expected), f'synthetic X flares follow the rate ({counts} vs {expected:.0f})'
    assert all(c <= m for c, m in zip(rec['flares']['x1_count'], rec['flares']['m1_count']))
    assert all((big is None) == (m == 0) for big, m in zip(rec['flares']['largest_w_m2'], rec['flares']['m1_count']))
    assert fl['record_largest'][0]['peak_flux_w_m2'] == max(v for v in rec['flares']['largest_w_m2'] if v is not None)
    assert fl['energy']['ratio_to_tsi_change'] < 0.01, 'flares carry a tiny share of the cycle energy'
    assert sum(y['x1_count'] for y in fl['record_annual']) <= counts

    # ---- Climate: recompute the implicit two-layer energy balance from the forcing ------------------------------
    c = s['climate']['config']
    T = D = 0.0
    dt = 1 / 12
    for k in range(n):
        F = rec['forcing_w_m2'][k]
        a11, a12 = c['upper_heat_capacity'] / dt + c['feedback_w_m2_k'] + c['exchange_w_m2_k'], -c['exchange_w_m2_k']
        a21, a22 = -c['exchange_w_m2_k'], c['deep_heat_capacity'] / dt + c['exchange_w_m2_k']
        b1, b2 = c['upper_heat_capacity'] / dt * T + F, c['deep_heat_capacity'] / dt * D
        det = a11 * a22 - a12 * a21
        T, D = (b1 * a22 - a12 * b2) / det, (a11 * b2 - a21 * b1) / det
        if k % 97 == 0:
            close(rec['surface_k'][k], T, 2e-4 + 2e-3 * abs(T), 'surface temperature from the energy balance')
    ref_tsi = s['irradiance']['reference_tsi_w_m2']
    for k in range(0, n, 89):
        close(rec['forcing_w_m2'][k], (rec['tsi_w_m2'][k] - ref_tsi) * (1 - c['albedo']) / 4, 2e-4 + 2e-3 * abs(rec['forcing_w_m2'][k]), 'forcing = dS (1 - albedo) / 4')
    assert all(0.005 < x['peak_to_peak_k'] < 0.2 for x in s['climate']['cycles']), 'a response of a few hundredths of a kelvin'

    # ---- Backtest ----------------------------------------------------------------------------------------------------
    bt = s['backtest']
    assert bt['phases_years'] == fast['forecast']['backtest_phases_years']
    for row in bt['summary']:
        entries = [e for e in bt['entries'] if e['phase_years'] == row['phase_years']]
        assert row['forecasts'] == len(entries)
        if entries:
            close(row['prior_mae'], sum(abs(e['prior_maximum'] - e['observed_maximum']) for e in entries) / len(entries), 0.06 + 0.002 * row['prior_mae'], 'prior MAE')
    late = [r for r in bt['summary'] if r['phase_years'] >= 5 and r['fitted']]
    early = [r for r in bt['summary'] if r['phase_years'] == 2 and r['fitted']]
    assert late and early and late[0]['fit_mae'] < early[0]['fit_mae'], 'the fit improves as the cycle progresses'

    # ---- Forecast ---------------------------------------------------------------------------------------------------
    fc = s['forecast']
    m = fc['months']
    assert fc['first_month'] == fc['issue_month'] and int(fc['first_month'][:4]) * 12 + int(fc['first_month'][5:]) - 1 == issue_month
    q = fc['sunspot']
    for k in range(m):
        assert q['p5'][k] <= q['p25'][k] <= q['p50'][k] <= q['p75'][k] <= q['p95'][k], f'ordered percentiles at {k}'
    for key in ['f107_sfu', 'tsi_w_m2', 'ozone_change_pct', 'stratosphere_k', 'uv_index_change_pct', 'surface_k']:
        t = fc[key]
        assert all(len(t[x]) == m for x in ['p5', 'p50', 'p95'])
        assert all(a <= b + 1e-9 <= c + 2e-9 for a, b, c in zip(t['p5'], t['p50'], t['p95'])), key
    for x, e in zip(fc['flares']['x1_probability']['p50'], fc['flares']['x1_expected']['p50']):
        close(x, 1 - math.exp(-e), 5e-3 * e * math.exp(-e) + 6e-4, 'P(at least one X flare) = 1 - exp(-expected), both rounded to 3 digits')
    tps = fc['turning_points']
    assert tps and all(t['year_p5'] <= t['year_median'] <= t['year_p95'] for t in tps if t['year_median'] is not None)
    kinds = [t['kind'] for t in tps]
    assert all(a != b for a, b in zip(kinds, kinds[1:])), 'minima and maxima alternate'
    first_min = next(t for t in tps if t['kind'] == 'minimum')
    assert first_min['cycle'] == fc['current_cycle']['cycle'] + 1 and fast['forecast']['issue_year'] < first_min['year_median'] < fast['forecast']['issue_year'] + 8
    cc = fc['current_cycle']
    assert cc['amplitude_sigma'] <= cc['prior_sigma'] and cc['maximum_observed']
    assert all(y['x10_probability_p5'] <= y['x10_probability_p50'] <= y['x10_probability_p95'] for y in fc['annual'])

    # ---- Butterfly: the density carries the model's spot area in two bands ------------------------------------------
    bf = s['butterfly']
    lat = bf['latitudes_deg']
    step = lat[1] - lat[0]
    assert all(len(row) == len(lat) for row in bf['density_uhem_per_deg']) and all(v >= 0 for row in bf['density_uhem_per_deg'] for v in row)
    area = fast['disk'].get('area_per_sunspot_uhem', 10.0)
    for year, row in list(zip(bf['years'], bf['density_uhem_per_deg']))[::41]:
        k = math.floor(year * 12) - first
        if 0 <= k < n and rec['model'][k] > 30:
            close(sum(row) * step / area, rec['model'][k], 0.05 * rec['model'][k] + 1, 'butterfly area = spot area per sunspot x model')

    # ---- Disk frames ---------------------------------------------------------------------------------------------
    frames, disk = timeline['frames'], s['disk']
    assert len(frames) == len(disk['frames']) >= 2
    cells = fast['disk']['cells']
    for frame, d in zip(frames, disk['frames']):
        assert frame['grid']['nx'] == cells and frame['elapsed_seconds'] == d['elapsed_seconds']
        assert set(frame['fields']['air_temperature']['values']) == {288}, 'placeholder forecast fields'
        assert len(d['brightness_k']) == cells * cells
        assert all((b is None) == (qb is None) for b, qb in zip(d['brightness_k'], disk['quiet_brightness_k'])), 'the same cells are on the disk'
        close(d['net_ppm'], d['spot_ppm'] + d['facular_ppm'], 1.01, 'net = spots + faculae')
        assert d['spot_ppm'] <= 0 <= d['facular_ppm']
        assert all(abs(r['latitude_deg']) <= 70 and abs(r['longitude_deg']) <= 90 for r in d['regions'])
        if d['source'] == 'forecast':
            assert not d['flares'], 'no synthetic flares in the forecast'
        assert all(f['peak_flux_w_m2'] >= 1e-5 and f['r_scale'] >= 1 and -1 <= f['region'] < len(d['regions']) for f in d['flares'])
    assert [f['elapsed_seconds'] for f in frames] == sorted(f['elapsed_seconds'] for f in frames)
    assert {d['source'] for d in disk['frames']} == {'record', 'forecast'}

    # ---- Forcing export -------------------------------------------------------------------------------------------
    ex = s['forcing_export']
    assert all(b == a + 1 for a, b in zip(ex['years'], ex['years'][1:])) and ex['source'][0] == 'record' and ex['source'][-1] == 'forecast'
    year0 = ex['years'][0]
    k0 = year0 * 12 - first
    close(ex['tsi_w_m2'][0], sum(rec['tsi_w_m2'][k0:k0 + 12]) / 12, 0.002, 'annual TSI of the record')

    # ---- The same record given month by month gives the same forecast ---------------------------------------------
    monthly = copy.deepcopy(fast)
    monthly['record'] = {'kind': 'monthly', 'data_origin': 'the synthetic record of the check', 'first_cycle_number': fast['record']['first_cycle_number'],
                         'first_year': first // 12, 'first_month': first % 12 + 1, 'values': sn}
    schema_ok(RUN_SCHEMA, monthly, 'monthly request')
    result, output = run(monthly, 'monthly')
    assert result.returncode == 0, result.stderr
    again = json.loads(output.read_text())['solar']
    assert again['record_kind'] == 'monthly' and again['record']['sunspot_number'] == sn
    assert again['forecast']['sunspot'] == fc['sunspot'] and again['forecast']['turning_points'] == tps, 'a monthly record reproduces the forecast'

    # ---- Invalid requests: an error and no output --------------------------------------------------------------------
    def broken(change, label):
        doc = copy.deepcopy(fast)
        change(doc)
        result, output = run(doc, 'broken')
        assert result.returncode != 0 and not output.exists(), f'{label} must be rejected'
        assert 'error:' in result.stderr, label

    broken(lambda d: d.update(schema_version='terraflux.solar.cycle-run/v0'), 'wrong schema version')
    broken(lambda d: d['irradiance'].update(facular_brightnes_w_m2=2.0), 'a misspelt parameter')
    broken(lambda d: d['forecast'].update(issue_year=1880.0), 'an issue date without enough history')
    broken(lambda d: d['forecast'].update(horizon_year=d['forecast']['issue_year'] + 1), 'a horizon too close')
    broken(lambda d: d['record']['cycles'][3].update(number=99), 'non-consecutive cycle numbers')
    broken(lambda d: d['record'].update(first_cycle_number=40), 'a first cycle outside the table')
    broken(lambda d: d['disk'].update(last_year=2099.0), 'disk frames beyond the forecast')
    broken(lambda d: d['uv_sites'].append(dict(d['uv_sites'][0])), 'duplicate UV sites')
    broken(lambda d: d['flares'].update(size_index=1.0), 'a flare size index without a finite rate')
    broken(lambda d: d.update(record={**monthly['record'], 'values': sn[:-1] + [-3.0]}), 'a negative monthly value')

print('solar cycle CLI check passed')
