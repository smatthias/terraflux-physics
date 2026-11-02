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
        frames=json.loads((directory / 'forecast-frames.json').read_text())
        for i in range(1,len(frames)):
            forecast_path=directory / f'forecast-{i}.json'
            forecast_path.write_text(json.dumps(frames[i]))
            subprocess.run([str(args.build_dir.resolve() / 'terraflux-grid-rainfall'),
                            str(forecast_path), str(directory / f'accumulation-{i}.json'),
                            str(directory / 'gridded.json')], check=True)
            frames[i]=compose(frames[i], json.loads((directory / 'gridded.json').read_text()))
        result=frames[-1]
        timeline={'schema_version':'terraflux.weather.timeline/v1', 'frames':frames}
        outputs=[(args.output,result),(args.output.with_name('engine-timeline.json'),timeline)]
        if args.check:
            # Floating-point kernel results can vary slightly between platforms.
            def equivalent(a, b):
                if type(a) in (float, int) and type(b) in (float, int):
                    return abs(a-b) <= 1e-9 * max(1, abs(a), abs(b))
                if isinstance(a, dict) and isinstance(b, dict):
                    return a.keys() == b.keys() and all(equivalent(a[k], b[k]) for k in a)
                if isinstance(a, list) and isinstance(b, list):
                    return len(a) == len(b) and all(equivalent(x,y) for x,y in zip(a,b))
                return a == b
            if not all(equivalent(json.loads(path.read_text()), content) for path,content in outputs):
                parser.exit(1, 'Engine example differs from committed fixture. Regenerate it.\n')
            print('Committed engine example matches regeneration')
        else:
            for path,content in outputs:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(content, indent=2, allow_nan=False) + '\n')
                print(f'Wrote {path}')

if __name__ == '__main__':
    main()
