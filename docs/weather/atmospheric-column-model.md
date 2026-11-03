# Atmospheric column model v1: vertical layers, thinning and exchange

This contract adds vertical awareness above each grid cell: the atmosphere thins with height, pressure and density fall hydrostatically, and two exchange processes connect the ground to higher altitudes. It is the basis for diagnosing cloud development and thunderstorm potential; it is a column model, not a three-dimensional solver. Implementation: `include/terraflux/weather/atmospheric_column.hpp`, `src/weather/atmospheric_column.cpp`.

## Layers and thinning

A column has `layer_count` layers of `layer_thickness_m`, indexed from the surface upward, with values at layer midpoints and pressures also at the interfaces. The environment is built from the cell's surface pressure `p_s`, temperature `T_s` and specific humidity `q_s`:

- `T(z) = T_s − Γ · min(z, z_tropopause)` (isothermal above the tropopause),
- `q(z) = q_s · exp(−z / H_q)`,
- hydrostatic balance `dp/dz = −ρ g` with the moist-air density `ρ = p / (R_d T_v)`, integrated exactly over each half-layer assuming linear virtual temperature,
- diagnostics per layer: density, potential temperature `θ = T (p₀/p)^κ`, relative humidity, layer mass `(p_bottom − p_top)/g`.

For the dry ISA (`Γ = 6.5 K/km`, tropopause 11 km) the interface pressures reproduce the analytic `p = p_s (1 − Γz/T_s)^{g/(R_dΓ)}` to 10⁻⁹ and the isothermal stratosphere to the same accuracy; they match the ISA table to 0.1 % (the table uses geopotential height). Roughly half the mass lies below 5.5 km and 94 % below 20 km. Moist columns are lighter and keep more pressure aloft. See `weather.atmospheric_column` cases 1–2.

## Exchange between ground and higher altitudes

### Surface-parcel ascent (diagnostic)

The lowest layer is lifted:

1. **Dry ascent** at constant `θ` and `q` until the **lifting condensation level (LCL)**, found by bisection where `q = q_s(T, p)`. This is the cloud-base height for surface-driven cumulus. It agrees with Bolton's (1980) closed-form LCL temperature to 0.4 % in pressure and with Espy's 125 m/K rule within 15 %.
2. **Pseudo-adiabatic ascent** above the LCL: `dT/dp = Γ_m R_d T / (g p)` with the saturated lapse rate `Γ_m = g (1 + L r_s/(R_d T)) / (c_p + L² r_s ε/(R_d T²))`. In height coordinates the parcel sits at the environment pressure `p(z)`, so `dT/dz = −Γ_m · T / T_v,env`. Condensate `q₀ − q_s` is removed (no loading). The ascent matches an independent fourth-order integration in pressure coordinates to 0.01 K over 14 km, and Bolton's equivalent potential temperature stays within 2.5 K along the path (the residual of the simplified first law, also present in an exact-partials reference integration).
3. **Buoyancy** `b = g (T_v,parcel − T_v,env) / T_v,env` at every step gives the **level of free convection (LFC)** (first positively buoyant level at or above the LCL with a non-zero positive area), the **equilibrium level (EL)** (top of the last positive area: cloud top), **CAPE** = ∫ max(b, 0) dz from LFC to EL and **CIN** = −∫ min(b, 0) dz from the surface to the LFC. Levels that do not exist are reported as missing.
4. **Convective potential** is a rule-of-thumb class by CAPE: none (< 100 J/kg), weak (< 1000), moderate (< 2500), strong (≥ 2500). It indicates thunderstorm *potential* if the air is lifted to its LFC; it is not a forecast of storms, which also needs a trigger, shear and moisture supply.

Reference behaviour (`weather.atmospheric_column` case 6): a 25 °C / 16 g/kg surface over a 6.5 K/km environment gives LCL ≈ 760 m, LFC ≈ 1.2 km, EL ≈ 10.8 km, CAPE ≈ 1600 J/kg (moderate), CIN ≈ 18 J/kg; CAPE grows with surface moisture; a dry parcel in a dry-adiabatic environment is neutrally buoyant to 2 × 10⁻⁴ m/s².

### Convective adjustment (prognostic on the column)

Where the environment itself is unstable, the column is adjusted until stable and unsaturated:

- **Dry convective adjustment** (Manabe et al. 1965): adjacent layers with `θ_k > θ_{k+1}` are mixed to a common `θ̄ = Σ m T / Σ m Π` that conserves enthalpy `Σ m c_p T`, with mass-weighted mixing of vapour and cloud water. This is the vertical exchange that carries surface heat and moisture upward when the ground is warmer than a dry-adiabatic profile allows.
- **Saturation adjustment**: a supersaturated layer condenses to saturation, releasing latent heat `L Δq` into the layer and leaving cloud water; cloud water in a subsaturated layer evaporates back. `c_p T + L q` and total water are conserved.
- The two passes repeat until nothing changes (at most 50 iterations). The result is idempotent, statically stable (`θ` non-decreasing), nowhere supersaturated, and cloud layers are exactly saturated. Heights and pressures are held fixed; the small hydrostatic re-adjustment from heating is neglected.

The adjusted column is the environment used for the parcel ascent, and the condensed water per column is reported.

## Configuration (`column`)

| Field | Meaning | Default |
| --- | --- | --- |
| `layer_count` | number of layers (≥ 2) | 24 |
| `layer_thickness_m` | uniform thickness | 500 |
| `environment_lapse_rate_k_m` | `Γ`, within ±0.05 K/m | 0.0065 |
| `tropopause_height_m` | isothermal above | 11 000 |
| `humidity_scale_height_m` | `H_q` | 2500 |
| `parcel_step_m` | ascent step, ≤ layer thickness | 50 |

Validation rejects fewer than 2 layers, non-positive thickness or scale height, a lapse rate that drives the column below 0 K, a parcel step above the layer thickness, and surface states outside the moist-air ranges.

## Use in the coupled model and viewer

`terraflux-run-coupled-wind` accepts an optional `column` section and writes a `columns` block (`terraflux.weather.column_diagnostics/v1`) inside `coupled_atmosphere`: per-cell, per-frame LCL, LFC, EL, CAPE, CIN, potential class and condensed water, plus column mass and top pressure. Each cell's column is rebuilt every frame from the coupled model's surface pressure, temperature and humidity, so horizontal transport of warm, moist air changes where convection is possible. The viewer exposes CAPE, cloud base (LCL) and equilibrium level as layers and lists all values in the cell inspector; the request editor has the column fields.

In the committed example the warm, moist low starts with weak CAPE (up to ~880 J/kg) that decays over 48 h: nothing resupplies heat and moisture at the surface, and upwind numerical diffusion smears the anomaly. That is a documented limit, not a feature.

`terraflux-run-atmospheric-column examples/column-request.json profile.json` writes a full profile (`terraflux.weather.column-profile/v1`): initial and adjusted layers, the adjustment report and the complete parcel path. `tests/tools/column_check.py` recomputes the hydrostatic profile, conservation, the dry-ascent `θ`, saturation above the LCL and the CAPE integral independently.

## Scope limits

One-way coupling only: the column reads the surface state and does not feed heat, moisture or momentum back into the horizontal model. No surface fluxes, radiation, precipitation, cloud microphysics, entrainment, moist convective adjustment, vertical wind shear, or horizontal advection aloft. The pseudo-adiabat neglects the heat capacity of vapour and condensate. Magnus saturation is over liquid water, so ice-phase effects above the freezing level are not represented. Keep each new process behind an explicit model-version change.
