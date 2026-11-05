# TerraFlux physics handover

Working branch: `feature/mobile-weather-contract` · [PR #4](https://github.com/smatthias/terraflux-physics/pull/4).

This is an existing C++/Python physics codebase with a Vue/Three.js inspection frontend. Examples use synthetic inputs. Local weather/terrain/water outputs are computed by implemented kernels; the separate regional weather illustration is generated analytically in JavaScript. AI development, behavior prediction, training and forecasting intelligence are reserved for Claude.

## Start and verify

Prerequisites: C++17 toolchain, Python 3.10+, modern CMake/CTest, Node.js 22/npm, libpng 1.6.59+ and nlohmann/json 3.12.0+. Native dependencies must be installed first; [CI](../.github/workflows/weather-ci.yml) contains the pinned build recipe.

```sh
# Complete automated verification, from the repository root.
python3 tools/verify_pipeline.py
# Optional separate native dependency prefix.
python3 tools/verify_pipeline.py --cmake-prefix-path /path/to/dependencies
# Frontend, after installation by verification (or npm ci inside viewer).
cd viewer
npm run dev
```

Open the URL printed by Vite. There is no login, database, backend service or live feed. All file imports stay in browser memory. [Verification details and explicit partial modes](VERIFY_PIPELINE.md).

## Inspect in the viewer

| Control | What to inspect |
| --- | --- |
| Load engine example | 14 computed snapshots: wet weather, taper, then rain-free depletion |
| Compare hydrology scenarios | Same forcing, baseline vs 10× lower infiltration; synchronized map, deltas and storage chart |
| Load outlet scenario | Prescribed external surface outlet, marker, cumulative discharge and interval-average flow |
| Regional weather demo | Separate synthetic 1,200 × 900 km clouds, pressure systems, front and humidity illustration |
| Load coupled wind example | 7 engine snapshots of the coupled wind / moist-layer run: wind, temperature, pressure, RH, density, specific humidity |
| Load hurricane landfall example | Engine run: merger, intensification to category 3, landfall with surge, inland decay, renewed genesis; tornado layers quiet in the tropics |
| Load ocean heat & rain example | Ocean streams view: rain lenses on the sea, the coastal band draining to the sea, a cold dry outbreak, sea-surface temperature, mixed layer, heat fluxes, evaporation, water-column profile and section |
| Load polar melt & overturning example | Polar melt view. Greenland in relief under three warming scenarios 1850–2300 with a control and a 9-member ensemble. The "When do the currents collapse?" table, scenario charts, the equilibrium diagram of the overturning, and ocean temperature and oxygen indicators |
| Load polar ocean (sea ice & hosing) example | Ocean streams view over 10 years with seasons, warming and meltwater hosing. Sea ice, the ice sheet on land, the hosing ring, a year-by-year table of the weakening overturning and same-season comparison |
| Load sea-level rise & coasts example | Sea level & coasts view. The polar scenarios' sea-level rise on a synthetic coast (delta with polders and dikes, estuary city, barrier islands, cliffs, low islands): land lost, 100-year storm floods with wind setup, land behind defences, rivers and salt, year-lost and flood-frequency maps, ranking, events and charts |
| Load ocean carbon & dead zones (today / 2100) | Carbon & ocean life view. A Gulf-like sea with a delta river over one year: CO₂ plumes over the sources and the air over land, the sea's uptake, pH and aragonite saturation, bottom oxygen and habitat classes, seasonal dead zones (red volumes), anoxic ☠ and corrosive (magenta) water, a water column with its oxygen over the year, a dead-zone table, carbon budgets and a today ↔ 2100 comparison |
| Load solar cycle & space weather example | Sun & solar cycle view. The solar disk as a dome with spot groups, faculae and flares (brightness, contrast or cycle colouring); sunspot record 1850–2026 with the fitted cycles and a forecast to 2050 (50 % / 90 % ranges, next minimum and maximum); minima & maxima table; backtest skill by cycle age; UV spectrum, TSI and UV at the top vs the UV index at the ground; flare counts, radio-blackout probabilities and an annual flare outlook; climate response; butterfly diagram. Below: grand minima and maxima since 1000 AD (Oort, Wolf, Spörer, Maunder) with long-term TSI, cosmic rays and climate contributions, a Maunder Minimum case study, the dynamo map and the outlook for a new grand minimum. The record is synthesised from an approximate, unverified cycle table |
| Load Alps foehn example | East–west Alpine ridge with a foreland basin under southerly flow: windward rain, 25 % blocking, north foehn +7–9 K, nocturnal cold pools; hillshade/relief map |
| Weather map | Flat map with switchable layers: base field, isobars (1/2/4 hPa), fronts, cloud genus, volumes, precipitation shading and symbols, thunderstorm/hail markers, wind |
| Load cloud formation example / mock | Engine fixture (7 frames, fronts and day/night cloud) or a labelled synthetic mock; cloud genus layer, volumes, front overlay, inspector and metrics |
| Coupled wind request | Edit time steps, grid, atmosphere, diffusion and high/low systems; bound checks; download the request for the C++ CLI |
| Open JSON | Import forecast/v1, mobile_response/v1 or timeline/v1 |
| Open comparison JSON | Import a timeline matching baseline grid, times, terrain, rainfall, atmosphere and coverage |
| Export all frames (CSV) | Full comparison timeline, values, deltas, units, parameters and coverage counts |

- Orbit/zoom/cell inspection in Three.js; interactive 2D fallback if WebGL is unavailable.
- Terrain geometry and exaggeration are display controls.
- Terrain-adjusted temperature is an optional stored C++ diagnostic, applied once per snapshot.
- Surface/soil colors represent depth; no geometrically accurate flood surface is claimed.
- Missing coverage is null, not zero rainfall or known dry ground.

## Implemented boundaries

| Area | Implemented | Principal limit |
| --- | --- | --- |
| Temperature | 2D dry scalar advection/diffusion, fixed wind, periodic boundaries | No evolving pressure or energy balance |
| Wind | Single-layer pressure-gradient, Coriolis (f/beta-plane) and linear-friction momentum over prescribed high/low pressure systems; geostrophic and balanced diagnostics | Pressure does not respond to wind; no momentum advection, so no gradient-wind curvature |
| Coupled moist layer | Wind-carried temperature and specific humidity with configurable temperature/humidity/momentum diffusion; density diagnosed from p, T, q feeds back into the wind; RH, vapour pressure, virtual temperature diagnostics; viewer layers and request editor | Static pressure, no condensation/latent heat, first-order upwind numerical diffusion dominates on coarse grids |
| Cloud formation | Cold/warm/stationary fronts from |∇T|, advection and convergence; frontal layered cloud and overrunning wedges; heated-parcel convection (Cu → TCu → Cb) and combined frontal thunderstorms; cloud genus/base/top/fraction per cell; cloud view with volumes and front overlay; synthetic mock in the documented format | Rule-of-thumb genera and fractions; no microphysics, precipitation, latent-heat feedback, shear or front propagation; prescribed diurnal heating, warm sector decays without surface fluxes |
| Precipitation outcomes | Genus → drizzle/rain/snow/sleet/showers with rate from condensation, wet-bulb phase, WMO intensity, duration and accumulation; lightning and updraft-sized hail; virga; hazards; weather-map view with isobars and switchable layers | Cell-mean rules without microphysics, drift, snowpack or freezing rain; no feedback; storms not resupplied |
| Storm systems | Tropical cyclone objects: MPI, GPI genesis, Holland pressure, steering/beta drift, Fujiwhara rotation and mergers, landfall with surge and Kaplan–DeMaria decay; storm wind/pressure/rain/surge fields; events; map layers | Parametrised objects without vortex dynamics, ocean feedback or bathymetry; steering from the single layer |
| Severe convection | Column capping inversion, Ekman 0–1 km shear, thermal-wind deep shear, Bunkers motion, helicity, Significant Tornado Parameter with classes and cap-break flags; map/field layers | Single-layer proxies; climatological discriminator, not a tornado forecast |
| Orography | Terrain-following adiabats (moist ascent, dry descent → foehn) with orographic precipitation, cross-barrier Froude blocking with angle of attack and barrier jets, diurnal valley cold-air pooling, terrain surface pressure; ridge builder by orientation; relief map layers | Single layer without mountain waves, gap winds or radiation geometry; configured stability; rule-of-thumb pool/foehn flags |
| Ocean circulation | Reduced-gravity β-plane layer (Sverdrup interior, Stommel western boundary current, double-gyre separation), linear EOS with salinity/temperature feedback, rivers/rain/evaporation with a surface freshwater content (plumes, fronts), deep-water formation and overturning with sponge return, stream/exchange-point diagnostics; viewer Ocean streams view with streamlines, salinity, plumes, formation sites and circulation time series | Single layer, linear momentum, closed basin (wall jets), parametrised sinking, idealised forcing; no eddies, tides or equator; free-drift sea ice without rheology |
| Polar melt | Greenland-like ice sheet (shallow-ice flow, degree-day SMB with melt–elevation feedback, calving, Rignot frontal melt); Semtner sea ice with brine rejection, free drift and ridging in the ocean and as an Arctic column; two-box overturning calibrated to 17 Sv with tipping point, hysteresis, recovery time and early warnings; oxygen solubility; warming scenarios to 2300 with events and ensemble; in the ocean CLI, sea ice, a coupled ice sheet, warming and hosing; Polar melt view and ocean polar layers | Conceptual box overturning (dates scenario-relative, calibration-dependent); coarse synthetic Greenland without sliding, shelves or isostasy; one Arctic column; free-drift sea ice without rheology in the ocean; the 2-D ocean weakens and recovers but has no tipping point; ecosystem indicators are physical stressors |
| Ocean carbon | Boundary-layer CO₂ over land and sea (background, sources, wind, vegetation, ocean uptake); carbonate chemistry with pH and aragonite saturation; Wanninkhof CO₂/O₂ exchange; z-level carbon, alkalinity, oxygen and nitrate to the seafloor with conservative transport; river nitrate, production, Martin export, seafloor respiration, denitrification; habitat classes with the metabolic index; seasonal dead zones; Carbon & ocean life view, today and 2100 | No phosphate/iron, zooplankton or organic matter pools; depth-uniform current without Ekman pumping; single fixed-height boundary layer; synthetic sea and forcing |
| Coastal flooding | Sea-level budget from warming with fingerprints and subsidence; inundation with connectivity and defences; Gumbel storm levels with wind setup; river push-back and salt intrusion; Bruun erosion; islands and regions; events to 2300; Sea level & coasts view | Bathtub without flow dynamics, breaching or adaptation; frictionless setup; synthetic coast unless per-cell data are given |
| Solar cycle | Sunspot record (SILSO monthly or synthesised from a cycle table), SILSO smoothing, minima/maxima detection, Hathaway cycle fits; forecast of the cycle in progress (onset-prior fit vs AR(1) prior, weighted by a backtest) and of later cycles with a 200-member ensemble; TSI/F10.7/Mg II/UV-band proxies, ozone, stratosphere and surface UV index; flare rates, size distribution, R scale, energy; two-layer energy-balance climate response; rendered solar disk and butterfly diagram; forcing export read by the ocean shortwave and the polar-melt scenarios; grand minima and maxima since 1000 AD (epoch envelope, slow background TSI in three scenarios, cosmogenic production, volcanic and CO₂ comparison forcings, illustrative regional winter response, stochastic Babcock–Leighton dynamo map calibrated to the Holocene statistics, risk of a new grand minimum); Sun & solar cycle view | Statistical proxy model: one smooth curve per cycle, no polar-field precursor (cycles not yet started are climatology), linear UV proxies uncertain by ~2×, rate-model flares (no list, CMEs or particles), global energy balance without top-down or ocean-dynamical amplification; pre-1755 cycles synthesised around an epoch catalogue (not a reconstruction), rounded comparison forcings, a one-dimensional dynamo map with too few grand maxima; the example's record is synthetic |
| Catchment hydrology | Depression-filled D8 network (lakes with spill/capacity/outlet, Strahler order, channels by initiation area, basins, ocean/edge outlets); snow, Horton infiltration, soil, percolation, Darcy aquifer with baseflow and springs; kinematic overland/channel cascade with bankfull overflow and lake spilling; flood classes, stream-power erosion/deposition, infinite-slope landslides with runout, Caine threshold; viewer Catchment and Hazards views with water tracing and hydrograph | Kinematic (one cell per step, no backwater); static terrain; uniform soil/aquifer; cell-average slope stability; synthetic forcing |
| Vertical column | Hydrostatic layers thinning with height; dry convective and saturation adjustment conserving enthalpy and water; surface-parcel LCL/LFC/EL/CAPE/CIN and potential class per cell and frame; viewer layers (CAPE, cloud base, equilibrium level) and editor fields | Diagnostic only (no feedback to the horizontal model), no surface fluxes, precipitation, microphysics, ice phase or shear; CAPE class is potential, not a storm forecast |
| Input pipeline | Palette-map decoding, canonical observations, projection/gridding, timestamp alignment | NOAA helper path is unverified and disconnected from viewer |
| Precipitation | Configurable empirical Z-R estimate, trapezoidal accumulation | Radar-derived estimate, not measured surface rain |
| Terrain | Explicit elevation datum, slope/aspect, optional temperature lapse rate | No terrain-driven wind or pressure dynamics |
| Surface water | Four-neighbor head-gradient routing and donor limiting | Storage/routing prototype, not shallow-water momentum |
| Soil | Capacity-limited infiltration and separate storage | Uniform scenario parameters; no resolved soil profile |
| Losses | Availability-limited prescribed evaporation and soil drainage | No weather-driven evapotranspiration or groundwater store |
| Outlets | Unique active boundary cells, crest/capacity limits, separate discharge budget | Prescribed sink, not calibrated weir/river hydraulics |
| Regional atmosphere | Analytic display of clouds, centers, front, pressure and humidity | Illustration only; does not drive local kernels |

The example process order is rain → routing → infiltration → evaporation → drainage → optional surface outlet. Atmospheric transport remains independent of hydrology. With initially empty stores, surface + soil + evaporated + drained + discharged volume balances covered rainfall input. Unknown-rainfall cells are impermeable masks in this prototype.

## Reviewer commands

```sh
# All registered C++ and integration cases, after building.
ctest --test-dir build --output-on-failure
# Selected water regressions.
ctest --test-dir build -R 'surface_water|infiltration|water_losses|surface_outlets|water_timestep' --output-on-failure
# Pipeline/configuration and fixture cases.
ctest --test-dir build -R 'rainfall_pipeline|hydrology_scenario|viewer_' --output-on-failure
# Python and frontend checks separately.
python3 -m unittest discover -s tests/tools -v
npm --prefix viewer test
npm --prefix viewer run build
# Verify committed synthetic baseline without changing it.
python3 tools/build_viewer_example.py --build-dir build --check
# Verify timestep sensitivity report.
python3 tools/check_water_timestep.py --build-dir build --check
# Solar cycle: kernels, CLI identities, coupling to the ocean and polar-melt runs, and the fixture.
ctest --test-dir build -R solar --output-on-failure
```

CTest includes baseline, comparison and outlet fixture regeneration, basin ponding/overflow, timestep sensitivity, outlet limits and full budget checks. The verification runner fails if no CTest cases are found and stops on the first failing stage.

## Relevant files

- Public kernels: [include/terraflux/weather](../include/terraflux/weather/) and [src/weather](../src/weather/).
- Kernel/integration tests: [tests/weather](../tests/weather/) and [tests/tools](../tests/tools/).
- Pressure-gradient wind: [pressure_wind.hpp](../include/terraflux/weather/pressure_wind.hpp), [contract](weather/pressure-wind-model.md), [example request](../examples/pressure-wind-request.json), [CLI check](../tests/tools/pressure_wind_check.py).
- Cloud formation: [cloud_formation.hpp](../include/terraflux/weather/cloud_formation.hpp), [contract and format](weather/cloud-formation-model.md), [schema](weather/cloud-formation-v1.schema.json), [example request](../examples/cloud-formation-request.json), [fixture](../viewer/public/clouds/engine-timeline.json), [CLI/fixture check](../tests/tools/cloud_formation_check.py), [viewer module and mock](../viewer/src/cloudFormation.js).
- Ocean circulation: [ocean_circulation.hpp](../include/terraflux/weather/ocean_circulation.hpp), [contract](weather/ocean-circulation-model.md), [request schema](weather/ocean-run-v1.schema.json), [block schema](weather/ocean-circulation-v1.schema.json), [Atlantic streams request](../examples/atlantic-streams-request.json), [CLI check](../tests/tools/ocean_circulation_check.py), [viewer module](../viewer/src/ocean.js).
- Polar melt: [ice_sheet.hpp](../include/terraflux/weather/ice_sheet.hpp), [sea_ice.hpp](../include/terraflux/weather/sea_ice.hpp), [overturning_box.hpp](../include/terraflux/weather/overturning_box.hpp), [marine_indicators.hpp](../include/terraflux/weather/marine_indicators.hpp), [contract](weather/polar-melt-model.md), [request schema](weather/polar-melt-run-v1.schema.json), [block schema](weather/polar-melt-v1.schema.json), [polar melt request](../examples/polar-melt-request.json), [polar ocean request](../examples/polar-ocean-request.json), [CLI checks](../tests/tools/polar_melt_check.py) and [polar ocean](../tests/tools/polar_ocean_check.py), [viewer module](../viewer/src/polar.js).
- Ocean carbon: [ocean_carbon.hpp](../include/terraflux/weather/ocean_carbon.hpp), [ocean_biogeochemistry.hpp](../include/terraflux/weather/ocean_biogeochemistry.hpp), [atmospheric_co2.hpp](../include/terraflux/weather/atmospheric_co2.hpp), [contract](weather/ocean-carbon-model.md), [request schema](weather/ocean-run-v1.schema.json), [block schema](weather/ocean-circulation-v1.schema.json), [today](../examples/ocean-carbon-request.json) and [2100](../examples/ocean-carbon-2100-request.json) requests (written by [make_ocean_carbon_examples.py](../tools/make_ocean_carbon_examples.py)), [CLI check](../tests/tools/ocean_carbon_check.py), [viewer module](../viewer/src/oceanCarbon.js).
- Coastal flooding: [sea_level.hpp](../include/terraflux/weather/sea_level.hpp), [coastal_flooding.hpp](../include/terraflux/weather/coastal_flooding.hpp), [contract](weather/coastal-flooding-model.md), [request schema](weather/coastal-flooding-run-v1.schema.json), [block schema](weather/coastal-flooding-v1.schema.json), [example request](../examples/coastal-flooding-request.json), [CLI check](../tests/tools/coastal_flooding_check.py), [viewer module](../viewer/src/coastal.js), [layer catalogue](../viewer/src/layers.js).
- Solar cycle: [solar_cycle.hpp](../include/terraflux/weather/solar_cycle.hpp), [solar_irradiance.hpp](../include/terraflux/weather/solar_irradiance.hpp), [solar_forcing.hpp](../include/terraflux/weather/solar_forcing.hpp), [contract](weather/solar-cycle-model.md), [grand minima and maxima](weather/solar-grand-minima-model.md), [solar_long_term.hpp](../include/terraflux/weather/solar_long_term.hpp), [request schema](weather/solar-cycle-run-v1.schema.json), [block schema](weather/solar-cycle-v1.schema.json), [example request](../examples/solar-cycle-request.json), [SILSO converter](../tools/silso_to_request.py), [CLI check](../tests/tools/solar_cycle_check.py), [coupling check](../tests/tools/solar_coupling_check.py), [viewer module](../viewer/src/solar.js).
- Catchment hydrology: [catchment_hydrology.hpp](../include/terraflux/weather/catchment_hydrology.hpp), [synthetic_terrain.hpp](../include/terraflux/weather/synthetic_terrain.hpp), [contract](weather/catchment-hydrology-model.md), [request schema](weather/catchment-run-v1.schema.json), [block schema](weather/catchment-hydrology-v1.schema.json), [alpine flood request](../examples/alpine-flood-request.json), [CLI check](../tests/tools/catchment_hydrology_check.py), [viewer module](../viewer/src/hydrology.js).
- Orography: [OrographyConfig in coupled_wind.hpp](../include/terraflux/weather/coupled_wind.hpp), [contract](weather/orography-model.md), [schema](weather/orography-v1.schema.json), [Alps request](../examples/alps-foehn-request.json), [Rockies request](../examples/rockies-barrier-request.json), [fixture](../viewer/public/alps/engine-timeline.json), [CLI check](../tests/tools/orography_check.py), [viewer module](../viewer/src/orography.js).
- Storm systems and severe convection: [storm_systems.hpp](../include/terraflux/weather/storm_systems.hpp), [severe_convection.hpp](../include/terraflux/weather/severe_convection.hpp), [storm contract](weather/storm-systems-model.md), [severe contract](weather/severe-convection-model.md), [hurricane request](../examples/hurricane-landfall-request.json), [tornado request](../examples/tornado-outbreak-request.json), [fixture](../viewer/public/hurricane/engine-timeline.json), [CLI check](../tests/tools/storm_systems_check.py), [viewer module](../viewer/src/stormSystems.js).
- Precipitation outcomes: [precipitation_outcomes.hpp](../include/terraflux/weather/precipitation_outcomes.hpp), [contract](weather/precipitation-outcomes-model.md), [schema](weather/precipitation-outcomes-v1.schema.json), [viewer map module](../viewer/src/weatherMap.js).
- Vertical column: [atmospheric_column.hpp](../include/terraflux/weather/atmospheric_column.hpp), [contract](weather/atmospheric-column-model.md), [example request](../examples/column-request.json), [CLI check](../tests/tools/column_check.py); Coriolis review: [coriolis-verification.md](weather/coriolis-verification.md).
- Coupled moist layer: [coupled_wind.hpp](../include/terraflux/weather/coupled_wind.hpp), [moist_air.hpp](../include/terraflux/weather/moist_air.hpp), [contract](weather/coupled-wind-model.md), [relations](weather/moist-air-relations.md), [example request](../examples/coupled-wind-request.json), [fixture](../viewer/public/coupled/engine-timeline.json), [CLI/fixture check](../tests/tools/coupled_wind_check.py), [viewer module](../viewer/src/coupledWind.js).
- Scenario configurations: [baseline](../examples/hydrology-scenario.json), [lower infiltration](../examples/hydrology-low-infiltration.json), [outlet](../examples/hydrology-outlet-scenario.json).
- Scenario validation: [hydrology_scenario.hpp](../tools/hydrology_scenario.hpp) and [JSON Schema](weather/hydrology-scenario-v1.schema.json).
- Reproduction: [C++ example generator](../tools/generate_viewer_example.cpp), [composer/build helper](../tools/build_viewer_example.py), [verification runner](../tools/verify_pipeline.py).
- Frontend: [App.vue](../viewer/src/App.vue), [input validation](../viewer/src/data.js), [comparison/export](../viewer/src/comparison.js), [regional illustration/metrics](../viewer/src/weatherSystems.js).
- Committed examples: [baseline](../viewer/public/examples/engine-timeline.json), [comparison](../viewer/public/comparison/engine-timeline.json), [outlet](../viewer/public/outlet/engine-timeline.json).

## Validation evidence and remaining checks

- Full C++/Python and viewer CI passed for verification-runner commit `782476f374e7d3d22fff6e99781b62411360ff44`: [Actions run 41446036826](https://github.com/smatthias/terraflux-physics/actions/runs/41446036826).
- That automated scope includes 16 registered CTest cases, six Python unit checks and eighteen viewer tests, plus production build and fixture regeneration.
- Browser checks used the 2D fallback. Actual WebGL rendering still needs a capable browser/GPU.
- No live-provider run or operational physical calibration has been verified.
- Timestep sensitivity on the committed synthetic case measures dependence, not convergence or physical truth: [report method](weather/water-timestep-sensitivity.md).
- Basin overflow is internal transfer inside a closed domain; external outlets are a separate tested operation.

## Baseline preservation for further work

- Keep example inputs and their synthetic labels explicit; live data integration is outside this handover baseline.
- Use a separate output directory when generating experimental scenarios; do not overwrite committed examples accidentally.
- Regenerate matching fixtures after intentional model/configuration changes and inspect budget and comparison differences.
- Preserve versioned contracts, null coverage, unit conventions and source/datum metadata.
- Keep new physics independently testable; AI and behavior prediction are outside the work assigned here.
- Packaging, credential inspection and the final task brief are separate handover steps; this document is not a prepared submission ZIP.
