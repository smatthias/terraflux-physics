# First radar map decoder

The C++17 library target **terraflux_weather_map** reads the v1 JSON sidecar and decodes PPM P3 or PNG rasters into georeferenced weather observations.

## Current scope

- Exact RGBA palette lookup to reflectivity values in dBZ.
- PNG alpha is retained; sidecar palette alpha defaults to 255 when omitted.
- Explicit no-data and ambiguous palette states remain null-valued observations.
- Unrecognized raster colors become ambiguous with a reason; the decoder never guesses a value.
- Pixel-corner affine georeferencing is evaluated at each pixel center.
- Sidecar fields, image dimensions, PPM sample ranges, palette entries, and configurable input limits are checked.
- PNG is decoded to 8-bit RGBA. Inputs with more than 8 bits per channel are rejected to avoid silently changing palette colors.

## Sidecar and raster usage

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
~~~

The committed fixture remains synthetic PPM. PNG support uses libpng; CMake requires libpng 1.6.59 or later and nlohmann/json 3.12.0 or later.

~~~sh
cmake -S . -B build
cmake --build build
~~~

The reader does not fetch live BOM products, estimate rainfall rate, or run weather physics. PNG and PPM inputs are decoded locally; provider retrieval remains separate.
