# Weather map input and interpretation

## Goal

Convert supported weather sources into georeferenced observations that the simulation can use without confusing an interpreted map value with a directly measured or simulated value.

## Input routes

1. **Structured grids:** machine-readable products with explicit coordinates, units, levels, and valid times.
2. **Rendered maps:** image products containing legends, contours, symbols, or color scales.

Keep format decoding, image interpretation, georeferencing, and weather physics in separate modules.

## First rendered-map target: radar reflectivity

Start with a radar reflectivity map whose palette or legend and georeferencing are known. Accept an image plus its source metadata, valid time, projection or map bounds, and legend/palette definition. The first output is a spatial field of reflectivity observations in dBZ, with no-data and confidence/provenance recorded per observation.

Radar reflectivity represents returned radar energy; it is not a direct surface rainfall-rate measurement. Do not convert dBZ to rain rate in this reader. A later precipitation-estimation module must choose and document an empirical reflectivity-to-rainfall relationship and its limits. See [NOAA's reflectivity overview](https://www.noaa.gov/jetstream/reflectivity) and the [NWS discussion of radar rainfall-estimation limits](https://www.weather.gov/mrx/radarrainfallestimates).

## Observation record

An interpreted observation should retain, where available:

- variable and value
- unit and vertical level or layer
- geographic location and coordinate reference system
- valid time and source identifier
- extraction method
- confidence or uncertainty
- missing, masked, or ambiguous status

For radar products, retain the product type and radar scan/elevation context when supplied.

## Interpretation steps

- Identify source type and available metadata.
- Recover projection and map bounds from metadata or known control points.
- Decode values using the source legend, symbols, contours, or structured field metadata.
- Reproject and align observations with the target simulation grid and time.
- Preserve the original source reference and interpretation diagnostics.

Unknown or ambiguous inputs must remain flagged as such. The interpreter should not turn an unlabeled color or uncertain contour into an exact physical value.

## Follow-on map classes

After the radar path is validated, consider synoptic charts for pressure contours, fronts, and wind symbols, then satellite imagery and other parameter maps. General-purpose interpretation of arbitrary map images is out of scope until supported products and required accuracy are defined.
