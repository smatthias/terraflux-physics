# Weather implementation

Keep input interpretation separate from numerical physics.

- `input/` — structured-data readers, rendered-map interpretation, georeferencing, and normalization into observations
- dynamics
- thermodynamics
- moisture and phase changes
- terrain and surface exchange
- coupling adapters

Keep numerical kernels deterministic and free of I/O. Each solver step should make its time interval, boundary conditions, and numerical assumptions explicit. Map readers should preserve provenance and uncertainty and must not directly mutate simulated state.
