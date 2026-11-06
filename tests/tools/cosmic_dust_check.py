"""Check terraflux-run-cosmic-dust: the output contract, the mass and water bookkeeping, the budgets recomputed from
the output alone, the linear scenarios, the map (global mean, daily mean, the morning side, day and night), the
comparison with a solar run, and that invalid requests produce no output."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
example = json.loads((root / 'examples/cosmic-dust-request.json').read_text())
cli = sys.argv[1]

try:
    import jsonschema
    RUN_SCHEMA = json.loads((root / 'docs/weather/cosmic-dust-run-v1.schema.json').read_text())
    OUT_SCHEMA = json.loads((root / 'docs/weather/cosmic-dust-v1.schema.json').read_text())
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
fast = copy.deepcopy(example)
fast['population']['mass_bins'] = 18
fast['map'].update(hours=8)
fast.pop('solar_timeline_path')
schema_ok(RUN_SCHEMA, fast, 'fast request')
AREA = 4 * math.pi * 6.371e6 ** 2
YEAR, DAY = 365.25 * 86400, 86400

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
    d = timeline['cosmic_dust']
    schema_ok(OUT_SCHEMA, d, 'output')
    assert 'solar_comparison' not in d

    # ---- Bookkeeping: mass, states, water --------------------------------------------------------------------------
    f = d['fate']
    close(f['ablated_t_per_day'] + f['surviving_t_per_day'], f['influx_t_per_day'], 2e-3, 'ablated + surviving = influx')
    close(sum(f['surviving_by_state_t_per_day'].values()), f['surviving_t_per_day'], 2e-3, 'survivors by state')
    comp = d['population']['composition']
    close(f['water_in_t_per_day'], f['influx_t_per_day'] * comp['water'], 1e-3, 'water in = influx x water fraction')
    close(f['water_released_t_per_day'] + f['water_to_surface_kg_per_day'] / 1000, f['water_in_t_per_day'], 2e-3, 'water released + delivered = water in')
    assert 0.1 < f['surviving_t_per_day'] / f['influx_t_per_day'] < 0.5, 'about a quarter reaches the ground'
    assert 80 < f['median_ablation_altitude_km'] < 100 and f['median_water_altitude_km'] > f['median_ablation_altitude_km'], 'water leaves higher than the metals'
    bins = d['population']['bins']
    close(sum(b['mass_fraction'] for b in bins), 1.0, 1e-3, 'mass fractions')
    assert all(0 <= b['surviving_fraction'] <= 1 and abs(sum(b['fate'].values()) - 1) < 2e-3 for b in bins)
    assert bins[0]['surviving_fraction'] > bins[-1]['surviving_fraction'], 'small particles survive, large ones burn'
    assert all(b['diameter_um'] < c['diameter_um'] for b, c in zip(bins, bins[1:]))
    pr = d['profiles']
    n = len(pr['altitude_km'])
    assert all(len(pr[k]) == n for k in ['ablation_t_per_day_per_bin', 'water_kg_per_day_per_bin', 'energy_w_per_bin', 'air_density_kg_m3'])
    close(sum(pr['ablation_t_per_day_per_bin']), f['ablated_t_per_day'], 5e-3, 'the ablation profile holds the ablated mass')
    close(sum(pr['water_kg_per_day_per_bin']) / 1000, f['water_released_t_per_day'], 5e-3, 'the water profile holds the released water')
    assert sum(pr['energy_w_per_bin']) <= f['kinetic_power_w'] * 1.02, 'no more energy deposited than brought (plus the fall)'
    speeds = d['population']['speeds_km_s']
    v2 = sum(w * (v * 1e3) ** 2 for v, w in speeds) / sum(w for _, w in speeds)
    close(f['kinetic_power_w'], 0.5 * f['influx_t_per_day'] * 1000 / DAY * v2, 2e-3, 'kinetic power = 1/2 influx <v^2>')

    # ---- Scenarios and budgets, recomputed -------------------------------------------------------------------------
    s = d['scenarios']
    c = d['comparisons']
    for key in ['low', 'high']:
        ratio = s[key]['influx_t_per_day'] / s['central']['influx_t_per_day']
        for path_ in [('surviving_t_per_day',), ('water', 'in_t_per_year'), ('smoke', 'optical_depth'), ('energy', 'kinetic_power_w')]:
            a, b = s[key], s['central']
            for p in path_:
                a, b = a[p], b[p]
            close(a, b * ratio, 2e-3, f'{key} scales with the influx: {path_}')
    sc = s['central']
    close(sc['water']['in_t_per_year'], f['water_in_t_per_day'] * 365.25, 2e-3, 'water per year')
    close(sc['water']['share_of_ocean_per_year'], sc['water']['in_t_per_year'] * 1000 / c['ocean_mass_kg'], 2e-3, 'share of the ocean')
    close(sc['water']['vs_hydrogen_escape_water_equivalent'], f['water_in_t_per_day'] * 1000 / DAY / (c['hydrogen_escape_kg_s'] * 18.015 / 2.016), 2e-3, 'water against the escaping hydrogen')
    mass = sc['mass']
    close(mass['net_t_per_year'], mass['gain_t_per_year'] - mass['hydrogen_loss_t_per_year'] - mass['helium_loss_t_per_year'], 2e-3, 'net mass = gain - losses')
    assert mass['net_t_per_year'] < 0, 'the Earth loses more mass to space than it gains from dust'
    close(sc['energy']['per_m2_w'], sc['energy']['kinetic_power_w'] / AREA, 2e-3, 'energy per square metre')
    close(sc['energy']['share_of_sunlight'], sc['energy']['kinetic_power_w'] / (1361 / 4 * AREA), 2e-3, 'share of sunlight')
    smoke = sc['smoke']
    burden = sc['ablated_t_per_day'] * 1000 / DAY * c['smoke_residence_years'] * YEAR
    close(smoke['burden_t'], burden / 1000, 2e-3, 'smoke burden = ablation x residence')
    close(smoke['optical_depth'], burden * c['smoke_mass_extinction_m2_kg'] / AREA, 2e-3, 'smoke optical depth')
    close(smoke['equilibrium_temperature_bound_k'], smoke['forcing_bound_w_m2'] / c['feedback_w_m2_k'], 2e-3, 'temperature bound = forcing / feedback')
    meso = 1.0524 / 9.80665 * AREA * c['mesospheric_water_ppmv'] * 1e-6 * 18.015 / 28.96
    close(sc['water']['share_of_mesospheric_water'], sc['water']['released_above_80km_t_per_day'] * 1000 / DAY * c['mesospheric_water_lifetime_days'] * DAY / meso, 5e-3, 'share of mesospheric water')
    assert sc['water']['share_of_mesospheric_water'] < 0.01 and smoke['forcing_bound_w_m2'] < 0.05, 'negligible for the atmosphere and the climate at today\'s influx'
    wi = d['what_if']
    assert [w['multiplier'] for w in wi] == fast['scenarios']['multipliers']
    assert all(b['forcing_bound_w_m2'] > a['forcing_bound_w_m2'] for a, b in zip(wi, wi[1:]))
    close(d['thresholds']['multiplier_for_1_w_m2'], 10 * d['thresholds']['multiplier_for_0_1_w_m2'], 2e-3, 'the thresholds are linear')
    close(d['deposition_export']['water_kg_m2_s'], f['water_in_t_per_day'] * 1000 / DAY / AREA, 3e-3, 'per-area water flux')

    # ---- The map -----------------------------------------------------------------------------------------------
    m = d['map']
    frames = timeline['frames']
    assert len(frames) == len(m['frames']) == fast['map']['hours']
    nx, ny = fast['map']['cells_east'], fast['map']['cells_north']
    lat = [-90 + (i // nx + 0.5) * 180 / ny for i in range(nx * ny)]
    w = [math.cos(math.radians(x)) for x in lat]
    W = sum(w)
    for frame, mf in zip(frames, m['frames']):
        assert frame['elapsed_seconds'] == mf['elapsed_seconds'] and frame['grid']['nx'] == nx
        rate = mf['influx_ug_m2_day']
        close(sum(a * b for a, b in zip(rate, w)) / W, m['mean_influx_ug_m2_day'], 3e-3, 'area-weighted mean of every hour = the mean influx')
        assert max(rate) > 2 * min(rate), 'the side facing the apex gets more'
        day = sum(b for a, b in zip(mf['daylight'], w) if a) / W
        assert 0.45 < day < 0.55, 'half the Earth in daylight'
        apex = next(p for p in mf['source_zenith_points'] if p['name'] == 'apex')
        # The apex lies 90 deg west of the Sun: its zenith point is about 6 hours of longitude west of the subsolar point.
        gap = (mf['subsolar']['longitude_deg'] - apex['longitude_deg']) % 360
        assert 60 < gap < 120, f'apex about 90 deg west of the Sun ({gap})'
        # Largest influx near the apex's zenith point.
        k = max(range(nx * ny), key=lambda i: rate[i])
        assert abs(lat[k] - apex['latitude_deg']) < 60
    daily = m['daily_mean_ug_m2_day']
    for i in range(0, nx * ny, 97):
        close(daily[i], sum(mf['influx_ug_m2_day'][i] for mf in m['frames']) / len(m['frames']), 5e-3, 'daily mean of the hours', 1e-4)
    assert len(m['land_mask']) == nx * ny and 0.2 < m['land_share_of_influx'] < 0.45
    assert len(m['by_latitude']) == ny

    # ---- The solar comparison ---------------------------------------------------------------------------------
    with_sun = copy.deepcopy(fast)
    with_sun['solar_timeline_path'] = 'viewer/public/solar/engine-timeline.json'
    result, output = run(with_sun, 'sun')
    assert result.returncode == 0, result.stderr
    sol = json.loads(output.read_text())['cosmic_dust']['solar_comparison']
    close(sol['dust_vs_solar_cycle'], sol['dust_kinetic_power_w_m2'] / sol['tsi_cycle_change_global_mean_w_m2'], 3e-3, 'dust against the solar cycle')
    assert sol['dust_vs_solar_cycle'] < 1e-4 and sol['dust_vs_flares'] < 0.1

    # ---- Invalid requests: an error and no output ----------------------------------------------------------
    def broken(change, label):
        doc = copy.deepcopy(fast)
        change(doc)
        result, output = run(doc, 'broken')
        assert result.returncode != 0 and not output.exists(), f'{label} must be rejected'
        assert 'error:' in result.stderr, label

    broken(lambda x: x.update(schema_version='terraflux.cosmic.dust-run/v0'), 'wrong schema version')
    broken(lambda x: x['influx'].update(low_t_per_day=80), 'low above central')
    broken(lambda x: x['entry'].update(heat_transfr=1.0), 'a misspelt parameter')
    broken(lambda x: x['population'].update(speeds=[[5, 1]]), 'an entry speed below the escape speed')
    broken(lambda x: x['population']['composition'].update(water=0.95), 'a composition above 100 %')
    broken(lambda x: x['map'].update(land_mask=x['map']['land_mask'][:-1]), 'a land mask with a missing row')
    broken(lambda x: x['map'].update(date_utc='5 Nov 2026'), 'a malformed date')
    broken(lambda x: x.update(solar_timeline_path='missing.json'), 'a missing solar timeline')

print('cosmic dust CLI check passed')
