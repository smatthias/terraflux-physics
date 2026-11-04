# Weather validation

Organize checks into:

- unit tests for individual physical calculations
- invariant tests for conservation, positivity, and boundedness
- reference cases with documented source and inputs
- map-input tests for georeferencing, known legend values, symbols, missing metadata, confidence, and provenance
- regression scenarios for combined processes

The dry scalar transport suite currently covers:

- constant-temperature preservation under divergence-free wind and diffusion
- one-step periodic upwind transport with expected cell values and conserved grid sum
- one-step diffusion with hand-computed expected cell values
- rejection of a timestep above the combined stability limit
- rejection of a discretely divergent wind field
- EPSG:4326 projection, observation/status preservation, and outside-domain counting
- duplicate-cell ambiguity with all source indices retained
- rejection of mapping radar reflectivity into an air-temperature field

The pressure-gradient wind suite (`weather.pressure_wind`, plus the CLI check `weather.pressure_wind_cli`) covers:

- exact down-gradient acceleration when the Coriolis parameter is zero
- a hand-computed trapezoidal Coriolis/friction step
- rightward deflection in the Northern Hemisphere and leftward in the Southern Hemisphere
- frictionless inertial oscillation about the geostrophic wind with conserved ageostrophic speed
- convergence to the friction-balanced wind between a high and a low, with cross-isobar angle `atan(k/f)`
- cyclonic flow around synthetic lows and anticyclonic flow around highs in both hemispheres
- acceptance of the periodic f-plane geostrophic wind by the dry transport divergence check
- the balanced wind as an exact fixed point of the stepper
- all Coriolis modes and rejection of unresolved timesteps and invalid inputs

The moist-air suite (`weather.moist_air`) covers reference densities and saturation values, humidity conversions, lighter warm/moist air, analytic sensitivities against finite differences, and input rejection.

The coupled wind / moist-layer suite (`weather.coupled_wind`, plus the CLI and fixture check `weather.coupled_wind_cli`) covers:

- equality with the standalone pressure-wind model under constant density, with uniform scalars preserved
- first-step acceleration proportional to the diagnosed inverse density (warm and moist air accelerate faster)
- a hand-computed periodic upwind transport step for temperature and humidity
- hand-computed explicit diffusion steps with zero-gradient (sum-conserving) and periodic boundaries
- reduced contrast and wind roughness with larger configured diffusivities between a high and a low; bounded density, RH and virtual temperature
- mid-run rejection once the wind-dependent scalar stability bound is broken
- supersaturation reported rather than removed
- rejection of invalid diffusivities, boundaries, modes and states

The pressure-wind suite also carries a verification block (cases V1–V3): exact damped-inertial solution in both hemispheres, a direction matrix over four gradient directions and both hemispheres using cross products, and textbook reference magnitudes (`f`, `β`, 1 hPa/100 km → 8.08 m/s). See [coriolis-verification.md](../../docs/weather/coriolis-verification.md).

The atmospheric column suite (`weather.atmospheric_column`, plus the CLI check `weather.atmospheric_column_cli` and the column block of `weather.coupled_wind_cli`) covers:

- dry ISA interface pressures against the analytic linear-lapse and isothermal formulas (10⁻⁹) and the ISA table (0.1 %); monotonic thinning; layer masses summing to the column mass
- moist columns being lighter
- potential temperature, dry/moist lapse rates, dew point inversion and Bolton θ_e helpers
- the moist ascent against an in-test fourth-order pressure-coordinate integration (0.01 K) with θ_e conserved to 2.5 K
- the LCL against Bolton's closed form (0.4 % in pressure) and Espy's rule
- convective indices: stable dry columns have no LFC; CAPE grows with moisture; LCL ≤ LFC < EL; a dry-adiabatic environment is neutral
- convective adjustment mixing and condensing with enthalpy and water conserved to 10⁻⁹, stable/unsaturated results, idempotence, untouched stable columns
- per-cell field diagnostics with missing levels and configuration/state rejections

The cloud formation suite (`weather.cloud_formation`, plus the CLI and fixture check `weather.cloud_formation_cli`) covers:

