# Weather physics architecture

## Purpose

Build a deterministic weather model that can evolve atmospheric conditions over terrain and later provide environmental inputs to TerraFlux fire-spread models. The first implementation should be a transparent simulation model with documented limits, not a claim of operational forecasting skill.

## Proposed process boundaries

- **State and units:** atmospheric variables, grid geometry, time, and explicit SI-unit conventions.
- **Dynamics:** wind velocity, transport, pressure-gradient acceleration, and vertical motion.
- **Thermodynamics:** temperature, density, and heat exchange.
- **Moisture and phase:** water vapor, condensation, cloud water, and precipitation.
- **Terrain and surface exchange:** roughness, heat and moisture fluxes, and terrain-driven flow effects.
- **Coupling:** a narrow adapter that supplies wind, temperature, humidity, and precipitation to other TerraFlux models.

These are boundaries for organizing code. They do not commit the project to a particular discretization or solver.

## Build sequence

1. Agree the target scale, dimensions, state variables, units, coordinate frame, and boundary conditions.
2. Define immutable configuration and state contracts with validation.
3. Implement the smallest dry-air transport case and compare it with analytic expectations.
4. Add thermodynamic and moisture processes one at a time, with invariants and reference cases.
5. Add terrain and surface exchange after the atmospheric baseline is stable.
6. Add adapters for fire physics without making weather depend on the renderer or fire model.

## Decisions still open

Before numerical implementation, choose the target spatial and temporal scales, vertical representation, time integration method, and intended scenario class. The numerical method should follow those decisions. Avoid implying cloud-resolving or operational forecast fidelity unless the chosen equations, resolution, and validation support it.

## Validation expectations

Every process should document its inputs, outputs, units, assumptions, and valid range. Tests should cover conservation where applicable, positivity and boundedness, deterministic repeatability, limiting cases, and regression fixtures. Reference data must record its source and conditions.
