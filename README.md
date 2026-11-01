# TerraFlux Physics

A C++ home for deterministic, testable physical models used by TerraFlux. The first planned area is weather physics, with a future interface for coupling weather to terrain and wildfire spread.

The engine is intended to read both structured weather datasets and rendered weather maps, interpret them into georeferenced observations, and keep those observations distinct from simulated state. The current implementation reads a v1 map sidecar, decodes palette-based PPM P3 or PNG reflectivity rasters, and writes canonical weather observation JSON. A first-order dry scalar transport solver is implemented for a narrow 2D case; broader atmospheric dynamics and live provider adapters remain future work.

## Layout

- `docs/weather/architecture.md` — model scope, boundaries, and implementation sequence
- `docs/weather/dry-transport-model.md` — first solver assumptions, discretization, stability bound, and analytic cases
- `docs/weather/dry-transport-config-v1.schema.json` — dry transport configuration schema
- `docs/weather/dry-transport-state-v1.schema.json` — dry transport state schema
- `docs/weather/map-interpretation.md` — map input and interpretation pipeline
- `docs/weather/map-decoder.md` — decoder API, CLI, formats, dependencies, and limitations
- `docs/weather/observation-schema.json` — v1 raster-sidecar schema
- `docs/weather/observation-record-v1.schema.json` — canonical observation schema
- `include/terraflux/weather/` — public observation and transport model types
- `src/weather/` — sidecar parsing, raster decoding, serialization, CLI, and transport solver
- `tests/weather/` — analytic transport cases and planned map/reference coverage
- `examples/weather/maps/` — synthetic palette rasters and metadata
- `CMakeLists.txt` — C++17 weather map and transport targets with CTest integration

## Working principles

- Use SI units and document coordinate and direction conventions.
- Preserve source, valid time, georeferencing, quality, and uncertainty for interpreted data.
- Keep map parsing and physical calculations independent of rendering, networking, and user interface code.
- State model assumptions, spatial and temporal scales, and boundary conditions.
- Validate simple cases before combining processes.
