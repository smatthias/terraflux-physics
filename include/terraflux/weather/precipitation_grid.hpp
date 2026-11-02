#pragma once
#include "terraflux/weather/observation_grid.hpp"

namespace terraflux::weather {
// Maps accumulation observations into containing forecast cells. No resampling
// or averaging: empty cells are no_data; multiple samples are ambiguous.
// Requires a common source/derivation and the exact forecast interval.
GriddedObservationField map_precipitation_accumulation_to_grid(
    const std::vector<WeatherObservation>& observations,
    const DryTransportConfig& config,
    double forecast_elapsed_seconds);
}
