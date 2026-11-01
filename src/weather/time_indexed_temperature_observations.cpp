#include "terraflux/weather/observation_grid.hpp"

#include <cmath>
#include <stdexcept>

namespace terraflux::weather {
namespace {

double time_tolerance(double seconds) {
    return 1.0e-6 * (seconds > 1.0 ? seconds : 1.0);
}

}  // namespace

DryTransportState advance_dry_transport_with_temperature_observations(
    const DryTransportState& initial,
    const DryTransportConfig& config,
    const std::vector<TimeIndexedTemperatureField>& observation_updates) {
    validate_dry_transport_config(config);
    validate_dry_transport_state(initial, config);
    if (initial.elapsed_seconds != 0.0) {
        throw std::invalid_argument(
            "time-indexed observation runs must start at elapsed time zero");
    }

    const long double total_duration =
        static_cast<long double>(config.timestep_seconds) *
        static_cast<long double>(config.steps);
    double previous_elapsed = -1.0;
    for (const auto& update : observation_updates) {
        if (!std::isfinite(update.elapsed_seconds) ||
            update.elapsed_seconds < 0.0 ||
            static_cast<long double>(update.elapsed_seconds) > total_duration) {
            throw std::invalid_argument(
                "temperature update time must fall within the configured simulation");
        }
        if (previous_elapsed >= 0.0 &&
            update.elapsed_seconds <= previous_elapsed +
                time_tolerance(previous_elapsed)) {
            throw std::invalid_argument(
                "temperature updates must be ordered and have unique times");
        }
        const double step_position =
            update.elapsed_seconds / config.timestep_seconds;
        if (!std::isfinite(step_position)) {
            throw std::invalid_argument("temperature update step position is not finite");
        }
        const double nearest_step = std::round(step_position);
        const double boundary_seconds = nearest_step * config.timestep_seconds;
        if (std::abs(update.elapsed_seconds - boundary_seconds) >
            time_tolerance(update.elapsed_seconds)) {
            throw std::invalid_argument(
                "temperature updates must align with a model timestep boundary");
        }
        previous_elapsed = update.elapsed_seconds;
    }

    DryTransportState state = initial;
    DryTransportConfig one_step_config = config;
    one_step_config.steps = 1;
    std::size_t next_update = 0;
    for (std::size_t step = 0; step <= config.steps; ++step) {
        const double boundary_seconds =
            static_cast<double>(step) * config.timestep_seconds;
        while (next_update < observation_updates.size() &&
               std::abs(observation_updates[next_update].elapsed_seconds -
                        boundary_seconds) <= time_tolerance(boundary_seconds)) {
            state = apply_air_temperature_observation_update(
                        state, observation_updates[next_update].field, config)
                        .state;
            ++next_update;
        }
        if (step < config.steps) {
            state = advance_dry_transport(state, one_step_config);
        }
    }
    if (next_update != observation_updates.size()) {
        throw std::invalid_argument(
            "temperature update does not coincide with a simulation timestep");
    }
    return state;
}

}  // namespace terraflux::weather
