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
- **Exchange points**: each river mouth with its discharge, the surface salinity at the mouth, the plume (surface water fresher than the reference by `plume_salinity_deficit`, connected to the mouth): area, freshwater volume held, extent, minimum salinity and the bearing of its freshwater centroid from the mouth (0 for a plume centred on its mouth, such as a one-cell plume, so rounding cannot flip it between 0° and 180° across compilers).
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

`tests/weather/ocean_circulation_test.cpp` (`weather.ocean_circulation`) covers the slab: Stommel/Sverdrup — the western boundary current carries the Sverdrup transport within 30 %, balances the interior, is ≥ 2.5× faster than the eastern side, the gyre sense is right, streams are detected with a northward core streamline along the west, and the f-plane shows no western intensification; the reference Stommel width and Sverdrup transport; exact dilution (`S₀V₀/(V₀+Qt)`), salt conservation, the freshwater-content step and surface salinity, exact evaporation/rain laws; plume deflection right (NH) and left (SH) with fronts and freshwater fraction; deep-water formation in a cold north with uniform upwelling, budgets, the surface branch flowing north, and a meltwater river weakening the overturning by > 30 %; EOS/stress helpers; contract rejections; a pinned regression. `tests/tools/ocean_circulation_check.py` (`weather.ocean_circulation_cli`): a coarse run with a river, a meltwater pulse and a catchment-fed river, the volume budget per frame, stream/plume/formation consistency, coupled-wind forcing, and invalid requests producing no output. The viewer validates the block on load (`viewer/src/ocean.js`, tests in `ocean.test.js`).

## 7. Rain over the sea and the coast, evaporation, air–sea heat and the layered column

Three optional extensions close the ocean's exchange with the air and the land. Each is opt-in: without `forcing.rain`, `forcing.air_sea` and `physics.thermal_layers` the model is the 1.5-layer slab above and its output is unchanged bit for bit (the Atlantic fixture is regenerated and compared in CTest). Committed example: [examples/ocean-heat-rain-request.json](../../examples/ocean-heat-rain-request.json) → `viewer/public/ocean-heat/engine-timeline.json` (**Load ocean heat & rain example**): a late-summer subtropical basin with a western continent, a tropical rain band crossing the sea to landfall, a storm over the coastal plain, steady ITCZ rain, a cold dry outbreak and the daily sun.

### 7.1 Rain on the sea and on the coast

Rain comes from events (uniform or a Gaussian footprint, optionally moving with `velocity_east/north_m_s`, optionally with its own temperature) and/or a coupled-wind timeline: the **cell-mean** precipitation `rate_mm_h × duration_fraction` of the precipitation outcomes (the outcome rate is the intensity while it rains; `accumulation = rate × duration × interval`) plus the orographic precipitation, with the outcomes' surface wet-bulb temperature as the rain temperature.

- **On the sea** the rain enters the top of the column as fresh water at its own temperature (the event temperature; else the coupled wet bulb; else the wet bulb of the air-sea atmosphere; else the surface-temperature target + `temperature_offset_k`). It adds volume, no salt, and cools the surface when it is colder than the sea; the equivalent rain heat flux `ρ c_p P (T_rain − SST)` is reported. In the layered column the rain stays in the thin top layer as a **fresh lens**, buoyant even when colder, which caps convection until wind mixing erodes it.
- **On land** within `coastal_runoff.band_m` of the sea (the coastal band) the rain drains directly to the nearest sea cell (ties: lowest index) through a linear reservoir, solved exactly over each step: `W₁ = W₀e^{−Δt/τ} + c P τ (1 − e^{−Δt/τ})`, runoff `= W₀ + c P Δt − W₁`; the rest `(1 − c) P` infiltrates or evaporates on land and is reported as a loss. Land beyond the band is left to the rivers and the catchment model: its rain is reported (`rain_inland`) but not routed, so it is not counted twice. Runoff arrives at the rain temperature of its source cell.
- Budgets: `volume = initial + river + net_precipitation + sponge + rain_sea + coastal_runoff − evaporation`; on land `rain_coast = coastal_runoff + coastal_loss + (storage − initial storage)`.

### 7.2 Evaporation and the air–sea heat exchange

With `forcing.air_sea` the surface exchanges heat and water with an atmosphere given analytically (air temperature = surface-temperature target + `air_temperature_offset_k` + a diurnal cycle peaking at 15 h local solar time + moving Gaussian **air masses** with temperature anomaly and humidity; relative humidity and cloud fraction linear south → north; rain above 0.1 mm/h moistens the air to ≥ 95 % and clouds it to ≥ 90 %) or from a coupled-wind timeline (air temperature, specific humidity, pressure, cloud fraction, wind and the storm wind speed, which is added in quadrature to the wind speed and drives the vertical mixing; its direction is unknown, so it does not enter the momentum). The wind speed of the analytic atmosphere is the 10 m wind equivalent to the stress. Bulk formulae, positive into the ocean:

