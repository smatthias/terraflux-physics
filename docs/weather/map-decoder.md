# First radar map decoder

The C++17 library target **terraflux_weather_map** decodes ASCII PPM P3 rasters into one georeferenced weather observation per pixel.

## Current scope

- Exact RGB palette lookup to reflectivity values in dBZ.
- Explicit no-data and ambiguous palette states remain null-valued observations.
- Unrecognized raster colors become ambiguous with a reason; the decoder never guesses a value.
- Pixel-corner affine georeferencing is evaluated at each pixel center.
- PPM dimensions, sample ranges, metadata dimensions, palette entries, and a configurable pixel limit are checked.

The first committed fixture is deliberately synthetic. The decoder accepts typed **RadarMapMetadata**; a JSON sidecar deserializer and PNG/BOM adapter are not included yet. Callers must load and validate the sidecar using the v1 schema before invoking the decoder.

## API shape

~~~cpp
#include "terraflux/weather/radar_reflectivity_decoder.hpp"

std::ifstream raster("examples/weather/maps/victoria-radar-synthetic.ppm");
terraflux::weather::RadarMapMetadata metadata = /* validated sidecar fields */;
auto observations =
    terraflux::weather::decode_reflectivity_ppm_p3(raster, metadata);
~~~

Build the library with CMake:

~~~sh
cmake -S . -B build
cmake --build build
~~~

This stage interprets reflectivity only. It does not estimate rainfall rate or run weather physics.
