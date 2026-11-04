"""Run the atmospheric column CLI on the committed example and verify the
profile independently; pass the built terraflux-run-atmospheric-column."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request = json.loads((root / 'examples' / 'column-request.json').read_text())
with tempfile.TemporaryDirectory() as temporary:
    output_path = Path(temporary) / 'column.json'
    subprocess.run([sys.argv[1], str(root / 'examples' / 'column-request.json'), str(output_path)], check=True)
    out = json.loads(output_path.read_text())

G, R_D, C_P, L, EPS = 9.80665, 287.05, 1004.6, 2.501e6, 0.622
cfg, surface = request['column'], request['surface']
n, dz = cfg['layer_count'], cfg['layer_thickness_m']
assert out['schema_version'] == 'terraflux.weather.column-profile/v1' and out['config'] == cfg
initial, adjusted, parcel = out['initial_layers'], out['adjusted_layers'], out['surface_parcel']
assert len(initial['height_m']) == n and len(initial['interface_pressure_pa']) == n + 1

# Hydrostatic thinning recomputed independently from the environment profile
# (exact integral for virtual temperature linear within each half-layer).
T = lambda z: surface['air_temperature_k'] - cfg['environment_lapse_rate_k_m'] * min(z, cfg['tropopause_height_m'])
q = lambda z: surface['specific_humidity_kg_kg'] * math.exp(-z / cfg['humidity_scale_height_m'])
tv = lambda z: T(z) * (1 + 0.608 * q(z))
def step(p, z0, z1):
    a, b = tv(z0), tv(z1)
    if abs(b - a) < 1e-9 * a:
        return p * math.exp(-G * (z1 - z0) / (R_D * a))
    return p * (b / a) ** (-G / (R_D * (b - a) / (z1 - z0)))
p, z = surface['pressure_pa'], 0.0
for k in range(n):
    p_mid = step(p, z, z + dz / 2)
    p = step(p_mid, z + dz / 2, z + dz)
    z += dz
    assert abs(initial['pressure_pa'][k] - p_mid) < 1e-6 * p_mid
    assert abs(initial['interface_pressure_pa'][k + 1] - p) < 1e-6 * p
    assert initial['air_density_kg_m3'][k] < (initial['air_density_kg_m3'][k - 1] if k else 2.0)
assert p / surface['pressure_pa'] < 0.15, 'the 15 km column holds more than 85 % of the mass'

# Adjustment conserves enthalpy and water; the adjusted column is stable and unsaturated.
energy = lambda c: sum(m * (C_P * t + L * qq) for m, t, qq in zip(c['layer_mass_kg_m2'], c['air_temperature_k'], c['specific_humidity_kg_kg']))
water = lambda c: sum(m * (qq + l) for m, qq, l in zip(c['layer_mass_kg_m2'], c['specific_humidity_kg_kg'], c['cloud_water_kg_kg']))
assert abs(energy(adjusted) - energy(initial)) <= 1e-9 * energy(initial)
assert abs(water(adjusted) - water(initial)) <= 1e-9 * water(initial)
assert all(b >= a * (1 - 1e-9) for a, b in zip(adjusted['potential_temperature_k'], adjusted['potential_temperature_k'][1:]))
assert all(rh <= 1 + 1e-6 for rh in adjusted['relative_humidity_fraction'])
assert all(l >= 0 for l in adjusted['cloud_water_kg_kg'])
report = out['convective_adjustment']
assert report['condensed_water_kg_m2'] >= 0 and report['iterations'] >= 1

# Surface parcel: dry ascent conserves potential temperature up to the LCL,
# the parcel stays saturated above it, CAPE is the positive area and the
# classification follows the thresholds.
theta = lambda t, pp: t * (1e5 / pp) ** (R_D / C_P)
lcl = parcel['lcl_height_m']
assert lcl is not None and 500 < lcl < 1500
theta0 = theta(parcel['temperature_k'][0], parcel['pressure_pa'][0])
for zz, t, pp, qq in zip(parcel['height_m'], parcel['temperature_k'], parcel['pressure_pa'], parcel['specific_humidity_kg_kg']):
    if zz < lcl:
        assert abs(theta(t, pp) - theta0) < 1e-6 and qq == parcel['specific_humidity_kg_kg'][0]
es = lambda t: 610.94 * math.exp(17.625 * (t - 273.15) / (t - 273.15 + 243.04))
for zz, t, pp, qq in zip(parcel['height_m'], parcel['temperature_k'], parcel['pressure_pa'], parcel['specific_humidity_kg_kg']):
    if zz > lcl + 1:
        qs = EPS * es(t) / (pp - (1 - EPS) * es(t))
        assert abs(qq - qs) < 1e-9, 'saturated above the LCL'
cape = 0.0
heights, buoyancy = parcel['height_m'], parcel['buoyancy_m_s2']
lfc, el = parcel['lfc_height_m'], parcel['equilibrium_level_m']
assert lfc is not None and el is not None and lcl <= lfc < el
for (z0, b0), (z1, b1) in zip(zip(heights, buoyancy), zip(heights[1:], buoyancy[1:])):
    if lfc <= z0 and z1 <= el:
        cape += 0.5 * (z1 - z0) * (max(b0, 0) + max(b1, 0))
assert abs(cape - parcel['cape_j_kg']) < 1e-6 * max(1, cape)
assert parcel['cape_j_kg'] > 1000 and parcel['convective_potential'] == 'moderate'
assert parcel['cin_j_kg'] >= 0 and el > 8000
print('atmospheric column CLI checks passed')
