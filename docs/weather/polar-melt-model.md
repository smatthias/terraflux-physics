# Polar melt v1: ice sheets, sea ice and when the overturning collapses

Melting polar ice affects the ocean in two ways. It warms and freshens the surface water, and the fresh water it adds to the subpolar North Atlantic caps the region where surface water sinks. That sinking drives the Atlantic meridional overturning circulation (AMOC). This document covers the ice and overturning components of the engine and the two ways they are run:

| | **Mode B: centuries** `terraflux-run-polar-melt` | **Mode A: processes in the ocean** `terraflux-run-ocean-circulation` |
|---|---|---|
| Question | When does the overturning cross its tipping point and collapse under a warming scenario? What happens to Greenland, the Arctic sea ice, ocean temperature and oxygen? | How do sea ice, a melting ice sheet and extra meltwater change the layered ocean, its seasons and its deep-water formation? |
| Ocean | Two-box overturning (Stommel 1961, mixed boundary conditions) calibrated to the present 17 Sv | The 2-D reduced-gravity ocean with the layered T/S column ([ocean-circulation-model.md](ocean-circulation-model.md) §7, §8) |
| Ice | Greenland-like ice sheet (shallow-ice flow, degree-day melt, frontal melt) and one central-Arctic sea-ice column | Sea ice on every sea cell and a coupled ice sheet on the land |
| Time | 1850–2300, 10-day steps, three scenarios, a control run and a 9-member ensemble | Years, 45-minute steps, with seasons |
| Request / output | `terraflux.polar.melt-run/v1` ([schema](polar-melt-run-v1.schema.json)) → `polar` block `terraflux.polar.melt/v1` ([schema](polar-melt-v1.schema.json)) | `physics.sea_ice`, `ice_sheet`, `forcing.warming`, `forcing.hosing` in `terraflux.ocean.circulation-run/v1` |
| Example | [examples/polar-melt-request.json](../../examples/polar-melt-request.json) → `viewer/public/polar/engine-timeline.json` (**🧊 Load polar melt & overturning example**, view **Polar melt**) | [examples/polar-ocean-request.json](../../examples/polar-ocean-request.json) → `viewer/public/polar-ocean/engine-timeline.json` (**❄ Load polar ocean (sea ice & hosing) example**, view **Ocean streams**) |

The next step, the rising sea and what it does to coasts, is [coastal-flooding-model.md](coastal-flooding-model.md). It takes these scenarios and Greenland's melt.

Kernels: `include/terraflux/weather/ice_sheet.hpp`, `sea_ice.hpp` (+ `sea_ice_physics.hpp`), `overturning_box.hpp`, `marine_indicators.hpp` and the sea-ice and glacial-water parts of `ocean_circulation.hpp`.

## 1. Components

### 1.1 Land ice sheet (Greenland)

- **Flow.** The shallow-ice approximation uses Glen's law (n = 3, A = 3.17·10⁻²⁴ Pa⁻³ s⁻¹), so q = −D ∇s with D = 2A(ρᵢg)ⁿ Hⁿ⁺² |∇s|ⁿ⁻¹/(n+2) on the cell faces (Mahaffy 1976). It is integrated explicitly with sub-steps below dx²/(4 D_max), and donor limiting keeps every thickness non-negative. Ice that flows onto a sea cell calves into that cell as icebergs.
- **Surface mass balance.** The air temperature is T_sea-level − 6.5 K/km × surface elevation. As the ice thins, its surface drops into warmer air, which is the melt–elevation feedback. Precipitation falls as snow below +1 °C, changing linearly to rain over ±1 K. Melt is the expected number of positive degree days of a normal distribution (σ = 5 K) around the mean. Snow melts first at 3 mm w.e. K⁻¹ d⁻¹, then ice at 8 mm w.e. K⁻¹ d⁻¹ (Reeh 1991), and 60 % of the snowmelt refreezes. Runoff is routed to the nearest sea cell by a breadth-first search.
- **Frontal ablation.** Marine margins melt from the ocean following Rignot et al. (2016): q_m = (a h q_sg^α + b) TF^β. TF is the thermal forcing (ocean temperature above the local freezing point at the front depth) and q_sg is the subglacial discharge of the surface melt. Both warmer water and more surface melt therefore speed up the retreat. On a coarse grid, `marine_front_fraction` sets the share of each margin cell that is a tidewater front.
- **Budgets.** The volume changes by accumulation − runoff − calving − frontal ablation, and the residual is reported. The sea-level equivalent is the ice volume × 0.917 spread over the global ocean area (3.618·10¹⁴ m²). There is no isostasy and no marine ice below flotation.

