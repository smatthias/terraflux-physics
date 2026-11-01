# Weather input validation

Use known, small fixtures to verify decoding, projection, coordinate registration, legend or symbol interpretation, units, valid time, confidence, and source provenance. Include ambiguous and incomplete inputs and verify they remain flagged instead of becoming guessed values.

For the first radar-reflectivity fixture, verify palette-to-dBZ decoding, georeferencing, no-data handling, timestamp/product metadata, and that the reader emits reflectivity rather than silently converting it to rainfall rate.

Keep the first image fixture synthetic. Its sidecar metadata should document the product identifier, UTC valid time, raster dimensions, coordinate transform or control points, palette/legend mapping, no-data color, and ambiguous colors. Do not include live BOM imagery until the applicable data-use terms are cleared.
