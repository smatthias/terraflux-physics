#!/usr/bin/env python3
"""Generate (or check) an engine viewer fixture with a built CLI (terraflux-run-ocean-circulation by default,
or --cli terraflux-run-polar-melt / terraflux-run-coastal-flooding).

The fixture is the CLI timeline written compactly with sorted keys, the format of the committed
viewer/public/ocean*/engine-timeline.json files. --check regenerates into a temporary directory and
compares with the committed file: numbers must agree to 1e-9 relative; per-step budget residuals
(*_balance_error_*), which are floating-point noise around zero, must agree to absolute bounds far below
the budget tolerances the viewer enforces.
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


def equivalent(a, b, key=''):
    if isinstance(a, bool) or isinstance(b, bool):
        return a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(a - b) <= max(1e-9 * max(1.0, abs(a), abs(b)), NOISE.get(key, 0.0))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(equivalent(a[k], b[k], k) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(equivalent(x, y, key) for x, y in zip(a, b))
    return a == b


def first_difference(a, b, path='', key=''):
    if equivalent(a, b, key):
        return None
    if isinstance(a, dict) and isinstance(b, dict):
        if a.keys() != b.keys():
            return f'{path}: keys differ ({sorted(set(a) ^ set(b))[:5]})'
        for k in a:
            found = first_difference(a[k], b[k], f'{path}/{k}', k)
            if found:
                return found
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            found = first_difference(x, y, f'{path}[{i}]', key)
            if found:
                return found
    return f'{path}: {str(a)[:80]} != {str(b)[:80]}'


def compact(document):
    return json.dumps(document, separators=(',', ':'), sort_keys=True, allow_nan=False) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--request', type=Path, default=ROOT / 'examples/ocean-heat-rain-request.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'viewer/public/ocean-heat/engine-timeline.json')
    parser.add_argument('--cli', default='terraflux-run-ocean-circulation', choices=['terraflux-run-ocean-circulation', 'terraflux-run-polar-melt', 'terraflux-run-coastal-flooding'])
    parser.add_argument('--check', action='store_true', help='check that the committed fixture matches regeneration')
    args = parser.parse_args()
    cli = args.build_dir.resolve() / args.cli
    with tempfile.TemporaryDirectory(prefix='terraflux-example-') as temporary:
        generated = Path(temporary) / 'timeline.json'
        # Requests may name other fixtures relative to the repository root (e.g. a catchment timeline).
        subprocess.run([str(cli), str(args.request.resolve()), str(generated)], check=True, cwd=ROOT)
        document = json.loads(generated.read_text())
    if args.check:
        committed = json.loads(args.output.read_text())
        difference = first_difference(committed, document)
        if difference:
            print(f'Fixture {args.output} differs from regeneration at {difference}. Regenerate it.', file=sys.stderr)
            return 1
        print(f'Committed fixture {args.output.resolve()} matches regeneration')
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(compact(document))
    print(f'Wrote {args.output}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
