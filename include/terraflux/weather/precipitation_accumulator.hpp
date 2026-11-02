#pragma once

#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <vector>

namespace terraflux::weather {

// Integrates a chronological series of precipitation-rate observations at each
// exact CRS/coordinate location using the trapezoidal rule. The output is one
// accumulation record per location over [earliest, latest] input valid times.
std::vector<WeatherObservation> accumulate_rainfall(
    const std::vector<WeatherObservation>& rate_samples);

}  // namespace terraflux::weather
