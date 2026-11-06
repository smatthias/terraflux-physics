"""Print (or write into a request) a coarse land mask: one string per row, north first, '#' land and '.' sea.

The cells are sampled with the `global-land-mask` package (MIT licence, derived from the GLOBE 1 km elevation data of
NOAA), which must be installed separately (pip install global-land-mask); nothing else in TerraFlux needs it.

Two grids:

* a global equirectangular grid (the cosmic-dust, solar-wind and earth-climate maps), sampled at the cell centres:

      python3 tools/make_land_mask.py --cells-east 72 --cells-north 36 --request examples/cosmic-dust-request.json

* a regional grid in the local azimuthal-equidistant frame of an anchor (the earth-forecast request), centred on the
  anchor, with a cell counted as land when most of 3 x 3 sample points inside it are land:

      python3 tools/make_land_mask.py --cells-east 52 --cells-north 32 --spacing-m 210000 --anchor 40 -45 \\
          --request examples/earth-forecast-request.json --key surface.land_mask
"""
import argparse
import json
import math
from pathlib import Path
import sys

EARTH_RADIUS_M = 6.371e6


def local_to_geographic(x, y, lat0, lon0):
    """Inverse spherical azimuthal equidistant projection (as in the C++ kernel)."""
    rho = math.hypot(x, y)
    if rho < 1e-6:
        return lat0, lon0
    c = rho / EARTH_RADIUS_M
    p0 = math.radians(lat0)
    lat = math.asin(max(-1.0, min(1.0, math.cos(c) * math.sin(p0) + y * math.sin(c) * math.cos(p0) / rho)))
    lon = math.radians(lon0) + math.atan2(x * math.sin(c), rho * math.cos(p0) * math.cos(c) - y * math.sin(p0) * math.sin(c))
    return math.degrees(lat), (math.degrees(lon) + 540.0) % 360.0 - 180.0


def land_rows(cells_east, cells_north):
    from global_land_mask import globe   # imported here: only this tool needs it
    rows = []
    for row in range(cells_north):
        lat = 90.0 - (row + 0.5) * 180.0 / cells_north
        rows.append(''.join('#' if globe.is_land(lat, -180.0 + (col + 0.5) * 360.0 / cells_east) else '.' for col in range(cells_east)))
    return rows


def regional_rows(cells_east, cells_north, spacing_m, lat0, lon0):
    from global_land_mask import globe
    rows = []
    for row in range(cells_north):
        j = cells_north - 1 - row
        line = []
        for i in range(cells_east):
            land = 0
            for a in (-1 / 3, 0.0, 1 / 3):
                for b in (-1 / 3, 0.0, 1 / 3):
                    x = (i + 0.5 + a - cells_east / 2) * spacing_m
                    y = (j + 0.5 + b - cells_north / 2) * spacing_m
                    lat, lon = local_to_geographic(x, y, lat0, lon0)
                    land += bool(globe.is_land(lat, lon))
            line.append('#' if land >= 5 else '.')
        rows.append(''.join(line))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cells-east', type=int, default=72)
    parser.add_argument('--cells-north', type=int, default=36)
    parser.add_argument('--spacing-m', type=float, help='regional grid: cell size (with --anchor)')
    parser.add_argument('--anchor', type=float, nargs=2, metavar=('LAT', 'LON'), help='regional grid: centre of the local frame')
    parser.add_argument('--request', type=Path, help='write the mask into this request instead of printing it')
    parser.add_argument('--key', default='map.land_mask', help='where in the request (dot path; default map.land_mask)')
    args = parser.parse_args()
    if (args.spacing_m is None) != (args.anchor is None):
        parser.error('--spacing-m and --anchor go together')
    try:
        if args.anchor:
            rows = regional_rows(args.cells_east, args.cells_north, args.spacing_m, args.anchor[0], args.anchor[1])
        else:
            rows = land_rows(args.cells_east, args.cells_north)
    except ImportError:
        print('error: install the global-land-mask package (pip install global-land-mask)', file=sys.stderr)
        return 1
    if args.request:
        request = json.loads(args.request.read_text())
        *parents, leaf = args.key.split('.')
        target = request
        for part in parents:
            target = target.setdefault(part, {})
        if args.key == 'map.land_mask':
            target.update(cells_east=args.cells_east, cells_north=args.cells_north)
        target[leaf] = rows
        target[leaf.replace('land_mask', 'land_mask_source')] = ('global-land-mask 1.0.0 (MIT; GLOBE 1 km elevation, NOAA), '
                                                                  + ('majority of 3 x 3 points per cell' if args.anchor else 'sampled at the cell centres')
                                                                  + ' by tools/make_land_mask.py')
        args.request.write_text(json.dumps(request, indent=2, ensure_ascii=False) + '\n')
        print(f'Wrote the land mask into {args.request} ({args.key})')
    else:
        print('\n'.join(rows))
    return 0


if __name__ == '__main__':
    sys.exit(main())
