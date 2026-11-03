"""Run the coupled wind CLI on the committed example, compare it with the
committed viewer fixture, and check the physics of the output; pass the
built terraflux-run-coupled-wind executable."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request_path = root / 'examples' / 'coupled-wind-request.json'
fixture_path = root / 'viewer' / 'public' / 'coupled' / 'engine-timeline.json'
request = json.loads(request_path.read_text())

with tempfile.TemporaryDirectory() as temporary:
    output_path = Path(temporary) / 'coupled-timeline.json'
    subprocess.run([sys.argv[1], str(request_path), str(output_path)], check=True)
    output = json.loads(output_path.read_text())


def equivalent(a, b):
    # Floating-point kernel results can vary slightly between platforms.
    if type(a) in (float, int) and type(b) in (float, int):
        return abs(a - b) <= 1e-9 * max(1, abs(a), abs(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equivalent(x, y) for x, y in zip(a, b))
    return a == b


assert equivalent(output, json.loads(fixture_path.read_text())), \
    'Coupled wind output differs from the committed viewer fixture. Regenerate viewer/public/coupled/engine-timeline.json.'

frames, atmosphere = output['frames'], output['coupled_atmosphere']
grid = request['grid']
nx, ny = grid['nx'], grid['ny']
count = nx * ny
steps, every = request['time']['steps'], request['snapshot_every_steps']
assert output['schema_version'] == 'terraflux.weather.timeline/v1'
assert atmosphere['schema_version'] == 'terraflux.weather.coupled_atmosphere/v1'
assert len(frames) == 1 + steps // every
assert [f['elapsed_seconds'] for f in frames] == [i * every * request['time']['timestep_seconds'] for i in range(len(frames))]
assert atmosphere['request']['diffusion'] == request['diffusion']
assert atmosphere['request']['density_mode'] == 'diagnosed'
for key in ('specific_humidity_kg_kg_by_frame', 'relative_humidity_pct_by_frame', 'air_density_kg_m3_by_frame',
            'supersaturated_cells_by_frame', 'scalar_stability_number_by_frame'):
    assert len(atmosphere[key]) == len(frames), key

# Moist-air relations reproduced independently: rho = p / (R_d T (1 + 0.608 q)),
# RH = e / e_s with the Magnus form. The stored diagnostics must match.
R_D, EPS = 287.05, 0.622
e_s = lambda t: 610.94 * math.exp(17.625 * (t - 273.15) / (t - 273.15 + 243.04))
pressure = atmosphere['pressure_pa']
for i, frame in enumerate(frames):
    T = frame['fields']['air_temperature']['values']
    q = atmosphere['specific_humidity_kg_kg_by_frame'][i]
    rho = atmosphere['air_density_kg_m3_by_frame'][i]
    rh = atmosphere['relative_humidity_pct_by_frame'][i]
    assert len(T) == len(q) == len(rho) == len(rh) == len(pressure) == count
    for j in range(count):
        assert abs(rho[j] - pressure[j] / (R_D * T[j] * (1 + 0.608 * q[j]))) < 1e-9
        e = q[j] * pressure[j] / (EPS + (1 - EPS) * q[j])
        assert abs(rh[j] - 100 * e / e_s(T[j])) < 1e-7
        assert 1.1 < rho[j] < 1.3 and 0 < rh[j] < 100 and 0 <= q[j] < 0.03
    assert atmosphere['supersaturated_cells_by_frame'][i] == 0
    assert 0 <= atmosphere['scalar_stability_number_by_frame'][i] <= 1

# Coupling direction: warm, moist air around the low is the lightest air.
T0, rho0 = frames[0]['fields']['air_temperature']['values'], atmosphere['air_density_kg_m3_by_frame'][0]
mean_t, mean_rho = sum(T0) / count, sum(rho0) / count
covariance = sum((t - mean_t) * (r - mean_rho) for t, r in zip(T0, rho0))
variance = math.sqrt(sum((t - mean_t) ** 2 for t in T0) * sum((r - mean_rho) ** 2 for r in rho0))
assert covariance / variance < -0.95, 'density must decrease where the air is warmer and moister'

# Upwind transport and diffusion never create new extrema.
for i in range(1, len(frames)):
    assert max(frames[i]['fields']['air_temperature']['values']) <= max(T0) + 1e-9
    assert min(frames[i]['fields']['air_temperature']['values']) >= min(T0) - 1e-9

# Southern Hemisphere low: clockwise flow, so the wind east of the center is
# southward once the flow has spun up; the wind is not calm anywhere nearby.
cell = lambda east, north: (int((north - grid['origin_north_m']) // grid['spacing_north_m']) * nx
                            + int((east - grid['origin_east_m']) // grid['spacing_east_m']))
low = request['pressure']['pressure_systems']['centers'][1]
final = frames[-1]['fields']
east_of_low = cell(low['east_m'] + 3 * grid['spacing_east_m'], low['north_m'])
assert final['wind_north']['values'][east_of_low] < -5
assert all(f['fields']['wind_east']['units'] == 'm/s' for f in frames)
print('coupled wind CLI and fixture checks passed')
