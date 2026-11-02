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

Open the URL printed by Vite. No login, database, or backend is required. Use **Open JSON** for a `terraflux.weather.forecast/v1` file or composed `terraflux.weather.mobile_response/v1` file. Files remain in browser memory. The initial synthetic demo is illustrative, not solver output. Temperature, wind speed, rainfall, wind vectors, orbit/zoom, and cell inspection are supported. Height represents the selected field, not terrain elevation. Single snapshots and validated snapshot timelines are supported; there is no live browser solver. Rainfall is unavailable for bare forecast files. Limit: 65,536 cells / 20 MB input.

Checks: `npm test` and `npm run build` inside `viewer/`.

### Reproducible engine example

Select **Load engine example** in the viewer to open the committed 12 × 10 example. It uses synthetic radar and atmospheric inputs evaluated by the production C++ dry-transport, rain-rate estimator, accumulator, and gridding kernels, then the Python composer. This is engine output from synthetic inputs, not live weather or a coupled rainfall/temperature simulation. It includes missing rainfall coverage.

After a normal CMake build, regenerate or verify the bundled file:

```sh
python3 tools/build_viewer_example.py --build-dir build
python3 tools/build_viewer_example.py --build-dir build --check
```

CTest runs the regeneration check. The comparison allows relative floating-point differences of 1e-9 across platforms. The snapshot file contains the final state after 120 × 30-second dry-transport steps. The companion timeline contains seven snapshots at ten-minute intervals, with static prescribed wind. A separate one-hour radar-derived rainfall layer is included only at the final frame.

### Timeline playback

**Load engine example** opens the generated seven-frame timeline. Play/pause, previous/next, and the frame slider select discrete solver snapshots. Playback stops at the end; Play from the end restarts. Scrubbing or loading another dataset stops playback. Color ranges are fixed across all frames for each field. No temporal interpolation or browser-side physics is performed.

JSON input also accepts `terraflux.weather.timeline/v1`: an object with a `frames` array of 2–240 forecast or mobile-response objects. All frames must have the same grid and start time, strictly increasing elapsed times, and no more than two million cell snapshots in total. Rainfall is enabled only on frames containing an interval-aligned rainfall layer. See [timeline contract](docs/weather/timeline-v1.md).
