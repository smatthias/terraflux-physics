# Catchment hydrology v1 — where the rain goes, and what massive rain does

`terraflux-run-catchment-hydrology` takes a terrain grid and a rainfall / temperature forcing and answers two questions per cell and per step: *where does the water go* (rivers, lakes, soil, groundwater, ocean) and *what does it do* (flooding, erosion and deposition, landslides). The kernel is `include/terraflux/weather/catchment_hydrology.hpp` / `src/weather/catchment_hydrology.cpp`; the request format is `terraflux.hydrology.catchment-run/v1` ([schema](catchment-run-v1.schema.json)) and the output is a viewer timeline with a `hydrology` block `terraflux.hydrology.catchment/v1` ([schema](catchment-hydrology-v1.schema.json)). The committed example is [examples/alpine-flood-request.json](../../examples/alpine-flood-request.json) → `viewer/public/flood/engine-timeline.json` (**Load alpine flood example** in the viewer).

It complements, and does not replace, the millimetre-scale closed-domain surface-water prototype ([surface water v1](surface-water-v1.md), [infiltration](infiltration-v1.md), [losses](water-losses-v1.md), [outlets](surface-outlets-v1.md)) that routes observed radar rainfall on small grids. Catchment hydrology v1 works on kilometre cells over days, with a drainage network, lakes, an aquifer and the hazard layer.

## 1. The drainage network (static, from the terrain)

| Step | Method | Result |
|---|---|---|
| Ocean | cells with elevation ≤ `domain.sea_level_m` | sinks with a fixed head; not routed |
| Depression filling | priority flood seeded at the ocean and, if `domain.open_edges`, every edge cell; a 10⁻⁷ m increment enforces strict descent across flats | a filled surface on which every land cell has a downhill path |
| Lakes | connected cells filled by more than 5 mm | each lake has cells, a spill elevation (lowest filled cell), a capacity ∑(spill − floor)·A, an area and an outlet (the receiver of the spill cell: land, another lake, ocean or edge) |
| Flow direction | D8: the steepest of eight neighbours on the filled surface | `downstream` receiver (−1 = leaves across the edge), reach length, slope floored at `channels.minimum_slope` |
| Accumulation | upstream-first order (descending filled elevation) | upstream area, Strahler order, `channel` mask where area ≥ `channels.channel_initiation_area_m2`, nearest downstream channel (`channel_target`), distance to the outlet |
| Channel geometry | downstream hydraulic geometry: width `w = c_w A_km²^{e_w}`, bankfull depth `d_bf = c_d A_km²^{e_d}` | per channel cell |
| Basins | cells sharing a terminal cell (the coast cell that drains into the ocean, or the edge cell) | sorted by area, largest first |
| Regolith | `D = D₀ · max(f_min, 1 − tanβ / t_thin)` from the terrain gradient | thinner soils on steep slopes; soil capacity = porosity × D |

A closed domain without any ocean cell is rejected (there is nowhere for the water to go).

## 2. The water column and the stores

All stores are equivalent water depths over the cell area (metres) except channel and lake volumes (m³). Per step, in this order:

1. **Precipitation and snow.** Rain falls as snow when the air temperature is below `snow.rain_snow_threshold_k`; snow melts at the degree-day rate `ddf · (T − 273.15 K)`. Rain on lakes goes straight into the lake; rain on the ocean is ignored (it is outside the land budget).
2. **Infiltration** at the wetness-dependent Horton capacity `f = f_min + (f_max − f_min)(1 − s)` with `s` the soil saturation, limited by the remaining soil capacity. What does not infiltrate stays on the surface: infiltration-excess runoff when the rain is heavier than `f`, saturation-excess runoff when the store is full.
3. **Evapotranspiration** at the potential rate, from surface water first, then from the soil under a linear moisture stress (full rate above field capacity). Lakes evaporate at the potential rate.
4. **Percolation** of the water above field capacity to the aquifer at `percolation_rate · (drainable / drainable capacity)`, limited by the remaining aquifer space.
5. **Groundwater.** An unconfined aquifer of thickness `B` and specific yield `S_y`; the water-table head is `z − B + g/S_y`. Explicit Darcy exchange between four-neighbours uses the donor's saturated thickness: `Δ = K b Δh / L · dt / L`, donor-limited, with the ocean at a fixed head (sea level) receiving submarine discharge and never intruding. A linear-reservoir **baseflow** `g · dt / τ_b` is delivered to the nearest downstream channel (or straight to the basin outlet where there is none), and **springs** return any water above the aquifer capacity to the surface. The explicit scheme requires `K B dt / (S_y L²) ≤ 0.25` (validated).