```
Q_sw = (1 − albedo) SW↓,   SW↓ = Zillman (1972) clear sky × Reed (1977) (1 − 0.62 C), sun from the date, time, latitude and longitude
Q_lw = −ε σ T_s⁴ (0.39 − 0.05 √e_a[hPa]) (1 − λC²) − 4 ε σ T_s³ (T_s − T_a)        (Berliand & Berliand 1952)
Q_h  = ρ_a c_pa C_H U (T_a − T_s)
Q_e  = −L_v(T_s) ρ_a C_E U (q_s − q_a),   q_s = 0.98 q_sat(T_s),   L_v = 2.501e6 − 2370 (T_s − 273.15)
E    = ρ_a C_E U (q_s − q_a) / ρ_fw   (m/s of fresh water; negative = condensation),   U = max(U10, minimum_wind)
```

The surface energy balance of the top layer (the slab without layers) is solved **implicitly** — backward Euler for the end-of-step surface temperature with Newton iterations — so it is stable for thin top layers and strong winds, and the reported fluxes are those at the end-of-step temperature: they sum exactly to the heat applied, and the evaporation is the water of the latent heat flux. Evaporation removes fresh water from the top (the salt stays: the surface gets saltier), limited by the minimum thickness. Without air-sea exchange the surface relaxes toward the target as before (the layered column applies it as a surface flux `ρ c_p h_mix (T* − SST)/τ_T`). The heat budget (`ρ₀ c_p Σ h (T − 273.15) A`) closes every step with every source entering at its own temperature; the largest per-step residual since the previous frame is reported.

### 7.3 The layered temperature and salinity column

`physics.thermal_layers.count` (2–40) splits the active layer into sigma layers of fixed fractions `s_k` of the thickness `h`, geometrically stretched so that the top layer is `surface_layer_m` thick at the initial thickness; each carries a temperature and a salinity. All sublayers move with the layer velocity, so their horizontal transport (upwind flux form with the same diffusion) sums exactly to the bulk transport: the bulk `T` and `S` of the 1.5-layer dynamics stay `Σ s_k T_k` and `Σ s_k S_k`, and the density that drives the currents is unchanged in form. After the horizontal transport, every step:

1. **Sources where they act:** rain, rivers and coastal runoff into the top layer; upwelled deep water into the bottom layer; sinking removes every sublayer in proportion; evaporation takes fresh water from the top layer.
2. **Surface energy balance** of the top layer (7.2); shortwave penetrates with the two-band profile `I(z)/I₀ = R e^{−z/ζ₁} + (1 − R) e^{−z/ζ₂}` (Paulson & Simpson 1977, Jerlov type I: R = 0.58, ζ₁ = 0.35 m, ζ₂ = 23 m); the light left at the layer bottom is absorbed by the bottom layer.
3. **Conservative remapping** of heat and salt to the sigma fractions of the new thickness.
4. **Convective adjustment**: statically unstable neighbours (linear equation of state in T and S) are merged, heat and salt conserved.
5. **Wind mixing** by the Kraus–Turner energy budget: `m₀ ρ₀ u*³ Δt` of turbulent kinetic energy per step deepens the mixed layer while it pays for the potential-energy increase `g Σ (ρ_j − ρ_mix) z_j dz_j`; the next layer is entrained partially when the energy runs out; unused energy is dissipated.
6. **Shear mixing** by the bulk Richardson criterion (Price, Weller & Pinkel 1986): the wind drives a slab current in the mixed layer, estimated over an inertial period as `ΔU = u*²/(max(|f|, 10⁻⁵) h_ml)`; the layer entrains the next while `Ri_b = g Δρ h_ml/(ρ₀ ΔU²) < Ri_c` (0.65). A thin fresh lens under a moderate wind is eroded within hours to days; in calm weather it persists.
7. **Background diffusion** of heat and salt, implicit (unconditionally stable), no flux through the surface and the layer bottom.

