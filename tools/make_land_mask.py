"""Print (or write into a request) a coarse land mask: one string per row, north first, '#' land and '.' sea.

The cells are sampled at their centres with the `global-land-mask` package (MIT licence, derived from the GLOBE
1 km elevation data of NOAA), which must be installed separately (pip install global-land-mask); nothing else in
TerraFlux needs it. Used for the map of examples/cosmic-dust-request.json:

    python3 tools/make_land_mask.py --cells-east 72 --cells-north 36 --request examples/cosmic-dust-request.json
"""
import argparse
import json
from pathlib import Path
import sys


def land_rows(cells_east, cells_north):
    from global_land_mask import globe   # imported here: only this tool needs it
    rows = []
    for row in range(cells_north):
        lat = 90.0 - (row + 0.5) * 180.0 / cells_north
        rows.append(''.join('#' if globe.is_land(lat, -180.0 + (col + 0.5) * 360.0 / cells_east) else '.' for col in range(cells_east)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cells-east', type=int, default=72)
    parser.add_argument('--cells-north', type=int, default=36)
    parser.add_argument('--request', type=Path, help='write the mask into this request (map.land_mask) instead of printing it')
    args = parser.parse_args()
    try:
        rows = land_rows(args.cells_east, args.cells_north)
    except ImportError:
        print('error: install the global-land-mask package (pip install global-land-mask)', file=sys.stderr)
        return 1
    if args.request:
        request = json.loads(args.request.read_text())
        request['map'].update(cells_east=args.cells_east, cells_north=args.cells_north, land_mask=rows,
                              land_mask_source='global-land-mask 1.0.0 (MIT; GLOBE 1 km elevation, NOAA), sampled at the cell centres by tools/make_land_mask.py')
        args.request.write_text(json.dumps(request, indent=2, ensure_ascii=False) + '\n')
        print(f'Wrote the land mask into {args.request}')
    else:
        print('\n'.join(rows))
    return 0


if __name__ == '__main__':
    sys.exit(main())
