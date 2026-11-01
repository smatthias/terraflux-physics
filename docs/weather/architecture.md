# Weather physics architecture

## Purpose

Build a deterministic weather model that can evolve atmospheric conditions over terrain and later provide environmental inputs to TerraFlux fire-spread models. It must also be able to ingest weather data and interpret rendered weather maps. The first implementation should be a transparent simulation model with documented limits, not a claim of operational forecasting skill.

## Proposed process boundaries

- **Observation input and interpretation:** decode structured weather datasets or rendered maps and produce georeferenced observations with provenance and confidence.
- **State and units:** atmospheric variables, grid geometry, time, and explicit SI-unit conventions.
- **Dynamics:** wind velocity, transport, pressure-gradient acceleration, and vertical motion.
- **Thermodynamics:** temperature, density, and heat exchange.
- **Moisture and phase:** water vapor, condensation, cloud water, and precipitation.
- **Terrain and surface exchange:** roughness, heat and moisture fluxes, and terrain-driven flow effects.
- **Coupling:** a narrow adapter that supplies wind, temperature, humidity, and precipitation to other TerraFlux models.

Map interpretation is a data-input process, not part of the physical solver. Keep observed or inferred values distinguishable from simulated values.

## Map interpretation pipeline

1. Read source metadata, valid time, units, and projection where available.
2. Decode structured grids directly; for rendered maps, identify the map class, legend, symbols, and geospatial control points.
3. Convert extracted features into a canonical observation representation with location, variable, value, units, confidence, and provenance.
4. Reproject, align in time, and map observations onto the simulation grid.
5. Provide observations as initial conditions, boundary inputs, constraints, or validation data according to their type and quality.

If a legend, projection, timestamp, or feature is ambiguous, preserve that uncertainty or mark the value unknown. Do not silently invent a precise value.

## Build sequence

1. Agree the target scale, dimensions, state variables, units, coordinate frame, and boundary conditions.
2. Define the observation schema, metadata requirements, confidence, and provenance.
3. Select one structured weather dataset and one bounded rendered-map class for initial input support. The first rendered-map class is radar reflectivity with a known legend and georeferencing.
4. Implement map decoding and georeferencing with explicit uncertainty and test fixtures.
5. Define immutable simulation configuration and state contracts with validation.
6. Implement the smallest dry-air transport case and compare it with analytic expectations.
7. Add thermodynamic and moisture processes one at a time, with invariants and reference cases.
8. Add terrain and surface exchange after the atmospheric baseline is stable.
9. Add adapters for fire physics without making weather depend on the renderer or fire model.

## Decisions still open

Choose the target spatial and temporal scales, vertical representation, time integration method, intended scenario class, and structured weather dataset. The first image reader is limited to radar reflectivity maps with a known palette/legend and recoverable map coordinates. Choose the source and map packaging format before implementation. Synoptic charts, satellite imagery, and other parameter maps can follow once this path is validated. Avoid implying cloud-resolving or operational forecast fidelity unless the equations, resolution, inputs, and validation support it.

## Validation expectations

Every process should document its inputs, outputs, units, assumptions, and valid range. Tests should cover conservation where applicable, positivity and boundedness, deterministic repeatability, limiting cases, map georeferencing, known legend values, missing metadata, and regression fixtures. Reference data must record its source and conditions.
