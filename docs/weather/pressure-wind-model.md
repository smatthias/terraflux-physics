# Pressure-gradient wind v1

This contract defines the first dynamics process: wind driven by the pressure difference between high- and low-pressure systems. It is a controlled single-layer momentum model with a prescribed pressure field, not an operational forecast.

## Scope

The v1 model is a two-dimensional, single-layer, dry-air domain. Each cell carries a horizontal wind `(u, v)` that is accelerated by the pressure-gradient force, deflected by the Coriolis force, and slowed by linear surface friction:

`∂u/∂t = −(1/ρ) ∂p/∂x + f v − k u`
`∂v/∂t = −(1/ρ) ∂p/∂y − f u − k v`

- `u`, `v`: eastward and northward wind in metres per second (m/s).
- `p`: pressure in pascals (Pa), prescribed and fixed during a run.
- `ρ`: air density in kilograms per cubic metre (kg/m³), constant.
- `f = 2Ω sin φ`: Coriolis parameter in 1/s, positive in the Northern Hemisphere and negative in the Southern Hemisphere.
- `k`: linear (Rayleigh) surface friction coefficient in 1/s.

Air does not flow straight from high to low pressure. The pressure-gradient force starts the motion toward low pressure, but as soon as the air moves the Coriolis force turns it: to the right of its motion in the Northern Hemisphere, to the left in the Southern Hemisphere. The model therefore has the following steady states, which are also available as separate analytic diagnostics:

- **Geostrophic wind** (`k = 0`): `u_g = −(1/ρf) ∂p/∂y`, `v_g = (1/ρf) ∂p/∂x`. The wind blows parallel to the isobars with low pressure on its left in the Northern Hemisphere and on its right in the Southern Hemisphere. Flow is counterclockwise around a Northern Hemisphere low and clockwise around a Northern Hemisphere high; both senses reverse south of the equator.
- **Friction-balanced wind** (`k > 0`): `u = −(k ∂p/∂x + f ∂p/∂y) / (ρ (k² + f²))`, `v = (f ∂p/∂x − k ∂p/∂y) / (ρ (k² + f²))`. Friction slows the wind to `f / √(f² + k²)` of the geostrophic speed and turns it across the isobars toward low pressure by the angle `atan(k / |f|)`. This produces the surface convergence into lows and divergence out of highs seen on weather charts.
- **No rotation** (`f = 0`, for example `coriolis_mode = "none"`): air accelerates straight down the pressure gradient, which is the intuitive but incorrect picture for synoptic scales.

Starting from rest with no friction, the wind performs an inertial oscillation about the geostrophic wind with period `2π/|f|` (about 17 h at 45° latitude), peaking at twice the geostrophic speed. With friction the oscillation decays over a time scale `1/k` toward the balanced wind.

The model excludes momentum advection (so no gradient-wind curvature correction), pressure evolution, mass continuity, vertical motion, thermodynamics, moisture, terrain, and open-boundary forcing.

## Configuration

The configuration has three explicit parts plus the gradient boundary rule.

### Time stepping (`time`)

- `start_time_utc`: UTC timestamp ending in `Z`.
- `timestep_seconds`: fixed positive step.
- `steps`: positive step count; the advance function applies all of them.

The solver rejects configurations where `timestep × (max|f| + k) > 1` instead of substepping. The trapezoidal update below is unconditionally stable, but beyond this bound the inertial rotation per step is no longer resolved.

### Grid (`grid`)

The model reuses the transport grid contract: uniform local east/north coordinates in metres, origin at the southwest corner, values at cell centers, row-major arrays (`index = y * nx + x`), and a geographic anchor at local `(0, 0)`. The anchor latitude sets the Coriolis parameter for the `f_plane` and `beta_plane` modes. See [dry transport model](dry-transport-model.md) for the full grid rules.

### Atmosphere (`atmosphere`)

- `air_density_kg_m3`: positive constant density (1.225 kg/m³ at sea level, 15 °C).
- `surface_friction_coefficient_s`: `k ≥ 0`. Typical boundary-layer values are 10⁻⁵–10⁻⁴ 1/s; zero gives a frictionless layer.
- `coriolis_mode`:
  - `none`: `f = 0` everywhere.
  - `f_plane`: constant `f = 2Ω sin φ₀` from the anchor latitude.
  - `beta_plane`: `f = f₀ + β y`, with `β = 2Ω cos φ₀ / R` and `y` the north distance from the anchor.
  - `prescribed`: constant `f` from `coriolis_parameter_s`, for idealized experiments and hand-computed checks.
- `earth_angular_speed_rad_s` (default 7.2921159 × 10⁻⁵) and `earth_radius_m` (default 6 371 000) are explicit constants, not tuning parameters.

### Pressure-gradient boundary (`pressure_gradient_boundary`)