### 1.2 Sea ice

This is zero-layer thermodynamic ice (Semtner 1976) with a thickness h over a fraction a of the cell.

- **Surface balance.** The ice surface temperature T_s solves Q_atm(T_s) + kᵢ(T_f − T_s)/h = 0, with T_s ≤ 0 °C; any surplus melts the top. Q_atm is made up of:
  - shortwave with the ice albedo (0.65 for cold ice, falling to 0.50 over the last kelvin below melting);
  - Berliand longwave;
  - sensible heat;
  - sublimation with L_s = L_v + L_f.
- **Base.** ρᵢ L_f dh/dt = F_c − F_oi, where F_c is conduction through the ice. F_oi = ρ₀ c_p c_h u* (T − T_f) is the ocean-to-ice heat flux (McPhee 1992), so one kelvin of warm water under the ice delivers hundreds of W/m². The thickness step is implicit (a safeguarded Newton iteration).
- **Freezing point.** T_f = 273.15 − 0.054 S − 7.53·10⁻⁴ z.
- **New ice.** Open water cooled below T_f freezes new ice of 0.5 m thickness (Hibler 1979), which raises a. When ice melts, thickness and concentration shrink together, a′ = a√(V′/V).
- **Salt.** Ice keeps 5 psu. Freezing therefore rejects brine into the water, and melt returns nearly fresh water.
- **Albedo feedback.** Less ice means more sunshine absorbed in the open water, which gives more basal melt.

### 1.3 The Arctic column (Mode B)

One mixed layer (50 m, relaxed to 32 psu) with sea ice stands for the central Arctic. It sits under a seasonal air temperature (annual mean, amplitude and Arctic amplification) and the daily-mean sun at 80° N. The heat of Atlantic water arriving from below scales with the overturning and with warming.

A fixed share of the ice volume (12 % per year) drifts out of the basin through Fram Strait. Without this sink, thermodynamic ice would thicken far beyond the observed few metres. The exported ice carries its latent deficit out as a budget term and melts in the North Atlantic. Liquid fresh water leaves through a reservoir with a 10-year residence time, standing in for the Beaufort Gyre.

### 1.4 The overturning

The overturning is a two-box model: a north box (subpolar North Atlantic, 10¹⁶ m³) and a south box (5·10¹⁶ m³). The overturning is q = k[α(T_S − T_N) − β(S_S − S_N)]. Temperatures relax to targets on a 5-year time scale, and salinities see the fresh water F into the north box.

- **Tipping point.** The steady sinking-north states satisfy F = ψ(q) = [kαD*q/(1 + qτc) − q²]/(kβS₀). ψ rises to a maximum and then falls.
  - Below the maximum there are two states: the strong one is stable and the weaker one is unstable.
  - Above the maximum, only the reversed, salinity-driven state remains.
  - F_crit = max ψ is the tipping point, a saddle-node. For τ → 0 it reduces to Stommel's closed form kα²D*²/(4βS₀) at half the thermal strength.