## 3. Routing: overland flow, channels, lakes, the sea

Routing is a kinematic cascade evaluated on the start-of-step stores (Jacobi), so water moves at most one cell per step:

- **Overland**: water above the detention storage moves to the receiver at the Manning velocity `v = h^{2/3} S^{1/2} / n_o` with the travel fraction `min(1, v dt / L)`. On channel cells it first refills the channel while the reach is below bankfull; the rest travels on as floodplain flow.
- **Channels**: depth `d = V / (w L)`, `v = d^{2/3} S^{1/2} / n_c`, discharge `Q = v w d`, outflow `min(V, Q dt)`. Water above bankfull spills on to the cell's surface store (the floodplain) and returns when the reach has room.
- **Lakes** receive everything routed into any of their cells, fill to the spill level and overflow at the outlet; lakes are processed from the highest spill downward so cascades resolve within the step. The level comes from the stage–volume curve of the lake floor.
- **Ocean and edges**: water delivered to an ocean cell or across an open edge leaves the budget as `ocean_discharge` / `edge_outflow`.

The time step must satisfy `dt ≤ 3600 s` and `dt ≤ 2 × spacing / (1 m/s)`; faster channel reaches are retarded to one cell per step (the cascade remains conservative; the hydrograph is then smoothed). The example uses 600 s on 1 km cells.

## 4. Hazards

- **Flooding.** The water on the surface store (overland sheet flow, ponding and overbank floodplain water) with the overland velocity gives the class: none below `flood_depth_threshold_m`; low (< 0.3 m and d·v < 0.3 m²/s), medium (< 1 m, d·v < 0.6), high (< 2 m, d·v < 1.5), extreme otherwise — the usual depth × velocity hazard bands. Channels above bankfull are flagged through `channel_depth_m ≥ bankfull_depth_m`.
- **Erosion and deposition.** Unit stream power `ω = ρ g q S` (q = discharge per unit width, overland `v h` or channel `Q / w`). Above the critical value `ω_c` the flow detaches sediment at `k_e (ω − ω_c)` per unit bed area and can carry `k_t (ω − ω_c)` per unit width. Sediment travels with the water (same travel fraction); where the load exceeds capacity it settles with `min(1, v_s dt / h)`. Both are bookkept as cumulative depths per cell (bulk density `ρ_b`): erosion in steep, full channels; deposition on floodplains, in lakes (at the entry cell) and as the sediment yield to the ocean/edge. Mass closes: erosion + landslide debris = deposition + in transit + yield.
- **Landslides.** Infinite-slope factor of safety `FS = [c + (γ_s − m γ_w) D cos²β tanφ] / [γ_s D sinβ cosβ]` with the saturated fraction `m` from the soil store (`soil / capacity`) or the water table inside the regolith, whichever is larger. A cell with `FS < 1` fails: the regolith above the residual depth, over the `failure_area_fraction` of the cell, becomes debris that runs downslope until the slope is below `runout_tan` or a channel, lake, ocean or edge is reached, where it is deposited; the pore water of the failed layer is released to the surface. The cell stays `unstable` (no repeat failure) until `FS` recovers above `restabilisation_factor_of_safety`. The empirical intensity–duration threshold `I = a D^b` (Caine 1980: 14.82 mm/h · D_h^−0.39) is tracked per cell as an independent warning (`rainfall_threshold_exceeded`); a dry spell of `dry_spell_reset_s` resets the storm clock.

## 5. Configuration

All sections except `time`, `grid`, `terrain` and `forcing` are optional; omitted keys take the defaults below (echoed in `hydrology.request`).

