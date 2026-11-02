# TerraFlux Physics

A C++ home for deterministic, testable physical models used by TerraFlux. The first planned area is weather physics, with a future interface for coupling weather to terrain and wildfire spread.

The engine is intended to read structured weather datasets and rendered weather maps, interpret them into georeferenced observations, and keep those observations distinct from simulated state. The current implementation decodes sidecar-backed radar and temperature PNG/PPM maps, tracks timestamped temperature frames through a 2D dry transport model, and writes versioned forecast JSON. NOAA MRMS is the temporary live radar data source; its GRIB2 acquisition helper is in place, while GRIB2 decoding into the model's canonical observation contract remains the next step. The planned Victoria source remains BOM radar, pending a suitable commercial licence and sample.

## Layout

- `docs/weather/architecture.md` — model scope, boundaries, and implementation sequence
- `docs/weather/dry-transport-model.md` — first solver assumptions, discretization, stability bound, and analytic cases
- `docs/weather/observation-grid.md` — georeferenced observation projection, cell assignment, and quality preservation
- `docs/weather/dry-transport-config-v1.schema.json` — dry transport configuration schema
- `docs/weather/dry-transport-state-v1.schema.json` — dry transport state schema
- `docs/weather/map-interpretation.md` — map input and interpretation pipeline
- `docs/weather/map-source-status.md` — NOAA prototype and BOM source status
- `docs/weather/noaa-mrms.md` — NOAA MRMS acquisition, bounded GRIB2 decoding, and attribution
- `docs/weather/precipitation-estimation.md` — configurable empirical conversion from radar reflectivity to rain-rate estimates
- `docs/weather/precipitation-accumulation.md` — timestamped rain-rate integration and interval quality rules
- `docs/weather/mobile-weather-response-v1.schema.json` and `mobile-weather-response-v1.md` — versioned mobile envelope combining forecast and aligned precipitation accumulation
- `docs/weather/map-decoder.md` — decoder API, CLI, formats, dependencies, and limitations
- `docs/weather/observation-schema.json` — v1 raster-sidecar schema
- `docs/weather/observation-record-v1.schema.json` — canonical observation schema
- `include/terraflux/weather/` — public observation, gridding, and transport model types
- `src/weather/` — sidecar parsing, raster and GRIB2 observation import, serialization, precipitation estimator and accumulator, CLI, observation mapping, and transport solver
- `tests/weather/` — analytic transport cases and planned map/reference coverage
- `examples/weather/maps/` — synthetic palette rasters and metadata
- `tools/fetch_noaa_mrms.py` — download one current NOAA MRMS composite and provenance
- `tools/decode_noaa_mrms.py` — decode a bounded MRMS grid crop into canonical reflectivity observations
- `terraflux-estimate-rain-rate` — estimate a precipitation rate from reflectivity observations with configurable Z-R coefficients
- `terraflux-accumulate-rainfall` — integrate timestamped rate observations into precipitation depth in millimetres
- `CMakeLists.txt` — C++17 weather map and transport targets with CTest integration

## Working principles

- Use SI units and document coordinate and direction conventions.
- Preserve source, valid time, georeferencing, quality, and uncertainty for interpreted data.
- Keep map parsing and physical calculations independent of rendering, networking, and user interface code.
- State model assumptions, spatial and temporal scales, and boundary conditions.
- Validate simple cases before combining processes.

### Combined weather response

