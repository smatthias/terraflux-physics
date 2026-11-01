# Weather validation

Organize checks into:

- unit tests for individual physical calculations
- invariant tests for conservation, positivity, and boundedness
- reference cases with documented source and inputs
- map-input tests for georeferencing, known legend values, symbols, missing metadata, confidence, and provenance
- regression scenarios for combined processes

Keep expected results and tolerances tied to the model assumptions and numerical resolution. Do not treat a visually plausible result as physical validation.
