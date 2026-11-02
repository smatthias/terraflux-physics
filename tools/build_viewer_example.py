#!/usr/bin/env python3
"""Generate the viewer fixture through built C++ kernels and file adapters."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from compose_mobile_weather import compose

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, default=ROOT / 'viewer/public/examples/engine-weather.json')
    parser.add_argument('--check', action='store_true', help='check that committed output matches regeneration')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='terraflux-example-') as temporary:
        directory = Path(temporary)
        subprocess.run([str(args.build_dir.resolve() / 'terraflux-generate-viewer-example'), str(directory)], check=True)
        subprocess.run([str(args.build_dir.resolve() / 'terraflux-grid-rainfall'),
                        str(directory / 'forecast.json'), str(directory / 'accumulation.json'),
                        str(directory / 'gridded.json')], check=True)
        result = compose(json.loads((directory / 'forecast.json').read_text()),
                         json.loads((directory / 'gridded.json').read_text()))
        if args.check:
            # Floating-point kernel results can vary slightly between platforms.
            committed = json.loads(args.output.read_text())
            def equivalent(a, b):
                if type(a) in (float, int) and type(b) in (float, int):
                    return abs(a-b) <= 1e-9 * max(1, abs(a), abs(b))
                if isinstance(a, dict) and isinstance(b, dict):
                    return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
                if isinstance(a, list) and isinstance(b, list):
                    return len(a) == len(b) and all(equivalent(x,y) for x,y in zip(a,b))
                return a == b
            if not equivalent(committed, result):
                parser.exit(1, 'Engine example differs from committed fixture. Regenerate it.\n')
            print('Committed engine example matches regeneration')
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
            print(f'Wrote {args.output}')

if __name__ == '__main__':
    main()