The surface salinity of the plumes, fronts, lenses and the formation criterion is then the top-layer salinity, and the sinking criterion uses the top-layer temperature (the column sinks when its surface is dense). The column starts from `T(z) = T_deep + (T_s − T_deep) exp(−max(0, z − mld₀)/scale)` with a uniform or target surface temperature, uniform salinity. Diagnostics per cell: SST (top layer), **mixed-layer depth** (density exceeds the top by 0.03 kg/m³, interpolated between layer midpoints), **isothermal-layer depth** (temperature departs by 0.2 K); a gap above 1 m is a **barrier layer** (de Boyer Montégut et al. 2004 thresholds).

### 7.4 Surface lenses

Connected water whose surface is fresher than the layer by `lens_salinity_deficit_psu` is a surface lens. Every sea cell keeps a decaying record (e-folding `lens_memory_s`, 10 days) of the fresh water it received from rain, coastal runoff and rivers; a lens reports the shares of its cells' records and its kind is the largest share (`rain`, `coastal_runoff`, `river`; `other` when nothing is recorded). A storm's rain trail therefore stays a rain lens after the rain has moved on, and a nearshore lens fed by a storm on the coast shows both its direct rain and the runoff that keeps arriving after the storm.

### 7.5 Configuration

| Section | Keys (default) |
|---|---|
| `physics` | `heat_capacity_j_kg_k` (3990), `lens_salinity_deficit_psu` (0.2), `lens_memory_s` (864000) |
| `physics.thermal_layers` | `count` (required, 2–40), `surface_layer_m` (5), `background_diffusivity_m2_s` (1e−5), `wind_mixing_efficiency` (m₀ 0.5), `bulk_richardson_number` (0.65; 0 disables the shear mixing), `initial_mixed_layer_depth_m` (30), `initial_thermocline_scale_m` (150), `shortwave_red_fraction` (0.58), `shortwave_red_depth_m` (0.35), `shortwave_blue_depth_m` (23), `mld_density_threshold_kg_m3` (0.03), `ild_temperature_threshold_k` (0.2), `initial_surface_temperature` (`uniform` or `surface_temperature_target`) |
| `forcing.rain` | `events[{start_seconds, duration_seconds, rate_mm_h (≤ 500), center_east_m, center_north_m, radius_m, velocity_east_m_s, velocity_north_m_s, temperature_k}]` and/or `coupled_timeline_path` (+ `coupled_time_offset_seconds`, `interpolation`); `temperature_offset_k` (−2); `coastal_runoff {band_m (0 = off), runoff_coefficient (0.6), time_scale_s (86400)}` |
| `forcing.air_sea` | `air_temperature_offset_k` (−1), `diurnal_amplitude_k` (0), `relative_humidity_south/north` (0.8), `cloud_fraction_south/north` (0.4), `surface_pressure_pa` (101325), `rain_saturates_air` (true, analytic air only), `shortwave {kind: astronomical \| constant, w_m2}`, `air_masses[{start_seconds, duration_seconds, center_east_m, center_north_m, radius_m, velocity_east/north_m_s, temperature_anomaly_k, relative_humidity}]`, `coupled_timeline_path` (+ offset, `interpolation`), `coefficients {air_density_kg_m3 1.22, air_heat_capacity_j_kg_k 1004, heat_transfer_coefficient 1.1e−3, moisture_transfer_coefficient 1.2e−3, minimum_wind_m_s 1, surface_albedo 0.06, surface_emissivity 0.97, longwave_cloud_factor 0.6, saline_vapour_factor 0.98, fresh_water_density_kg_m3 1000}` |
| `forcing.wind` (coupled) | `interpolation`: `piecewise_constant` (default, as before) or `linear` in time between the coupled frames |

### 7.6 Output

With any extension the `ocean` block adds `processes` (`rain`, `coastal_runoff`, `air_sea`, `thermal_layers`), `initial_heat_content_j`, `thermal_layers {count, fractions}`, `coastal {band_m, runoff_coefficient, runoff_time_scale_s, receiver (per cell: the receiving sea cell or −1), initial_storage_m3}`, the extension settings in `request`, and per frame:

