#pragma once

#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <vector>

namespace terraflux::weather {

// Empirical Z-R power law: Z = coefficient * R^exponent.
// Z is in mm^6 m^-3; R is in mm h^-1.
struct ReflectivityRainfallRelation {
    double coefficient{300.0};
    double exponent{1.4};
};

// Converts radar-reflectivity observations from dBZ to estimated rain rate.
// Missing and ambiguous observations keep their quality status and location.
// The empirical relation is recorded in each output observation's derivation.
std::vector<WeatherObservation> estimate_rain_rate(
    const std::vector<WeatherObservation>& reflectivity,
    ReflectivityRainfallRelation relation = {});

}  // namespace terraflux::weather
