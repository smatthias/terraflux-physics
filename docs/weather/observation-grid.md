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