- `rain`: `rate_mm_h`, `coastal_store_mm`, `coastal_inflow_m3_s` (per cell); `rain_on_sea_m3_s`, `rain_on_coastal_sea_m3_s` (sea cells next to land), `rain_on_coast_m3_s`, `rain_inland_m3_s`, `coastal_runoff_m3_s`, `coastal_loss_m3_s`, `coastal_storage_m3`, `coastal_balance_error_m3`, `max_rate_mm_h`, `raining_sea_cells`, `raining_land_cells`;
- `air_sea`: per cell `relative_humidity`, `cloud_fraction`, `shortwave_w_m2`, `longwave_w_m2`, `sensible_w_m2`, `latent_w_m2`, `rain_heat_w_m2`, `net_heat_w_m2` (three significant digits) and `evaporation_mm_day`; `evaporation_m3_s`, mean and maximum evaporation and the basin-mean fluxes (the air temperature and the 10 m wind are the forecast frame fields);
- `thermal`: `layer_temperature_k` and `layer_salinity_psu` (one array per layer, top first), `sst_k`, `mixed_layer_depth_m`, `isothermal_layer_depth_m`, their statistics and `barrier_layer_cells`;
- `surface_lenses` (largest 20: kind, `source_shares`, freshest cell, cells, area, minimum surface salinity, deficit, freshwater volume, mean SST), `heat_budget {heat_content_j, heat_balance_error_j, surface_heat_w, cumulative_surface_heat_j}` and the cumulative volumes `rain_sea`, `rain_coast`, `rain_inland`, `coastal_runoff`, `coastal_loss`, `evaporation`.

The first frame after the spin-up shows the fluxes of the last spin-up step; its cumulative totals are zero. The fixture is built and checked with `tools/build_ocean_example.py` (compact JSON, numbers to 1e−9 relative; the per-step budget residuals, which are rounding noise, to absolute bounds).

### 7.7 Viewer

