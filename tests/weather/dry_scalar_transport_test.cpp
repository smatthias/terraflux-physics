#include "terraflux/weather/observation_grid.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {

using terraflux::weather::DryTransportConfig;
using terraflux::weather::DryTransportState;
using terraflux::weather::ObservationStatus;
using terraflux::weather::WeatherObservation;
using terraflux::weather::map_weather_observations_to_grid;
using terraflux::weather::TransportGrid2D;
using terraflux::weather::advance_dry_transport;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, const std::string& message) {
    if (std::abs(actual - expected) > 1.0e-12) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

DryTransportConfig make_config(
    double timestep,
    std::size_t steps,
    double diffusivity = 0.0) {
    DryTransportConfig config;
    config.start_time_utc = "2026-11-01T00:00:00Z";
    config.timestep_seconds = timestep;
    config.steps = steps;
    config.horizontal_mixing_diffusivity_m2_s = diffusivity;
    config.grid = TransportGrid2D{4, 2, 0.0, 0.0, 1.0, 1.0};
    return config;
}

DryTransportState make_state(
    std::vector<double> temperature,
    std::vector<double> east_wind,
    std::vector<double> north_wind) {
    DryTransportState state;
    state.air_temperature_k = std::move(temperature);
    state.wind_east_m_s = std::move(east_wind);
    state.wind_north_m_s = std::move(north_wind);
    return state;
}

void constant_temperature_is_preserved() {
    const auto config = make_config(0.1, 3, 0.05);
    const auto state = make_state(
        std::vector<double>(8, 290.0),
        std::vector<double>(8, 1.5),
        std::vector<double>(8, -0.2));
    const auto result = advance_dry_transport(state, config);
    for (double temperature : result.air_temperature_k) {
        require_near(temperature, 290.0, "constant temperature must remain constant");
    }
    require_near(result.elapsed_seconds, 0.3, "elapsed time must advance by all steps");
}

void periodic_upwind_transport_conserves_grid_sum() {
    const auto config = make_config(0.5, 1);
    const auto state = make_state(
        {4.0, 3.0, 2.0, 1.0, 4.0, 3.0, 2.0, 1.0},
        std::vector<double>(8, 1.0),
        std::vector<double>(8, 0.0));
    const auto result = advance_dry_transport(state, config);
    const std::vector<double> expected{2.5, 3.5, 2.5, 1.5, 2.5, 3.5, 2.5, 1.5};
    double before = 0.0;
    double after = 0.0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        before += state.air_temperature_k[i];
        after += result.air_temperature_k[i];
        require_near(result.air_temperature_k[i], expected[i], "periodic upwind value");
    }
    require_near(after, before, "periodic transport must conserve the grid sum");
}

void diffusion_matches_single_explicit_step() {
    const auto config = make_config(0.1, 1, 0.1);
    const auto state = make_state(
        {4.0, 2.0, 2.0, 2.0, 4.0, 2.0, 2.0, 2.0},
        std::vector<double>(8, 0.0),
        std::vector<double>(8, 0.0));
    const auto result = advance_dry_transport(state, config);
    const std::vector<double> expected{3.96, 2.02, 2.0, 2.02, 3.96, 2.02, 2.0, 2.02};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require_near(result.air_temperature_k[i], expected[i], "diffusion one-step value");
    }
}

