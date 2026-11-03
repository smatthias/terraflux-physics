# Weather validation

Organize checks into:

- unit tests for individual physical calculations
- invariant tests for conservation, positivity, and boundedness
- reference cases with documented source and inputs
- map-input tests for georeferencing, known legend values, symbols, missing metadata, confidence, and provenance
- regression scenarios for combined processes

The dry scalar transport suite currently covers:

- constant-temperature preservation under divergence-free wind and diffusion
- one-step periodic upwind transport with expected cell values and conserved grid sum
- one-step diffusion with hand-computed expected cell values
- rejection of a timestep above the combined stability limit
- rejection of a discretely divergent wind field
- EPSG:4326 projection, observation/status preservation, and outside-domain counting
- duplicate-cell ambiguity with all source indices retained
- rejection of mapping radar reflectivity into an air-temperature field

The pressure-gradient wind suite (`weather.pressure_wind`, plus the CLI check `weather.pressure_wind_cli`) covers:

- exact down-gradient acceleration when the Coriolis parameter is zero
- a hand-computed trapezoidal Coriolis/friction step
- rightward deflection in the Northern Hemisphere and leftward in the Southern Hemisphere
- frictionless inertial oscillation about the geostrophic wind with conserved ageostrophic speed
- convergence to the friction-balanced wind between a high and a low, with cross-isobar angle `atan(k/f)`
- cyclonic flow around synthetic lows and anticyclonic flow around highs in both hemispheres
- acceptance of the periodic f-plane geostrophic wind by the dry transport divergence check
- the balanced wind as an exact fixed point of the stepper
- all Coriolis modes and rejection of unresolved timesteps and invalid inputs

Build and run the registered CTest case with:

~~~sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
~~~

Keep expected results and tolerances tied to the model assumptions and numerical resolution. Do not treat a visually plausible result as physical validation.
