# Storm systems v1: tropical cyclones — energy, genesis, interaction, landfall

This contract adds tropical cyclones (hurricanes, typhoons) as tracked objects on the coupled layer: where they draw their energy from humid air over a warm ocean, how they arise, how they interact and merge, and what happens at landfall. Implementation: `include/terraflux/weather/storm_systems.hpp`, `src/weather/storm_systems.cpp`; schema: [storm-systems-v1.schema.json](storm-systems-v1.schema.json) with the surface in [surface-v1.schema.json](surface-v1.schema.json).

## Energy: humid air over a warm sea

A hurricane is a heat engine. Its fuel is the enthalpy difference between air saturated at the sea-surface temperature and the boundary-layer air, and its efficiency is the Carnot efficiency between the sea surface and the cold outflow at the tropopause. Emanuel's **Maximum Potential Intensity**:

`V_pot² = (C_k/C_D) · (T_s − T_o)/T_o · (k_s* − k_b)`, `k = c_p T + L q`,

with `T_s` the sea-surface temperature, `T_o` the outflow temperature (column temperature at the tropopause), `k_s*` the saturation enthalpy at the sea surface and `k_b` the boundary-layer enthalpy; `C_k/C_D = 0.9`, and a surface-wind factor 0.8 converts the gradient-level bound to a surface wind. Over land, below 26.5 °C or within ~10° of the equator the potential intensity is zero. Reference: a 29 °C sea under a 200 K outflow gives 60–80 m/s (category 4–5); warmer water or a colder outflow raise it.

Also reported per cell: the enthalpy disequilibrium `k_s* − k_b`, the mid-level relative humidity, the outflow temperature, the relative vorticity `ζ = ∂v/∂x − ∂u/∂y` and the deep-layer vertical shear estimated from the thermal wind, `|ΔV| = (g/(|f| T)) |∇T| Δz` (strong in baroclinic zones, near zero in the barotropic tropics).

## Genesis: when and where storms arise

The Emanuel–Nolan (2004) genesis potential index combines the ingredients:

`GPI = |10⁵ η|^{3/2} · (RH_mid/50)³ · (V_pot/70)³ · (1 + 0.1 V_shear)⁻²`

with `η = f + ζ` the absolute vorticity (a pre-existing disturbance), `RH_mid` in percent, `V_pot` the potential intensity and `V_shear` the deep shear. A new storm (17 m/s) is seeded at the most favourable ocean cell whose GPI reaches `genesis_gpi_threshold` (8), at least `genesis_separation_m` (600 km) from existing storms and `genesis_edge_margin_m` inside the domain. Storms can also be **seeded** explicitly in the request.

## Evolution over the ocean and the collision of storms

Per sub-step (default 10 min) against the coupled state at the start of each snapshot interval:

- **Intensification** relaxes the maximum wind toward the shear-reduced potential intensity `V_pot · max(0, 1 − V_shear/shear_limit)` with the intensification time scale (24 h); storms in hostile shear weaken the same way.
- **Wind–pressure relation** (Holland 1980): `p_env − p_c = ρ e V_max² / B` with `B = 1.3` (50 m/s ↔ 60 hPa; 77 m/s ↔ ~135 hPa, the record range).
- **Motion**: the environmental steering flow averaged within `steering_radius_m`, a poleward/westward **beta drift** (1.5 / 1 m/s), and the **tangential wind induced by every other storm** — a Rankine vortex outside its core, `V = V_max R_max / d`, cyclonic, scaled by `induction_factor` (0.4, the deep-layer mean circulation). Two storms therefore rotate around their common centroid: the **Fujiwhara effect**, counterclockwise in the NH and clockwise in the SH (tested).
- **Merger**: when the cores overlap (`d < R_max,1 + R_max,2 + merge_distance_m`) the stronger storm absorbs the weaker one at the intensity-weighted centroid, keeping the larger wind and radius; a `merger` event records both ids.
- **Categories** follow Saffir–Simpson (depression < 17 m/s, tropical storm < 33, then 33/43/50/58/70 m/s for categories 1–5). Storms below 10 m/s dissipate; storms leaving the domain are recorded as `exit`.