void unstable_timestep_is_rejected() {
    const auto config = make_config(1.01, 1);
    const auto state = make_state(
        std::vector<double>(8, 290.0),
        std::vector<double>(8, 1.0),
        std::vector<double>(8, 0.0));
    bool rejected = false;
    try {
        (void)advance_dry_transport(state, config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "solver must reject a timestep above its stability limit");
}

void divergent_wind_is_rejected() {
    const auto config = make_config(0.1, 1);
    const auto state = make_state(
        std::vector<double>(8, 290.0),
        {0.0, 1.0, 2.0, 3.0, 0.0, 1.0, 2.0, 3.0},
        std::vector<double>(8, 0.0));
    bool rejected = false;
    try {
        (void)advance_dry_transport(state, config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "solver must reject a discretely divergent wind field");
}


WeatherObservation make_observation(
    double longitude,
    ObservationStatus status,
    std::optional<double> value = std::nullopt,
    std::string reason = {}) {
    WeatherObservation observation;
    observation.source_id = "synthetic-grid-test";
    observation.source_kind = "synthetic_fixture";
    observation.valid_time_utc = "2026-11-01T00:00:00Z";
    observation.variable = "radar_reflectivity";
    observation.value = value;
    observation.units = "dBZ";
    observation.location = {"EPSG:4326", longitude, 0.0};
    observation.vertical_reference = "synthetic test level";
    observation.status = status;
    observation.reason = std::move(reason);
    return observation;
}

DryTransportConfig make_mapping_config() {
    auto config = make_config(0.1, 1);
    config.grid.origin_east_m = -200.0;
    config.grid.origin_north_m = -100.0;
    config.grid.spacing_east_m = 100.0;
    config.grid.spacing_north_m = 100.0;
    config.grid.geographic_anchor_longitude_deg = 0.0;
    config.grid.geographic_anchor_latitude_deg = 0.0;
    return config;
}

void observations_project_and_preserve_quality_on_grid() {
    const auto config = make_mapping_config();
    const std::vector<WeatherObservation> observations{
        make_observation(0.0, ObservationStatus::decoded, 30.0),
        make_observation(-0.001, ObservationStatus::no_data),
        make_observation(0.001, ObservationStatus::ambiguous, std::nullopt,
                         "pixel color has no verified legend value"),
        make_observation(0.01, ObservationStatus::decoded, 45.0),
    };
    const auto field = map_weather_observations_to_grid(
        observations, config, "radar_reflectivity", "dBZ");

    require(field.valid_time_utc == "2026-11-01T00:00:00Z",
            "mapped field must retain its valid time");
    require(field.outside_domain_count == 1,
            "one observation should be counted outside the grid");
    require(field.cells.size() == 8, "mapped field must match grid dimensions");
    require(field.cells[6].status == ObservationStatus::decoded,
            "anchor point should map to its decoded cell");
    require_near(*field.cells[6].value, 30.0,
                 "decoded observation value must be retained");
    require(field.cells[0].status == ObservationStatus::no_data,
            "explicit no-data status must be retained");
    require(!field.cells[0].value, "no-data cells must remain nonnumeric");
    require(field.cells[7].status == ObservationStatus::ambiguous,
            "ambiguous status must be retained");
    require(!field.cells[7].value, "ambiguous cells must remain nonnumeric");
    require(field.cells[5].status == ObservationStatus::no_data &&
                field.cells[5].observation_count == 0,
            "empty cells must remain marked as missing");
    require(field.source_offsets[7] - field.source_offsets[6] == 1 &&
                field.source_observation_indices[field.source_offsets[6]] == 0,
            "mapped cell must retain its source observation index");
}

void duplicate_observations_are_ambiguous_not_averaged() {
    const auto config = make_mapping_config();
    const std::vector<WeatherObservation> observations{
        make_observation(0.0, ObservationStatus::decoded, 20.0),
        make_observation(0.0001, ObservationStatus::decoded, 40.0),
    };
    const auto field = map_weather_observations_to_grid(
        observations, config, "radar_reflectivity", "dBZ");
    const auto& cell = field.cells[6];
    require(cell.status == ObservationStatus::ambiguous,
            "colliding observations must become ambiguous");
    require(!cell.value && cell.observation_count == 2,
            "colliding values must not be averaged or selected");
    require(field.source_offsets[7] - field.source_offsets[6] == 2,
            "all duplicate source indices must be retained");
}

void radar_reflectivity_is_not_accepted_as_temperature() {
    const auto config = make_mapping_config();
    const std::vector<WeatherObservation> observations{
        make_observation(0.0, ObservationStatus::decoded, 30.0),
    };
    bool rejected = false;
    try {
        (void)map_weather_observations_to_grid(
            observations, config, "air_temperature", "K");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "reflectivity must not be mapped as air temperature");
}

}  // namespace

int main() {
    try {
        constant_temperature_is_preserved();
        periodic_upwind_transport_conserves_grid_sum();
        diffusion_matches_single_explicit_step();
        unstable_timestep_is_rejected();
        divergent_wind_is_rejected();
        observations_project_and_preserve_quality_on_grid();
        duplicate_observations_are_ambiguous_not_averaged();
        radar_reflectivity_is_not_accepted_as_temperature();
    } catch (const std::exception& error) {
        std::cerr << "dry scalar transport test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "weather transport and observation-grid cases passed\n";
    return 0;
}
