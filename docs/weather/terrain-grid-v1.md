# Optional terrain grid v1

A weather timeline may contain a top-level `terrain` object, shared by every snapshot. Existing forecasts and mobile responses are unchanged. This is a visualization/input contract. An optional lapse-rate diagnostic uses it; the dry transport kernel does not consume elevation.

Required fields:

| Field | Meaning |
| --- | --- |
| `schema_version` | `terraflux.terrain.grid/v1` |
| `grid` | Exact complete forecast grid, including coordinate system, order and anchor |
| `units` | `m` |
| `vertical_reference` | Nonempty explicit datum identity |
| `source_id` | Nonempty terrain source identity |
| `values` | Exactly nx × ny finite elevations, row-major y then x |

Negative elevations are allowed. Missing or nonfinite elevations are rejected in v1; no missing elevation is silently converted to zero. A terrain grid with different geometry must be reprojected/resampled before attachment.

The bundled Gaussian hill uses `synthetic_local_datum`, not a real-world elevation datum. Its heights are generated deterministically by the C++ example generator. Terrain remains constant across the timeline.

In Three.js, **Place fields on terrain** uses relative terrain height above the minimum elevation as geometry; field values control color. Vertical exaggeration (1–5×) is a display control only. The terrain layer always uses terrain geometry regardless of the overlay toggle. Cell inspection reports original elevations and datum. The 2D fallback shows elevation colors without 3D geometry.

Terrain elevation can change the optional diagnostic temperature of the dry-transport examples. The coupled model consumes elevation when its `orography` section is configured (terrain-following adiabats, Froude blocking, valley pooling; see [orography v1](orography-model.md)), and `terraflux-run-coupled-wind` then writes this block itself. Surface fluxes and water routing over terrain remain separate. The catchment hydrology CLI writes the same block for the terrain it routes water over (prescribed values or the shared synthetic builder with tilt and roughness); its `hydrology` block, the river network and the lakes are defined on that elevation.
