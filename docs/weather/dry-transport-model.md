# First weather solver contract: dry scalar transport v1

This contract defines the smallest weather process to implement next. It is a controlled transport model, not an operational forecast.

## Scope

The v1 model is a two-dimensional, single-layer, dry-air domain. It transports air temperature through a fixed horizontal wind field and applies a configurable horizontal mixing diffusivity:

`∂T/∂t + u ∂T/∂x + v ∂T/∂y = κ (∂²T/∂x² + ∂²T/∂y²)`

- `T`: air temperature in kelvin (K), the transported scalar.
- `u`: prescribed eastward wind in metres per second (m/s).
- `v`: prescribed northward wind in metres per second (m/s).
- `κ`: horizontal mixing diffusivity in square metres per second (m²/s).

The wind field is supplied in the initial state and remains fixed during a run. The model does not evolve wind or pressure. It does not include vertical motion, moisture, clouds, precipitation, radiation, or terrain effects.

## Grid and coordinates

- Uniform Cartesian grid in local east/north coordinates, measured in metres.
- `nx` and `ny` are each at least 2; grid spacing is positive in both directions.
- The origin is the southwest domain corner. Cell values are located at cell centers.
- Arrays are row-major: `index = y * nx + x`.
- Temperature and both wind arrays each contain exactly `nx * ny` finite values.

A map observation must be reprojected into this local metric frame before it initializes a solver cell. The coordinate conversion and observation-to-grid interpolation are not part of this contract.

## Time and boundaries

- The configuration supplies a UTC start timestamp, a fixed positive timestep in seconds, and a positive step count.
- The state records elapsed simulation time in seconds.
- Both horizontal axes use periodic boundary conditions in v1: values leaving one edge re-enter at the opposite edge.
- Periodic boundaries make small deterministic experiments straightforward; they do not represent open regional boundaries.

## Validation rules

- Grid dimensions, their product, and state-array lengths must be valid and consistent.
- Grid origins, spacing, timestep, diffusivity, state values, and elapsed time must be finite.
- Spacing and timestep must be greater than zero; diffusivity and elapsed time cannot be negative.
- Air temperature must be greater than 0 K.
- The v1 boundary condition is periodic on both axes.
- The numerical solver must add timestep stability checks after its discretization is selected.

The C++ contract types and validators are in `include/terraflux/weather/transport_model.hpp`; the JSON schemas are [configuration v1](dry-transport-config-v1.schema.json) and [state v1](dry-transport-state-v1.schema.json). The schemas define the format; the C++ validators check array lengths and numeric constraints.

## Next implementation boundary

Implement a deterministic update for this equation and verify conservation and limiting cases before adding dynamic wind, moisture, terrain, or surface exchange. Keep each new process behind an explicit model-version change.