- `one_sided` (default): central differences inside, one-sided differences at the domain edges, no wrap. Suited to regional fields such as Gaussian systems.
- `periodic`: central differences with periodic wrap. With `f_plane` or `prescribed` rotation the resulting geostrophic wind is discretely divergence-free on the transport model's face stencil, so it can be supplied as the prescribed wind of the [dry transport model](dry-transport-model.md). Friction-balanced winds are convergent by construction and will be rejected by that model.

## Pressure input

Pressure is either supplied per cell (`pressure_pa`, positive pascals) or built from idealized systems: a positive background plus Gaussian anomalies `Δp exp(−(r / radius)²)`. A `high` center requires a positive anomaly and a `low` a negative one; the e-folding radius must be positive and the resulting pressure must stay positive everywhere. The builder evaluates each center at every cell center; centers may lie outside the grid.

## Numerical update

The pressure-gradient force is constant per cell, so it is applied exactly. The Coriolis and friction terms are linear and use the trapezoidal (Crank–Nicolson) rule. With `α = kΔt/2` and `γ = fΔt/2`:

`(1 + α) u¹ − γ v¹ = (1 − α) u⁰ + γ v⁰ − Δt (1/ρ) ∂p/∂x`
`γ u¹ + (1 + α) v¹ = (1 − α) v⁰ − γ u⁰ − Δt (1/ρ) ∂p/∂y`

solved directly per cell. Properties used by the tests:

- With `k = 0` the update is a pure rotation by `2 atan(γ)`; the ageostrophic speed is conserved to roundoff.
- The fixed point of the update equals the analytic friction-balanced wind for any timestep, so a balanced initial state does not drift.
- For constant forcing without rotation or friction the scheme reproduces `u = a t` exactly.

## Validation rules

- Grid dimensions, their product, and state-array lengths must be valid and consistent.
- Origins, spacing, timestep, density, friction, Coriolis inputs, state values, and elapsed time must be finite.
- Spacing, timestep, density, background pressure, and center radii must be greater than zero; friction and elapsed time cannot be negative.
- Pressure must be greater than 0 Pa; wind must be finite.
- `coriolis_mode` must be one of the four listed modes; `prescribed` requires a finite `coriolis_parameter_s`.
- `timestep × (max|f| + k) ≤ 1`.
- The geostrophic diagnostic is undefined where `f = 0`; the balanced diagnostic is undefined where `f = 0` and `k = 0`. The CLI reports these as `undefined` rather than failing the run.
- Solver updates reject non-finite wind components.

The C++ contract types, validators, `build_pressure_field`, `coriolis_parameter`, `calculate_pressure_gradient`, `geostrophic_wind`, `balanced_wind`, and `advance_pressure_wind` are in `include/terraflux/weather/pressure_wind.hpp` and `src/weather/pressure_wind*.cpp`. The JSON schemas are [run request v1](pressure-wind-run-v1.schema.json) and [forecast v1](pressure-wind-forecast-v1.schema.json).

## Command line

```sh
terraflux-run-pressure-wind examples/pressure-wind-request.json pressure-wind.json
```

The committed example places a 1023 hPa high west of a 1006 hPa low (1015 hPa background) over southeastern Australia on a 48 × 36, 25 km beta-plane grid and integrates 48 h in 10-minute steps with `k = 2 × 10⁻⁵` 1/s, writing snapshots every 6 h. The output records the time, grid, and atmosphere configuration, the pressure field and its provenance, the final wind, the snapshots, and the per-cell Coriolis parameter with both analytic balances. `tests/tools/pressure_wind_check.py` runs it under CTest and checks hemisphere sense, convergence to balance, the cross-isobar angle, and clockwise flow around the Southern Hemisphere low.

## Analytic cases

The registered test executable covers exact down-gradient acceleration without rotation, a hand-computed trapezoidal step, right/left deflection by hemisphere, inertial-oscillation amplitude conservation and the half-period peak, convergence to the balanced wind with the `atan(k/f)` cross-isobar angle and `f/√(f²+k²)` speed reduction, cyclonic/anticyclonic circulation around synthetic lows and highs in both hemispheres, acceptance of the periodic geostrophic wind by the dry transport model, the balanced fixed point, all Coriolis modes, and rejection of invalid configurations. Run them through CTest using the commands in [weather validation](../../tests/weather/README.md).

## Scope limits

This is a linear momentum baseline with a frozen pressure field: wind responds to pressure, but pressure does not respond to wind, so there is no cyclogenesis, filling, or propagation of systems. Omitting momentum advection means curved flow is geostrophic rather than gradient-balanced; for tight lows the real wind is subgeostrophic. Linear drag stands in for the full boundary layer. Keep each new process behind an explicit model-version change.
