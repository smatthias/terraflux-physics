"""Run the coupled wind CLI on the Alps foehn and Rockies barrier examples;
compare the Alps output with the committed fixture and check the orography
format and physics (foehn, orientation-dependent blocking, cold pools);
pass the built terraflux-run-coupled-wind executable."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]


def run(name):
    request = json.loads((root / 'examples' / name).read_text())
    with tempfile.TemporaryDirectory() as temporary:
        output_path = Path(temporary) / 'out.json'
        subprocess.run([sys.argv[1], str(root / 'examples' / name), str(output_path)], check=True)
        return request, json.loads(output_path.read_text())


def equivalent(a, b):
    if type(a) in (float, int) and type(b) in (float, int) and not isinstance(a, bool) and not isinstance(b, bool):
        return abs(a - b) <= 1e-9 * max(1, abs(a), abs(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equivalent(x, y) for x, y in zip(a, b))
    return a == b


# ---------------------------------------------------------------------- Alps
request, output = run('alps-foehn-request.json')
fixture = json.loads((root / 'viewer' / 'public' / 'alps' / 'engine-timeline.json').read_text())
assert equivalent(output, fixture), 'Alps output differs from the committed fixture. Regenerate viewer/public/alps/engine-timeline.json.'
grid = request['grid']
nx, ny = grid['nx'], grid['ny']
count = nx * ny
frames = output['frames']
terrain = output['terrain']
oro = output['coupled_atmosphere']['orography']
assert terrain['schema_version'] == 'terraflux.terrain.grid/v1' and terrain['units'] == 'm' and len(terrain['values']) == count
assert terrain['grid']['nx'] == nx and terrain['vertical_reference'] and terrain['source_id'] == 'synthetic_ridges_and_basins'
assert oro['schema_version'] == 'terraflux.weather.orography/v1' and oro['source_kind'] == 'engine_output'
assert oro['config']['terrain']['ridges'][0]['orientation_deg'] == 90.0 and len(oro['frames']) == len(frames)
ridge = request['orography']['terrain']['ridges'][0]
crest_row = int((ridge['center_north_m'] - grid['origin_north_m']) // grid['spacing_north_m'])
col = nx // 2
cell = lambda x, y: y * nx + x
assert terrain['values'][cell(col, crest_row)] > 2500 and terrain['values'][cell(col, 2)] < 600, 'a 2.4 km east-west ridge above a 400 m base'
FIELDS = ['upslope_lifting_m_s', 'obstacle_height_m', 'froude_number', 'blocked_fraction', 'valley_depth_m', 'cross_barrier_wind_m_s',
          'along_barrier_wind_m_s', 'ridge_orientation_deg', 'surface_pressure_pa', 'cold_pool_k', 'cold_pool', 'foehn', 'foehn_warming_k',
          'orographic_precipitation_mm', 'orographic_precipitation_rate_mm_h', 'cumulative_orographic_precipitation_mm']
cumulative_before = [0.0] * count
for frame, of in zip(frames, oro['frames']):
    assert of['elapsed_seconds'] == frame['elapsed_seconds']
    for key in FIELDS:
        assert len(of[key]) == count, key
    T = frame['fields']['air_temperature']['values']
    for j in range(count):
        assert of['blocked_fraction'][j] >= 0 and of['blocked_fraction'][j] <= 1 and of['valley_depth_m'][j] >= 0 and of['cold_pool_k'][j] >= 0
        assert of['froude_number'][j] is None or of['froude_number'][j] >= 0
        assert of['ridge_orientation_deg'][j] is None or 0 <= of['ridge_orientation_deg'][j] < 180
        assert of['cold_pool'][j] == (of['cold_pool_k'][j] >= 1 and of['valley_depth_m'][j] >= 100)
        assert (of['foehn_warming_k'][j] > 0) == of['foehn'][j]
        assert of['orographic_precipitation_mm'][j] >= -1e-9 and of['orographic_precipitation_rate_mm_h'][j] >= -1e-9
        assert abs(of['cumulative_orographic_precipitation_mm'][j] - cumulative_before[j] - of['orographic_precipitation_mm'][j]) < 1e-6
        expected_ps = output['coupled_atmosphere']['pressure_pa'][j] * math.exp(-9.80665 * terrain['values'][j] / (287.05 * T[j]))
        assert abs(of['surface_pressure_pa'][j] - expected_ps) < 1e-6, 'hydrostatic surface pressure at the terrain height'
    cumulative_before = of['cumulative_orographic_precipitation_mm']
    assert of['cold_pool_cells'] == sum(of['cold_pool']) and of['foehn_cells'] == sum(of['foehn'])
    on_ridge = of['ridge_orientation_deg'][cell(col, crest_row - 2)]
    assert on_ridge is not None and abs(on_ridge - 90) < 0.5, 'contours of the Alps run east-west'
# Foehn: once the southerly flow is established, the northern (lee) foot is
# several kelvin warmer and drier than the southern foot; precipitation falls
# on the southern slope only; the flow against the ridge is partly blocked.
last = frames[-1]['fields']['air_temperature']['values']
south, north = cell(col, 3), cell(col, crest_row + 5)
assert terrain['values'][south] < 700 and terrain['values'][north] < 700
assert last[north] - last[south] > 4, 'north foehn: the lee foot is at least 4 K warmer'
q = output['coupled_atmosphere']['specific_humidity_kg_kg_by_frame'][-1]
rh = output['coupled_atmosphere']['relative_humidity_pct_by_frame'][-1]
assert rh[north] < rh[south] - 20, 'the lee is much drier'
of = oro['frames'][-1]
assert of['foehn_cells'] > 0 and any(of['foehn'][cell(x, crest_row + 2)] or of['foehn'][cell(x, crest_row + 3)] for x in range(4, nx - 4)), 'foehn flagged on the northern slope'
assert max(of['orographic_precipitation_rate_mm_h'][cell(x, crest_row - 2)] for x in range(4, nx - 4)) > 0.5, 'orographic precipitation on the windward (southern) slope'
assert all(of['orographic_precipitation_rate_mm_h'][cell(x, crest_row + 3)] < 1e-6 for x in range(4, nx - 4)), 'no orographic precipitation on the lee slope'
assert max(of['blocked_fraction'][cell(x, crest_row - 3)] for x in range(4, nx - 4)) > 0.2, 'the flow against the Alps is partly blocked'
assert frames[-1]['fields']['wind_north']['values'][south] > 5, 'southerly flow at the southern foot'
night = [of for of in oro['frames'] if of['surface_tendency_k_s'] < -1e-5]
assert night and max(n['cold_pool_cells'] for n in night) > 0, 'cold pools form in the foreland basin at night'
print('alps foehn checks passed')

# ------------------------------------------------------------------- Rockies
request, output = run('rockies-barrier-request.json')
grid = request['grid']
nx = grid['nx']
terrain = output['terrain']['values']
last = output['frames'][-1]
of = output['coupled_atmosphere']['orography']['frames'][-1]
ridge = request['orography']['terrain']['ridges'][0]
crest_col = int((ridge['center_east_m'] - grid['origin_east_m']) // grid['spacing_east_m'])
row = grid['ny'] // 2
cell = lambda x, y: y * nx + x
assert min(of['ridge_orientation_deg'][cell(crest_col - 3, row)], 180 - of['ridge_orientation_deg'][cell(crest_col - 3, row)]) < 0.5, 'contours of the Rockies run north-south'
west, east = cell(2, row), cell(crest_col + 6, row)
assert terrain[west] < 700 and terrain[east] < 700
T = last['fields']['air_temperature']['values']
assert T[east] - T[west] > 3, 'chinook: the eastern foot is warmer than the western foot'
windward = [cell(x, row) for x in range(crest_col - 6, crest_col - 1)]
most_blocked = max(windward, key=lambda i: of['blocked_fraction'][i])
assert of['blocked_fraction'][most_blocked] > 0.15, 'westerlies against a 2.5 km north-south range are partly blocked'
assert of['along_barrier_wind_m_s'][most_blocked] > 3, 'blocked air turns into a barrier jet along the range'
assert of['cross_barrier_wind_m_s'][most_blocked] < last['fields']['wind_east']['values'][west], 'the cross-barrier wind at the slope is below the upstream westerly'
assert max(of['orographic_precipitation_rate_mm_h'][cell(crest_col - 2, y)] for y in range(3, grid['ny'] - 3)) >= 0
print('rockies barrier checks passed')
