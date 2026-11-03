# Coupled wind / moist-layer model v1

This contract extends the [pressure-gradient wind model](pressure-wind-model.md) with the three fields that move with and feed back on the wind: air temperature, specific humidity and the air density diagnosed from them. It remains a single-layer controlled experiment over a prescribed pressure field, not an operational forecast.

## Scope and equations

Per cell the state is pressure `p` (static), wind `(u, v)`, temperature `T` and specific humidity `q`. Each timestep:

1. **Density diagnosis** — `ρ = p / (R_d T (1 + 0.608 q))` from the [moist-air relations](moist-air-relations.md). With `density_mode = "constant"` the configured `air_density_kg_m3` is used instead and `T`, `q` become passive tracers.
2. **Wind** — `∂u/∂t = −(1/ρ) ∂p/∂x + f v − k u + ν ∇²u`, `∂v/∂t = −(1/ρ) ∂p/∂y − f u − k v + ν ∇²v`. Coriolis (`f`) and friction (`k`) use the trapezoidal update of the pressure-wind model; the pressure-gradient force uses the per-cell density; momentum diffusion `ν ∇²` is explicit on the previous wind.
3. **Scalar transport** — `∂T/∂t + u ∂T/∂x + v ∂T/∂y = κ_T ∇²T` and the same for `q` with `κ_q`, using the updated wind, first-order upwind differences in advective form, and explicit diffusion.

Coupling directions in v1: wind → temperature and humidity (advection); temperature and humidity → density (diagnosis); density → wind (pressure-gradient acceleration). Pressure responds to nothing. Relative humidity, vapour pressure, saturation vapour pressure and virtual temperature are diagnostics; supersaturation is counted and reported, not condensed. There is no latent heat, radiation, surface flux, vertical motion or mass continuity.

Advective (non-conservative) form is deliberate: the balanced wind is convergent into lows, and in a single layer without continuity a flux-form scheme would pile up temperature at convergence zones, which is unphysical for intensive quantities. Uniform fields stay uniform and upwind transport creates no new extrema.

## Configuration

The request has the same `time`, `grid` and `atmosphere` sections as the pressure-wind model, plus:

### Diffusion (`diffusion`)

- `temperature_m2_s`, `humidity_m2_s`, `momentum_m2_s`: horizontal mixing diffusivities `κ_T`, `κ_q`, `ν` in m²/s, each `≥ 0`. They smooth the fields between the pressure systems and are the user-facing control requested for this version.
- Momentum diffusion is bounded at configuration time: `2 ν Δt (1/Δx² + 1/Δy²) ≤ 1`.
- Scalar diffusion shares the per-step bound with advection (below).
- **Numerical diffusion caveat:** first-order upwind carries an implicit diffusivity of about `|u| Δx (1 − C) / 2` (`C` = Courant number). At 20 m/s on 40 km cells that is ~4 × 10⁵ m²/s, far above typical configured values of 10³–10⁴ m²/s. Configured diffusivities therefore matter most in calm regions and for fields without wind; the viewer's request editor reports the estimate so that chosen values can be judged against it. Reducing the grid spacing reduces the numerical part proportionally.

### Boundaries and modes

- `pressure_gradient_boundary`: `one_sided` (default) or `periodic`, as in the pressure-wind model.
- `scalar_boundary`: `zero_gradient` (default; the value outside the domain equals the edge cell, giving no diffusive flux and inflow that carries the edge value) or `periodic`. Momentum diffusion uses the same rule.
- `density_mode`: `diagnosed` (default, the coupling) or `constant`.

### Initial fields

`pressure` is given per cell or as Gaussian high/low systems (see the pressure-wind contract). `initial_fields.air_temperature_k` and `initial_fields.specific_humidity_kg_kg` are either per-cell `values` or a `background` plus Gaussian `anomalies` `{east_m, north_m, radius_m, amplitude}`; the example co-locates a cool, dry anomaly with the high and a warm, moist anomaly with the low. `initial_wind` is optional and defaults to rest.

## Stability and rejection rules