- the cloud-genus classification table and per-genus fractions
- the diurnal heating cycle
- cold, warm and stationary fronts from the temperature gradient and advection, with sides and distances; no front below the gradient threshold
- boundary-layer convergence lifting `w = −H ∇·V`
- heating driving the surface parcel from clear through cumulus and towering cumulus to cumulonimbus, with CAPE rising and CIN falling; dry air giving at most fair-weather cumulus
- frontal lifting releasing inhibited convection (combined mechanism), absent without lifting
- overrunning ahead of a warm front with cloud base rising along the wedge (nimbostratus → altostratus)
- a pinned regression scenario (12 × 10 baroclinic case, 8 h through the coupled model, diagnosed at 6 K and 0 K heating): front, genus, mechanism and trigger counts plus selected cell values
- configuration and input rejections

The precipitation outcomes suite (`weather.precipitation_outcomes`, plus the precipitation block of `weather.cloud_formation_cli`) covers:

- wet-bulb temperature against the psychrometric table, hail terminal velocities, WMO intensity classes and the genus → character table
- harmless genera delivering nothing while still reporting cloud temperatures
- nimbostratus prolonged moderate rain with the rate formula re-evaluated independently
- cumulonimbus heavy/violent showers with lightning, updraft-sized hail and melting; a weak storm with lightning but no hail; towering-cumulus showers
- phase by surface wet-bulb: snow, sleet, rain, dry cold air snowing, warm stratus drizzling at the cap
- virga from altostratus over dry air
- a pinned regression (12 × 10 baroclinic scenario at 6 K and 0 K heating): type and intensity counts, totals and selected cell values
- configuration and input rejections

The storm systems suite (`weather.storm_systems`, plus `weather.storm_systems_cli` covering the hurricane and tornado examples) covers: MPI/GPI/Holland/surge/category/Kaplan–DeMaria references; the environment over sea, land and near the equator with solid-body vorticity and baroclinic shear; monotonic intensification to the potential intensity; landfall event, reduction and exact inland decay; Fujiwhara rotation sense in both hemispheres; merger of overlapping cores; genesis only in favourable environments; Rankine/Holland/rain/surge fields; rejections.

The severe convection suite (`weather.severe_convection`) covers: STP reference values and clamps; the capping inversion raising CIN and storing CAPE until heating breaks the cap; helicity sign and Bunkers deviation mirrored between hemispheres; deep shear equal to the Ekman shear plus the thermal wind; tornado potential requiring strong flow and released convection; rejections. The column suite also covers the inversion profile.

The orography suite (`weather.orography`, plus `weather.orography_cli` covering the Alps and Rockies examples) covers: the exact dry adiabatic rate for unsaturated slope flow and descent, the moist rate with condensation for saturated ascent; the foehn across a 2 km ridge (windward precipitation, moist-rate cooling, 4–9 K lee warming, drying, flag); orientation-controlled Froude blocking (north–south range blocked with a barrier jet, east–west range untouched, fast flow unblocked); nocturnal cold pools in a basin and their daytime erosion; the diurnal tendency helper and rejections; a pinned regression.

The catchment hydrology suite (`weather.catchment_hydrology`, plus `weather.catchment_hydrology_cli` running the CLI on a shortened alpine flood example, a coupled-timeline forcing and invalid requests) covers: every land cell of a tilted plane drains to the sea along strictly descending receivers with accumulating area, Strahler order, channels from the initiation area, hydraulic-geometry power laws and basins that partition the land; open edges and the closed-without-ocean rejection; a pit as a lake with the analytic spill level, capacity and outlet that fills monotonically and spills before anything reaches the sea; steady rain closing the budget every step with hourly outflow equal to rain × area within 5 %; exact Horton infiltration, saturation excess, percolation, springs, degree-day melt and evapotranspiration; explicit Darcy transfers, conservation and linear-reservoir baseflow; the infinite-slope factor of safety, Caine threshold, Manning velocity and flood classes; a cloudburst on a steep ridge (single failure per wet period, debris runout, flooding, threshold warnings, stream-power erosion ordering, sediment mass balance, restabilisation); contract rejections; and a pinned regression.

