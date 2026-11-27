#!/usr/bin/env python3
"""Sample a global GEBCO elevation NetCDF into the viewer's bounded relief JSON.

Requires numpy and netCDF4. Only sampled rows/columns are read, not the full global raster.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def axis_indices(values, size, span, longitude=False):
    values = np.asarray(values, dtype=float)
    if values.ndim != 1 or len(values) < 2 or not np.isfinite(values).all():
        raise ValueError('Coordinates must be finite one-dimensional axes')
    if longitude:
        values = (values + 180) % 360 - 180
    order = np.argsort(values)
    sorted_values = values[order]
    step = span / len(values)
    expected = -span / 2 + (np.arange(len(values)) + 0.5) * step
    if not np.allclose(sorted_values, expected, atol=step * 0.001, rtol=0):
        raise ValueError('Expected a complete, regular, global cell-centred grid; subsets are unsupported')
    indices = np.minimum(len(values) - 1, np.floor((np.arange(size) + 0.5) * len(values) / size).astype(int))
    return order[indices]


def convert(path, nx, ny, source):
    from netCDF4 import Dataset
    if nx < 2 or ny < 2 or nx * ny > 1000000:
        raise ValueError('Output grid must have at least 2 cells per axis and at most 1,000,000 cells')
    if not source.strip():
        raise ValueError('Source attribution including dataset version is required')
    with Dataset(path) as ds:
        for name in ('lat', 'lon', 'elevation'):
            if name not in ds.variables:
                raise ValueError(f'Missing GEBCO variable: {name}')
        lat, lon, elevation = (ds.variables[name] for name in ('lat', 'lon', 'elevation'))
        if lat.dimensions != ('lat',) or lon.dimensions != ('lon',) or elevation.dimensions != ('lat', 'lon'):
            raise ValueError('Expected elevation(lat, lon) with lat/lon coordinate axes')
        if getattr(lat, 'units', '') != 'degrees_north' or getattr(lon, 'units', '') != 'degrees_east':
            raise ValueError('Expected latitude/longitude in degrees_north/degrees_east')
        if getattr(elevation, 'units', '') not in ('m', 'metres', 'meters'):
            raise ValueError('Elevation must be in metres')
        if getattr(elevation, 'positive', 'up') != 'up':
            raise ValueError('Elevation must be positive upward')
        raw_lat, raw_lon = lat[:], lon[:]
        if np.ma.getmaskarray(raw_lat).any() or np.ma.getmaskarray(raw_lon).any():
            raise ValueError('Coordinate axes cannot contain missing values')
        ys = axis_indices(raw_lat, ny, 180)
        xs = axis_indices(raw_lon, nx, 360, longitude=True)
        values = []
        for y in ys:
            # netCDF4 orthogonal indexing loads only requested positions, in target order.
            row = np.ma.asarray(elevation[int(y), xs], dtype=float)
            for value in row:
                if np.ma.is_masked(value) or not np.isfinite(value):
                    values.append(None)
                elif not -12000 <= value <= 10000:
                    raise ValueError('Elevation outside viewer range (-12000..10000 m)')
                else:
                    values.append(float(value))
    return dict(schema='terraflux.global-relief/v1', nx=nx, ny=ny, units='m',
                datum='mean_sea_level', row_order='south_to_north', source=source,
                sampling='nearest_cell', elevation_m=values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--nx', type=int, default=360)
    parser.add_argument('--ny', type=int, default=180)
    parser.add_argument('--source', required=True, help='Dataset version and attribution (preserved in output)')
    args = parser.parse_args()
    try:
        document = convert(args.input, args.nx, args.ny, args.source)
        args.output.write_text(json.dumps(document, allow_nan=False) + '\n')
    except (ValueError, OSError, ImportError) as error:
        parser.exit(1, f'{error}\n')
    print(f'{args.nx} x {args.ny} relief -> {args.output}')


if __name__ == '__main__':
    main()
