# Mobile weather response v1

The mobile response envelope combines the current temperature and wind forecast with a separate precipitation accumulation layer. Validate it with [mobile-weather-response-v1.schema.json](mobile-weather-response-v1.schema.json).

## Shape and alignment

- `forecast` contains the existing [temperature forecast v1](temperature-forecast-v1.schema.json), including grid geometry, timestamps, model fields, and observation update counts.
- `precipitation_accumulation` carries a rainfall depth layer in millimetres, with `period_start_utc` and `valid_time_utc` marking the integrated interval.
- Both accumulation arrays use the forecast grid's row-major order: `index = y * nx + x`. Each array must contain exactly `nx * ny` entries and use the exact same grid geometry and coordinate system.
- `quality` is parallel to `values`. Decoded cells contain a number; `no_data` and `ambiguous` cells contain JSON `null`. Never render these missing cells as zero rainfall.
- The response records the trapezoidal integration method and may include the source and derivation strings from the accumulation observations.

## Model boundary

The accumulation comes from the separate `terraflux-accumulate-rainfall` pipeline described in [precipitation accumulation](precipitation-accumulation.md). It remains an observation-derived layer and is not mixed into the dry temperature transport solver.

## File composer

Run with Python 3.10+ (standard library only):

```sh
python3 tools/compose_mobile_weather.py forecast.json gridded-rainfall.json mobile-response.json
python3 -m unittest discover -s tests/tools -v
```

`gridded-rainfall.json` is an object with exactly three keys:
- `schema_version`: `terraflux.weather.gridded_accumulation/v1`
- `grid`: the complete forecast grid geometry, coordinate system, and array order
- `precipitation_accumulation`: the layer defined above

The composer checks exact grid equality, array sizes, finite physical values, units, and quality/value consistency. The accumulation interval must start at forecast.start_time_utc and end at that time plus forecast.elapsed_seconds. Invalid input does not replace the destination; successful writes replace it atomically.

The accumulator CLI emits geographic observation records, **not** this gridded wrapper. Map those observations using the existing C++ spherical azimuthal-equidistant projection:

```sh
./build/terraflux-grid-rainfall forecast.json accumulation.json gridded-rainfall.json
python3 tools/compose_mobile_weather.py forecast.json gridded-rainfall.json mobile-response.json
```

The mapper checks a common source/derivation and the forecast interval, assigns each geographic observation to its containing cell, leaves uncovered cells as `no_data`, and marks multiple observations in a cell `ambiguous` without averaging. Out-of-domain observations are counted on stderr. This is conservative point assignment, not area-weighted radar resampling; dense input on a coarse grid can therefore produce many ambiguous cells. The gridding CLI supports up to one million cells; the current viewer limit is 65,536. Open `mobile-response.json` in the viewer to inspect the combined result. This composer provides no network API. Full JSON Schema validation remains a separate consumer check.
