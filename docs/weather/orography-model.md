# Orography v1: mountains, valleys, orientation, foehn and cold-air pools

This contract brings terrain into the coupled layer. Mountain ranges act as natural borders whose **orientation** decides which air masses they separate: a north–south range (Rockies, Andes) blocks zonal flow and lets polar and tropical air meet along its eastern side, while an east–west range (Alps, Pyrenees, Himalaya) blocks the north–south exchange. Air forced over a range produces the **foehn** (chinook) on the lee; air that cannot climb is blocked and deflected along the range; valleys and basins trap cold air at night. Implementation: `OrographyConfig` in `include/terraflux/weather/coupled_wind.hpp`, `src/weather/coupled_wind.cpp`; schema: [orography-v1.schema.json](orography-v1.schema.json); the elevation itself travels as the existing [terrain grid](terrain-grid-v1.md).

## Terrain-following layer: adiabatic rates

The single layer follows the terrain. Air moving with the wind `V` across a slope `∇h` changes height at the orographic vertical velocity `w = V · ∇h`, and its temperature follows the adiabats:

- **descent, or unsaturated ascent**: the dry adiabatic rate `g/c_p = 0.976 K per 100 m` — the textbook "about 1 °C per 100 m";
- **saturated ascent**: the pseudo-adiabatic rate `Γ_m(T, p)` (latent heat and expansion included), 0.65 K/100 m near 0 °C, 0.53 at 10 °C, 0.35 at 30 °C — the textbook "about 0.6 °C per 100 m" for cool, humid air. The excess vapour condenses and falls as **orographic precipitation** (`condensate × ρH`, with the boundary-layer depth `H` as the layer mass).

Because the ascent is partly at the moist rate and the descent entirely at the dry rate, air crossing a ridge arrives on the lee warmer and drier than it left the windward foot. That is the **foehn**: in the tested case (10 °C, 90 % RH westerlies over a 2 km north–south ridge) the windward slope receives tens of millimetres, the crest cools at 0.63 K/100 m, the lee foot ends at 16.3 °C and 37 % RH — **+6.4 K**, the textbook value for a 2 km crossing. Supersaturation created by advection or cooling is removed by a saturation adjustment conserving `c_p T + L q`.

The surface pressure follows the terrain hydrostatically (`p_s = p_sl exp(−g h/(R_d T))`) for density and saturation; the wind is still driven by the sea-level pressure field.

## Blocking and orientation: the Froude number

Whether air goes over or around an obstacle is decided by the cross-barrier Froude number `Fr = U_n / (N Δh)`: `U_n` the up-slope wind component, `N` the ambient stability frequency (0.01 s⁻¹ by default) and `Δh` the rise of the terrain the air faces within `obstacle_radius_m` downwind. The blocked fraction `clamp(1 − Fr, 0, 1)` is weighted by the **angle of attack** `(U_n/|V|)²`, so flow parallel to a ridge is not blocked at all. The up-slope component is removed at the blocking time scale in proportion to the blocked fraction; the Coriolis force then turns the blocked air along the range into a **barrier jet**.

Consequences, all tested with the same 10 m/s westerlies: a 2.5 km **north–south** ridge (Fr ≈ 0.4) cuts the cross-barrier wind at the foot by a fifth and drives a 10–13 m/s jet along the range; the same westerlies along an **east–west** ridge are untouched; a 20 m/s flow over a 1.5 km ridge (Fr ≥ 1) crosses unblocked. The diagnostic `ridge_orientation_deg` reports the local contour direction (0 = north–south, 90 = east–west), `cross_barrier_wind_m_s` and `along_barrier_wind_m_s` the decomposition of the wind relative to it.

## Cold-air pools in valleys

A diurnal surface cycle (`daytime_heating_k_per_s`, `nocturnal_cooling_k_per_s`, `diurnal_phase_seconds`) acts on the layer. Both heating and cooling are enhanced in proportion to the **valley depth** (the neighbourhood mean elevation within `influence_radius_m` minus the local elevation) by `valley_pooling_per_km`: the sheltered, shallow air of a basin cools faster at night — cold air pools — and warms faster by day, eroding the pool. A cell is a **cold pool** when it is at least 1 K colder than its neighbourhood reduced to the same height (6.5 K/km) in a valley at least 100 m deep. In the tested calm night a 500 m basin ends 1–2.5 K colder than the plateau and is flagged; the next day's heating erodes the deficit. Pooled cold air also raises the effective stability of the blocked regime in reality; v1 uses the configured `N`.