- **Hysteresis.** Once collapsed, the circulation stays collapsed at today's forcing. Only net evaporation over the north restarts it.
- **Warming.** Warming enters through the targets. Polar amplification (north 1.85× the global warming, south 1×) shrinks D* = T*_S − T*_N, and with it both the circulation and its threshold.
- **Resilience.** The recovery time −1/max Re λ of the linearised system measures how fast the circulation recovers from a small perturbation. It grows without bound towards the threshold (critical slowing down) and is a noise-free resilience indicator. With `noise_sv` > 0 the run also reports the statistical early-warning indicators (variance and lag-1 autocorrelation over a sliding window, with Kendall's τ). These need windows much longer than the recovery time.
- **Oxygen.** Dissolved-oxygen saturation follows Garcia & Gordon (1992). It is the capacity of warmer, fresher water to hold oxygen, not a biogeochemical tracer.

### 1.5 Coupling in Mode B, per 10-day step

1. The warming scenario sets the Greenland air. This includes amplification (1.6× in the example), the seasonal cycle, the latitude gradient and the lapse rate to the ice surface. Snowfall is scaled by 7 %/K. The ocean thermal forcing at the fronts rises by half of the north box's warming (`thermal_forcing_per_north_k` 0.5). The subsurface Atlantic water at the fronts follows both the climate and the overturning's heat transport, but warms less than the upper 1000 m. In the high scenario it rises from 2.5 K to 5.5 K by 2100, in line with the 2–4 K increase of the ISMIP6 ocean forcing. Greenland steps every third model step.
2. The warming sets the Arctic air over the sea-ice column, whose Atlantic-water heat follows the overturning.
3. The fresh water into the north box is the sum of four terms:
   - the calibrated present forcing;
   - the Greenland anomaly (runoff, calving and frontal melt relative to the spun-up ice sheet);
   - the Arctic anomaly (released liquid fresh water plus exported ice relative to the spun-up column);
   - a stronger water cycle, 0.01 Sv/K.
4. The box targets warm with polar amplification. The overturning's north temperature feeds back on the Greenland fronts.

Both the ice sheet and the Arctic column are spun up under the start climate: Greenland for 3000 years, the Arctic for 100 years. Their freshwater fluxes at the end of the spin-up are the baseline, so only the anomalies force the overturning. A control run holds the start climate. Its sea-level drift, which comes from an ice sheet that is not fully in equilibrium after 3000 years, is subtracted from the scenarios (`sea_level_rise_vs_control_m`).

## 2. Calibration and why

- **Present circulation.** The box is calibrated to 17 Sv, T_S − T_N = 15 K and S_S − S_N = 1.0 psu. The calibration derives k and the temperature targets so that this state is steady: k = 9.5·10⁹ m³/s, present F = 0.486 Sv, and F_crit = 0.675 Sv at the start climate, so the start lies 0.19 Sv below the threshold.
  - The salinity contrast is the least constrained parameter, and it sets the distance to the tipping point. A larger contrast means a more salt-limited circulation that sits closer to the threshold. That is why it is the first ensemble axis (0.8 / 1.0 / 1.2 psu).
  - The second axis is Greenland's warming amplification (1.3 / 1.6 / 2.0).
- **Box volumes and relaxation time.** The volumes, τ = 5 years and the amplifications give a recovery time of 22 years today and an AMOC weakening by 2100 of about −17 % / −25 % / −44 % for the low / intermediate / high scenarios. These lie within the CMIP6 ranges assessed by IPCC AR6: a multi-model mean of 24 % (4–46 %) for SSP1-2.6 and 39 % (17–55 %) for SSP5-8.5.
- **Arctic column.** Mean air 255.65 K, seasonal amplitude 17 K, amplification 3, export 12 %/yr and area 7·10⁶ km² give about 5.6 m of ice in March after the spin-up. The first ice-free September comes at about 1.9 K of global warming, close to the CMIP6-based estimates of about 1.5–2 K (SIMIP Community 2020).
- **Greenland.** The setup:
  - an elliptic island on 26 × 56 cells of 50 km with a 3000 m initial dome;
  - 0.6 m w.e. of snowfall at 263 K;
  - Glen's rate factor with an enhancement factor of 4 (A = 1.27·10⁻²³ Pa⁻³ s⁻¹, a common choice for shallow-ice models that cannot resolve fast outlet glaciers);
  - a thermal forcing of 2.5 K plus half the north box's warming.

  The calibration reproduces the size and the mass turnover of the ice sheet:
  - The spun-up sheet holds 7.40 m of sea-level equivalent (observed: 7.42 m).
  - Its surface mass balance is 378 Gt/yr, balanced by about the same discharge (calving and frontal melt). Runoff is about 255 Gt/yr of a total freshwater output of 633 Gt/yr.
  - For comparison, reconstructions of the 1961–1990 reference period give an SMB of about 400 Gt/yr, discharge of 400–450 Gt/yr and runoff of 250–300 Gt/yr.
  - In 2020 the SMB has fallen to 284 Gt/yr.
  - It turns negative at about 2.5–2.7 K of global warming (intermediate 2087, high 2058); the low scenario never gets there. Published thresholds are about 1.9–4.6 K (Gregory & Huybrechts 2006) for a negative SMB and 0.8–3.2 K (Robinson et al. 2012) for long-term decline, which includes dynamics.
  - Greenland's sea-level contribution from 2020 to 2100 is 7 / 10 / 17 cm for the low / intermediate / high scenarios. IPCC AR6 gives 6 (1–10) / 8 (4–13) / 13 (9–18) cm for SSP1-2.6 / SSP2-4.5 / SSP5-8.5, so the model sits in the upper half of the likely ranges.

## 3. Results of the example (central calibration)

| Scenario (global warming 2100 / 2300) | AMOC 2100 | First ice-free September | Greenland SMB < 0 | Tipping point crossed | Weakened 50 % | Collapsed (< 20 %) | Ensemble: collapsed members (range) | Greenland sea level vs control 2100 / 2300 |
|---|---|---|---|---|---|---|---|---|
| low, about SSP1-2.6 (1.8 / 1.5 K) | 14.2 Sv (−17 %) | — | — | — | — | — | 0 / 9 | 10 cm / 21 cm |
| intermediate, about SSP2-4.5 (2.7 / 3.5 K) | 12.7 Sv (−25 %) | 2046 (1.9 K) | 2087 (2.5 K) | 2121 (2.8 K) | 2274 (3.5 K) | — (slow passage) | 3 / 9 (2178–2195); 6 / 9 cross the threshold | 13 cm / 45 cm |
| high, about SSP5-8.5 (4.4 / 8.5 K) | 9.6 Sv (−44 %) | 2037 (1.9 K) | 2058 (2.7 K) | 2060 (2.8 K) | 2110 (4.7 K) | **2139 (5.4 K)** | 9 / 9 (2109–2178, median 2139) | 20 cm / 149 cm |

Warming is relative to 1850. Percentages are relative to the 1850 strength. In 2020 the model circulation is already about 8 % weaker than in 1850.

What the table says:

- **When do the currents collapse?** In this model, the tipping point is crossed at about 2.8 K of global warming, after the summer sea ice is gone and at about the time Greenland starts to lose mass at its surface. The collapse follows the crossing with a lag of about 80 years in the high scenario.
  - The lag is *slow passage*: the circulation is already committed but takes decades to run down.
  - In the intermediate scenario the forcing lingers near the threshold. The central run crosses it in 2121 and is half as strong by 2274, but has not collapsed by 2300. A third of the ensemble collapses between 2178 and 2195.
  - Read the dates as scenario-relative, not as a forecast. They move by decades with the salinity contrast, the Greenland sensitivity and the box calibration, which is why the ensemble is shown.
- **Freshwater cap versus warming.** At the crossing in the high scenario (about 2060), only about 0.06 Sv of fresh water has been added: 0.029 Sv from Greenland, 0.028 Sv from the stronger water cycle, and about zero from the Arctic. The Arctic releases stored fresh water as it melts, but thinner ice also exports less. Meanwhile the threshold has fallen by 0.13 Sv, from 0.67 Sv to 0.54 Sv, because warming weakens the thermal drive. It falls to 0.31 Sv at +8.5 K.
  - The 0.19 Sv gap at the start therefore closes about one third through fresh water and two thirds through warming.
  - Greenland's meltwater alone (0.06 Sv in 2100 in the high scenario) would not tip the circulation of the start climate. It is the combination that does.
  - Greenland's sensitivity matters less than the present salinity contrast. Raising the amplification from 1.3× to 2.0× moves the crossing by only 5 years in the high scenario (2062 → 2057), but by 37 years in the intermediate one (2136 → 2099). It moves the collapse by 13–17 years.
  - A present salinity contrast of 1.2 psu instead of 1.0 crosses about 20 years earlier; 0.8 psu crosses about 23 years later.
  - Mode A adds the other side of the cap: how a fresh surface layer shuts down convection locally, season by season.
- **Ocean temperature and the ecosystem.** By 2100 in the high scenario, the tropical and South Atlantic box is 4.6 K warmer and the North Atlantic 6.0 K warmer. By 2300 the figures are 9.0 K and 12.8 K.
  - Oxygen solubility falls by 8 % in the south and 12 % in the north by 2100, and by 14 % / 22 % by 2300.
  - The central Arctic is ice-free for about 110 days a year in the intermediate scenario by 2100. In the high scenario it is ice-free for 180 days by 2100 and all year by 2300.
  - These are physical stressors on ecosystems (warming, deoxygenation, habitat loss of sea ice, sea-level rise), not an ecosystem model.
- **Could the collapse be seen coming?** A test run: the high scenario with 0.03 Sv of annual freshwater noise (`noise_sv`), 20 random seeds, and the control with the same noise. In each record, the trend of the variance and of the lag-1 autocorrelation of the annual overturning was measured over sliding windows (Kendall τ; here τ > 0.5 counts as a warning).
  - With data until 2025 the warming runs are indistinguishable from the control: 1–2 of 20 warn in both.
  - With 50-year windows and data until the tipping point (about 2060), the rising variance warns in 14 of 20 warming runs and in 2 of 20 controls. The autocorrelation warns in only 2 of 20.
  - With 30-year windows nothing is detected.
  - The statistical warning therefore comes, if at all, around the time the threshold is crossed. It also needs the variance rather than the autocorrelation, because the overturning's recovery time (decades) is long compared with the windows a century-scale record allows.
  - The viewer lists the early-warning trends of a run made with noise.

## 4. Output (Mode B)

The timeline frames carry the Greenland maps of the `map_scenario` every `output_every_years`, on a grid whose terrain is the bed. The `polar` block contains:

- `output_years`, `land_mask`, `greenland_frames` (thickness, surface, SMB and melt maps, volume, sea-level rise);
- `calibration` and `spinup`;
- per scenario:
  - `series`: warming; overturning and heat transport; threshold and fresh water with its Greenland, Arctic and hydrological parts; recovery time; box temperatures and salinities; Greenland volume, sea-level equivalent, rise (also vs control), SMB and melt; thermal forcing; Arctic March and September ice, minimum concentration and ice-free days; temperature and oxygen changes;
  - `events`: each as `{year, global_warming_k}` or null;
  - `trajectory` (fresh water, overturning);
  - the `ensemble` (members with their events, collapse counts and range);
- the `equilibrium_diagram` at the start climate and at the warmest scenario's end;
- `limitations`.

## 5. Viewer

**Polar melt** view:

- Greenland's ice in relief. Bed plus ice, with a 25× extra vertical scale so that a 3 km dome on a 2800 km island is visible.
- Colour options: ice thickness, surface mass balance (red melt ↔ blue snow), melt, or the annual air temperature. Field layers show ice thickness, SMB and melt.
- A metrics strip for the scenario in focus: overturning and its change, fresh water against the tipping point and its parts, recovery time, Greenland sea-level equivalent and rise vs control, SMB, Arctic March/September ice, ocean temperature and oxygen changes.
- The **"When do the currents collapse?"** table: per scenario, the year and warming of each event, the ensemble range and the sea level in 2100 and at the end. For a run made with `noise_sv`, it also lists each scenario's early-warning trends (variance and autocorrelation, Kendall τ).
- Scenario charts: overturning, sea level vs control, September ice, North Atlantic temperature, oxygen, and fresh water against the threshold.
- The equilibrium diagram of the overturning: stable, unstable and reversed branches at the start and at the warmest climate, with the focused scenario's trajectory.
- The playback line gives the year, the scenario and its warming.

The viewer checks the contract on load (`viewer/src/polar.js`, tests in `polar.test.js`). The checks include event order, ensemble counts, ice only on land, and sea-level rise equal to the ice lost.

Mode A is shown in the **Ocean streams** view ([ocean-circulation-model.md](ocean-circulation-model.md) §8.4).

## 6. Verification

`tests/weather/polar_melt_test.cpp` (`weather.polar_melt`, about 3 s) covers:

- **Positive degree days.** Limits and quadrature.
- **Domes and routing.** Vialov and elliptic domes, and routing to the nearest sea cell.
- **Surface mass balance.** Snow accumulates; ice melts at its factor; snowmelt refreezes; the budgets close; the lapse rate gives a high summit less melt.
- **Flow and calving.** Flow conserves ice, spreads and thins the dome and never goes negative; calving matches the volume lost and the sea-level rise.
- **Frontal ablation.** The Rignot rate, the retreat q_m H/dx, and the meltwater reaching the sea.
- **Freezing point and surface balance.** The freezing point, including pressure; the surface balance closes; thin ice is warmer on top; summer melt with darker ice.
- **Growth, freezing and seasons.** A winter at −30 °C grows 1–2 m of ice, slowing with thickness; brine rejection; open water freezing without supercooling; the energy of the seasonal column closes; perennial ice turns seasonal and then ice-free with warming; more Atlantic-water heat thins the ice.
- **The overturning.**
  - Stommel's closed form, the calibration reproducing 17 Sv, a steady century, and a threshold a fraction of a sverdrup away.
  - Bistability, hysteresis, a recovering sub-threshold pulse, and polar amplification lowering the threshold.
  - The recovery time growing towards the threshold and matching the decay of a perturbation.
  - The early-warning statistics and Kendall's τ.
- **Oxygen and rejections.** Oxygen solubility reference values and invalid-input rejections.
- **The ocean kernel's sea ice and glacial water.** Freezing over with brine rejection; summer melt returning fresh water; warm water melting ice from below; glacial meltwater and icebergs (volume, no salt, latent cooling, a lens attributed to the glacier); contract checks. Volume, salt and heat budgets close every step.

The suite was mutation-checked: deliberately broken terms make it fail.

Other checks:

- `tests/tools/polar_melt_check.py` (`weather.polar_melt_cli`) runs a fast variant with every event happening. It checks the contract, the identities and the physics from the file alone, including the thermal forcing at the fronts following the north box and the early-warning statistics of a noisy run. It also checks schema validity and request rejections.
- `weather.viewer_polar_melt` regenerates the example fixture (about 35 s) and compares.
- Mode A checks are listed in [ocean-circulation-model.md](ocean-circulation-model.md) §8.5.

## 7. Limitations

- **Conceptual overturning.** The two-box model has a single threshold and no spatial structure: no convection sites, no gyre feedback, no Southern Ocean and no wind-driven part. Its dates and thresholds depend on the calibration, above all the present salinity contrast. They are scenario-relative, not forecasts. Comprehensive models disagree on whether and when the AMOC collapses this century. The ensemble shows how strongly the answer depends on poorly known quantities.
- **Greenland.**
  - A synthetic elliptic island on a 50 km grid. Degree-day melt, not an energy balance.
  - Shallow-ice flow with no sliding, ice streams, ice shelves or isostasy. Outlet glaciers are represented by the Rignot frontal melt on a share of the margin.
  - The 3000-year spin-up leaves a small drift, which is removed with the control run.
  - The calibration matches the volume and the mass turnover (SMB, discharge, runoff) of the reference period. Its discharge does not decline as fronts retreat out of the water, which may explain why the sea-level contribution sits in the upper half of the AR6 ranges.
- **Arctic.** One column stands for the whole Arctic Ocean. There is no ice drift except a fixed export share and no ice-thickness distribution. The liquid freshwater reservoir is a linear store.
- **Early warnings.** Early-warning statistics need long, noisy records, and they come late (§3). The deterministic recovery time shows the slowing down noise-free.
- **Ecosystem indicators.** These are physical stressors only: temperature, oxygen solubility, sea ice and sea level. There is no biology, no carbon chemistry (acidification) and no oxygen consumption.
- **Mode A** is a process demonstration in a synthetic basin over a decade, with prescribed, fast warming and hosing. Its overturning is a parametrised sinking. In the recovery experiment it weakens to a quarter under 0.5 Sv of hosing and recovers to about 90 % within a decade after the hosing stops. It does not tip like the box model (see [ocean-circulation-model.md](ocean-circulation-model.md) §8.6).
