# Ocean circulation v1 — the streams, their salt, and the fresh water

`terraflux-run-ocean-circulation` simulates the upper ocean of a basin: the wind- and density-driven streams (a Gulf-Stream-like western boundary current, its separation and extension, the subpolar gyre), the salinity of the water, the fresh water that rivers and a catchment run pour into it, the points where the exchange happens and how the plume spreads, the mixing fronts, and the deep-water formation that drives the overturning — and what fresh water does to it. Kernel: `include/terraflux/weather/ocean_circulation.hpp` / `src/weather/ocean_circulation.cpp`; request `terraflux.ocean.circulation-run/v1` ([schema](ocean-run-v1.schema.json)); output timeline with an `ocean` block `terraflux.ocean.circulation/v1` ([schema](ocean-circulation-v1.schema.json)). Committed example: [examples/atlantic-streams-request.json](../../examples/atlantic-streams-request.json) → `viewer/public/ocean/engine-timeline.json` (**Load Atlantic streams example**, view **Ocean streams**).

## 1. Model

A **reduced-gravity (1.5-layer) upper ocean** on an Arakawa C-grid: an active layer of thickness `h` (the thermocline depth), velocity `(u, v)`, bulk salinity `S` and temperature `T` over a motionless deep layer of fixed `T_deep`, `S_deep`. Density follows the linear equation of state `ρ = ρ₀[1 − α(T − T₀) + β_S(S − S₀)]`; the reduced gravity `g' = g(ρ_deep − ρ)/ρ₀` is what makes the interface tilt a pressure gradient. Fresh or warm water is light (large `g'`), cold salty water is heavy (small or negative `g'`).

```
du/dt = −∂(g'h)/∂x + f v − r u + τx/(ρ₀h) + A∇²u          (forward–backward Coriolis, free slip)
dv/dt = −∂(g'h)/∂y − f u − r v + τy/(ρ₀h) + A∇²v
dh/dt = −∇·(h u) + (P − E) + rivers/area − sinking + upwelling + sponge
d(hS)/dt = −∇·(h u S) + κ∇²(hS) + sources   (rivers carry S_river, rain dilutes, evaporation leaves the salt)
d(hT)/dt = −∇·(h u T) + κ∇²(hT) + sources + h(T* − T)/τ_T
```

- **Wind-driven gyres.** On a β-plane the wind-stress curl drives the Sverdrup interior and its return is squeezed into the western boundary current of width `r/β` (Stommel): the model reproduces the Sverdrup transport `τ₀πL_x/(ρ₀βL_y)` within the test tolerance and shows no western intensification on an f-plane. With two gyres (subtropical + subpolar) the northward and southward boundary currents meet and the stream leaves the coast — at the cape in the example — as an eastward jet.
- **Fresh water.** River water and rain enter a surface mixed layer of depth `h_mix` as a freshwater content `F` (metres) carried by the currents and mixed down on the time scale `τ_mix`; the **surface salinity** `S_s = S(1 − F/h_mix)` is what a plume looks like from above, where the mixing fronts (`|∇S_s|` above the threshold) are, and what caps convection. The bulk salinity dilutes exactly (`S = S₀V₀/(V₀ + Qt)` for a river into a still basin), evaporation concentrates it exactly (`S = S₀h₀/(h₀ − Et)`). A plume is a buoyant current that the Coriolis force turns to the right along the coast in the northern hemisphere and to the left in the southern (tested).
- **Deep-water formation and the overturning.** Where the surface water is as dense as the deep layer (`g'` from `T` and `S_s` below `formation_reduced_gravity`) it sinks at `(h − h_min)/τ_conv`; the total sinking is the overturning (Sv). The sunk volume returns as upwelling — in the sponge rows when a sponge exists (the deep return leaves the basin, the surface branch flows through it toward the formation sites), otherwise uniformly. Fresh water on top of a formation site raises `g'` above the threshold and switches the sinking off: the meltwater pulse of the example collapses the overturning.
- **Sponge.** Rows at the southern (and/or northern) wall relax `h`, `S`, `T` and the freshwater content toward the initial state and damp the currents, as an open-boundary proxy; the relaxation is a reported budget source.