## Landfall

When the centre moves onto a land cell:

1. the wind drops by the landfall reduction `R = 0.9` and a **landfall event** records time, place, intensity, category and the **storm surge** estimate `η = Δp/(ρ_w g) + c_w V_max²` (inverse barometer plus wind set-up with `c_w = 1.5 × 10⁻³ s²/m`: a 50 m/s storm gives ~4.3 m, category 5 ~10 m — bathymetry and coastline shape are not represented);
2. the sea no longer feeds the storm (potential intensity 0) and the wind decays following **Kaplan & DeMaria (1995)**: `V(t) = V_b + (R V_0 − V_b) e^{−αt}`, `α = 0.095 h⁻¹`, `V_b = 13.75 m/s` — a 60 m/s storm is a depression after a day inland (tested to 10⁻⁶ against the closed form);
3. the core rain rate `0.4 mm/h per m/s` keeps falling along the track, decaying with `exp(−r / 3R_max)`.

Per-cell **storm fields**: the Rankine wind envelope of all storms, the Holland pressure anomaly `−Δp (1 − exp(−(R_max/r)^B))`, the rain rate, and the surge on coastal land cells within 6 `R_max` of a centre (decaying with `exp(−r / 2R_max)`).

## Configuration

Request `surface` (land mask per cell or a straight coastline, SST per cell or background + gradient) and `storms` (seeds, thresholds and the parameters above; all optional except seeds). `storms` requires `surface` and `column`. Validation rejects non-binary land masks, non-positive SST, factors outside (0, 1], seeds outside the grid, and everything the column rejects.

## Output format

`coupled_atmosphere.surface` (land, SST, provenance) and `coupled_atmosphere.storms`: config echo, a global `events` list (`seed`, `genesis`, `landfall`, `merger`, `dissipation`, `exit`) and per frame the storm objects (id, position, maximum wind, radius, central/environment pressure, category, over-land flag, ages, potential intensity, surge estimate, core rain, full track) plus the environment fields and the storm fields named above. The viewer draws centres (ring at the radius of maximum wind, category label and colour), tracks, ✖ landfall / ⊕ merger / ✦ genesis markers, the storm wind field, coastal surge and land/sea; the field explorer adds storm wind, potential intensity, genesis index and surge layers.

## Example and tests

`examples/hurricane-landfall-request.json` (fixture `viewer/public/hurricane/engine-timeline.json`): 16 × 12 cells at 60 km at 15° N, a 29.4 °C sea east of a straight coast at −200 km, easterly steering under a subtropical high, two seeded tropical storms 170 km apart, 72 h with 18-hourly frames. The seeds merge within the first hour, the storm intensifies to category 2–3, makes landfall on the western coast with ~4 m surge around day one, decays inland by Kaplan–DeMaria, and the warm sea keeps spawning new storms that repeat the cycle. Storms are advanced against each interval's starting state, so frame spacing affects the track details (documented).

`weather.storm_systems` (C++): MPI, GPI, Holland, surge, category and Kaplan–DeMaria references; environment over sea/land/equator with vorticity and baroclinic shear; monotonic intensification to the potential intensity; landfall event, reduction and exact inland decay; Fujiwhara rotation sense in both hemispheres with conserved separation; merger of overlapping cores; genesis only in favourable environments with the edge margin; Rankine/Holland/rain/surge fields; rejections. `weather.storm_systems_cli` (Python): CLI output equals the committed fixture, format and consistency rules, Holland and surge relations per storm, event sequence (seeds → merger → landfall with surge → genesis), inland decay.

## Scope limits

Storms are parametrised objects: no vortex dynamics, eyewall replacement, size evolution, extratropical transition, ocean cooling by the storm, rainfall transport or feedback on the coupled flow; steering uses the single-layer wind; the surge ignores bathymetry and coastline geometry; the induced-motion factor, efficiencies and thresholds are rules of thumb. Keep each new process behind an explicit model-version change.
