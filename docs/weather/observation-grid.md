# Observation fields on the dry transport grid

`map_weather_observations_to_grid` projects a homogeneous set of georeferenced weather observations onto the grid configured for dry transport. It creates an observation field; it does not turn one weather variable into another or fill the solver's state automatically.

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

To create a solver temperature field, supply observations whose variable is air temperature and whose units are K, then apply an explicit background or missing-data policy. No such assimilation policy is defined yet. The mapper itself preserves missing and ambiguous cells for that later decision.

## C++ API

~~~cpp
#include "terraflux/weather/observation_grid.hpp"

auto field = terraflux::weather::map_weather_observations_to_grid(
    observations, config, "radar_reflectivity", "dBZ");
~~~

`field.cells` is row-major with index `y * nx + x`. The grid and its geographic anchor are copied into the result so callers retain the exact mapping context.
