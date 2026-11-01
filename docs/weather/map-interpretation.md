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

## First reference source candidate: BOM Melbourne radar

Use the [Bureau of Meteorology (BOM) Melbourne (Broadmeadows) 256 km radar product](https://www.bom.gov.au/products/IDR012.shtml) as the first concrete source candidate for the Victoria wildfire-weather use case. This is a reference product choice, not yet authorization to ingest or redistribute live BOM imagery.

The BOM radar guide documents timestamped PNG products using the pattern `IDRnnnx.T.yyyymmddhhmm.png` and matching coordinate sidecars named `IDRnnnx.map`; the identifier includes the radar/product range code, and the timestamp is UTC. For Melbourne 256 km, the product page identifies the product as `IDR012`; use the matching `IDR012.map` sidecar. The public guide does not document the sidecar's internal syntax, so the current decoder does not parse it. Keep the radar product identifier, UTC valid time, source URL/feed, palette/legend identity, and sidecar version with every decoded observation. Do not infer a palette or map transform from a screenshot when required metadata is missing. See the [BOM Radar Images User Guide](https://www.bom.gov.au/weather/radar/about/radar_images_user_guide.shtml).

Keep the provider-specific retrieval and file naming in a BOM adapter. The interpretation pipeline should accept a documented image, palette, coordinate transform, and timestamp package so other licensed sources can use the same canonical observation format.

## Source-use and fixture policy

BOM copyright and data terms vary by product and use. Resolve the applicable licence and permissions before production ingestion, persistent storage, redistribution, or commercial use. Until then, use a synthetic, clearly labelled palette fixture in the repository; do not commit live BOM radar imagery. The [BOM copyright guidance](https://www.bom.gov.au/other/copyright.shtml) describes restrictions and licensing routes.

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
