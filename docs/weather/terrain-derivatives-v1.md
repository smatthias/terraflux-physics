# Terrain derivatives v1

`calculate_terrain_derivatives` computes cell-centered gradients in local east/north coordinates from an aligned metric elevation grid. Interior derivatives use central differences over two cell spacings; boundary derivatives use one-sided differences over one spacing. There is no periodic wrap. A grid requires at least two cells per dimension and finite elevations and positive metric spacings.

Slope is `atan(hypot(dz/deast, dz/dnorth))`, converted to degrees. Aspect is the downhill bearing `atan2(-dz/deast, -dz/dnorth)`, normalized clockwise from north to [0, 360). North is 0°, east 90°, south 180°, west 270°. Exactly zero gradient yields slope 0 and null aspect. Negative terrain elevation is permitted; nonfinite inputs and overflow are rejected.

The timeline may carry `terrain_derivatives` alongside `terrain`, with:

- `schema_version`: `terraflux.terrain.derivatives/v1`
- `method`: `central_interior_one_sided_edges`
- `slope_deg`: nx × ny finite angles from 0 through 90
- `aspect_deg`: nx × ny downhill bearings in [0, 360), or null exactly on flat cells

Arrays inherit the terrain grid, row order, source identity and vertical datum. They remain constant across all frames. The viewer uses a cyclic compass color scale for aspect; gray denotes undefined flat aspect. Geometry exaggeration never changes these physical slopes or bearings.

These finite differences describe resolved grid-scale terrain, not subgrid cliffs. They are diagnostic inputs for future terrain-driven flow and runoff; no runoff, wind deflection or surface exchange is implemented by this calculation.
