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

`tools/compose_mobile_weather.py` composes forecast and explicitly gridded rainfall files with spatial, interval, and missing-data checks. See [the input contract and commands](docs/weather/mobile-weather-response-v1.md#file-composer). Raw rainfall observation gridding remains pending.
