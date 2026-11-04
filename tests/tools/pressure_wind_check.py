"""Run the pressure-wind CLI on the committed example and check the physics
of the output; pass the built terraflux-run-pressure-wind executable."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request_path = root / 'examples' / 'pressure-wind-request.json'
request = json.loads(request_path.read_text())
grid = request['grid']
nx, ny = grid['nx'], grid['ny']
cell = lambda east, north: (int((north - grid['origin_north_m']) // grid['spacing_north_m']) * nx
                            + int((east - grid['origin_east_m']) // grid['spacing_east_m']))

with tempfile.TemporaryDirectory() as temporary:
    output_path = Path(temporary) / 'pressure-wind.json'
    subprocess.run([sys.argv[1], str(request_path), str(output_path)], check=True)
    output = json.loads(output_path.read_text())

assert output['schema_version'] == 'terraflux.weather.pressure-wind-forecast/v1'
assert output['elapsed_seconds'] == request['time']['timestep_seconds'] * request['time']['steps']
assert len(output['snapshots']) == request['time']['steps'] // request['snapshot_every_steps']
assert output['snapshots'][-1]['elapsed_seconds'] == output['elapsed_seconds']
count = nx * ny
for key in ('wind_east', 'wind_north'):
    values = output['fields'][key]['values']
    assert output['fields'][key]['units'] == 'm/s' and len(values) == count
    assert all(math.isfinite(v) for v in values)
pressure = output['pressure']['values']
assert len(pressure) == count and all(p > 0 for p in pressure)
assert output['pressure']['source']['kind'] == 'synthetic_gaussian_systems'

# Southern Hemisphere: f < 0 everywhere and more negative toward the south.
f = output['diagnostics']['coriolis_parameter_s']
assert all(v < 0 for v in f) and f[0] < f[-1]

# After 48 hours with k = 2e-5 1/s the wind is within e^{-3.5} of balance.
u, v = output['fields']['wind_east']['values'], output['fields']['wind_north']['values']
bu = output['diagnostics']['balanced_wind']['wind_east']['values']
bv = output['diagnostics']['balanced_wind']['wind_north']['values']
scale = max(math.hypot(a, b) for a, b in zip(bu, bv))
assert 5 < scale < 40, scale
assert max(math.hypot(a - b, c - d) for a, b, c, d in zip(u, bu, v, bv)) < 0.05 * scale

# Midway between the western high and the eastern low the pressure falls
# eastward. The balanced wind there is mostly along the isobars, with a
# smaller drift toward the low turned by exactly atan(k / |f|).
high, low = request['pressure']['pressure_systems']['centers']
i = cell(0.5 * (high['east_m'] + low['east_m']), 0.5 * (high['north_m'] + low['north_m']))
assert pressure[i + 1] < pressure[i - 1]
gx = (pressure[i + 1] - pressure[i - 1]) / (2 * grid['spacing_east_m'])
gy = (pressure[i + nx] - pressure[i - nx]) / (2 * grid['spacing_north_m'])
g = math.hypot(gx, gy)
down_east, down_north = -gx / g, -gy / g
k = request['atmosphere']['surface_friction_coefficient_s']
for east, north, tolerance in ((bu[i], bv[i], 1e-6), (u[i], v[i], 0.05)):
    toward_low = east * down_east + north * down_north
    along_isobar = east * down_north - north * down_east
    assert toward_low > 0 and abs(along_isobar) > toward_low, (east, north)
    assert abs(math.atan2(toward_low, abs(along_isobar)) - math.atan(k / abs(f[i]))) < tolerance
    # Southern Hemisphere: low pressure lies to the right of the flow, so with
    # pressure falling eastward the along-isobar flow is northward.
    assert north > 0 and along_isobar < 0

# Clockwise flow around the low in the Southern Hemisphere: east of the
# center the wind is southward; the center itself is nearly calm.
east_of_low = cell(low['east_m'] + 4 * grid['spacing_east_m'], low['north_m'])
assert v[east_of_low] < 0
center = cell(low['east_m'], low['north_m'])
assert math.hypot(u[center], v[center]) < 0.1 * scale
print('pressure wind CLI checks passed')
