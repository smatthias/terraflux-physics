# Weather input validation

Use known, small fixtures to verify decoding, projection, coordinate registration, legend or symbol interpretation, units, valid time, confidence, and source provenance. Include ambiguous and incomplete inputs and verify they remain flagged instead of becoming guessed values.

For the first radar-reflectivity fixture, verify palette-to-dBZ decoding, georeferencing, no-data handling, timestamp/product metadata, and that the reader emits reflectivity rather than silently converting it to rainfall rate.