The budget closes every step: `volume = initial + Σ(rivers + P − E + sponge)` and salt likewise with the sinking/upwelling exchange; both errors are reported per frame.

## 2. Diagnostics

- **Streams**: 4-connected cells faster than `stream_speed_threshold` (two or more). Each stream reports its core cell (fastest), cell count, max and mean speed, mean salinity and temperature, the dominant direction, the transport in Sverdrups across the section through the core (summing `h·u_along·width` over the stream cells of the row or column perpendicular to the flow) and the **core streamline** traced upstream and downstream through the interpolated current field (grid fractions). Streams are sorted by transport.
- **Exchange points**: each river mouth with its discharge, the surface salinity at the mouth, the plume (surface water fresher than the reference by `plume_salinity_deficit`, connected to the mouth): area, freshwater volume held, extent, minimum salinity and the bearing of its freshwater centroid from the mouth.
- **Formation sites** (cells with sinking), **mixing fronts**, the basin-mean salinity, minimum surface salinity and fastest current.

## 3. Configuration

| Section | Keys (default) |
|---|---|
| `time` | `start_time_utc`, `timestep_seconds` (gravity-wave CFL `dt·c(1/dx + 1/dy) ≤ 1` with `c = √(2.25 g'h₀)`, `f·dt ≤ 0.5`, `r·dt ≤ 1`, explicit viscous bound), `steps`, `spinup_steps` (unrecorded; gyres need the Rossby crossing time, years) |
| `grid` | shared local metric grid |
| `basin` | `terrain` (values or the shared synthetic builder; continents are tall ridges) with `sea_level_m`, **or** `ocean_mask`; optional `sponge {south_rows, north_rows}` (strength ramps from 1 at the wall to 0) |
| `ocean` | `initial_thickness_m` (500), `initial_salinity_psu` (35), `initial_temperature_k` (291.15) |
| `physics` | EOS (`reference_density_kg_m3` 1025, `thermal_expansion_per_k` 2e−4, `haline_contraction_per_psu` 7.6e−4, references 283.15 K / 35 psu, deep 277.15 K / 34.9 psu), `minimum_reduced_gravity_m_s2` (1e−3), `linear_friction_s` (1e−6), `lateral_viscosity_m2_s` (500), `tracer_diffusivity_m2_s` (500), `minimum_thickness_m` (20), `temperature_relaxation_s` (30 d), `formation_reduced_gravity_m_s2` (4e−3), `convection_time_scale_s` (10 d), `sponge_time_scale_s` (5 d), `upwelling_in_sponge` (true), `mixed_layer_depth_m` (50), `freshwater_mixing_time_s` (30 d), `coriolis_mode` (beta_plane; none/f_plane/prescribed + `coriolis_parameter_s`), `stream_speed_threshold_m_s` (0.15), `plume_salinity_deficit_psu` (0.5), `front_gradient_psu_per_100km` (0.5) |
| `forcing.wind` | `gyre` (`stress_amplitude_pa`, `gyres` 1–4, `south_margin_m`, `wall_taper_m`): `τx = −τ₀cos(nπy'/L')`; `uniform`; or `coupled_timeline` (stress `ρ_a C_d |U| U` and the surface-temperature target sampled from a coupled-wind timeline) |
| `forcing.surface_temperature` | `south_k`, `north_k` (linear target) and `cold_patches[{east_m, north_m, radius_m, temperature_k}]` (formation sites) |
| `forcing.net_precipitation` | `south_m_s`, `north_m_s` (P − E, linear) |
| `forcing.rivers[]` | `name`, `east_m`, `north_m` (snapped to the nearest water cell), `discharge_m3_s`, `temperature_k`, `salinity_psu`, `events[{start_seconds, duration_seconds, discharge_m3_s}]`, `catchment_timeline_path` + `catchment_scale` (the catchment run's ocean + edge outflow rate between its frames, scaled) |
| `snapshot_every_steps` | 1; ≤ 240 frames |

## 4. Output

Timeline `frames` (forecast/v1 with the surface-temperature target and the equivalent 10 m wind), `terrain` and `ocean`: `ocean_mask`, `sponge`, `initial_volume_m3`, `initial_salt_kg`, request/basin/forcing echoes and per frame `u_m_s`, `v_m_s` (cell centres), `thickness_m`, `salinity_psu`, `surface_salinity_psu`, `temperature_k`, `sinking_m_s`, `front`, `stream_id`, `overturning_sv`, `mean_upwelling_m_s`, `river_input_m3_s`, `net_precipitation_m3_s`, `sponge_volume_m3_s`, `sponge_salt_kg_s`, `volume_m3`, `salt_kg`, `kinetic_energy_j`, budget errors, `cumulative_m3 {river, net_precipitation, sinking, sponge}`, `streams[]`, `exchange_points[]`, formation and salinity summaries. Values are rounded to 4–5 significant digits.

## 5. Viewer

The **Ocean streams** view colours the water by surface salinity (fresh ↔ salty), layer salinity, current speed, temperature, thermocline depth or freshwater fraction, over the land from the terrain, and composes: the streams as core streamlines sized by transport and coloured by speed with their labels (western boundary current, northern jet, …), current arrows scaled by speed, the river mouths ⊕ with discharge and plume salinity, the plumes (pale tint), the mixing fronts (white), the formation sites ⇩ and the sponge rows. The field explorer adds current speed, surface and layer salinity, layer temperature, thermocline depth, freshwater fraction and sinking rate; the inspector reports the local state, the stream a cell belongs to and the exchange point at a mouth; the metrics panel lists the main stream, the overturning and formation area, freshwater in, salinity statistics, plume totals, and plots the overturning, the main-stream transport and the river input over time.

## 6. Verification

`tests/weather/ocean_circulation_test.cpp` (`weather.ocean_circulation`): Stommel/Sverdrup — the western boundary current carries the Sverdrup transport within 30 %, balances the interior, is ≥ 2.5× faster than the eastern side, the gyre sense is right, streams are detected with a northward core streamline along the west, and the f-plane shows no western intensification; the reference Stommel width and Sverdrup transport; exact dilution (`S₀V₀/(V₀+Qt)`), salt conservation, the freshwater-content step and surface salinity, exact evaporation/rain laws; plume deflection right (NH) and left (SH) with fronts and freshwater fraction; deep-water formation in a cold north with uniform upwelling, budgets, the surface branch flowing north, and a meltwater river weakening the overturning by > 30 %; EOS/stress helpers; contract rejections; a pinned regression. `tests/tools/ocean_circulation_check.py` (`weather.ocean_circulation_cli`): a coarse run with a river, a meltwater pulse and a catchment-fed river, the volume budget per frame, stream/plume/formation consistency, coupled-wind forcing, and invalid requests producing no output. The viewer validates the block on load (`viewer/src/ocean.js`, tests in `ocean.test.js`).

## 7. Limitations

- One active layer: no vertical structure, no barotropic mode, no bottom topography, no eddies (linear momentum, 50–60 km cells); boundary currents are grid-limited in width and speed, and a closed basin produces boundary jets along walls where the real ocean continues (the sponge mitigates the southern one). The overturning is a parametrised sinking, not a resolved deep circulation.
- The example is an idealised Atlantic-like basin with synthetic continents and forcing; the magnitudes (streams of order 10–70 Sv, overturning of order 1 Sv, plumes of 10³–10⁵ km²) are illustrative, not a reanalysis.
- Sea ice, tides, waves, the equator and the wind's response to the sea surface are absent.