| Section | Keys (default) |
|---|---|
| `time` | `start_time_utc`, `timestep_seconds` (≤ 3600 and ≤ 2×spacing), `steps`, `spinup_steps` (0; dry steps before the record so rivers carry baseflow; totals reset afterwards) |
| `grid` | the shared local metric grid (`nx`, `ny`, origin, spacing, anchor) |
| `terrain` | `values` per cell **or** the shared synthetic builder: `base_elevation_m` (0), `slope_east_m_per_m`, `slope_north_m_per_m` (0), `ridges[{orientation_deg, center_east_m, center_north_m, height_m (negative = valley), half_width_m, length_m}]`, `basins[{center_east_m, center_north_m, depth_m, radius_m}]`, `roughness_m` (0; deterministic hashed noise, makes the drainage dendritic), `roughness_seed` (1) |
| `domain` | `sea_level_m` (0), `open_edges` (true) |
| `soil` | `regolith_depth_m` (1), `slope_thinning_tan` (1.5), `minimum_depth_fraction` (0.3), `porosity` (0.4), `field_capacity_fraction` (0.5), `infiltration_max_m_s` (1e−5 = 36 mm/h), `infiltration_min_m_s` (1e−6), `percolation_rate_m_s` (2e−6), `potential_evapotranspiration_m_s` (3.5e−8 ≈ 3 mm/day), `detention_storage_m` (0.002), `initial_saturation` (0.3) |
| `groundwater` | `aquifer_thickness_m` (20), `specific_yield` (0.05), `hydraulic_conductivity_m_s` (5e−5), `baseflow_time_scale_s` (90 days), `initial_fill_fraction` (0.5) |
| `channels` | `channel_initiation_area_m2` (4 km²), `manning_overland` (0.1), `manning_channel` (0.035), `width_coefficient_m` (4) / `width_exponent` (0.5), `bankfull_depth_coefficient_m` (0.6) / `bankfull_depth_exponent` (0.3), `minimum_slope` (1e−4), `lake_initial_fill_fraction` (1) |
| `snow` | `rain_snow_threshold_k` (274.15), `degree_day_factor_m_k_s` (4 mm/K/day), `initial_snow_m` (0) |
| `hazards` | `erodibility_kg_j` (1e−6), `critical_stream_power_w_m2` (30), `transport_capacity_kg_j` (1e−3 kg·m/J), `settling_velocity_m_s` (1e−3), `bulk_density_kg_m3` (1600), `cohesion_pa` (2000), `friction_angle_deg` (32), `soil_unit_weight_n_m3` (18000), `water_unit_weight_n_m3` (9810), `minimum_failure_tan` (0.05), `runout_tan` (0.2), `residual_depth_fraction` (0.2), `failure_area_fraction` (0.05), `restabilisation_factor_of_safety` (1.1), `flood_depth_threshold_m` (0.1), `intensity_duration_a_mm_h` (14.82), `intensity_duration_b` (−0.39), `dry_spell_reset_s` (6 h) |
| `forcing.air_temperature` | `sea_level_k` (287), `lapse_rate_k_m` (0.0065), `diurnal_amplitude_k` (0), `diurnal_phase_seconds` (0): `T = T₀ − Γ max(0, z) + A sin(2π (t − φ)/86400)` |
| `forcing.rainfall` | `events[{start_seconds, duration_seconds, rate_mm_h, center_east_m, center_north_m, radius_m (Gaussian; omit for uniform), orographic_enhancement_per_km}]` and/or `coupled_timeline_path` (+ `coupled_time_offset_seconds`): rain = `orography.orographic_precipitation_rate_mm_h` + `precipitation.rate_mm_h × precipitation.duration_fraction` (the outcome rate is the intensity while it rains, so the cell-mean rate over the interval is rate × duration, as in `accumulation = rate × duration × interval`) and air temperature from a coupled-wind timeline, sampled at the nearest coupled cell and held piecewise constant between its frames. The hydrology grid must lie inside the coupled grid (same local frame) |
| `snapshot_every_steps` | 1; the timeline keeps ≤ 240 frames |

Event rates are evaluated at the interval midpoint; the orographic enhancement multiplies the rate by `1 + f · z/1000`. Individual events are limited to 500 mm/h and the summed rate per cell to 3600 mm/h.

## 6. Output

The timeline carries `frames` (forecast/v1 with the forcing air temperature and zero wind), `terrain` (terrain-grid v1) and `hydrology`:

- `network`: `downstream`, `flow_direction_deg` (0 = east, counter-clockwise; null on ocean/terminal cells), `terminal` (none/ocean/edge), `ocean`, `upstream_area_km2`, `strahler_order`, `channel`, `lake_id`, `basin_id`, `distance_to_outlet_km`, `slope`, `channel_width_m`, `bankfull_depth_m`, `regolith_depth_m`, `channel_target`, `filled_depth_m`, `lakes[]` (cells, area, capacity, spill elevation, outlet) and `basins[]` (outlet cell, area, terminal kind; largest first).
- `frames[]`: `rainfall_rate_mm_h`, `snow_mm`, `surface_water_mm`, `soil_saturation`, `water_table_depth_m`, `discharge_m3_s`, `channel_depth_m`, `flood_class` (0–4), `erosion_mm`, `deposition_mm` (cumulative), `sediment_flux_kg_s`, `factor_of_safety` (null where undefined), `landslide`, `unstable`, `rainfall_threshold_exceeded`, per-lake `lake_level_m` / `lake_volume_m3`, `storage_m3` (snow, surface, soil, groundwater, channels, lakes, total), `cumulative_m3` (precipitation, evapotranspiration, ocean_discharge, edge_outflow, infiltration, percolation, baseflow, snowmelt), `balance_error_m3`, `sediment_yield_kg`, landslide and flood counters, `flooded_area_km2`, `max_discharge_m3_s`, `peak_discharge_cell`. Values are rounded to five significant digits.
- `events[]`: landslides with time, cell, runout cell and length, factor of safety, volume and where the debris ended (`slope`, `channel`, `lake`, `ocean`, `edge`).
- `initial_storage_m3`: in every frame `storage.total + ET + ocean_discharge + edge_outflow − precipitation = initial_storage_m3` to 10⁻⁶ relative; the viewer and the CLI check enforce it.