`tools/compose_mobile_weather.py` composes forecast and explicitly gridded rainfall files with spatial, interval, and missing-data checks. See [the input contract and commands](docs/weather/mobile-weather-response-v1.md#file-composer). The `terraflux-grid-rainfall` CLI maps geographic accumulation observations onto the forecast grid before composition.

## Vue + Three.js physics viewer

```sh
cd viewer
npm ci
npm run dev
```

Open the URL printed by Vite. No login, database, or backend is required. Use **Open JSON** for a `terraflux.weather.forecast/v1` file or composed `terraflux.weather.mobile_response/v1` file. Files remain in browser memory. The initial synthetic demo is illustrative, not solver output. Temperature, wind speed, rainfall, wind vectors, orbit/zoom, and cell inspection are supported. Without terrain, height represents the selected field. With optional terrain enabled, height represents relative elevation and color represents the selected field. Single snapshots and validated snapshot timelines are supported; there is no live browser solver. Rainfall is unavailable for bare forecast files. Limit: 65,536 cells / 20 MB input.

Checks: `npm test` and `npm run build` inside `viewer/`.

### Reproducible engine example

Select **Load engine example** in the viewer to open the committed 12 × 10 example. It uses synthetic radar and atmospheric inputs evaluated by the production C++ dry-transport, rain-rate estimator, accumulator, and gridding kernels, then the Python composer. This is engine output from synthetic inputs, not live weather or a coupled rainfall/temperature simulation. It includes missing rainfall coverage.

After a normal CMake build, regenerate or verify the bundled file:

```sh
python3 tools/build_viewer_example.py --build-dir build
python3 tools/build_viewer_example.py --build-dir build --check
```

CTest runs the regeneration check. The comparison allows relative floating-point differences of 1e-9 across platforms. The snapshot file contains the final state after 260 × 30-second dry-transport steps. The companion timeline contains 14 snapshots at ten-minute intervals, with static prescribed wind. Each positive-time frame includes radar-derived rainfall accumulated from the start up to that frame.

### Timeline playback

**Load engine example** opens the generated 14-frame timeline. Play/pause, previous/next, and the frame slider select discrete solver snapshots. Playback stops at the end; Play from the end restarts. Scrubbing or loading another dataset stops playback. Color ranges are fixed across all frames for each field. No temporal interpolation or browser-side physics is performed.

JSON input also accepts `terraflux.weather.timeline/v1`: an object with a `frames` array of 2–240 forecast or mobile-response objects. All frames must have the same grid and start time, strictly increasing elapsed times, and no more than two million cell snapshots in total. Rainfall is enabled only on frames containing an interval-aligned rainfall layer. See [timeline contract](docs/weather/timeline-v1.md).

### Optional terrain

The bundled timeline includes a deterministic synthetic Gaussian hill. Select **Terrain elevation** to inspect it, or use **Place fields on terrain** and the height-scale slider to display weather colors on its geometry. The 2D fallback shows elevation as color. Original metre values and their explicit datum appear in the cell inspector. An optional lapse-rate diagnostic can adjust displayed temperatures without modifying transport. See [terrain grid contract](docs/weather/terrain-grid-v1.md).

### Terrain temperature diagnostic

The C++ `adjust_temperature_for_terrain` function optionally computes `Tadjusted = Treference - lapse_rate × (elevation - reference_elevation)`. It preserves inputs and validates matching vertical datums, finite values, array lengths, and positive kelvin output. The function is disabled by default; the caller supplies grid-aligned arrays.

The example uses an explicitly chosen 0.0065 K/m lapse rate and 80 m reference elevation in its synthetic datum. These are scenario assumptions, not measured weather. In the viewer, **Terrain-adjusted temperature** switches between stored C++ diagnostic output and the original field, with the correction shown in the inspector. The adjustment is applied once to each snapshot and never feeds back into transport. Wind, pressure, moisture, and surface fluxes are unaffected.

The timeline may carry `terrain_temperature` with schema version `terraflux.weather.terrain_temperature/v1`, units `K`, finite `lapse_rate_k_per_m` and `reference_elevation_m`, a matching `vertical_reference`, and `values_by_frame` arrays matching every forecast. The viewer verifies the declared relation and rejects inconsistent output.

### Terrain slope and aspect

The C++ `calculate_terrain_derivatives` function computes metric terrain gradients, slope angle, and downhill aspect. The example bundles these fields for **Terrain slope** and **Downhill aspect** viewer layers. Aspect is clockwise from north, with undefined flat cells preserved as null. One-sided boundary differences do not wrap across the domain. See [method and contract](docs/weather/terrain-derivatives-v1.md).

### Surface-water storage and runoff prototype

The C++ `advance_surface_water` function adds interval rainfall and routes stored water between four neighboring cells down free-surface head gradients. Donor-limited transfers keep storage nonnegative. Boundaries are closed. Optional capacity-limited infiltration transfers surface water into a separate soil store; prescribed evaporation and soil drainage can remove water with separate accounting; there is no external surface outlet or momentum solver. It is a conservative storage/routing prototype, not shallow-water hydraulics.

The example applies each ten-minute rainfall increment uniformly over twenty 30-second steps, with routing speed parameter 0.002 m/s. Cells lacking rainfall coverage are impermeable masks and retain unknown/null water depth in the output. Water depth is stored in metres and displayed in millimetres. The **Surface water depth** layer includes stored volume, cumulative rainfall input and mass-balance residual for every frame. Terrain and weather remain independent of the routed water state. See [surface-water contract](docs/weather/surface-water-v1.md).

### Soil-water storage

The optional C++ infiltration step transfers available surface water into a capacity-limited soil store. The example runs rain → routing → infiltration → evaporation → drainage each 30 seconds. **Soil water storage** is a separate playback layer; budget readouts show surface and soil volumes against cumulative rainfall. The example rate and capacity are illustrative scenario parameters. See [infiltration contract and limits](docs/weather/infiltration-v1.md).

### Evaporation and soil drainage

`apply_water_losses` optionally removes a shared evaporation demand from surface then soil storage, followed by availability-limited soil drainage. Playback budgets show cumulative evaporation and drainage alongside remaining storage. The synthetic example uses prescribed rates, not a weather-driven evaporation calculation. All removed water remains explicitly represented in the mass budget. See [loss contract](docs/weather/water-losses-v1.md).

### Wet-to-dry regression scenario

The generated example covers 12:00–14:10 UTC: one hour of synthetic rain-rate estimates, a ten-minute linear taper to zero, then one rain-free hour. Explicit zero rates are scenario forcing; missing coverage remains unknown. This controlled edit is identified in observation derivation metadata. Cumulative rainfall stops growing at 13:10, while routing, infiltration, evaporation and drainage continue. The timeline labels intervals from rainfall increments. Regression checks verify constant dry-phase rain input, declining combined storage, nonnegative depths, growing accounted losses and mass closure. Soil and surface stores decrease over the dry period; they need not be completely empty.

### Clouds, regional weather and metrics

Select **Regional weather demo** for a separate 1,200 × 900 km analytic scene with moving low/high-pressure centers, a front, cloud cover, pressure and humidity layers. Cloud and system overlays work in Three.js and the 2D fallback. These are explicitly synthetic illustrations, not output from an atmospheric circulation or cloud model, and they do not drive local hydrology.

Metrics now show domain area, temperature range/mean, wind mean/maximum, cloud percentage, pressure range and humidity. Engine examples additionally show rainfall coverage/mean, wet area, peak surface-water depth, soil capacity usage and water budgets. See [display contract and metric definitions](docs/weather/regional-display-v1.md).

### Configurable hydrology scenario

Edit `examples/hydrology-scenario.json` to set routing speed, infiltration, soil capacity, evaporation, drainage and the water timestep. The C++ example generator validates the complete configuration before creating output. Supported boundary, missing-data and process-order settings are explicit; unsupported modes and unknown keys are rejected. Water timesteps must be integer divisors of the 600-second output interval. Atmospheric transport keeps its independent 30-second timestep.

```sh
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-scenario.json
# Preserve the bundled fixture when experimenting with another config:
python3 tools/build_viewer_example.py --build-dir build --scenario my-scenario.json --output /tmp/custom-weather/engine-weather.json
```

Open `/tmp/custom-weather/engine-timeline.json` in the viewer. The timeline records the exact applied `water_scenario`, and its water output carries matching parameters. Water/soil budget readouts show timestep and rate settings. The [JSON Schema](docs/weather/hydrology-scenario-v1.schema.json) documents the file contract. Parameters remain illustrative and prescribed; no AI or predictive model is involved.

### Deterministic scenario comparison

Select **Compare hydrology scenarios** to load the baseline and a C++-generated variant with infiltration reduced from 3.60 to 0.36 mm/h. Playback is synchronized, the map can switch between runs, and water/soil layers share a common color range. A table shows selected-time surface storage, soil storage, evaporation, drainage and rainfall, with delta defined as variant minus baseline. The chart compares surface storage across the full timeline and marks the current frame.

**Open comparison JSON** accepts another generated timeline only when grid, times, terrain, rainfall, atmospheric forcing and coverage match the current baseline. Both runs must provide water output at every snapshot. This is a deterministic parameter comparison, not AI prediction or an assessment of real soil behavior.

Regenerate or verify the bundled variant:

```sh
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-low-infiltration.json --output viewer/public/comparison/engine-weather.json --timeline-only
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-low-infiltration.json --output viewer/public/comparison/engine-weather.json --timeline-only --check
```

CTest checks both baseline and variant regeneration. The `--timeline-only` flag avoids saving an unused final-snapshot file for comparisons.

### Comparison CSV export

After loading a comparison, select **Export all frames (CSV)**. The download contains one row per snapshot, regardless of the current playback frame or visible scenario. Columns include UTC valid time, elapsed seconds, scenario labels, known/masked cell counts, baseline/variant/delta volumes (surface, soil, evaporation, drainage, rain and balance residual), applied timestep/rate parameters and soil capacities. Volume units are m³, storage capacities metres and rates m/s, identified in column names. Delta means variant minus baseline.

The CSV uses UTF-8 with BOM and CRLF rows, preserves numeric precision, escapes quoted/multiline labels, and neutralizes labels that could be interpreted as spreadsheet formulas. The export contains aggregates for the modeled coverage; masked cells are counted explicitly. Data are downloaded locally without a server or database.

### Water timestep sensitivity

`python3 tools/check_water_timestep.py --build-dir build --check` reruns the synthetic hydrology case at 15, 30 and 60 second water timesteps. It checks storage positivity, capacity, coverage, input consistency and full mass accounting, then verifies the committed report. CTest includes this regression. The report measures spatial surface-water differences from the 15-second run; it is not proof of physical accuracy or convergence. See [method and results](docs/weather/water-timestep-sensitivity.md).

### Basin ponding and overflow regression

`ctest --test-dir build -R surface_water_basin --output-on-failure` checks stable ponding below a spill point, equal-head retention, internal overflow above it, a raised spill barrier and rainfall-driven filling. Each step verifies nonnegative depths and the closed-domain water budget. This is a controlled routing regression, not a hydraulic spillway model. See [geometry and limits](docs/weather/surface-water-basin-test.md).