The ocean circulation suite (`weather.ocean_circulation`, plus `weather.ocean_circulation_cli` running the CLI on a coarse basin with rivers, a meltwater pulse, a catchment-fed river and coupled-wind forcing) covers: the Stommel/Sverdrup gyre on a β-plane (western boundary current carrying the Sverdrup transport within 30 %, interior balance, western intensification, gyre sense, stream detection with a northward core streamline, budgets) against the f-plane without intensification, the reference Stommel width and Sverdrup transport; exact river dilution and salt conservation, the surface freshwater content and surface salinity, exact evaporation/rain laws; plume deflection to the right (NH) and left (SH); deep-water formation in a cold north with uniform upwelling, budgets, the northward surface branch and a meltwater river weakening the overturning by more than 30 %; EOS and wind-stress helpers; contract rejections; and a pinned regression.

The ocean heat and rain suite (`weather.ocean_heat_rain`, plus `weather.ocean_heat_rain_cli` running a re-timed version of the example and the ocean under the engine's hurricane run, and `weather.viewer_ocean_heat_rain` / `weather.viewer_ocean_streams` regenerating both ocean fixtures) covers: the bulk air-sea formulae against hand calculations (Dalton evaporation, latent heat = L_v E, sensible heat, Berliand longwave, gustiness floor, condensation, albedo); the sun's position and the Zillman/Reed shortwave; the sigma-layer geometry and the two-band shortwave penetration (each layer absorbs exactly its share); the initial thermocline; rain on the open sea (exact volume, salt conservation, a fresh, cool and stable rain lens that the wind erodes); coastal rain (receivers within the band, coastal/inland split, the land budget, the exact linear-reservoir recession, nearshore freshening); evaporation (volume loss, the concentration law, a saltier surface, cooling, humidity dependence); calm sunshine forming a shallow warm layer and Kraus–Turner wind mixing deepening it; bulk-Richardson shear mixing to the critical value; convective deepening under a cold-air outbreak; layer–bulk consistency under a gyre; deep-water formation under an outbreak and its suppression by a fresh rain cap; the legacy slab's heat budget; contract rejections; and a pinned regression. Every case checks the volume, salt and heat budgets at every step.

The polar melt suite (`weather.polar_melt`) covers:

- positive degree days (limits, quadrature);
- Vialov and elliptic domes and routing to the nearest sea cell;
- the ice-sheet surface mass balance: snowfall, ice and snow melt, refreezing, runoff only to the sea, the lapse-rate melt–elevation feedback;
- flow that conserves ice, spreads the dome and never goes negative;
- calving with the matching sea-level rise;
- Rignot frontal melt and the retreat of the face;
- the seawater freezing point and the sea-ice surface balance (closure, thin ice, summer melt, albedo);
- sea-ice growth in a −30 °C winter with energy closure and brine rejection; open water freezing without supercooling;
- the seasonal column from perennial to ice-free under warming, and thinning by Atlantic-water heat;
- the overturning:
  - Stommel's closed form and the calibration to 17 Sv, steady for a century;
  - bistability, the collapse past the threshold, hysteresis and recovery only with net evaporation, a recovering sub-threshold pulse, polar amplification lowering the threshold;
  - the recovery time growing towards the threshold and matching the decay of a perturbation;
  - early-warning statistics and Kendall's τ;
- oxygen solubility reference values;
- the ocean kernel's sea ice (freezing over with brine rejection, summer melt, melt from below by warm water) and glacial meltwater and icebergs (volume, no salt, latent cooling, a glacier lens), with volume, salt and heat budgets every step;
- contract rejections.

Related CTest cases:

- `weather.polar_melt_cli` runs a fast variant of the polar melt CLI with every event, checking contract, identities, physics, schemas and rejections.
- `weather.polar_ocean_cli` runs one year of the polar ocean example with hosing and warming against a control. It checks budgets with sea-ice and glacial water, map–total identities, the hosing ramp, a weaker next-winter overturning, a glacial lens and rejections.
- `weather.viewer_polar_melt` and `weather.viewer_polar_ocean` regenerate the two fixtures and compare them. The latter takes about 3.5 minutes and has the label `slow`.

Build and run the registered CTest case with:

~~~sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -LE slow   # skip the 3.5-minute polar ocean fixture check
~~~

Keep expected results and tolerances tied to the model assumptions and numerical resolution. Do not treat a visually plausible result as physical validation.