## Configuration (`orography`)

`terrain`: per-cell `values`, or `base_elevation_m` with Gaussian `ridges` (`orientation_deg` of the axis from north, centre, `height_m`, `half_width_m`, `length_m` with smoothly tapered ends) and `basins` (centre, `depth_m`, `radius_m`). Parameters: `blocking_time_scale_s` (1800; at least one timestep), `obstacle_radius_m` (200 km), `influence_radius_m` (100 km), `brunt_vaisala_s` (0.01), `boundary_layer_depth_m` (1500), the diurnal rates and phase, `valley_pooling_per_km` (1), `initial_lapse_rate_k_m` (0.0065, applied to the initial temperature so mountain tops start colder). Validation rejects mismatched elevation arrays, non-positive scales and rates, and a timestep above the blocking time scale.

## Output format

The CLI writes the top-level `terrain` block (terrain-grid v1, `synthetic_local_datum`) and `coupled_atmosphere.orography`: config echo and per frame `upslope_lifting_m_s`, `obstacle_height_m`, `froude_number` (null without obstacle), `blocked_fraction`, `valley_depth_m`, `cross_barrier_wind_m_s`, `along_barrier_wind_m_s`, `ridge_orientation_deg` (null on flat cells), `surface_pressure_pa`, `cold_pool_k`, `cold_pool`, `foehn`, `foehn_warming_k`, `orographic_precipitation_mm` (interval), `_rate_mm_h`, `cumulative_…`, and the totals `cold_pool_cells`, `foehn_cells`, `surface_tendency_k_s`.

## Viewer

**Load Alps foehn example** opens the weather map with the **terrain hillshade** base and **relief** (3D terrain with the height-scale slider). Switchable layers: orographic lifting (blue up-slope, orange descent), **F** foehn cells, **❄** cold-air pools, ● orographic precipitation, blocked flow; the field explorer adds lifting, blocked fraction, foehn warming, cold-pool deficit, orographic precipitation and valley depth layers, and **Place fields on terrain** drapes any field on the relief. The inspector lists elevation, surface pressure, lifting, contour direction, cross/along-barrier wind, obstacle height, Froude number, blocking, valley depth, cold pool and foehn values; the metrics strip shows foehn and cold-pool counts, rising/sinking fractions, blocking, orographic rain, the dominant ridge orientation and the surface tendency. The request editor builds ridges by orientation, basins and all parameters.

## Examples and tests

- `examples/alps-foehn-request.json` (fixture `viewer/public/alps/engine-timeline.json`): 24 × 20 cells at 40 km at 46.8° N, an east–west Alpine ridge (2.4 km, 700 km long) above a 400 m base with a foreland basin to the north, a low far to the west giving a 10–12 m/s southerly, 36 h from sunset with 6-hourly frames. The south side gets 2–4 mm/h orographic rain, the flow is ~25 % blocked and deflected westward, the northern foot is 7–9 K warmer and much drier (north foehn), the foreland basin forms cold pools at night and is flushed by the foehn by day.
- `examples/rockies-barrier-request.json` (CTest only): a north–south range with westerlies — blocking with a poleward barrier jet on the windward side and a chinook (+7 K) on the eastern foot.
- `weather.orography` (C++): exact dry rate and moist rate for slope flow with condensation; the foehn scenario; orientation-controlled blocking; nocturnal cold pools and their daytime erosion; helpers and rejections; a pinned regression. `weather.orography_cli` (Python): CLI output equals the Alps fixture, format rules, hydrostatic surface pressure recomputed, foehn/precipitation/blocking/cold-pool physics for the Alps and the Rockies.

## Scope limits

A single terrain-following layer: no mountain waves, rotors, gap winds, downslope windstorms, snow cover or radiation geometry; blocking is a parametrised drag with a configured stability; the cold-pool and foehn flags are neighbourhood rules of thumb; the wind still follows the sea-level pressure field. Keep each new process behind an explicit model-version change.
