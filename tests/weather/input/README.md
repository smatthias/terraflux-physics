# Weather input validation

Use known, small fixtures to verify decoding, projection, coordinate registration, legend or symbol interpretation, units, valid time, confidence, and source provenance. Include ambiguous and incomplete inputs and verify they remain flagged instead of becoming guessed values.

The reader accepts PPM P3 and PNG rasters with a v1 JSON sidecar. Check exact palette-to-dBZ decoding, pixel-center georeferencing, no-data handling, and metadata/raster dimension agreement. For PNG, check indexed and RGBA colors, transparent palette entries, and rejection of unsupported bit depths.

The first image fixture remains synthetic. Its sidecar follows [map-sidecar v1](../../../docs/weather/observation-schema.json); each output cell follows [weather observation v1](../../../docs/weather/observation-record-v1.schema.json). Check that no-data and ambiguous cells remain distinct and never acquire a guessed numeric value. Do not include live BOM imagery until applicable source-use terms are cleared.
