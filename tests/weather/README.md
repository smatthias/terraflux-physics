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

Build and run the registered CTest case with:

~~~sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
~~~

Keep expected results and tolerances tied to the model assumptions and numerical resolution. Do not treat a visually plausible result as physical validation.
