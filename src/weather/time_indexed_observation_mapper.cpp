#include "terraflux/weather/observation_grid.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace terraflux::weather {

std::vector<TimeIndexedTemperatureField>
map_air_temperature_observations_to_updates(
    const std::vector<WeatherObservation>& observations,
    const DryTransportConfig& config) {
    validate_dry_transport_config(config);
    if (observations.empty()) {
        return {};
    }

    struct ObservationGroup {
        std::string valid_time_utc;
        double epoch_seconds{0.0};
        std::vector<WeatherObservation> observations;
        std::vector<std::size_t> original_indices;
    };

    std::map<std::string, ObservationGroup> groups;
    for (std::size_t index = 0; index < observations.size(); ++index) {
        const WeatherObservation& observation = observations[index];
        if (observation.variable != "air_temperature" || observation.units != "K") {
            throw std::invalid_argument(
                "time-indexed mapping requires air_temperature observations in K");
        }
        const double epoch_seconds =
            detail::utc_timestamp_seconds(observation.valid_time_utc);
        auto [it, inserted] = groups.try_emplace(observation.valid_time_utc);
        if (inserted) {
            it->second.valid_time_utc = observation.valid_time_utc;
            it->second.epoch_seconds = epoch_seconds;
        }
        it->second.observations.push_back(observation);
        it->second.original_indices.push_back(index);
    }

    std::vector<ObservationGroup*> chronological_groups;
    chronological_groups.reserve(groups.size());
    for (auto& entry : groups) {
        chronological_groups.push_back(&entry.second);
    }
    std::sort(
        chronological_groups.begin(),
        chronological_groups.end(),
        [](const ObservationGroup* left, const ObservationGroup* right) {
            return left->epoch_seconds < right->epoch_seconds;
        });

    std::vector<TimeIndexedTemperatureField> updates;
    updates.reserve(chronological_groups.size());
    double previous_elapsed = -1.0;
    for (ObservationGroup* group : chronological_groups) {
        const double elapsed =
            group->epoch_seconds - detail::utc_timestamp_seconds(config.start_time_utc);
        if (!std::isfinite(elapsed)) {
            throw std::invalid_argument("observation time offset is not finite");
        }
        if (previous_elapsed >= 0.0 &&
            std::abs(elapsed - previous_elapsed) <= 1.0e-6) {
            throw std::invalid_argument(
                "different UTC timestamps resolve to the same observation time");
        }

        GriddedObservationField field = map_weather_observations_to_grid(
            group->observations, config, "air_temperature", "K");
        for (std::size_t& local_index : field.source_observation_indices) {
            if (local_index >= group->original_indices.size()) {
                throw std::logic_error(
                    "mapped observation source index is outside its time group");
            }
            local_index = group->original_indices[local_index];
        }
        updates.push_back({elapsed, std::move(field)});
        previous_elapsed = elapsed;
    }
    return updates;
}

}  // namespace terraflux::weather
