"""Generate (or check) an engine viewer fixture with a built CLI (terraflux-run-ocean-circulation by default,
or --cli terraflux-run-polar-melt / terraflux-run-coastal-flooding / terraflux-run-solar-cycle /
terraflux-run-cosmic-dust).

The fixture is the CLI timeline written compactly with sorted keys, the format of the committed
viewer/public/ocean*/engine-timeline.json files. --check regenerates into a temporary directory and
compares with the committed file.

The runs are not bit-for-bit reproducible across compilers and C libraries: exp, pow or sin may differ in
the last bit, and so may what the compiler folds at build time. The comparison therefore has two stages:

1. The structure must be identical: the same keys, list lengths and strings.
2. Every number must agree to --rtol relative (default 1e-6, floor 1e-6 absolute), or within one and a half
   units of its last printed digit (outputs rounded to a few significant digits can flip: 54.28 vs 54.27).
   Per-step budget residuals (*_balance_error_*, the chemistry's transport residual) are floating-point
   noise around zero and only need to stay below absolute bounds far inside what the viewer enforces.

   A field (all values under one path pattern, for example every frame's sea-ice concentration map) whose
   numbers do not all agree is still accepted when its relative RMS difference
   RMS(committed - regenerated) / RMS(values) is at most --field-tolerance (default 1e-4). Long runs with
   thresholds (sea ice appearing or not in a cell, a mixed-layer criterion, a stream's core cell) amplify
   last-bit differences into a different sequence of weather. Their climate stays the same, so they use
   a larger tolerance. Diagnostics that jump when such a threshold flips (ice drift and surface
   temperature where ice comes and goes, front flags, stream ids and cores) get ten times the tolerance.

On the machine that wrote the fixture the comparison is exact. Anything that changes the physics moves far
more than these tolerances.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

# Absolute tolerances for per-step residuals (the viewer checks them against 1e-6 of the totals).
NOISE = {'heat_balance_error_j': 1e12, 'volume_balance_error_m3': 10.0, 'salt_balance_error_kg': 1e4, 'coastal_balance_error_m3': 1.0,
         # The transport residual of the ocean chemistry (solver tolerance; inventories are ~1e14 mol).
         'transport_carbon_mol': 1e5, 'transport_nitrogen_mol': 1e4, 'transport_oxygen_mol': 1e5}


# Diagnostics that jump when a threshold flips in one cell (presence of ice, fronts, stream membership).
DISCONTINUOUS = {'ice_drift_east_m_s', 'ice_drift_north_m_s', 'ice_surface_temperature_k', 'ocean_to_ice_heat_w_m2', 'ice_water_m3_s',
                 'front', 'stream_id', 'core_cell', 'minimum_oxygen_depth_m'}


def quantum(value):
    """One unit of the last printed digit of a JSON number (0 for numbers printed to full precision)."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return 0.0
    if isinstance(value, int):
        return 1.0
    text = repr(float(value))
    mantissa, _, exponent = text.partition('e')
    digits = len(mantissa.replace('-', '').replace('.', '').lstrip('0'))
    if digits >= 15:   # printed to full double precision: not a rounded output
        return 0.0
    decimals = len(mantissa.partition('.')[2])
    return 10.0 ** (int(exponent or 0) - decimals)


