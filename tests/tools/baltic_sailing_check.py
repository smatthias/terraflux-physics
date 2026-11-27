"""Run the coupled wind CLI on the Baltic sailing example, compare it with the committed viewer fixture and check what
the sailing view's route planning relies on: a grid over the western Baltic, three-hourly snapshots over three and a
half days, a moderate south-westerly breeze after the spin-up, and an initial state below saturation; pass the built
terraflux-run-coupled-wind executable."""
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request_path = root / 'examples' / 'baltic-sailing-request.json'
fixture_path = root / 'viewer' / 'public' / 'baltic-sailing' / 'engine-timeline.json'
request = json.loads(request_path.read_text())

with tempfile.TemporaryDirectory() as temporary:
    output_path = Path(temporary) / 'baltic.json'
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
    'Baltic sailing output differs from the committed fixture. Regenerate viewer/public/baltic-sailing/engine-timeline.json.'

grid = request['grid']
assert (grid['geographic_anchor_latitude_deg'], grid['geographic_anchor_longitude_deg']) == (54.8, 12.3), 'anchored in the western Baltic'
assert grid['nx'] * grid['spacing_east_m'] >= 500e3 and grid['ny'] * grid['spacing_north_m'] >= 300e3, 'Kiel Bight to Bornholm'
assert request['snapshot_every_steps'] * request['time']['timestep_seconds'] == 10800, 'three-hourly snapshots'
frames = output['frames']
assert len(frames) == 29 and all(b['elapsed_seconds'] - a['elapsed_seconds'] == 10800 for a, b in zip(frames, frames[1:]))

# After the spin-up (from Friday 00 UTC, frame 4) a moderate south-westerly: a median of 8-20 kt from 200-280 degrees.
for frame in frames[4:]:
    fields = frame['fields']
    u, v = fields['wind_east']['values'], fields['wind_north']['values']
    speed_kt = [math.hypot(a, b) * 1.9438445 for a, b in zip(u, v)]
    direction = [(math.degrees(math.atan2(-a, -b)) + 360) % 360 for a, b in zip(u, v)]
    assert 8 <= statistics.median(speed_kt) <= 20, f"median wind {statistics.median(speed_kt):.1f} kt at {frame['elapsed_seconds']} s"
    assert 200 <= statistics.median(direction) <= 280, f"median direction {statistics.median(direction):.0f} at {frame['elapsed_seconds']} s"

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
print('Baltic sailing CLI and fixture checks passed')
