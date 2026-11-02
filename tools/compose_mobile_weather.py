#!/usr/bin/env python3
"""Compose a forecast and explicitly gridded rainfall layer (stdlib only)."""
import argparse
import datetime as dt
import json
import math
import os
from pathlib import Path
import tempfile

GRID_KEYS = {'nx', 'ny', 'origin_east_m', 'origin_north_m', 'spacing_east_m',
             'spacing_north_m', 'geographic_anchor_longitude_deg',
             'geographic_anchor_latitude_deg', 'coordinate_system', 'array_order'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def timestamp(value):
    require(isinstance(value, str) and value.endswith('Z'), 'timestamps must use UTC Z')
    return dt.datetime.fromisoformat(value[:-1] + '+00:00')


def compose(forecast, rainfall):
    require(set(forecast) == {'schema_version', 'start_time_utc', 'elapsed_seconds', 'grid',
                              'observation_updates', 'fields'}, 'invalid forecast keys')
    require(set(rainfall) == {'schema_version', 'grid', 'precipitation_accumulation'},
            'invalid gridded accumulation keys')
    require(forecast['schema_version'] == 'terraflux.weather.forecast/v1', 'unsupported forecast')
    require(rainfall['schema_version'] == 'terraflux.weather.gridded_accumulation/v1',
            'unsupported gridded accumulation')
    grid = forecast['grid']
    require(set(grid) == GRID_KEYS, 'invalid grid keys')
    require(grid == rainfall['grid'], 'rainfall and forecast grids must match exactly')
    for key in ('nx', 'ny'):
        require(type(grid[key]) is int and grid[key] >= 2, 'grid dimensions must be integers >= 2')
    for key in GRID_KEYS - {'nx', 'ny', 'coordinate_system', 'array_order'}:
        require(number(grid[key]), 'grid geometry must be finite')
    require(grid['spacing_east_m'] > 0 and grid['spacing_north_m'] > 0, 'spacing must be positive')
    require(-180 <= grid['geographic_anchor_longitude_deg'] <= 180 and
            -90 <= grid['geographic_anchor_latitude_deg'] <= 90, 'invalid geographic anchor')
    require(grid['coordinate_system'] == 'local_azimuthal_equidistant_m' and
            grid['array_order'] == 'row_major_y_then_x', 'unsupported grid coordinates/order')
    count = grid['nx'] * grid['ny']
    elapsed = forecast['elapsed_seconds']
    require(number(elapsed) and elapsed >= 0, 'invalid forecast elapsed time')
    start = timestamp(forecast['start_time_utc'])
    fields = forecast['fields']
    require(set(fields) == {'air_temperature', 'wind_east', 'wind_north'}, 'invalid forecast fields')
    for name, units in [('air_temperature', 'K'), ('wind_east', 'm/s'), ('wind_north', 'm/s')]:
        field = fields[name]
        require(set(field) == {'units', 'values'}, 'invalid forecast field keys')
        require(field['units'] == units, 'invalid forecast units')
        require(isinstance(field['values'], list) and len(field['values']) == count,
                'forecast array length mismatch')
        require(all(number(v) and (name != 'air_temperature' or v > 0) for v in field['values']),
                'invalid forecast values')
    require(isinstance(forecast['observation_updates'], list), 'invalid observation updates')
    layer = rainfall['precipitation_accumulation']
    required = {'period_start_utc', 'valid_time_utc', 'units', 'method', 'values', 'quality'}
    require(required <= set(layer) <= required | {'source_id', 'derivation'}, 'invalid rainfall layer keys')
    require(timestamp(layer['period_start_utc']) == start, 'rainfall start must match forecast start')
    require(timestamp(layer['valid_time_utc']) == start + dt.timedelta(seconds=elapsed),
            'rainfall end must match forecast valid time')
    require(elapsed > 0, 'accumulation interval must be positive')
    require(layer['units'] == 'mm' and layer['method'] == 'trapezoidal_rate_integration',
            'unsupported rainfall units/method')
    require(isinstance(layer['values'], list) and isinstance(layer['quality'], list) and
            len(layer['values']) == len(layer['quality']) == count, 'rainfall array length mismatch')
    for value, quality in zip(layer['values'], layer['quality']):
        require(quality in ('decoded', 'no_data', 'ambiguous'), 'invalid rainfall quality')
        require((number(value) and value >= 0) if quality == 'decoded' else value is None,
                'rainfall value/quality mismatch')
    for key in ('source_id', 'derivation'):
        if key in layer:
            require(isinstance(layer[key], str) and bool(layer[key]), 'invalid rainfall provenance')
    return {'schema_version': 'terraflux.weather.mobile_response/v1',
            'forecast': forecast, 'precipitation_accumulation': layer}


def load(path):
    with path.open(encoding='utf-8') as source:
        return json.load(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('forecast', type=Path)
    parser.add_argument('rainfall', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    temporary = None
    try:
        result = compose(load(args.forecast), load(args.rainfall))
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=args.output.parent,
                                         delete=False) as target:
            temporary = Path(target.name)
            json.dump(result, target, indent=2, allow_nan=False)
            target.write('\n')
        os.replace(temporary, args.output)
    except (ValueError, KeyError, TypeError, OverflowError, OSError) as error:
        parser.exit(1, f'error: {error}\n')
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