In the **Ocean streams** view the water colour adds sea-surface temperature (over the run's range), mixed-layer depth, net heat flux (blue loss ↔ red gain) and evaporation; overlays show rain on sea and land by rate, the coastal band that drains directly to the sea (tan), coastal runoff arrows from draining land to the receiving sea cell (length ∝ water still on the land) and surface-lens labels by source. The field explorer adds sea-surface temperature, mixed-layer depth, net heat flux, latent heat flux, evaporation, rain on sea and coast and the coastal store. The inspector decomposes the heat into sun, longwave, sensible and latent (and rain), gives evaporation, humidity, cloud, rain and arriving runoff for a sea cell and, for land, whether its rain drains to the sea (and where) or is left to the rivers. The metrics strip lists SST, mixed layer and barrier layers, net heat and its parts, evaporation, rain on sea/coast/inland, runoff and the water still on land, rain − evaporation over the sea, the lenses, the heat gained and its budget residual and the cumulative volumes; time series show SST and mixed layer, net heat flux and the fresh water exchanged; the **water column** panel shows the temperature and salinity profile with the mixed and isothermal layer depths at the current time and a time–depth temperature section over the run (square-root depth), for the selected sea cell or, by default, the cell that cooled most. The playback line reports whether it is raining on sea and land or whether the coast is still draining.

### 7.8 Verification

`tests/weather/ocean_heat_rain_test.cpp` (`weather.ocean_heat_rain`): the bulk formulae against hand calculations (Dalton evaporation, latent = L_v E, sensible, Berliand longwave clear and overcast, gustiness floor, condensation over a cold sea, albedo); the sun (equinox and solstice noon, longitude, winter sun, Zillman clear sky, Reed cloud factor, daily mean); the layer geometry and the two-band transmission; the initial thermocline; rain on the open sea (exact volume, salt conserved, a fresh and cooler lens in the top layer that is statically stable and that wind mixing erodes, the slab's weaker signal); coastal rain (receivers in the band, coastal/inland split, land budget, loss share, exact linear-reservoir recession, sea volume = runoff, nearshore freshening); evaporation under dry wind (volume loss, salt conserved, the concentration law, latent heat = L_v E at the surface temperature, a saltier surface, cooling, humid air evaporating less); sunshine (each layer absorbs exactly its share of the shortwave, the heat content grows by the absorbed sunshine, a shallow warm layer under calm, a deeper mixed layer and cooler surface with wind); shear mixing to the critical Richardson number (a light breeze leaves the lens, a wind erodes it); convective deepening under a cold-air outbreak with no static instability left; layer–bulk consistency and closed budgets under a gyre with a warm western boundary current; deep-water formation under an outbreak and a fresh rain cap weakening it; the legacy slab's budgets; contract rejections; and a pinned regression. Every case checks the volume, salt and heat budgets each step. `tests/tools/ocean_heat_rain_check.py` (`weather.ocean_heat_rain_cli`) runs a short re-timed version of the example and the ocean under the engine's hurricane run (it asserts a cold wake — more cooling and a deeper mixed layer under the storm winds than elsewhere, evaporation above 10 mm/day; in this run the surface cools by about 0.6 K under weak winds, 1.2 K under 15–30 m/s and 3.7 K under winds above 30 m/s, and the mixed layer deepens from 25 to about 50 m there), checks the output contract and every budget identity from the files alone, the flux and evaporation consistency, the source attribution of the lenses, and that invalid requests produce no output; with `jsonschema` installed it validates requests and outputs against the schemas. `weather.viewer_ocean_heat_rain` and `weather.viewer_ocean_streams` regenerate both fixtures and compare. The viewer validates the extension blocks on load (`viewer/src/ocean.js`, tests in `ocean.test.js`).

## 8. Polar processes: sea ice, a coupled ice sheet, warming and meltwater hosing

These extensions bring polar melt into the 2-D ocean. Sea ice forms and melts on every sea cell, a land ice sheet melts into the sea next to it, the air warms over the years, and extra meltwater can be "hosed" onto the sinking region. Together they show season by season how a fresh, warm surface shuts down the deep-water formation. The century-scale question of when the overturning tips is answered by the box model in [polar-melt-model.md](polar-melt-model.md), which also documents the sea-ice and ice-sheet physics. Committed example: [examples/polar-ocean-request.json](../../examples/polar-ocean-request.json) → `viewer/public/polar-ocean/engine-timeline.json` (**❄ Load polar ocean (sea ice & hosing) example**).

### 8.1 Model

- **Sea ice** (`physics.sea_ice`, needs `thermal_layers` and `forcing.air_sea`). Each sea cell carries a concentration a and a thickness h of zero-layer ice.
  - **Open water.** The open fraction 1 − a exchanges heat and water with the air through the bulk formulae. The implicit surface balance is solved for the open part.
  - **Ice from below and above.** The covered part takes the ocean-to-ice heat F_oi from the top layer and evolves with its own surface balance: ice albedo, conduction and sublimation.
  - **Frazil and new ice.** Top-layer water cooled below its freezing point freezes as frazil, and the latent heat brings the water back to freezing. New ice in open water raises a.
  - **Salt.** Freezing rejects brine; ice keeps 5 psu. Melt returns nearly fresh water at the freezing point.
  - **Minimum thickness.** Ice thinner than `minimum_thickness_m` melts with the water's heat.
  - **Wind.** The ice shelters the water: the wind stress on the ocean is scaled by 1 − a.
  - **Drift.** The ice drifts freely with the upper-ocean current plus 2 % of the 10 m wind, turned 30° to the right of the wind in the northern hemisphere and to the left in the southern (Nansen's rule). The speed is capped at 1.5 m/s.
    - Area, volume and surface temperature move upwind between sea cells, in sub-steps of Courant number ≤ 0.25. Coasts are walls.
    - Converging ice ridges: the concentration is capped at the maximum, the volume kept and the ice thickens.
    - Free drift has no internal ice stress. A packed cell (at the maximum concentration and at least `maximum_ridge_thickness_m` thick) therefore takes no more ice; this is a cavitating-fluid stand-in for the ice strength (Flato & Hibler 1992).
    - Drifting ice carries its fresh water to where it melts.
  - **Initial ice.** Ice is placed where the initial surface lies within `initial_band_k` of freezing. The initial water is never below its freezing point.
  - **Budgets.** The ocean's heat budget includes the heat given to the ice. Water and salt are conserved between ocean and ice.
- **Glacial fresh water.** The forcing carries per-cell `glacial_meltwater_m3_s` and `glacial_ice_m3_s`.
  - Meltwater is ice-sheet runoff and hosing. It enters as liquid at 0 °C with no salt.
  - Ice is icebergs and frontal melt. It melts in the top layer and takes its latent heat from the sea, cooling it.
  - Both feed the lens attribution as a fourth source, `glacial`.
- **Seasons.** `forcing.air_sea.seasonal_amplitude_south_k` / `_north_k` add an annual air temperature cycle, warmest on day 196, to the analytic air.
- **Warming.** `forcing.warming` adds dT = min(max_k, rate·(t − start)) × amplification to the surface-temperature target and the air. The amplification changes linearly from south to north, so polar amplification is possible. It is off during the spin-up.
- **Hosing.** `forcing.hosing` adds F = min(max_sv, ramp·(t − start)) of fresh water at 0 °C, spread evenly over the sea cells within a circle. After the optional `end_seconds` it ramps down at the same rate to zero, which lets you test whether the circulation recovers (§8.6).
- **Coupled ice sheet** (`ice_sheet`). The ice sheet lives on the land cells of an elliptic region; the bed is the terrain.
  - It starts as an elliptic dome and steps every `step_every` ocean steps with the ice-sheet kernel of [polar-melt-model.md](polar-melt-model.md) §1.1.
  - Its air is the air-sea air temperature over the land, reduced by the lapse rate to its surface.
  - Snowfall scales with the annual-mean air: the surface-temperature target plus the air offset, with the warming but without the seasons. The parameters are `precipitation_m_we_yr` at `precipitation_reference_k` and `precipitation_per_k`. Summer warmth therefore does not move the snow into the summer. Under a coupled atmosphere its air temperature is used.
  - The ocean thermal forcing of its fronts is the layered column's temperature above freezing at `front_depth_m` in the adjacent sea cells.
  - Its runoff reaches the nearest sea cells as meltwater, and its icebergs and frontal melt as ice.
  - Warming therefore melts it, and a warmer ocean eats its fronts.

### 8.2 Configuration

| Section | Keys (default) |
|---|---|
| `physics.sea_ice` | `enabled` (true), `initial_thickness_m` (1.5), `initial_concentration` (0.9), `initial_band_k` (0.5), `albedo_cold` (0.65), `albedo_melting` (0.5), `conductivity_w_m_k` (2.03), `ice_salinity_psu` (5), `minimum_thickness_m` (0.05), `new_ice_thickness_m` (0.5), `heat_transfer_coefficient` (0.006), `friction_velocity_m_s` (0.01), `maximum_concentration` (0.99), `drift` (true), `wind_drift_factor` (0.02), `wind_turning_deg` (30), `maximum_drift_m_s` (1.5), `maximum_ridge_thickness_m` (4) |
| `forcing.air_sea` | `seasonal_amplitude_south_k`, `seasonal_amplitude_north_k` (0; analytic air only) |
| `forcing.warming` | `rate_k_per_year` (required, within ±2), `start_seconds` (0), `max_k` (5), `amplification_south` / `amplification_north` (1) |
| `forcing.hosing` | `center_east_m`, `center_north_m`, `radius_m`, `ramp_sv_per_year` (0–1), `max_sv` (0–5), `start_seconds` (0), `end_seconds` (none: then down at the same ramp) |
| `ice_sheet` | `region {center_east_m, center_north_m, radius_east_m, radius_north_m}`, `initial_dome {center_thickness_m, optional centre and radii}`, `precipitation_m_we_yr` (0.4), `precipitation_reference_k` (263.15), `precipitation_per_k` (0.07), `front_depth_m` (300), `step_every` (24), `physics {rate_factor_pa3_s, degree_day_factor_snow_mm_k_day, degree_day_factor_ice_mm_k_day, pdd_standard_deviation_k, refreezing_fraction, lapse_rate_k_m, front_water_depth_m, marine_front_fraction}` |

### 8.3 Output

`processes` adds `sea_ice`, `ice_sheet`, `warming` and `hosing`, and the forcing provenance records the warming and hosing. Per frame:

- **`sea_ice`.** Per cell `ice_thickness_m` (of the covered part), `ice_concentration`, `ice_surface_temperature_k` (0 without ice), `ocean_to_ice_heat_w_m2` and the drift `ice_drift_east_m_s` / `ice_drift_north_m_s`. Totals `ice_area_km2` (Σ a·A), `ice_extent_km2` (cells above 15 %), `ice_volume_km3` (Σ a·h·A), `ice_water_m3_s` (melt positive) and `cumulative_ice_water_m3`. Under ice the `air_sea` fluxes are those of the open fraction, and `net_heat_w_m2` = shortwave + longwave + sensible + latent − `ocean_to_ice_heat_w_m2`.
- **`ice_sheet`.** `ice_thickness_m`, `surface_elevation_m`, `smb_m_ice_yr`, `melt_m_ice_yr`, `volume_m3`, `sea_level_equivalent_m`, `sea_level_rise_m`, `smb_gt_yr`, `runoff_m3_s`, `calving_m3_s`, `frontal_ablation_m3_s` and `mean_front_thermal_forcing_k`. The top-level `ice_sheet` block holds `domain`, `bed_elevation_m`, `initial_volume_m3`, `initial_sea_level_equivalent_m` and `source`.
- **Polar processes only.** `hosing_sv`; `glacial {meltwater_m3_s, ice_m3_s, cumulative_m3}`; `thermal.surface_oxygen_umol_kg` (oxygen saturation from SST and surface salinity, 0 on land); `cumulative_m3.ice_water` and `.glacial`; and `source_shares.glacial` on every lens, whose `kind` may be `glacial`.

Identities the viewer and the CLI check enforce:

- volume = initial + river + net precipitation + sponge + rain on the sea + coastal runoff − evaporation + ice water + glacial;
- glacial ice = calving + frontal ablation;
- glacial meltwater = runoff + hosing;
- ice-sheet volume and SMB equal the sums of their maps;
- sea-level rise = (initial − current volume) × 0.917 / 3.618·10¹⁴ m².

Runs without polar processes produce exactly the output they produced before.

### 8.4 Example and viewer

The example basin is 3,400 × 4,200 km on 17 × 21 cells of 200 km, anchored at 42° N. It has a Greenland-like ice sheet on the northern land, Arctic rivers of 60,000 m³/s and six σ-layers, with seasons of 5 K in the south and 14 K in the north. After a 2-year spin-up it runs 10 years:

- air warming at 0.4 K/yr up to +4 K, 2.5× in the north;
- hosing near the sinking region ramping by 0.05 Sv/yr up to 0.5 Sv;
- quarterly snapshots, about 5 minutes of computation.

What happens:

- **Seasonal sinking.** The deep-water formation happens in winter: 25–30 Sv in January–April, 0–6 Sv in summer.
- **Overturning.** The annual mean falls from 15.9 Sv in year 0 to 4.2 Sv in year 9 (−73 %). It is 25 % weaker from year 3 and 50 % weaker from year 5.
- **Northern water.** The deepest northern mixing shrinks from about 470 m to 270 m. The northern surface warms by 3.9 K (annual mean) and its oxygen capacity falls from 344 to 318 µmol/kg.
- **Sea ice.** The ice drifts north-westward at up to about 20 km/day and piles up, ridged to 4 m, against the northern edge of the basin and the coast of the ice-sheet island. There it survives the summers until year 6; from year 7 the summers are ice-free.
  - The winter maximum first grows from 1.3 to 1.9 M km² in years 0–3. This is consistent with less heat carried north by the weakening overturning and a fresher surface that freezes more easily.
  - It then shrinks to 0.8 M km² by year 9 as the warming takes over.
- **Ice sheet.** The warmer air first brings more snow: the surface mass balance rises from 110 to 181 Gt/yr in years 0–6, then falls to 96 Gt/yr in year 9 as melt takes over. Summer balances go negative from year 8.
  - Its discharge (icebergs and frontal melt, about 13,000 m³/s or 420 Gt/yr) exceeds that gain, so it loses mass throughout. It raises the sea by 5.2 mm, about 0.5 mm/yr.

These are the responses of a coarse, idealised basin to a fast, prescribed forcing: a process demonstration, not a projection.

In the **Ocean streams** view:

- **Map overlays.** Sea ice is drawn white over the water (opacity = concentration, brightness = thickness). The ice sheet is white on land, reddish where it loses mass at the surface, with a label giving its thickness, SMB and sea-level rise. The hosing region is a yellow ring labelled with the current rate, and the water colour adds the surface oxygen capacity.
- **Field layers.** Sea-ice concentration, sea-ice thickness, surface oxygen, and the ice-sheet thickness, SMB and melt.
- **Metrics strip.** Overturning now and its annual mean, year 0 → latest; hosing and all glacial water; warming south/north; sea-ice area, extent, volume and freezing/melting; ice-sheet SMB, discharge and sea level; the northern SST, mixed layer and oxygen.
- **Charts.** Overturning with annual-mean dots, glacial water and hosing, sea-ice area, SMB, northern SST and oxygen.
- **"When does the overturning weaken?"** A year-by-year table of annual means. It gives the first year below 75 %, 50 % and 20 % of year 0, the first ice-free summer and the first year of negative SMB. Clicking a row jumps to that year. A same-season line compares the current time of year across all years, removing the seasonal cycle.
- **Inspector and playback.** The inspector gives the sea ice of a cell (concentration, thickness, surface temperature, heat from the water), its oxygen capacity, and the ice-sheet state of a land cell. The playback line gives the model date and the sea-ice area and overturning.

### 8.5 Verification

- `tests/weather/polar_melt_test.cpp` (`weather.polar_melt`) checks the kernel's sea ice and glacial water, with volume, salt and heat budgets every step:
  - freezing over with brine rejection;
  - summer melt returning water and restoring open water;
  - warm water melting ice from below;
  - meltwater and icebergs (volume, no salt, latent cooling, a glacier lens);
  - free drift: 2 % of the wind, turned right of it in the north, left in the south and not at all without rotation. The drift conserves the ice, packs it against the downwind coast and opens the upwind side, ridges up to the ridge thickness, and carries ice that melts over warm water;
  - the contract.
- `tests/tools/polar_ocean_check.py` (`weather.polar_ocean_cli`, about 40 s) runs one year of the example from January twice: with quick hosing (0.5 Sv within six months, ramping down from October) and +2 K of warming, and as a control without either. It checks, from the files alone:
  - every identity above;
  - the sea-ice maps against their totals;
  - the ice surface at most melting;
  - oxygen falling with temperature;
  - the hosing ramp up and down;
  - the drift (none on land, within its cap);
  - the ice sheet on land.
  It also checks the physics:
  - seasonal ice and winter sinking;
  - spring drift of centimetres to decimetres per second;
  - ridged ice within the ridge thickness plus growth;
  - a next-winter overturning at least 20 % weaker with hosing (16.5 vs 24.5 Sv in the current run) under a fresher northern surface;
  - a glacial lens;
  - a warmer, less oxygenated south;
  - ice-sheet mass loss.
  Finally it checks that invalid requests (sea ice without layers or air-sea, out-of-range ice, drift, ridge and warming parameters, hosing without sea cells or ending before it starts, an ice sheet without land) fail with their message and write nothing. With `jsonschema` it validates requests and outputs.
- `weather.viewer_polar_ocean` regenerates the fixture and compares it (about 5 minutes, label `slow`: `ctest -LE slow` skips it).
- The viewer validates the polar blocks on load (`viewer/src/ocean.js`, tests in `ocean.test.js`).

### 8.6 Limitations

- **Sea ice.** Free drift without rheology. Ridging is limited by a packed-cell rule, not by ice strength, so ice can neither resist divergence nor transmit stress across a pack. There is no snow on the ice, no heat stored in the ice, a single thickness category and no melt ponds. Leads close at `maximum_concentration`. On 200 km cells the upwind transport is diffusive.
- **Ice sheet.** Coarse: 17 cells of 200 km in the example, so it stands in for Greenland's mass budget, not its geometry. Its snowfall is a parameter scaled with the annual-mean air, not the atmosphere's precipitation. The air over it is the analytic air-sea air.
- **Overturning: no tipping point here.** The sinking is parametrised from the local surface density (§1), so the salt-advection feedback that gives the box model its tipping point and hysteresis is weak in this closed basin. The recovery experiment shows it: [examples/polar-ocean-recovery-request.json](../../examples/polar-ocean-recovery-request.json) runs 25 years without warming, hosing up to 0.5 Sv until year 8 and then ramping down to zero by year 13. Compare it with the same request without `forcing.hosing`.
  - The annual-mean overturning keeps weakening for about two years after the hosing peaks, down to 23 % of the control in years 10–11.
  - Once the hosing stops it recovers: 53 % of the control in year 15, 79 % in year 19, and a plateau of about 88 % from year 21 on.
  - The remaining 12 % is the fresh water the closed basin keeps. Its mean salinity is still 0.27 psu below the control at year 24 and returns only as slowly as the upwelling brings deep salt.
  - So the 2-D ocean shows the weakening, its lag and a slow recovery, but no self-sustaining collapsed state. Use the box model for the tipping point.
  - Ten years is also far too short for the deep ocean to adjust.
- **No feedback to the air.** Warming, seasons and hosing are prescribed. The ice does not warm or moisten the air above it, and the air does not react to the open water.

## 9. Limitations

- One active layer for the dynamics: the velocity has no vertical structure (the layered column resolves temperature and salinity, not currents), no barotropic mode, no bottom topography, no eddies (linear momentum, 50–60 km cells); boundary currents are grid-limited in width and speed, and a closed basin produces boundary jets along walls where the real ocean continues (the sponge mitigates the southern one). The overturning is a parametrised sinking, not a resolved deep circulation.
- The example is an idealised Atlantic-like basin with synthetic continents and forcing; the magnitudes (streams of order 10–70 Sv, overturning of order 1 Sv, plumes of 10³–10⁵ km²) are illustrative, not a reanalysis.
- Tides, waves, the equator and the wind's response to the sea surface are absent: the air-sea fluxes change the ocean, not the atmosphere (no feedback to a coupled run). Sea ice is thermodynamic only (§8.6).
- The layered column is a one-dimensional mixed-layer model under a slab current: Kraus–Turner and the bulk-Richardson estimate stand in for resolved shear and turbulence, there is no heat or salt flux through the layer bottom apart from the upwelling, and sigma layers follow the thermocline, so the top layer thins where the layer thins. Shortwave uses one water type; the light left at the layer bottom stays in the layer.
- The bulk coefficients are constant (no stability or sea-state dependence); the coupled storm wind is a speed without a direction, so a storm mixes and cools the sea but does not drive currents or Ekman upwelling.
- Coastal runoff is a linear reservoir to the nearest sea cell within a band, not a routed catchment; land beyond the band is left to the rivers (use a river with a catchment timeline). Rain falls at the cell's rain temperature and does not evaporate on its way down.
