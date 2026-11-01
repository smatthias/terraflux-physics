# Weather implementation

Organize implementation by physical process, not by visualization layer:

- dynamics
- thermodynamics
- moisture and phase changes
- terrain and surface exchange
- coupling adapters

Keep kernels deterministic and free of I/O. Each solver step should make its time interval, boundary conditions, and numerical assumptions explicit. The exact file and namespace layout can be set when the first process is specified.
