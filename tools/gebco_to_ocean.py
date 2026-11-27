#!/usr/bin/env python3
"""Sample GEBCO onto an ocean request grid; preserve missing coverage and provenance.

Writes a bathymetry document. Optional --ocean-output writes a runnable copy of
--request only with complete coverage. Requires tools/requirements-relief.txt.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import numpy as np


def cell_coordinates(grid):
    nx, ny = grid['nx'], grid['ny']
    if type(nx) is not int or type(ny) is not int or min(nx, ny) < 1 or nx * ny > 100000:
        raise ValueError('Target grid must contain 1..100,000 cells')
    keys = ('origin_east_m', 'origin_north_m', 'spacing_east_m', 'spacing_north_m',
            'geographic_anchor_latitude_deg', 'geographic_anchor_longitude_deg')
    if any(type(grid[k]) not in (int, float) or not math.isfinite(grid[k]) for k in keys):
        raise ValueError('Grid coordinates must be finite numbers')
    if min(grid['spacing_east_m'], grid['spacing_north_m']) <= 0:
        raise ValueError('Grid spacing must be positive')
    lat0, lon0 = grid['geographic_anchor_latitude_deg'], grid['geographic_anchor_longitude_deg']
    if abs(lat0) >= 85 or not -180 <= lon0 <= 180:
        raise ValueError('Local ocean projection requires anchor within 85 degrees latitude and +/-180 longitude')
    # Matches run_ocean_circulation.cpp air/sea forcing coordinates, not the forecast's azimuthal projection.
    cells = np.arange(nx * ny)
    lat = lat0 + (grid['origin_north_m'] + (cells // nx + 0.5) * grid['spacing_north_m']) / 111195.0
    lon = lon0 + (grid['origin_east_m'] + (cells % nx + 0.5) * grid['spacing_east_m']) / (111195.0 * math.cos(math.radians(lat0)))
    if np.any(np.abs(lat) > 90):
        raise ValueError('Target cells cross a pole')
    return lat, (lon + 180) % 360 - 180


def nearest_axis(raw, targets, longitude=False):
    if np.ma.getmaskarray(raw).any():
        raise ValueError('Missing coordinate values')
    raw = np.asarray(raw, dtype=float)
    if raw.ndim != 1 or raw.size < 2 or not np.isfinite(raw).all():
        raise ValueError('Expected finite one-dimensional coordinate axes')
    delta = np.diff(raw)
    step = abs(float(delta[0]))
    if step <= 0 or not np.allclose(delta, delta[0], atol=step * 0.001, rtol=0):
        raise ValueError('Expected regular monotonic coordinate axes')
    if longitude and abs(raw[-1] - raw[0]) >= 360:
        raise ValueError('Longitude grid contains duplicate seam or spans more than one world')
    coords = (raw + 180) % 360 - 180 if longitude else raw
    order = np.argsort(coords); coords = coords[order]
    at = np.searchsorted(coords, targets)
    if longitude:
        lo, hi = (at - 1) % len(coords), at % len(coords)
        distance = lambda idx: np.abs((coords[idx] - targets + 180) % 360 - 180)
    else:
        if np.any(np.abs(raw) > 90): raise ValueError('Invalid latitude axis')
        lo, hi = np.clip(at - 1, 0, len(coords) - 1), np.clip(at, 0, len(coords) - 1)
        distance = lambda idx: np.abs(coords[idx] - targets)
    chosen = np.where(distance(lo) < distance(hi), lo, hi)
    return order[chosen], distance(chosen) <= step / 2 + step * 0.001


def sample(path, request, source):
    from netCDF4 import Dataset
    if request.get('schema_version') != 'terraflux.ocean.circulation-run/v1':
        raise ValueError('Expected an ocean circulation request')
    if not source.strip(): raise ValueError('Source attribution is required')
    grid = request['grid']; lat, lon = cell_coordinates(grid)
    with Dataset(path) as ds:
        latitude, longitude, elevation = (ds.variables[n] for n in ('lat', 'lon', 'elevation'))
        if latitude.dimensions != ('lat',) or longitude.dimensions != ('lon',) or elevation.dimensions != ('lat', 'lon'):
            raise ValueError('Expected elevation(lat, lon) with coordinate axes')
        if getattr(latitude, 'units', '') != 'degrees_north' or getattr(longitude, 'units', '') != 'degrees_east':
            raise ValueError('Expected geographic degree coordinates')
        if getattr(elevation, 'units', '') not in ('m', 'metres', 'meters') or getattr(elevation, 'positive', 'up') != 'up':
            raise ValueError('Expected metre elevations positive upward')
        ys, y_valid = nearest_axis(latitude[:], lat)
        xs, x_valid = nearest_axis(longitude[:], lon, True)
        values = [None] * len(lat)
        valid = x_valid & y_valid
        for y in np.unique(ys[valid]):
            cells = np.flatnonzero(valid & (ys == y))
            row = np.ma.asarray(elevation[int(y), xs[cells]], dtype=float)
            for cell, value in zip(cells, row):
                if np.ma.is_masked(value) or not np.isfinite(value): continue
                if not -12000 <= value <= 10000: raise ValueError('Elevation outside -12000..10000 m')
                values[int(cell)] = float(value)
    return dict(schema='terraflux.regional-bathymetry/v1', grid=copy.deepcopy(grid), units='m',
                vertical_reference='mean_sea_level', projection='ocean_local_linear', source=source,
                sampling='nearest_cell', latitude_deg=lat.tolist(), longitude_deg=lon.tolist(),
                elevation_m=values, coverage_mask=[int(v is not None) for v in values],
                ocean_mask=[None if v is None else int(v <= 0) for v in values],
                depth_m=[None if v is None else max(0, -v) for v in values])


def ocean_request(request, bathymetry):
    if request['grid'] != bathymetry['grid']: raise ValueError('Bathymetry grid differs from request')
    if any(v is None for v in bathymetry['elevation_m']):
        raise ValueError('Incomplete bathymetry coverage; runnable request not written')
    result = copy.deepcopy(request)
    basin = result.setdefault('basin', {})
    basin.pop('ocean_mask', None)
    basin['sea_level_m'] = 0
    basin['terrain'] = dict(values=bathymetry['elevation_m'], source=bathymetry['source'],
                            vertical_reference='mean_sea_level', projection='ocean_local_linear')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', help='Local NetCDF or HTTPS OPeNDAP URL')
    parser.add_argument('output', type=Path, help='Regional bathymetry JSON')
    parser.add_argument('--request', type=Path, required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--ocean-output', type=Path)
    args = parser.parse_args()
    try:
        if args.output.resolve() == args.request.resolve() or (args.ocean_output and args.ocean_output.resolve() in (args.request.resolve(), args.output.resolve())):
            raise ValueError('Use separate input and output paths')
        request = json.loads(args.request.read_text())
        bathymetry = sample(args.input, request, args.source)
        args.output.write_text(json.dumps(bathymetry, allow_nan=False) + '\n')
        if args.ocean_output:
            result = ocean_request(request, bathymetry)
            args.ocean_output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    except (ValueError, OSError, ImportError, KeyError) as error:
        parser.exit(1, f'{error}\n')
    print(f"{sum(bathymetry['coverage_mask'])}/{len(bathymetry['coverage_mask'])} covered cells -> {args.output}")


if __name__ == '__main__':
    main()
