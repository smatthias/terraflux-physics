# Weather input validation

Use known, small fixtures to verify decoding, projection, coordinate registration, legend or symbol interpretation, units, valid time, confidence, and source provenance. Include ambiguous and incomplete inputs and verify they remain flagged instead of becoming guessed values.

For the first radar-reflectivity fixture, verify palette-to-dBZ decoding, georeferencing, no-data handling, timestamp/product metadata, and that the reader emits reflectivity rather than silently converting it to rainfall rate.

The first image fixture is synthetic. Its sidecar follows [map-sidecar v1](../../docs/weather/observation-schema.json); each decoded cell is represented by [weather observation v1](../../docs/weather/observation-record-v1.schema.json). Check that no-data and ambiguous cells remain distinct and never acquire a guessed numeric value. Do not include live BOM imagery until applicable source-use terms are cleared.
