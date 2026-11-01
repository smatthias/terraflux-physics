# Weather input implementation

Separate adapters for structured grids from interpreters for rendered maps. Convert both routes to the canonical observation representation, then apply georeferencing and time alignment.

Keep supported source formats and map classes explicit. Report missing metadata and ambiguous extraction results instead of silently guessing.
