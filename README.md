# TerraFlux Physics

A C++ home for deterministic, testable physical models used by TerraFlux. The first planned area is weather physics, with a future interface for coupling weather to terrain and wildfire spread.

The engine is intended to read both structured weather datasets and rendered weather maps, interpret them into georeferenced observations, and keep those observations distinct from simulated state. The first implementation is a small radar-reflectivity map decoder; the weather solver and production data adapters are still future work.

## Layout

- `docs/weather/architecture.md` — model scope, boundaries, and implementation sequence
- `docs/weather/map-interpretation.md` — map input and interpretation pipeline
- `docs/weather/map-decoder.md` — current decoder API, supported format, and limitations
- `docs/weather/observation-schema.json` — v1 raster-sidecar schema
- `docs/weather/observation-record-v1.schema.json` — canonical observation schema
- `include/terraflux/weather/` — stable public data types and interfaces
- `src/weather/` — weather-map decoding implementation
- `tests/weather/` — planned unit, invariant, reference, and regression checks
- `examples/weather/maps/` — synthetic palette raster and its metadata
- `CMakeLists.txt` — C++17 decoder library target

## Working principles

- Use SI units and document coordinate and direction conventions.
- Preserve source, valid time, georeferencing, quality, and uncertainty for interpreted data.
- Keep map parsing and physical calculations independent of rendering, networking, and user interface code.
- State model assumptions, spatial and temporal scales, and boundary conditions.
- Validate simple cases before combining processes.
