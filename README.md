# TerraFlux Physics

A C++ home for deterministic, testable physical models used by TerraFlux. The first planned area is weather physics, with a future interface for coupling weather to terrain and wildfire spread.

The engine is intended to read both structured weather datasets and rendered weather maps, interpret them into georeferenced observations, and keep those observations distinct from simulated state. The current implementation reads a v1 map sidecar and decodes palette-based PPM P3 or PNG reflectivity rasters; the weather solver and live provider adapters remain future work.

## Layout

- `docs/weather/architecture.md` — model scope, boundaries, and implementation sequence
- `docs/weather/map-interpretation.md` — map input and interpretation pipeline
- `docs/weather/map-decoder.md` — decoder API, formats, dependencies, and limitations
- `docs/weather/observation-schema.json` — v1 raster-sidecar schema
- `docs/weather/observation-record-v1.schema.json` — canonical observation schema
- `include/terraflux/weather/` — public weather data types and interfaces
- `src/weather/` — sidecar parsing and raster decoding
- `tests/weather/` — planned unit, invariant, reference, and regression checks
- `examples/weather/maps/` — synthetic palette raster and its metadata
- `CMakeLists.txt` — C++17 decoder library target and dependencies

## Working principles

- Use SI units and document coordinate and direction conventions.
- Preserve source, valid time, georeferencing, quality, and uncertainty for interpreted data.
- Keep map parsing and physical calculations independent of rendering, networking, and user interface code.
- State model assumptions, spatial and temporal scales, and boundary conditions.
- Validate simple cases before combining processes.