- Coriolis/friction: `Δt (max|f| + k) ≤ 1` (configuration).
- Momentum diffusion: `2 ν Δt (1/Δx² + 1/Δy²) ≤ 1` (configuration).
- Scalar transport, checked **every step** against the evolving wind: `Δt [ |u|/Δx + |v|/Δy + 2 max(κ_T, κ_q) (1/Δx² + 1/Δy²) ] ≤ 1` in every cell. Because spin-up from rest overshoots the balanced wind (by up to 2× without friction, `1 + e^(−kπ/|f|)` with friction), the peak wind during the first half inertial period usually sets the timestep. The solver rejects the run at the offending step instead of substepping.
- Updates that produce non-finite values, non-positive temperature or humidity outside `[0, 1)` abort the run.

## Validation rules

All pressure-wind rules apply. Additionally: diffusivities finite and non-negative; `scalar_boundary` and `density_mode` among the listed values; temperature `> 0 K`; specific humidity within `[0, 1)`; all five state arrays sized `nx × ny`.

The C++ types, validators, `build_gaussian_field`, `diagnose_moist_layer`, `coupled_scalar_stability_number` and `advance_coupled_wind` are in `include/terraflux/weather/coupled_wind.hpp` and `src/weather/coupled_wind*.cpp`. Schemas: [run request v1](coupled-wind-run-v1.schema.json) and the [coupled atmosphere timeline extension v1](coupled-atmosphere-v1.schema.json).

## Vertical column diagnostics (optional)

A `column` section enables the [atmospheric column model](atmospheric-column-model.md) per cell and frame: hydrostatic layers above the cell's surface pressure, temperature and humidity, convective adjustment, and a lifted surface parcel giving LCL (cloud base), LFC, EL (cloud top), CAPE, CIN and a convective-potential class. The coupling is one-way: the column reads the horizontal state, nothing feeds back. Results are written to `coupled_atmosphere.columns`.

## Command line and viewer output

```sh
terraflux-run-coupled-wind examples/coupled-wind-request.json coupled-timeline.json
```

The output is a `terraflux.weather.timeline/v1` document whose frames are ordinary `forecast/v1` snapshots (temperature and wind), starting with the initial state and then one frame per `snapshot_every_steps`. A `coupled_atmosphere` extension carries the static pressure and Coriolis fields, per-frame specific humidity, relative humidity, diagnosed density, supersaturated-cell counts and scalar stability numbers, the full request echo and input provenance. The viewer opens it directly: pressure, relative humidity, air density and specific humidity become selectable layers, the cell inspector shows all coupled values, and the **Coupled wind request** panel can be filled from the loaded run, edited and downloaded.

The committed example (`examples/coupled-wind-request.json`, fixture `viewer/public/coupled/engine-timeline.json`) uses a 24 × 18 grid at 40 km, 300 s steps over 48 h with 8 h snapshots, a 1023 hPa cool/dry high west of a 1006 hPa warm/moist low at 37.8° S on a beta plane, `k = 2 × 10⁻⁵` 1/s and 5000 m²/s for all three diffusivities. `tests/tools/coupled_wind_check.py` regenerates it with the built CLI under CTest, compares it with the committed fixture (1 × 10⁻⁹ relative), recomputes the moist-air diagnostics independently and checks the sign of the density–temperature coupling, extremum preservation and the hemisphere sense of the circulation.

## Analytic and reference cases

`weather.coupled_wind` covers: equality with the standalone pressure-wind model under constant density; first-step acceleration proportional to the diagnosed inverse density (warm and moist air accelerate faster); a hand-computed periodic upwind step; hand-computed explicit diffusion steps for zero-gradient (sum-conserving) and periodic boundaries; reduced temperature/humidity contrast and wind roughness with larger configured diffusivities between a high and a low; bounded diagnostics; mid-run rejection once the scalar bound is broken; supersaturation reporting; and configuration/state rejections. `weather.moist_air` covers the thermodynamic relations and their sensitivities.

## Scope limits

Pressure is frozen, so temperature and humidity cannot change the pressure field (no hydrostatic adjustment, thermal wind or cyclogenesis). First-order upwind transport is diffusive on coarse grids. Linear drag, no boundary-layer structure, no phase change, no radiation or surface exchange. Keep each new process behind an explicit model-version change.
