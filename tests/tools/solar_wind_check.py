"""Check terraflux-run-solar-wind: the output contract, identities recomputed from the output (pressure, polar-cap
flux, the auroral and outflow maps against their global rates, the hydrogen routes against the supply), the storm's
sequence (Forbush decrease, solar protons, Dst), the solar-cycle climatology, the scenarios, the budgets, a run
without the solar cycle, and that invalid requests produce no output."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/solar-wind-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/solar-wind-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/solar-wind-v1.schema.json').read_text())
except ImportError:
    jsonschema = None

MU0, RE, AMU = 4e-7 * math.pi, 6.371e6, 1.66053907e-27


def schema_ok(schema, document, label):
    if jsonschema is None:
        return
    errors = list(jsonschema.Draft202012Validator(schema).iter_errors(document))
    assert not errors, f'{label}: {errors[0].message[:200]} at {list(errors[0].path)[:6]}'


def close(a, b, rel, label, absolute=0.0):
    assert abs(a - b) <= rel * max(abs(a), abs(b)) + absolute, f'{label}: {a} vs {b}'


def spearman(x, y):
    def ranks(v):
        order = sorted(range(len(v)), key=lambda i: v[i])
        r = [0] * len(v)
        for k, i in enumerate(order):
            r[i] = k
        return r
    rx, ry = ranks(x), ranks(y)
    n = len(x)
    mx, my = sum(rx) / n, sum(ry) / n
    cov = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    return cov / math.sqrt(sum((a - mx) ** 2 for a in rx) * sum((b - my) ** 2 for b in ry))


schema_ok(RUN_SCHEMA, example, 'example request')
fast = copy.deepcopy(example)
fast['storm'].update(hours=72, frame_every_hours=6)
fast['cycle'].update(first_year=1996, last_year=2010)
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
    w = timeline['solar_wind']
    schema_ok(OUT_SCHEMA, w, 'output')
    storm, series, frames = w['storm'], w['storm']['series'], w['storm']['frames']
    hours, every = fast['storm']['hours'], fast['storm']['frame_every_hours']
    assert all(len(v) == hours + 1 for v in series.values()), 'one value per hour'
    assert len(frames) == len(timeline['frames']) == hours // every + 1
    nx, ny = fast['map']['cells_east'], fast['map']['cells_north']
    count = nx * ny
    st = w['static']
    assert len(st['magnetic_latitude_deg']) == len(st['cutoff_gv']) == len(st['land_mask']) == count
    lat = [-90 + (i // nx + 0.5) * 180 / ny for i in range(count)]
    area = [RE ** 2 * math.radians(360 / nx) * (math.sin(math.radians(la + 90 / ny)) - math.sin(math.radians(la - 90 / ny))) for la in lat]
    moment = w['dipole']['moment_a_m2']
    r_iono = RE + 110e3

    # Identities of the hourly series.
    for k in range(hours + 1):
        close(series['pressure_npa'][k], 1.67262192e-6 * series['density_cm3'][k] * series['speed_km_s'][k] ** 2, 2e-3, 'dynamic pressure')
        assert 0 <= series['kp'][k] <= 9
        colat = 90 - series['cap_boundary_mlat_deg'][k]
        close(series['open_flux_gwb'][k] * 1e9, MU0 * moment * math.sin(math.radians(colat)) ** 2 / (2 * r_iono), 3e-3, 'polar-cap flux of a dipole')
        close(series['hydrogen_jeans_per_s'][k] + series['hydrogen_charge_exchange_per_s'][k] + series['hydrogen_polar_wind_per_s'][k], series['hydrogen_escape_per_s'][k], 3e-3, 'hydrogen routes')
        assert series['oval_equatorward_midnight_mlat_deg'][k] < series['cap_boundary_mlat_deg'][k]
        assert series['neutron_pole_change_pct'][k] <= 1e-9 and series['neutron_pole_change_pct'][k] <= series['neutron_equator_change_pct'][k] + 1e-9, 'Forbush decrease, deepest at the pole'
    supply = w['budgets']['mean_escape']['diffusion_limit_per_s']
    close(sum(series['hydrogen_escape_per_s']) / (hours + 1), supply, 0.03, 'over days the hydrogen escape equals the supply from below')
    assert max(series['hydrogen_escape_per_s']) > 1.1 * supply, 'the storm briefly raises the hydrogen escape'
    arrival = fast['storm']['cme']['arrival_h']
    dst, kp = series['dst_nt'], series['kp']
    k_min = dst.index(min(dst))
    assert k_min > arrival and min(dst) < -150 and dst[-1] > min(dst) / 2, 'main phase after the arrival, then recovery'
    assert max(kp[arrival:]) > 7 and max(kp[:arrival]) < 5, 'the storm raises Kp'
    assert min(series['standoff_re'][arrival:]) < 7 < series['standoff_re'][0], 'the magnetopause is pushed in'
    assert max(series['oxygen_escape_per_s']) > 10 * min(series['oxygen_escape_per_s']), 'heavy-ion escape rises steeply in the storm'
    assert max(series['exospheric_k']) > series['exospheric_k'][0] + 200 and max(series['density_400km_ratio']) > 2, 'the thermosphere heats and expands'
    onset = fast['storm']['sep']['onset_h']
    assert series['sep_flux_pfu'][onset] == 0 and max(series['sep_flux_pfu']) == fast['storm']['sep']['peak_pfu']
    assert min(series['ozone_upper_north_pct']) < -1 and series['ozone_upper_north_pct'][onset] == 0, 'solar protons destroy polar ozone'

    # The maps: the auroral power and the O+ upflow per hemisphere against the global rates.
    mlat = st['magnetic_latitude_deg']
    for f, frame in zip(frames, timeline['frames']):
        assert frame['elapsed_seconds'] == f['elapsed_seconds'] and frame['grid']['nx'] == nx
        k = int(f['hour'])
        for hemi in (1, -1):
            cells = [i for i in range(count) if (mlat[i] >= 0) == (hemi > 0)]
            power = sum(f['aurora_mw_m2'][i] * 1e-3 * area[i] for i in cells)
            close(power, series['hemispheric_power_gw'][k] * 1e9, 0.08, f'hemispheric power at hour {k}')
            up = sum(f['oxygen_outflow_1e7_cm2_s'][i] * 1e7 * area[i] * 1e4 for i in cells)
            close(up, 0.5 * series['oxygen_upflow_per_s'][k], 0.08, f'O+ upflow at hour {k}')
        region = f['region']
        cap = [abs(mlat[i]) for i in range(count) if region[i] == 2]
        closed = [abs(mlat[i]) for i in range(count) if region[i] == 0]
        assert cap and min(cap) > 50 and max(closed) < 85 and sum(cap) / len(cap) > sum(closed) / len(closed), 'open field at the poles'
        dose = f['dose_by_cutoff_usv_h']
        assert all(a >= b - 1e-9 for a, b in zip(dose, dose[1:])), 'less dose behind a higher cutoff'
        sep = f['sep_by_cutoff_1e9_cm2_s']
        assert all(a >= b - 1e-12 for a, b in zip(sep, sep[1:])) and sep[-1] <= sep[0] * 1e-3 + 1e-12, 'solar protons only at low cutoffs'
        temps = frame['fields']['air_temperature']['values']
        assert 500 < min(temps) and max(temps) < 3000, 'exospheric temperature as the air temperature'
    cut = st['cutoff_gv']
    assert max(cut) > 12 and min(cut) < 0.2 and all(c >= 0 for c in cut)

    # The latitude profile and the calibration diagnostics.
    prof = w['latitude_profile']
    assert all(a >= b for a, b in zip(prof['cutoff_gv'], prof['cutoff_gv'][1:])), 'the cutoff falls towards the pole'
    assert all(lo >= hi for lo, hi in zip(prof['dose_min_usv_h'], prof['dose_max_usv_h'])), 'more dose at solar minimum'
    assert prof['dose_min_usv_h'][-1] / prof['dose_min_usv_h'][0] > 2, 'the poles receive more than twice the dose of the equator'
    for resp in w['responses']:
        assert resp['pole_over_equator'] > 1.3 and resp['pole_change_phi_04_to_10_pct'] > resp['equator_change_phi_04_to_10_pct'] > 0, resp['name']

    # The solar cycle.
    cy = w['cycle']
    months = cy['months']
    assert months[0] == '1996-01' and months[-1] == '2010-12' and all(len(v) == len(months) for v in cy['series'].values())
    c = cy['series']
    for k in range(len(months)):
        close(c['hydrogen_escape_per_s'][k], supply, 2e-3, 'diffusion-limited hydrogen')
        m = c['hydrogen_escape_per_s'][k] * 1.00784 * AMU + c['oxygen_escape_per_s'][k] * 15.999 * AMU + c['helium_escape_per_s'][k] * 4.0026 * AMU
        close(c['mass_loss_kg_s'][k], m, 3e-3, 'mass loss')
    jeans_share = [a / b for a, b in zip(c['hydrogen_jeans_per_s'], c['hydrogen_escape_per_s'])]
    assert spearman(c['f107_sfu'], jeans_share) > 0.8, 'Jeans escape carries more at solar maximum'
    assert spearman(c['sunspot_number'], c['dose_pole_usv_h']) < -0.5, 'less cosmic radiation at solar maximum'
    span = lambda v: (max(v) - min(v)) / max(v)
    assert span(c['neutron_pole']) > span(c['neutron_equator']), 'the solar cycle matters more at the poles'
    assert spearman(c['sunspot_number'], c['oxygen_escape_per_s']) > 0.5, 'more oxygen escapes at solar maximum'

    # Scenarios.
    dip = sorted(w['dipole_scenarios'], key=lambda r: -r['moment_ratio'])
    assert all(a['production_pct'] <= b['production_pct'] and a['cutoff_equator_gv'] > b['cutoff_equator_gv'] and a['polar_window_share'] <= b['polar_window_share'] for a, b in zip(dip, dip[1:]))
    today = [r for r in dip if r['moment_ratio'] == 1.0][0]
    close(today['production_pct'], 100, 1e-6, 'today is the reference')
    for r in dip:
        close(r['cutoff_equator_gv'], w['dipole']['cutoff_equator_gv'] * r['moment_ratio'], 5e-3, 'cutoff ~ moment')
        close(r['standoff_re'] / today['standoff_re'], r['moment_ratio'] ** (1 / 3), 5e-3, 'r0 ~ M^(1/3)')
    laschamp = dip[-1]
    assert 1.5 < laschamp['production_pct'] / 100 < 3.5, 'about twice the cosmogenic production at a tenth of the dipole (Laschamp)'
    ss = w['storm_scenarios']
    assert all(a['dst_nt'] >= b['dst_nt'] and a['aurora_overhead_mlat_deg'] >= b['aurora_overhead_mlat_deg'] for a, b in zip(ss, ss[1:])), 'stronger storms reach lower latitudes'
    assert ss[-1]['dst_nt'] < -800 and ss[-1]['g_scale'] == 5 and ss[0]['g_scale'] == 0
    seps = w['sep_scenarios']
    hard = [r for r in seps if r['efold_rigidity_gv'] >= 0.3][0]
    soft = [r for r in seps if r['efold_rigidity_gv'] < 0.15]
    assert hard['dose_pole_ratio'] > 2 and all(r['dose_pole_ratio'] < 1.1 for r in soft), 'only hard spectra raise the dose at flight altitude'
    assert all(r['ozone_upper_pct'] < 0 and 55 < r['reach_mlat_deg'] < 75 for r in soft), 'solar protons reach the polar caps and destroy ozone'
    ft = lambda ch4: 0.53 + 3.8 + 2 * ch4
    for r in w['methane_scenarios']:
        close(r['relative_to_today_pct'], ft(r['methane_ppmv']) / ft(1.93) * 100, 5e-3, 'hydrogen escape ~ total hydrogen')
    cities = {r['name']: r for r in w['cities']}
    assert cities['Singapore']['aurora_at_storm_peak'] == 'not visible' and cities['Singapore']['cutoff_gv'] > 12
    assert all(r['dose_min_usv_h'] >= r['dose_max_usv_h'] for r in w['cities'])
    assert all((r['magnetic_latitude_deg'] > 0) == (r['latitude_deg'] > 0) for r in w['cities'] if abs(r['latitude_deg']) > 15)

    # Budgets.
    b = w['budgets']
    me = b['mean_escape']
    close(me['total_kg_s'], me['hydrogen_kg_s'] + me['oxygen_kg_s'] + me['helium_kg_s'], 2e-3, 'total escape')
    close(me['polar_kg_s'], me['hydrogen_kg_s'] * me['hydrogen_polar_wind_share'] + me['oxygen_kg_s'] + me['helium_kg_s'], 3e-3, 'polar escape')
    assert 2.5 < me['hydrogen_kg_s'] < 5 and 0.03 < me['oxygen_kg_s'] < 3 and me['polar_share'] < 0.5, 'hydrogen dominates by mass, and mostly leaves globally'
    close(b['reservoirs']['ocean_share_over_earth_history'], me['hydrogen_kg_s'] * 18.015 / 2.016 * 365.25 * 86400 * 4.5e9 / 1.4e21, 3e-3, 'ocean share')
    assert b['reservoirs']['ocean_share_over_earth_history'] < 0.01 and b['energy']['share_of_sunlight'] < 1e-5
    assert b['storm']['oxygen_lost_kg'] > 2 * b['storm']['oxygen_quiet_kg'], 'a storm removes days to weeks of oxygen'
    close(b['storm']['hydrogen_lost_kg'], b['storm']['hydrogen_quiet_kg'], 0.03, 'but not more hydrogen over days')
    assert b['clouds']['forcing_constrained_w_m2'] < b['clouds']['tsi_cycle_forcing_w_m2'] < b['clouds']['forcing_hypothesis_w_m2']

    # Without the solar cycle.
    lean = copy.deepcopy(fast)
    lean.pop('solar_cycle_path')
    lean.pop('cycle')
    result, output = run(lean, 'lean')
    assert result.returncode == 0, result.stderr
    lw = json.loads(output.read_text())['solar_wind']
    schema_ok(OUT_SCHEMA, lw, 'output without the cycle')
    assert lw['cycle'] is None and lw['budgets']['mean_escape']['source'] == 'storm background'

    # Invalid requests produce no output.
    def broken(label, mutate):
        doc = copy.deepcopy(fast)
        mutate(doc)
        result, output = run(doc, 'broken')
        assert result.returncode != 0 and not output.exists(), f'{label} must be rejected'
        assert 'error:' in result.stderr, label

    broken('schema version', lambda d: d.update(schema_version='terraflux.solar.wind-run/v0'))
    broken('unknown field', lambda d: d.update(extra=1))
    broken('unknown storm field', lambda d: d['storm'].update(wind=1))
    broken('too many frames', lambda d: d['storm'].update(hours=480, frame_every_hours=1))
    broken('negative density', lambda d: d['storm']['background'].update(density_cm3=-1))
    broken('Bz beyond the cloud field', lambda d: d['storm']['cme'].update(cloud_bz_start_nt=-50))
    broken('land mask size', lambda d: d['map'].update(land_mask=['#.']))
    broken('missing solar timeline', lambda d: d.update(solar_cycle_path='no/such/file.json'))
    broken('dipole moment', lambda d: d['dipole'].update(moment_a_m2=1))
    broken('response name', lambda d: d.update(radiation={'responses': {'neutron': {'anchor': 1}}}))
    broken('cycle outside the record', lambda d: d['cycle'].update(first_year=2090, last_year=2095))

print('solar wind CLI checks passed')
