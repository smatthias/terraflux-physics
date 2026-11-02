#include "terraflux/weather/precipitation_grid.hpp"
#include "utc_time.hpp"
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {
GriddedObservationField map_precipitation_accumulation_to_grid(
    const std::vector<WeatherObservation>& observations,
    const DryTransportConfig& config,
    double forecast_elapsed_seconds) {
    validate_dry_transport_config(config);
    if (observations.empty() || !std::isfinite(forecast_elapsed_seconds) ||
        forecast_elapsed_seconds <= 0) {
        throw std::invalid_argument("nonempty accumulation and positive forecast duration required");
    }
    const auto& reference = observations.front();
    const double start = detail::utc_seconds(config.start_time_utc);
    for (const auto& observation : observations) {
        if (observation.variable != "precipitation_accumulation" ||
            observation.units != "mm" ||
            observation.interpretation != "derived_temporal_integration") {
            throw std::invalid_argument("expected temporally integrated precipitation accumulation in mm");
        }
        if (detail::utc_seconds(observation.period_start_utc) != start ||
            std::abs((detail::utc_seconds(observation.valid_time_utc) - start) -
                     forecast_elapsed_seconds) > 1e-6) {
            throw std::invalid_argument("accumulation interval must match forecast start and duration");
        }
        if (observation.source_id.empty() ||
            observation.source_id != reference.source_id ||
            observation.source_kind != reference.source_kind ||
            observation.product_id != reference.product_id ||
            observation.derivation != reference.derivation ||
            observation.vertical_reference != reference.vertical_reference) {
            throw std::invalid_argument("accumulation source and derivation must match");
        }
        if (observation.value && *observation.value < 0) {
            throw std::invalid_argument("accumulation cannot be negative");
        }
    }
    return map_weather_observations_to_grid(
        observations, config, "precipitation_accumulation", "mm");
}
}
