#!/usr/bin/env python3
"""Turn public current data into the tables the sailing view loads (docs/weather/sailing-currents.md).

    netcdf IN.nc OUT.csv [--bbox W,S,E,N] [--stride N] [--depth-index K] [--kind total|tidal] [--source TEXT]
        A gridded current field in NetCDF with CF conventions - e.g. a Copernicus Marine Service subset
        (variables uo and vo, m/s; coordinates time, latitude, longitude, optionally depth) - to the viewer's CSV
        (time_utc, lat, lon, u, v). Reads NetCDF-4 with xarray or netCDF4 when installed, NetCDF-3 with scipy.
    noaa-merge PREDICTIONS.json METADATA.json OUT.json
        NOAA CO-OPS current predictions (product=currents_predictions, JSON) with the station position added from
        the station metadata (mdapi stations/<id>.json), so that the viewer can place the station.
    noaa-urls STATION BEGIN END [--bin N] [--interval 6|h|30|MAX_SLACK]
        Print the NOAA CO-OPS URLs for the predictions and the station metadata (BEGIN, END as YYYYMMDD).

Copernicus Marine data: free with registration, licence requires attribution ("E.U. Copernicus Marine Service
Information"). NOAA CO-OPS predictions: public domain (U.S. Government work).
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import sys
from pathlib import Path

EPOCH = dt.datetime(1970, 1, 1, tzinfo=dt.timezone.utc)
UNITS = {'seconds': 1, 'second': 1, 's': 1, 'minutes': 60, 'minute': 60, 'hours': 3600, 'hour': 3600, 'h': 3600, 'days': 86400, 'day': 86400, 'd': 86400}


def cf_times(values, units: str) -> list[dt.datetime]:
    """CF time coordinate ('hours since 1950-01-01 00:00:00') to UTC datetimes."""
    try:
        step, origin = units.split(' since ')
        scale = UNITS[step.strip().lower()]
    except (ValueError, KeyError) as error:
        raise ValueError(f'Unsupported time units "{units}"') from error
    text = origin.strip().replace('T', ' ').replace('Z', '')
    for fmt in ('%Y-%m-%d %H:%M:%S', '%Y-%m-%d %H:%M', '%Y-%m-%d'):
        try:
            start = dt.datetime.strptime(text[:19] if fmt.endswith('%S') else text[:16] if fmt.endswith('%M') else text[:10], fmt).replace(tzinfo=dt.timezone.utc)
            break
        except ValueError:
            continue
    else:
        raise ValueError(f'Unsupported time origin "{origin}"')
    return [start + dt.timedelta(seconds=float(v) * scale) for v in values]


def open_netcdf(path: Path):
    """The variables of a NetCDF file as {name: (dims, values(list/array), attrs)}; xarray, netCDF4 or scipy."""
    try:
        import xarray  # type: ignore
        ds = xarray.open_dataset(path, decode_times=False)
        return {k: (tuple(v.dims), v.values, dict(v.attrs)) for k, v in {**ds.coords, **ds.data_vars}.items()}
    except ImportError:
        pass
    try:
        import netCDF4  # type: ignore
        ds = netCDF4.Dataset(path)
        ds.set_auto_mask(True)
        return {k: (tuple(v.dimensions), v[:], {a: v.getncattr(a) for a in v.ncattrs()}) for k, v in ds.variables.items()}
    except ImportError:
        pass
    try:
        from scipy.io import netcdf_file  # type: ignore
    except ImportError as error:
        raise SystemExit('Reading NetCDF needs xarray, netCDF4 or scipy (NetCDF-3 only): pip install xarray netCDF4') from error
    ds = netcdf_file(path, 'r', mmap=False)
    out = {}
    for k, v in ds.variables.items():
        attrs = {a: (val.decode() if isinstance(val, bytes) else val) for a, val in v._attributes.items()}
        data = v[:].copy().astype(float)
        for key in ('_FillValue', 'missing_value'):
            if key in attrs:
                data[data == attrs[key]] = math.nan
        if 'scale_factor' in attrs or 'add_offset' in attrs:
            data = data * float(attrs.get('scale_factor', 1)) + float(attrs.get('add_offset', 0))
        out[k] = (tuple(v.dimensions), data, attrs)
    return out


def pick(variables, *names, standard=()):
    for n in names:
        if n in variables:
            return n
    for k, (_, _, attrs) in variables.items():
        if attrs.get('standard_name') in standard:
            return k
    return None


def netcdf_to_rows(variables, bbox=None, stride=1, depth_index=0):
    """Yield (time, lat, lon, u, v) from the variables of a CF current field (u, v m/s)."""
    un = pick(variables, 'uo', 'u', 'water_u', 'east_velocity', standard=('eastward_sea_water_velocity', 'surface_eastward_sea_water_velocity'))
    vn = pick(variables, 'vo', 'v', 'water_v', 'north_velocity', standard=('northward_sea_water_velocity', 'surface_northward_sea_water_velocity'))
    tn, la, lo = pick(variables, 'time', 't'), pick(variables, 'latitude', 'lat', 'nav_lat'), pick(variables, 'longitude', 'lon', 'nav_lon')
    if not (un and vn and tn and la and lo):
        raise ValueError('Need current components (uo/vo or CF eastward/northward_sea_water_velocity), time, latitude and longitude.')
    dims, u, uattrs = variables[un]
    _, v, _ = variables[vn]
    times = cf_times(list(variables[tn][1]), variables[tn][2].get('units', 'seconds since 1970-01-01'))
    lats, lons = list(variables[la][1]), list(variables[lo][1])
    scale = 0.01 if str(uattrs.get('units', 'm s-1')).strip().lower() in ('cm/s', 'cm s-1') else 1.0
    order = [d for d in dims]
    ti, yi, xi = order.index(tn) if tn in order else 0, order.index(la) if la in order else len(order) - 2, order.index(lo) if lo in order else len(order) - 1
    di = next((k for k, d in enumerate(order) if d not in (tn, la, lo)), None)
    for t, when in enumerate(times):
        for y in range(0, len(lats), stride):
            for x in range(0, len(lons), stride):
                lat, lon = float(lats[y]), float(lons[x])
                lon = ((lon + 180) % 360) - 180
                if bbox and not (bbox[0] <= lon <= bbox[2] and bbox[1] <= lat <= bbox[3]):
                    continue
                index = [0] * len(order)
                index[ti], index[yi], index[xi] = t, y, x
                if di is not None:
                    index[di] = depth_index
                uu, vv = float(u[tuple(index)]) * scale, float(v[tuple(index)]) * scale
                if math.isnan(uu) or math.isnan(vv) or abs(uu) > 20 or abs(vv) > 20:
                    continue
                yield when, lat, lon, uu, vv


def write_csv(rows, out: Path, kind='total', source='') -> int:
    n = 0
    with out.open('w') as f:
        f.write(f'# kind: {kind}\n')
        if source:
            f.write(f'# source: {source}\n')
        f.write('time_utc,lat,lon,u,v\n')
        for when, lat, lon, u, v in rows:
            f.write(f'{when.strftime("%Y-%m-%dT%H:%MZ")},{lat:.5f},{lon:.5f},{u:.4f},{v:.4f}\n')
            n += 1
    return n


def noaa_merge(predictions: dict, metadata: dict) -> dict:
    """Add the station position (and id, name) from the mdapi metadata to the predictions JSON."""
    if 'current_predictions' not in predictions:
        raise ValueError('Not NOAA current predictions (no current_predictions).')
    station = (metadata.get('stations') or [metadata])[0]
    lat, lng = station.get('lat'), station.get('lng', station.get('lon'))
    if lat is None or lng is None:
        raise ValueError('No lat/lng in the station metadata.')
    return {**predictions, 'lat': float(lat), 'lng': float(lng), 'station': {'id': station.get('id'), 'name': station.get('name'), 'lat': float(lat), 'lng': float(lng)}}


def noaa_urls(station: str, begin: str, end: str, bin_: str | None = None, interval: str = '6') -> list[str]:
    data = ('https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=currents_predictions'
            f'&station={station}&begin_date={begin}&end_date={end}&time_zone=gmt&units=english&interval={interval}&format=json&application=terraflux')
    if bin_:
        data += f'&bin={bin_}'
    return [data, f'https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations/{station}.json']


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    a = sub.add_parser('netcdf')
    a.add_argument('input', type=Path)
    a.add_argument('output', type=Path)
    a.add_argument('--bbox', help='west,south,east,north')
    a.add_argument('--stride', type=int, default=1)
    a.add_argument('--depth-index', type=int, default=0)
    a.add_argument('--kind', choices=('total', 'tidal'), default='total')
    a.add_argument('--source', default='E.U. Copernicus Marine Service Information')
    b = sub.add_parser('noaa-merge')
    b.add_argument('predictions', type=Path)
    b.add_argument('metadata', type=Path)
    b.add_argument('output', type=Path)
    c = sub.add_parser('noaa-urls')
    c.add_argument('station')
    c.add_argument('begin')
    c.add_argument('end')
    c.add_argument('--bin')
    c.add_argument('--interval', default='6')
    args = p.parse_args(argv)
    if args.command == 'netcdf':
        bbox = tuple(float(x) for x in args.bbox.split(',')) if args.bbox else None
        n = write_csv(netcdf_to_rows(open_netcdf(args.input), bbox, max(1, args.stride), args.depth_index), args.output, args.kind, args.source)
        print(f'{n} rows -> {args.output}')
    elif args.command == 'noaa-merge':
        merged = noaa_merge(json.loads(args.predictions.read_text()), json.loads(args.metadata.read_text()))
        args.output.write_text(json.dumps(merged) + '\n')
        print(f'{len(merged["current_predictions"]["cp"])} predictions at {merged["lat"]}, {merged["lng"]} -> {args.output}')
    else:
        print('\n'.join(noaa_urls(args.station, args.begin, args.end, args.bin, args.interval)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