## 7. Viewer

The **Catchment** view composes, over the hillshaded relief (or basins / water-table depth / soil saturation as base): the river network as reaches sized by upstream area and brightened by discharge, lakes at their level with labels, overland and floodplain water, snow, rainfall tint and optional D8 arrows. Clicking a cell traces its water to the outlet (white path; the inspector reports the distance, lakes passed and basin). The **Hazards** view shows the flood classes, reaches above bankfull (orange), erosion (red) / deposition (tan), landslides ▲ with their runout lines, rainfall-threshold warnings (!), still-unstable slopes and an optional factor-of-safety shading. The field explorer adds discharge, surface/flood water, soil saturation, water-table depth, snowpack, erosion, deposition, factor of safety and flood class layers; the metrics panel lists peak discharge, flooded area, class counts, landslides and debris, sediment yield, runoff ratio, the stores and the cumulative budget, and a hydrograph (with the hyetograph) at the peak or the selected reach.

## 8. Verification

`tests/weather/catchment_hydrology_test.cpp` (`weather.catchment_hydrology`) covers: every land cell of a tilted plane drains to the sea along strictly descending receivers, areas accumulate, basins partition the land, channels start at the initiation area and hydraulic geometry follows the power laws; open edges drain across the edge and a closed domain without ocean is rejected; a pit becomes a lake with the analytic spill level, capacity and outlet, fills monotonically, never exceeds its capacity and nothing reaches the sea before it spills; steady rain on a closed basin closes the budget every step and the hourly outflow equals rain × land area within 5 %; the exact Horton, saturation-excess, percolation, spring, degree-day melt and evapotranspiration rates; explicit Darcy transfers with the donor thickness, conservation, and the linear-reservoir baseflow; the infinite-slope formula (dry cohesionless FS = tanφ/tanβ, saturation lowers FS, a 35° slope fails only when saturated), the Caine threshold, Manning velocity and the flood classes; a cloudburst on a steep ridge fails the saturated slopes exactly once per wet period with debris deposited where the runout ends, floods the plain, exceeds the intensity–duration threshold, erodes the steep reaches more than the flat ones and closes the sediment mass balance; slopes restabilise as they dry; contract rejections; and a pinned regression. `tests/tools/catchment_hydrology_check.py` (`weather.catchment_hydrology_cli`) runs the CLI on a shortened example, checks the network, the per-frame contract and the water budget, drives the hydrology from a synthetic coupled timeline and confirms invalid requests produce no output. The viewer validates the block on load (`viewer/src/hydrology.js`, tests in `hydrology.test.js`).

## 9. Limitations

- Routing is a kinematic cascade without momentum, backwater or hydraulic structures; travel is limited to one cell per step, so peaks are smoothed when the channel Courant number exceeds one. Floodplain water is conveyed with the overland law on the same D8 path as the channel.
- Terrain is static: erosion, deposition and landslide debris are bookkept as depths but do not change the elevations or the network during the run.
- The aquifer is a single unconfined layer with uniform properties; soils are uniform apart from the slope thinning; vegetation, frozen ground, snow redistribution and lake ice are absent.
- The infinite-slope model is a cell-average screening tool (hence `failure_area_fraction`); it does not resolve landslide geometry, and the stream-power erosion law has no armouring or bedrock/alluvial distinction. Parameter defaults are plausible mid-latitude values, not calibrations.
- Precipitation on ocean cells is not part of the catchment budget and the ocean level is fixed; the ocean model takes the rain on the sea and the diffuse runoff of the coastal band (`terraflux-run-ocean-circulation` with `forcing.rain`, see [ocean-circulation-model.md §7](ocean-circulation-model.md#7-rain-over-the-sea-and-the-coast-evaporation-airsea-heat-and-the-layered-column)). Storm surge from the storm module is not coupled to the coast.
