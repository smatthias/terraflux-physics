# Weather map input and interpretation

## Goal

Convert supported weather sources into georeferenced observations that the simulation can use without confusing an interpreted map value with a directly measured or simulated value.

## Input routes

1. **Structured grids:** machine-readable products with explicit coordinates, units, levels, and valid times.
2. **Rendered maps:** image products such as radar, satellite, parameter maps, and synoptic charts containing legends, contours, symbols, or color scales.

Keep format decoding, image interpretation, georeferencing, and weather physics in separate modules.

## Observation record

An interpreted observation should retain, where available:

- variable and value
- unit and vertical level or layer
- geographic location and coordinate reference system
- valid time and source identifier
- extraction method
- confidence or uncertainty
- missing, masked, or ambiguous status

## Interpretation steps

- Identify source type and available metadata.
- Recover projection and map bounds from metadata or known control points.
- Decode values using the source legend, symbols, contours, or structured field metadata.
- Reproject and align observations with the target simulation grid and time.
- Preserve the original source reference and interpretation diagnostics.

Unknown or ambiguous inputs must remain flagged as such. The interpreter should not turn an unlabeled color or uncertain contour into an exact physical value.

## Initial scope

Start with a small, named set of map products with known legends and georeferencing. General-purpose interpretation of arbitrary map images is out of scope until the supported products and required accuracy are defined.