class Comparison:
    """Walks the committed and the regenerated document together; see the module docstring."""

    def __init__(self, rtol, field_tolerance):
        self.rtol, self.field_tolerance = rtol, field_tolerance
        self.structure = None                 # first structural difference
        self.fields = {}                      # pattern -> [sum of squared differences, sum of squared values, first failing path, its values, failures, count]
        self.exact = True

    def walk(self, a, b, path='', pattern='', key=''):
        if self.structure:
            return
        if isinstance(a, dict) and isinstance(b, dict):
            if a.keys() != b.keys():
                self.structure = f'{path}: keys differ ({sorted(set(a) ^ set(b))[:5]})'
                return
            for k in a:
                self.walk(a[k], b[k], f'{path}/{k}', f'{pattern}/{k}', k)
        elif isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                self.structure = f'{path}: {len(a)} != {len(b)} entries'
                return
            for i, (x, y) in enumerate(zip(a, b)):
                self.walk(x, y, f'{path}[{i}]', f'{pattern}[]', key)
        elif isinstance(a, (int, float)) and isinstance(b, (int, float)) and isinstance(a, bool) == isinstance(b, bool):
            x, y = float(a), float(b)
            field = self.fields.setdefault(pattern, [0.0, 0.0, None, None, 0, 0, key])
            field[0] += (x - y) ** 2
            field[1] += 0.5 * (x * x + y * y)
            field[5] += 1
            scale = max(1.0, abs(x), abs(y))
            if abs(x - y) <= max(1e-9 * scale, NOISE.get(key, 0.0)):
                return
            self.exact = False
            if abs(x - y) <= max(self.rtol * scale, 1.5 * max(quantum(a), quantum(b))):
                return
            field[4] += 1
            if field[2] is None:
                field[2], field[3] = path, (a, b)
        elif type(a) is not type(b) or a != b:   # strings, nulls, or a number where the other has something else
            self.structure = f'{path}: {str(a)[:80]} != {str(b)[:80]}'

    def failures(self):
        """Fields that disagree beyond their tolerance, as (pattern, relative RMS, first path, values, failures, count, tolerance)."""
        out = []
        for pattern, (diff2, value2, path, values, failed, count, key) in self.fields.items():
            if not failed:
                continue
            error = (diff2 / value2) ** 0.5 if value2 > 0 else float('inf')
            tolerance = self.field_tolerance * (10.0 if key in DISCONTINUOUS else 1.0)
            out.append((pattern, error, path, values, failed, count, tolerance))
        return sorted(out, key=lambda f: -f[1] / max(f[6], 1e-300))


def check(committed, generated, rtol, field_tolerance):
    """(ok, message) for a committed fixture against its regeneration."""
    comparison = Comparison(rtol, field_tolerance)
    comparison.walk(committed, generated)
    if comparison.structure:
        return False, f'differs from regeneration at {comparison.structure}'
    fields = comparison.failures()
    beyond = [f for f in fields if f[1] > f[6]]
    if beyond:
        pattern, error, path, (a, b), failed, count, tolerance = beyond[0]
        return False, (f'differs from regeneration at {path}: {a} != {b} ({failed} of {count} values of {pattern} differ; '
                       f'relative RMS difference {error:.3g} > {tolerance:.3g})')
    if comparison.exact:
        return True, 'matches regeneration'
    if not fields:
        return True, f'matches regeneration within {comparison.rtol:g} relative and the printed digits'
    worst = max(fields, key=lambda f: f[1] / max(f[6], 1e-300))
    return True, (f'matches regeneration within cross-platform tolerance ({len(fields)} fields differ in single values; '
                  f'the largest relative RMS difference is {worst[1]:.3g} in {worst[0]}, tolerance {worst[6]:.3g})')


def compact(document):
    return json.dumps(document, separators=(',', ':'), sort_keys=True, allow_nan=False) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--request', type=Path, default=ROOT / 'examples/ocean-heat-rain-request.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'viewer/public/ocean-heat/engine-timeline.json')
    parser.add_argument('--cli', default='terraflux-run-ocean-circulation',
                        choices=['terraflux-run-ocean-circulation', 'terraflux-run-polar-melt', 'terraflux-run-coastal-flooding', 'terraflux-run-solar-cycle',
                                 'terraflux-run-cosmic-dust'])
    parser.add_argument('--check', action='store_true', help='check that the committed fixture matches regeneration')
    parser.add_argument('--rtol', type=float, default=1e-6, help='relative tolerance of single numbers in --check (default 1e-6)')
    parser.add_argument('--field-tolerance', type=float, default=1e-4,
                        help='relative RMS difference a whole field may have in --check when single numbers differ (default 1e-4)')
    args = parser.parse_args()
    cli = args.build_dir.resolve() / args.cli
    with tempfile.TemporaryDirectory(prefix='terraflux-example-') as temporary:
        generated = Path(temporary) / 'timeline.json'
        # Requests may name other fixtures relative to the repository root (e.g. a catchment timeline).
        subprocess.run([str(cli), str(args.request.resolve()), str(generated)], check=True, cwd=ROOT)
        document = json.loads(generated.read_text())
    if args.check:
        committed = json.loads(args.output.read_text())
        ok, message = check(committed, document, args.rtol, args.field_tolerance)
        if not ok:
            print(f'Fixture {args.output} {message}. Regenerate it.', file=sys.stderr)
            return 1
        print(f'Committed fixture {args.output.resolve()} {message}')
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(compact(document))
    print(f'Wrote {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
