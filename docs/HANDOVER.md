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
| Temperature | 2D dry scalar advection/diffusion, fixed wind, periodic boundaries | No evolving wind/pressure or energy balance |
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
```

CTest includes baseline, comparison and outlet fixture regeneration, basin ponding/overflow, timestep sensitivity, outlet limits and full budget checks. The verification runner fails if no CTest cases are found and stops on the first failing stage.

## Relevant files

- Public kernels: [include/terraflux/weather](../include/terraflux/weather/) and [src/weather](../src/weather/).
- Kernel/integration tests: [tests/weather](../tests/weather/) and [tests/tools](../tests/tools/).
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
