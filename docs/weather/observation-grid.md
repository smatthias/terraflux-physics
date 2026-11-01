# Observation fields on the dry transport grid

`map_weather_observations_to_grid` projects a homogeneous set of georeferenced weather observations onto the grid configured for dry transport. It creates an observation field while preserving quality and source-index provenance.

## Coordinate conversion

The transport grid uses local east/north metres in a spherical azimuthal-equidistant projection centered at `geographic_anchor_longitude_deg` and `geographic_anchor_latitude_deg`. The mapper currently accepts source locations in `EPSG:4326` only and uses a mean Earth radius of 6,371,008.8 m. Other input projections are rejected until an explicit transform is added.

The projected domain is divided into the configured uniform cells. A point is assigned to the cell containing it; the east and north domain maxima are exclusive. Observations outside the domain are counted and omitted from cells.

## Variable and quality handling

- One mapping call accepts one variable, one unit, and one valid time. Mixed values are rejected.
- Each cell retains an optional numeric value, observation status, reason, and observation count.
- Cells with no in-domain observation remain `no_data` with a missing-observation reason.
- A single decoded value is retained. Explicit `no_data` and `ambiguous` statuses remain nonnumeric.
- If multiple observations land in one cell, that cell becomes `ambiguous`; the mapper does not average them. Source observation indices remain available through `source_offsets` and `source_observation_indices`.
- dBZ remains radar reflectivity. It is never interpreted as air temperature in kelvin.

## Initializing temperature

`overlay_air_temperature_observations` applies the explicit v1 initialization policy:

1. Start with a complete `DryTransportState` at elapsed time zero. Its temperature and wind arrays must already cover every grid cell and pass the transport-state validation.
2. Supply a gridded field with variable `air_temperature`, units `K`, the same grid and local coordinate system as the config, and a valid time exactly equal to `config.start_time_utc`.
3. The function copies the background state and replaces temperatures only where a cell has exactly one decoded, finite value greater than 0 K.
4. `no_data` and `ambiguous` cells keep the background temperature. The returned counts report applied observation cells, retained background cells, and the two retained-status counts separately.

The background input is not mutated. The original gridded observation field remains available with its quality flags and source indices. The overlay rejects radar reflectivity, other variables or units, time mismatches, grid mismatches, and malformed provenance instead of guessing a conversion, interpolating in time, or averaging collisions.




## Temperature raster maps

The temperature map decoder accepts the shared v1 sidecar layout only when its observation declaration is `air_temperature` with units `K`. Palette values are interpreted as kelvin, validated as finite and greater than zero, and emitted as canonical observations. PNG and ASCII PPM P3 are supported. Unlisted pixel colors remain ambiguous; no-data and ambiguous palette entries carry no numeric value.

Decode the included synthetic example with:

~~~sh
terraflux-decode-temperature-map \
  examples/temperature-map-sidecar.json \
  examples/temperature-map.ppm \
  /tmp/temperature-observations.json
~~~

The result can be passed to `advance_dry_transport_from_observation_json`. Radar sidecars continue through the separate reflectivity decoder and are rejected if sent to the temperature pipeline.

Run the decoded map directly through the model with a config/background request:

~~~sh
terraflux-run-temperature-map \
  examples/temperature-map-sidecar.json \
  examples/temperature-map.ppm \
  examples/temperature-run-request.json \
  /tmp/temperature-forecast.json
~~~

The request schema is `terraflux.weather.temperature-run/v1`. It contains the model start time, timestep and step count, diffusivity, periodic boundary setting, grid geometry/projection anchor, and complete initial temperature/east-wind/north-wind arrays in row-major order. The forecast output uses `terraflux.weather.forecast/v1` and includes the grid plus updated temperature and wind fields.


## Canonical JSON to forecast

The canonical observation JSON reader reconstructs validated `WeatherObservation` records. `advance_dry_transport_from_observation_json` then reads the records, groups air-temperature values by valid time, maps each group, and runs the timed forecast.

~~~cpp
#include "terraflux/weather/temperature_observation_pipeline.hpp"

std::ifstream input("temperature-observations.json");
auto forecast =
    terraflux::weather::advance_dry_transport_from_observation_json(
        input, initial_state, config);
~~~

The existing PNG/PPM radar decoder writes radar-reflectivity records in dBZ. Those records are intentionally rejected by this temperature pipeline; a temperature map source must provide canonical `air_temperature` observations in K. No reflectivity-to-temperature conversion is performed.

## Updating a running forecast

`advance_dry_transport_with_temperature_observations` accepts an ordered list of temperature fields paired with elapsed seconds from `config.start_time_utc`. Each update must align with a configured timestep boundary and fall within the run, including its start or end. Its field valid time must match that boundary in UTC (`YYYY-MM-DDTHH:MM:SSZ`, optionally with up to nine fractional-second digits).

At each boundary, decoded cells replace the current forecast temperatures. Empty, no-data, and ambiguous cells keep their current forecast values. The update is applied before the next transport step; an update at the final boundary changes the returned final state without advancing it further. Updates must be strictly chronological and unique. The routine advances one existing transport step at a time, retaining the model's current stability and wind checks.

~~~cpp
auto updates = terraflux::weather::map_air_temperature_observations_to_updates(
    raw_air_temperature_observations, config);

auto forecast = terraflux::weather::advance_dry_transport_with_temperature_observations(
    initial_state, config, updates);
~~~

The adapter groups raw observations by UTC valid time, maps each group onto the transport grid, sorts updates chronologically, and retains source indices into the original input vector. It accepts only air temperature in K. The elapsed offset schedules each update; the field's UTC valid time is checked against the actual simulated time before values are applied. The simulation runner rejects timestamps outside the run or between timestep boundaries.

## C++ API

~~~cpp
#include "terraflux/weather/observation_grid.hpp"

auto field = terraflux::weather::map_weather_observations_to_grid(
    observations, config, "air_temperature", "K");

auto initialized =
    terraflux::weather::overlay_air_temperature_observations(
        complete_background_state, field, config);
~~~

`field.cells` is row-major with index `y * nx + x`. The grid and its geographic anchor are copied into the result so callers retain the exact mapping context.


## Tracking a sequence of temperature maps

A caller can supply timestamped map snapshots as a v1 series manifest. Each entry pairs a sidecar and raster; relative file paths are resolved from the manifest directory. The runner decodes every frame, groups observations by valid time, checks that each update falls on a configured timestep boundary, and applies the updates chronologically. The forecast output lists each update time and its decoded, no-data, ambiguous, and outside-domain cell counts.

~~~sh
terraflux-run-temperature-map \
  --series examples/temperature-map-series.json \
  examples/temperature-run-request.json \
  /tmp/temperature-track.json
~~~

The manifest format is defined in [temperature map series v1](temperature-map-series-v1.schema.json). The included series is synthetic and demonstrates the time-alignment contract. This interface accepts caller-supplied maps; it does not download provider data or imply permission to use a live feed. Confirm source terms and provide trustworthy sidecars before connecting an external provider.
