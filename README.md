# TerraFlux Physics

A C++ home for deterministic, testable physical models used by TerraFlux. The first planned area is weather physics, with a future interface for coupling weather to terrain and wildfire spread.

This repository is a planning scaffold. It does not yet contain a weather solver and is not a weather forecast product.

## Layout

- `docs/weather/architecture.md` — model scope, boundaries, and implementation sequence
- `include/terraflux/weather/` — stable public data types and interfaces
- `src/weather/` — numerical implementation, organized by physical process
- `tests/weather/` — unit, invariant, reference, and regression checks
- `examples/weather/` — small deterministic scenarios

## Working principles

- Use SI units and document coordinate and direction conventions.
- Keep physical calculations independent of rendering, networking, and user interface code.
- State model assumptions, spatial and temporal scales, and boundary conditions.
- Validate simple cases before combining processes.
