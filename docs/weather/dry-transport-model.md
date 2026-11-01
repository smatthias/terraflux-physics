# First weather solver contract: dry scalar transport v1

This contract defines the smallest weather process to implement next. It is a controlled transport model, not an operational forecast.

## Scope

The v1 model is a two-dimensional, single-layer, dry-air domain. It transports air temperature through a fixed horizontal wind field and applies a configurable horizontal mixing diffusivity. The update is conservative in flux form:

`∂T/∂t + ∂(uT)/∂x + ∂(vT)/∂y = κ (∂²T/∂x² + ∂²T/∂y²)`

- `T`: air temperature in kelvin (K), the transported scalar.
- `u`: prescribed eastward wind in metres per second (m/s).
- `v`: prescribed northward wind in metres per second (m/s).
- `κ`: horizontal mixing diffusivity in square metres per second (m²/s).

Wind is supplied in the initial state and remains fixed during a run. It must be discretely divergence-free on the periodic grid. The model does not evolve wind or pressure. It excludes vertical motion, moisture, clouds, precipitation, radiation, and terrain effects.

## Grid and coordinates

- Uniform Cartesian grid in local east/north coordinates, measured in metres.
- `nx` and `ny` are each at least 2; grid spacing is positive in both directions.
- The origin is the southwest domain corner. Cell values are located at cell centers.
- Arrays are row-major: `index = y * nx + x`.
- Temperature and both wind arrays each contain exactly `nx * ny` finite values.

A map observation must be reprojected into this local metric frame before it initializes a solver cell. The coordinate conversion and observation-to-grid interpolation are not part of this contract.

## Numerical update

For each timestep, the solver computes periodic face winds by averaging adjacent cell-centered wind values. It uses first-order upwind fluxes for advection and a centered second-order Laplacian for diffusion. The wind-divergence constraint preserves a constant temperature field and lets periodic face fluxes conserve the grid sum, up to floating-point roundoff.

Before advancing, it checks the combined monotonicity/stability bound for every cell:

`Δt [ (max(u_e,0)+max(-u_w,0))/Δx + (max(v_n,0)+max(-v_s,0))/Δy + 2κ(1/Δx²+1/Δy²) ] ≤ 1`

Here `u_e`, `u_w`, `v_n`, and `v_s` are the averaged velocities on a cell's four faces. If any cell exceeds the bound, the solver rejects the configuration instead of substepping or silently changing the requested timestep.

## Time and boundaries

- The configuration supplies a UTC start timestamp, a fixed positive timestep in seconds, and a positive step count.
- The state records elapsed simulation time in seconds; the advance function applies all configured steps.
- Both horizontal axes use periodic boundaries: values leaving one edge re-enter at the opposite edge.
- Periodic boundaries make small deterministic experiments straightforward; they do not represent open regional boundaries.

## Validation rules

- Grid dimensions, their product, and state-array lengths must be valid and consistent.
- Grid origins, spacing, timestep, diffusivity, state values, and elapsed time must be finite.
- Spacing and timestep must be greater than zero; diffusivity and elapsed time cannot be negative.
- Air temperature must be greater than 0 K; the wind field must satisfy the discrete divergence constraint.
- The v1 boundary condition is periodic on both axes.
- Solver updates reject non-finite or non-positive temperatures.

The C++ contract types, validators, and `advance_dry_transport` are in `include/terraflux/weather/transport_model.hpp` and `src/weather/`. The JSON schemas are [configuration v1](dry-transport-config-v1.schema.json) and [state v1](dry-transport-state-v1.schema.json).

## Scope limits

This is a transport baseline, not a complete atmospheric model: temperature is advected as a scalar, wind is prescribed, and there is no pressure, vertical structure, moisture, terrain coupling, or open-boundary forcing. Keep each new process behind an explicit model-version change.
