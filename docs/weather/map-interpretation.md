# Weather map input and interpretation

## Goal
Convert supported weather sources into georeferenced observations that the simulation can use without confusing an interpreted map value with a directly measured or simulated value.

## Input routes
1. **Structured grids:** machine-readable products with explicit coordinates, units, levels, and valid times.
2. **Rendered maps:** image products containing legends, contours, symbols, or color scales.

Keep format decoding, image interpretation, georeferencing, and weather physics in separate modules.

## First rendered-map target: radar reflectivity

Start with a radar reflectivity map whose palette or legend and georeferencing are known. Accept an image plus its source metadata, valid time, projection or map bounds, and legend/palette definition. The first output is a spatial field of reflectivity observations in dBZ, with no-data and confidence/provenance recorded per observation.

Radar reflectivity represents returned radar energy; it is not a direct surface rainfall-rate measurement. Keep map decoding independent of rainfall estimation. A separate estimator applies a documented empirical reflectivity-to-rainfall relationship and records the coefficients and limits with each result. See [NOAA's reflectivity overview](https://www.noaa.gov/jetstream/reflectivity), the [NWS default Z-R relation](https://www.weather.gov/tae/research-zrpaper), and the [NWS discussion of radar rainfall-estimation limits](https://www.weather.gov/mrx/radarrainfallestimates).

## Temporary structured-grid source: NOAA MRMS

Use NOAA MRMS `MergedReflectivityComposite` as the temporary live prototype source. It is delivered as GRIB2 and retains numerical grid values, so the ingestion path should decode the scientific field directly rather than render and re-read a color image. MRMS blends multiple radar and sensor inputs and has U.S. coverage; it is not a replacement for the planned Victoria/BOM product.

Keep HTTP acquisition, GRIB2 decoding, grid transformation, and physics separate. The download helper records the source URL and retrieval metadata but deliberately does not claim a valid time; read the product time from GRIB2 metadata. Keep NOAA attribution and modified-data provenance with outputs.

## Planned Victoria map source: BOM Melbourne radar

The first operational Victoria source candidate remains the [Bureau of Meteorology (BOM) Melbourne (Broadmeadows) 256 km radar product](https://www.bom.gov.au/products/IDR012.shtml). This is a product choice, not authorization to ingest or redistribute live BOM imagery.

The BOM radar guide documents timestamped PNG products using the pattern `IDRnnnx.T.yyyymmddhhmm.png` and matching coordinate sidecars named `IDRnnnx.map`; the timestamp is UTC. The public guide does not document the sidecar's internal syntax, so the current decoder does not parse it. Keep product ID, UTC valid time, source URL/feed, palette identity, and sidecar version with every decoded observation. Do not infer a palette or map transform from a screenshot when required metadata is missing.

Keep provider-specific retrieval and file naming in an adapter. The interpretation pipeline should accept documented field metadata, a coordinate transform, and timestamp package so other permitted sources can use the same canonical observation format.

## Source-use and fixture policy

Use NOAA MRMS for temporary U.S. prototype data according to NOAA's open data terms and attribution request. For BOM, resolve the applicable product licence before production ingestion, persistent storage, redistribution, or commercial use. Synthetic, clearly labelled palette fixtures remain the deterministic decoder inputs; do not commit real provider data without checking its redistribution terms.

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
