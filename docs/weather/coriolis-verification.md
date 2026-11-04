# Coriolis direction and pressure-gradient acceleration: review and verification

This note records the review of the sign conventions in the pressure-gradient wind kernel (`src/weather/pressure_wind.cpp`) and the independent checks added to `tests/weather/pressure_wind_test.cpp` (cases V1–V3). No kernel change was needed; one test expectation written during the review was itself wrong and was corrected (see "Findings").

## Conventions in the code

Local frame: `x` east, `y` north, `z` up; wind `(u, v)` eastward and northward in m/s; row-major cells `index = y · nx + x`.

| Term | Equation | Code |
| --- | --- | --- |
| Pressure-gradient acceleration | `a = −(1/ρ) ∇p`, pointing from high to low pressure | `acceleration_east = −dp/dx / ρ`, `acceleration_north = −dp/dy / ρ` |
| Coriolis acceleration | `−f k̂ × V = (+f v, −f u)` | trapezoidal system `(1+α)u¹ − γv¹ = (1−α)u⁰ + γv⁰ + Δt a`, `γu¹ + (1+α)v¹ = (1−α)v⁰ − γu⁰ + Δt b`, `γ = fΔt/2` |
| Coriolis parameter | `f = 2Ω sin φ`, `Ω = 7.2921159 × 10⁻⁵ rad/s` | `f0 = 2 Ω sin(latitude)` |
| Beta plane | `f = f₀ + β y`, `β = 2Ω cos φ / R` | `beta = 2 Ω cos(latitude) / earth_radius` |
| Geostrophic wind | `u_g = −(1/ρf) ∂p/∂y`, `v_g = (1/ρf) ∂p/∂x` | `geostrophic_wind` |
| Friction-balanced wind | `u = −(k ∂p/∂x + f ∂p/∂y) / (ρ(k²+f²))`, `v = (f ∂p/∂x − k ∂p/∂y) / (ρ(k²+f²))` | `balanced_wind` |

These match Holton, *An Introduction to Dynamic Meteorology*, eqs. for horizontal momentum on the f-plane. Positive `f` north of the equator deflects moving air to the right; negative `f` south of it deflects to the left; `β > 0` in both hemispheres because `cos φ > 0`.

## Verification cases

**V1 — exact solution.** Each cell obeys a linear ODE with constant coefficients whose closed form is `w(t) = w_b + e^{−kt} R(−ft) (w₀ − w_b)`, with `w = (u, v)`, `w_b` the balanced wind and `R` a counterclockwise rotation. The trapezoidal scheme reproduces it to 10⁻³ of the initial ageostrophic speed after two inertial periods at `fΔt = 0.02`, for `f = ±10⁻⁴ s⁻¹`. The sign of the rotation (clockwise for `f > 0`) is therefore verified against an analytic reference, not against the scheme's own algebra.

**V2 — direction matrix.** For pressure falling toward each of east, north, west and south, at 45° N and 45° S (8 combinations), using cross and dot products:
- (a) with rotation off, the first-step wind is parallel to `−∇p` and points toward low pressure (`|a × w| < 10⁻¹²`, `a · w > 0`);
- (b) with rotation on, `(−∇p) × w₁` has the sign of `−f`: the deflection is to the right of the pressure-gradient force in the NH and to the left in the SH;
- (c) the frictionless balanced wind is parallel to the isobars (`w · (−∇p) = 0`) and its left-hand normal `k̂ × w` points toward low pressure when `f > 0` and away from it when `f < 0` — Buys Ballot's law in both hemispheres.

**V3 — reference magnitudes.** `f(45° N) = 1.0313 × 10⁻⁴ s⁻¹`, `f(0°) = 0`, `f(90°) = 2Ω`; `β(45°) = 1.619 × 10⁻¹¹ m⁻¹ s⁻¹`; `f` increases northward at both 45° N and 45° S. A gradient of 1 hPa per 100 km with `ρ = 1.2 kg/m³` is an acceleration of `8.3 × 10⁻⁴ m/s²` and gives a geostrophic speed of 8.08 m/s at 45° N; with pressure rising eastward (low to the west) the NH geostrophic wind blows northward, keeping the low on its left.

Earlier cases already covered: no Coriolis → straight down-gradient acceleration `u = a t`; a hand-computed trapezoidal step; right/left deflection by hemisphere; frictionless inertial oscillation with conserved ageostrophic speed and the 2× peak after half a period; convergence to the balanced wind between a high and a low with cross-isobar angle `atan(k/f)` and speed factor `f/√(f²+k²)`; counterclockwise (NH) / clockwise (SH) flow around lows and the reverse around highs.

## Findings

- Kernel conventions are correct and consistent with the documentation; no code change was required.
- While writing V3 the expected sign of the geostrophic wind for "pressure rising eastward" was first written as southward; the test failed and the expectation, not the kernel, was wrong (low to the west → northward flow in the NH). The corrected case is retained as a guard against the same slip in future reviews.
- The linear model has no momentum advection, so flow around curved isobars is geostrophic rather than gradient-balanced: real winds around a tight low are subgeostrophic and around a high supergeostrophic. This remains a documented scope limit of the wind model, not a sign issue.
- The coupled model reuses `step_pressure_wind` with a per-cell density, so the same conventions apply there; `weather.coupled_wind` case 1 checks equality with the standalone kernel under constant density.
