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
4. Reproject, align in time, and map observations onto the simulation grid. The first mapper supports EPSG:4326 to local azimuthal-equidistant coordinates and preserves cell quality states.
5. Provide observations as initial conditions, boundary inputs, constraints, or validation data according to their type and quality.

If a legend, projection, timestamp, or feature is ambiguous, preserve that uncertainty or mark the value unknown. Do not silently invent a precise value.

## Build sequence

1. Agree the target scale, dimensions, state variables, units, coordinate frame, and boundary conditions.
2. Define the observation schema, metadata requirements, confidence, and provenance.
3. Select one structured weather dataset and one bounded rendered-map class for initial input support. The first rendered-map class is radar reflectivity with a known legend and georeferencing.
4. Implement map decoding and georeferencing with explicit uncertainty and test fixtures.
5. Define immutable simulation configuration and state contracts with validation. **Completed for the dry scalar transport v1 contract.**
6. Implement the smallest dry-air transport case and compare it with analytic expectations. **Kernel implemented; constant-field, periodic-advection, diffusion, and rejection cases pass.**
   - Pressure-gradient wind with Coriolis deflection and linear friction over a prescribed pressure field. **Kernel implemented; down-gradient, hemisphere deflection, inertial-oscillation, balanced-wind, circulation, and rejection cases pass.** See [pressure-gradient wind v1](pressure-wind-model.md).
   - Coupled wind / moist layer: temperature and specific humidity carried by the wind with configurable diffusion, density diagnosed from pressure, temperature and humidity and fed back into the wind. **Kernel implemented; density-feedback, transport, diffusion, smoothing, stability-rejection and supersaturation cases pass.** See [coupled wind model v1](coupled-wind-model.md) and [moist-air relations](moist-air-relations.md).
   - Vertical column: hydrostatic layers thinning with height, dry convective and saturation adjustment, lifted surface parcel with LCL, LFC, EL, CAPE and CIN as the basis for cloud and thunderstorm potential. **Kernel implemented; ISA profile, independent pseudo-adiabat integration, Bolton LCL, conservation and index behaviour cases pass.** One-way diagnostic over the coupled model's surface state. See [atmospheric column model v1](atmospheric-column-model.md).
   - Cloud formation: frontal analysis (gradient, advection, convergence lifting, overrunning wedges) and heated-parcel convection, separately and combined, with rule-of-thumb cloud genera and thunderstorm flags; synthetic mocks accepted in the documented format. **Kernel implemented; rule, front, lifting, heating-sequence, combined-release, overrunning and pinned regression cases pass.** See [cloud formation v1](cloud-formation-model.md).
   - Precipitation outcomes: genus → character table, condensation-based cell-mean rates, wet-bulb phase (rain/sleet/snow), WMO intensity, duration fractions and accumulation, lightning and updraft-sized hail with melting, virga and hazards; weather-map view with switchable layers and isobars. **Kernel implemented; reference, genus, phase, storm, virga and pinned regression cases pass.** See [precipitation outcomes v1](precipitation-outcomes-model.md).
   - Storm systems: tropical cyclones as tracked objects with Emanuel potential intensity, Emanuel–Nolan genesis, Holland wind–pressure, steering and beta drift, Fujiwhara induction and mergers, landfall with surge and Kaplan–DeMaria decay; per-cell storm fields and events. **Kernel implemented; reference, environment, intensification, landfall, rotation/merger, genesis and field cases pass.** See [storm systems v1](storm-systems-model.md).
   - Severe convection: capping inversion in the column (loaded-gun profile), Ekman low-level shear, thermal-wind deep shear, Bunkers storm motion, storm-relative helicity and the Significant Tornado Parameter. **Kernel implemented; STP, inversion, hemisphere-mirror and release cases pass.** See [severe convection v1](severe-convection-model.md).
   - Orography: terrain-following adiabatic ascent/descent with orographic precipitation (foehn), cross-barrier Froude blocking weighted by the angle of attack (orientation-dependent barriers, barrier jets), diurnal valley cold-air pooling, terrain surface pressure. **Kernel implemented; adiabatic-rate, foehn, orientation, cold-pool and regression cases pass.** See [orography v1](orography-model.md).
   - Catchment hydrology: priority-flood depression filling (lakes), D8 network with Strahler order, channels, basins and ocean/edge outlets; snow, Horton infiltration, soil, percolation, Darcy aquifer with baseflow and springs; kinematic overland and channel routing with bankfull overflow and lake spilling; flood depth × velocity classes, stream-power erosion/deposition with sediment yield, infinite-slope landslides with runout and the Caine intensity–duration warning. **Kernel implemented; network, lake, steady-rain budget, column-rate, groundwater, hazard, rejection and regression cases pass.** See [catchment hydrology v1](catchment-hydrology-model.md)
   - Ocean circulation: reduced-gravity C-grid layer with forward–backward Coriolis, upwind thickness/tracer transport, linear EOS density feedback, river/rain/evaporation sources with a surface freshwater content, deep-water formation with return upwelling (sponge or uniform), stream/plume/front diagnostics. **Kernel implemented; Stommel/Sverdrup, f-plane symmetry, exact dilution/evaporation, plume deflection by hemisphere, formation/freshwater-weakening, rejection and regression cases pass.** See [ocean circulation v1](ocean-circulation-model.md)
   - Coriolis direction and pressure-gradient acceleration reviewed against the exact solution, a hemisphere/direction matrix and textbook magnitudes. See [coriolis-verification.md](coriolis-verification.md).
