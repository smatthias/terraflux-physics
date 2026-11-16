"""Run the coupled wind CLI on the Germany VFR example, compare it with the committed viewer fixture and check what the
VFR (GAFOR) view relies on: a grid over Germany, two-hour snapshots, clouds and precipitation, and an initial state
below saturation; pass the built terraflux-run-coupled-wind executable."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request_path = root / 'examples' / 'germany-vfr-request.json'
fixture_path = root / 'viewer' / 'public' / 'gafor-germany' / 'engine-timeline.json'
request = json.loads(request_path.read_text())

with tempfile.TemporaryDirectory() as temporary:
    output_path = Path(temporary) / 'germany.json'
    subprocess.run([sys.argv[1], str(request_path), str(output_path)], check=True)
    output = json.loads(output_path.read_text())


def equivalent(a, b):
    if type(a) in (float, int) and type(b) in (float, int) and not isinstance(a, bool) and not isinstance(b, bool):
        return abs(a - b) <= 1e-9 * max(1, abs(a), abs(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equivalent(x, y) for x, y in zip(a, b))
    return a == b


assert equivalent(output, json.loads(fixture_path.read_text())), \
    'Germany VFR output differs from the committed fixture. Regenerate viewer/public/gafor-germany/engine-timeline.json.'

grid = request['grid']
assert (grid['geographic_anchor_latitude_deg'], grid['geographic_anchor_longitude_deg']) == (51.2, 10.4), 'anchored in Germany'
assert grid['nx'] * grid['spacing_east_m'] >= 800e3 and grid['ny'] * grid['spacing_north_m'] >= 900e3, 'the grid spans Germany'
assert request['snapshot_every_steps'] * request['time']['timestep_seconds'] == 7200, 'two-hour snapshots (GAFOR periods)'
frames = output['frames']
assert len(frames) == 37 and all(b['elapsed_seconds'] - a['elapsed_seconds'] == 7200 for a, b in zip(frames, frames[1:]))
atmosphere = output['coupled_atmosphere']
assert 'clouds' in atmosphere and 'precipitation' in atmosphere, 'the VFR view needs clouds and precipitation'

# The initial state is below saturation everywhere (the precipitation kernel's wet-bulb solver needs it).
T, H = request['initial_fields']['air_temperature_k'], request['initial_fields']['specific_humidity_kg_kg']
p0 = request['pressure']['pressure_systems']['background_pressure_pa']


def field(spec, east, north):
    return spec['background'] + sum(a['amplitude'] * math.exp(-((east - a['east_m']) ** 2 + (north - a['north_m']) ** 2) / a['radius_m'] ** 2) for a in spec['anomalies'])


worst = 0.0
for y in range(grid['ny']):
    for x in range(grid['nx']):
        east = grid['origin_east_m'] + (x + 0.5) * grid['spacing_east_m']
        north = grid['origin_north_m'] + (y + 0.5) * grid['spacing_north_m']
        t = field(T, east, north) - 273.15
        q = field(H, east, north)
        e = q * p0 / (0.622 + 0.378 * q)
        worst = max(worst, e / (610.94 * math.exp(17.625 * t / (t + 243.04))))
assert worst <= 1.0, f'initial relative humidity {worst:.3f} exceeds saturation'
print('Germany VFR CLI and fixture checks passed')
