# Moist-air relations: temperature, density and humidity

This note documents the estimated dependence between air temperature, air density and humidity used by the [coupled wind / moist-layer model](coupled-wind-model.md). The relations are standard ideal-gas and Magnus-form thermodynamics for an unsaturated layer; the C++ implementation is `include/terraflux/weather/moist_air.hpp` and `src/weather/moist_air.cpp`.

## Variables and units

| Symbol | Meaning | Unit |
| --- | --- | --- |
| `p` | pressure | Pa |
| `T` | air temperature | K |
| `q` | specific humidity (mass of water vapour per mass of moist air) | kg/kg |
| `e` | partial pressure of water vapour | Pa |
| `e_s(T)` | saturation vapour pressure over liquid water | Pa |
| `T_v` | virtual temperature | K |
| `ρ` | moist-air density | kg/m³ |
| `RH` | relative humidity `e / e_s` | fraction (×100 for %) |

Constants: `R_d = 287.05 J/(kg·K)`, `R_v = 461.5 J/(kg·K)`, `ε = R_d/R_v = 0.622`, virtual-temperature factor `1/ε − 1 = 0.608`.

## Relations

1. **Saturation vapour pressure** (Magnus form, Alduchov & Eskridge 1996, accurate to ~0.4 % between −40 °C and +50 °C):
   `e_s(T) = 610.94 · exp(17.625 · t / (t + 243.04))`, with `t = T − 273.15` in °C.
2. **Vapour pressure from specific humidity:** `e = q p / (ε + (1 − ε) q)`; inverse `q = ε e / (p − (1 − ε) e)`.
3. **Virtual temperature:** `T_v = T (1 + 0.608 q)` — the temperature dry air would need to have the same density as the moist air.
4. **Density (ideal gas for moist air):** `ρ = p / (R_d T_v) = p / (R_d T (1 + 0.608 q))`.
5. **Relative humidity:** `RH = e(q, p) / e_s(T)`.

## Estimated dependence (sensitivities)

Partial derivatives of the relations above, evaluated at a reference sea-level state `p = 101 325 Pa`, `T = 288.15 K` (15 °C), `q = 8 g/kg`, where `ρ = 1.2191 kg/m³` and `RH = 76.2 %`. The test `weather.moist_air` checks each analytic derivative against a central finite difference and the magnitudes below.

| Dependence | Expression | Value at reference | Practical reading |
| --- | --- | --- | --- |
| density on temperature | `∂ρ/∂T = −ρ/T` | −4.23 × 10⁻³ kg/m³/K | **−0.35 % per K** (fixed `p`, `q`) |
| density on humidity | `∂ρ/∂q = −0.608 ρ / (1 + 0.608 q)` | −0.738 kg/m³ per kg/kg | **−0.6 % per 10 g/kg**: moist air is lighter |
| density on pressure | `∂ρ/∂p = ρ/p` | 1.20 × 10⁻⁵ kg/m³/Pa | **+1.0 % per 10 hPa** |
| saturation pressure on temperature | `d ln e_s/dT = 17.625 · 243.04 / (t + 243.04)²` | 0.0643 1/K | `e_s` grows **~6.4 % per K** (Clausius–Clapeyron) |
| relative humidity on temperature | `∂RH/∂T = −RH · d ln e_s/dT` | −0.049 1/K | at fixed moisture, warming 1 K lowers RH by **~4.9 points** |
| relative humidity on humidity | `∂RH/∂q = p ε / ((ε + (1 − ε) q)² e_s)` | 94.8 per kg/kg | **+0.95 points per 0.1 g/kg** |

Orders of magnitude for synoptic contrasts: a 10 K temperature difference between the cool air of a high and the warm air of a low changes density by about 3.5 %; a 10 g/kg humidity difference adds about 0.6 % in the same direction (warm air is usually also moist); a 20 hPa pressure difference changes density by about 2 %. In the coupled model these density differences change the pressure-gradient acceleration `−(1/ρ)∇p` by the same relative amounts, so for a given pressure field the wind responds a few percent more strongly in warm, moist air than in cool, dry air.

## Where each dependence enters the coupled model

- `ρ(p, T, q)` is diagnosed every step and divides the pressure-gradient force in the wind equation (`density_mode = "diagnosed"`). With `density_mode = "constant"` the relations are still reported as diagnostics but do not feed back.
- `T` and `q` are transported by the wind and mixed by configurable diffusivities, so the density field moves with the air.
- `RH`, `e`, `e_s` and `T_v` are diagnostics only. No condensation, latent heating or precipitation is modelled: if transport brings `q` above saturation the model reports supersaturated cells instead of removing water.
- Pressure is prescribed and does not respond to density or temperature; there is no hydrostatic or thermal-wind coupling in v1.

## Validity limits

- Magnus coefficients are for liquid water; below 0 °C the ice-surface saturation pressure is lower by up to ~10 % at −20 °C.
- The ideal-gas relation neglects the enhancement factor (~0.4 %) and compressibility corrections.
- Specific humidity must stay within `[0, 1)`; the functions reject `T ≤ 0 K`, `p ≤ 0 Pa`, vapour pressure at or above the total pressure, and temperatures below the Magnus singularity.
- Single-layer quantities: no vertical profile, so `p`, `T`, `q` describe the same level (surface or a representative layer).
