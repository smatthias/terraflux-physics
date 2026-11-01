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

The current command-line programs still emit their own local JSON files. A mobile adapter or service must compose those outputs into this envelope and verify that the grids, coordinate systems, and interval timestamps align. This contract does not imply that a mobile API or automatic composer is implemented.
