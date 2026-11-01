# First radar map decoder

The C++17 library target **terraflux_weather_map** reads the v1 JSON sidecar, decodes PPM P3 or PNG rasters, and writes georeferenced observations in canonical v1 JSON form.

## Current scope

- Exact RGBA palette lookup to reflectivity values in dBZ.
- PNG alpha is retained; sidecar palette alpha defaults to 255 when omitted.
- Explicit no-data and ambiguous palette states remain null-valued observations.
- Unrecognized raster colors become ambiguous with a reason; the decoder never guesses a value.
- Pixel-corner affine georeferencing is evaluated at each pixel center, including rotated or sheared grids.
- Sidecar fields, image dimensions, PPM sample ranges, palette entries, and configurable input limits are checked.
- PNG is decoded to 8-bit RGBA. Inputs with more than 8 bits per channel are rejected to avoid silently changing palette colors.
- Observation output follows the canonical v1 record shape, retaining source, valid time, units, coordinates, vertical reference, quality state, and optional confidence.

## Sidecar, raster, and output usage

The sidecar loader type-checks the required v1 fields and returns typed metadata. It is not a general JSON Schema validation engine; applications can validate the document against the checked-in schema before loading it.

~~~cpp
#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <fstream>

std::ifstream sidecar("examples/weather/maps/victoria-radar-synthetic.json");
auto metadata = terraflux::weather::read_radar_map_sidecar_json(
    sidecar, "victoria-radar-synthetic.json");

std::ifstream raster("examples/weather/maps/victoria-radar-synthetic.ppm",
                     std::ios::binary);
auto observations =
    terraflux::weather::decode_reflectivity_map(raster, metadata);

std::ofstream output("observations.json");
terraflux::weather::write_weather_observations_json(output, observations);
~~~

The writer emits a JSON array; each element follows [weather observation v1](observation-record-v1.schema.json). It rejects invalid status/value combinations, unsupported source kinds, non-finite coordinates or values, and confidence outside 0..1.

The committed fixtures are synthetic. The axis-aligned and rotated-grid sidecars both use the small PPM palette raster; the rotated-grid fixture has nonzero cross-axis coefficients. PNG support uses libpng; CMake requires libpng 1.6.59 or later and nlohmann/json 3.12.0 or later.

~~~sh
cmake -S . -B build
cmake --build build
~~~

The reader does not fetch live BOM products, estimate rainfall rate, or run weather physics. PNG and PPM inputs are decoded locally; provider retrieval remains separate.

## Native BOM coordinate files

The BOM Radar Images User Guide identifies the coordinate files by the `IDRnnnx.map` naming pattern and explains that the final digit selects a radar-image radius. The public guide does not describe the file contents or coordinate convention. The decoder therefore accepts the documented, normalized JSON sidecar; it does not claim to parse BOM `.map` files. Add that adapter only after a real sample or a format specification is available, and keep the synthetic fixtures separate from BOM source files.
