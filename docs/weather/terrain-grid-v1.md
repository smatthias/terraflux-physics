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

Terrain elevation can change the optional diagnostic temperature, but changes neither temperature transport nor prescribed wind. Terrain-driven flow, lapse-rate physics, surface fluxes and water routing remain future work.