7. Add thermodynamic and moisture processes one at a time, with invariants and reference cases.
8. Add terrain and surface exchange after the atmospheric baseline is stable.
9. Add adapters for fire physics without making weather depend on the renderer or fire model.

## First solver contract selected

The first solver contract is a 2D, single-layer dry scalar advection-diffusion model for air temperature in kelvin, transported by a prescribed static east/north wind field, with configurable horizontal mixing diffusivity and periodic boundaries. It uses a uniform local east/north grid in metres, a geographic anchor for the spherical azimuthal-equidistant map transform, and row-major cell arrays. This baseline does not evolve wind or pressure and excludes vertical motion, moisture, clouds, precipitation, radiation, and terrain. See [dry transport model v1](dry-transport-model.md).

The first dynamics contract is a single-layer linear momentum model: a prescribed static pressure field (per cell or Gaussian high/low centers) accelerates the air, the Coriolis force on an f-plane or beta-plane deflects it, and linear surface friction slows it. Its steady states are the geostrophic and friction-balanced winds, so flow follows the isobars rather than running straight from high to low pressure. Configuration is split into explicit time, grid, and atmosphere sections. It does not evolve pressure or advect momentum. See [pressure-gradient wind v1](pressure-wind-model.md).

The first thermodynamic/moisture coupling is the coupled wind / moist-layer contract: the wind model above plus advective upwind transport of temperature and specific humidity with separate configurable diffusivities (temperature, humidity, momentum), air density diagnosed from the ideal-gas relation for moist air and used in the pressure-gradient acceleration, and relative humidity as a diagnostic. Pressure stays prescribed; no phase change is modelled. See [coupled wind model v1](coupled-wind-model.md).

The first vertical structure is the atmospheric column contract: hydrostatic layers from each cell's surface state, convective adjustment as the ground-to-altitude exchange, and a surface-parcel ascent for cloud base, cloud top and convective energy. It is diagnostic (column reads the surface state, nothing feeds back) and has no surface fluxes, precipitation or microphysics. See [atmospheric column model v1](atmospheric-column-model.md).

The first cloud contract is cloud formation v1: fronts from the horizontal state and convection from the heated surface parcel, combined where frontal lifting releases convective energy, producing cloud genus, base, top, fraction and thunderstorm flags per cell. It is diagnostic (no microphysics, precipitation or feedback) and its output format is documented so that engine output and labelled synthetic mocks share one contract. See [cloud formation v1](cloud-formation-model.md).

Precipitation outcomes v1 closes the diagnostic chain from clouds to weather at the ground (type, phase, rate, accumulation, lightning, hail, hazards) with explicit formulas and thresholds; the viewer's weather-map view composes these with isobars, fronts and cloud layers. See [precipitation outcomes v1](precipitation-outcomes-model.md).

Storm systems v1 and severe convection v1 add tracked tropical cyclones (energy, genesis, interaction, landfall) and tornado potential (inversion, shear, helicity) as parametrised diagnostics on the coupled layer, with their events and fields on the weather map. See [storm systems v1](storm-systems-model.md) and [severe convection v1](severe-convection-model.md).

Orography v1 adds mountains and valleys to the coupled layer as a terrain-following parametrisation (adiabatic rates, Froude blocking, valley pooling), with the elevation shared through the terrain-grid contract. See [orography v1](orography-model.md).

Ocean circulation v1 is the ocean branch: a reduced-gravity upper layer in a basin, forced by wind stress (analytic or from a coupled-wind timeline), surface temperature and precipitation, fed by rivers (constant, events, or a catchment timeline's outflow), producing the streams, salinity, exchange points, formation sites and the overturning as an `ocean` timeline block. It runs on its own grid and time step (tens of kilometres, half-hours, years of spin-up) because gyres adjust over the Rossby-wave crossing time. See [ocean circulation v1](ocean-circulation-model.md).

Catchment hydrology v1 is the land-surface branch: it consumes a terrain grid (the same synthetic builder as the orography, or prescribed values) and precipitation (analytic events and/or a coupled-wind timeline) and produces the drainage network, the water stores and discharge, and the flood/erosion/landslide hazard layer as a separate `hydrology` timeline block. It runs on its own time step and grid because rivers and aquifers evolve over hours to days on kilometre cells, while the atmosphere runs minutes on tens of kilometres. See [catchment hydrology v1](catchment-hydrology-model.md).

The observation mapper grids EPSG:4326 fields on the configured local metric grid while keeping variable and quality state separate; radar reflectivity remains dBZ and is not converted into temperature. See [observation gridding](observation-grid.md). The configured grid spacing and timestep are scenario inputs. The implemented first-order upwind and centered-diffusion kernel checks its combined per-cell stability bound before advancing. This baseline is a controlled transport experiment, not a regional forecast model.

## Map-source decisions

The first radar reference candidate is BOM Melbourne (Broadmeadows) 256 km reflectivity (IDR012). Keep synthetic fixtures until the intended commercial use is covered by a BOM data licence. The public radar guide lists timestamped PNG files and matching .map coordinate files, but a licensed sample and the .map format are still needed for a provider adapter. See [live map-source status](map-source-status.md).

## Validation expectations

Every process should document its inputs, outputs, units, assumptions, and valid range. Validation should cover conservation where applicable, positivity and boundedness, deterministic repeatability, limiting cases, map georeferencing, known legend values, missing metadata, and regression fixtures. Reference data must record its source and conditions. Do not claim operational forecast fidelity without suitable equations, resolution, inputs, and validation.
