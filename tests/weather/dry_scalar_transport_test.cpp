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
using terraflux::weather::map_air_temperature_observations_to_updates;
using terraflux::weather::overlay_air_temperature_observations;
using terraflux::weather::TransportGrid2D;
using terraflux::weather::TimeIndexedTemperatureField;
using terraflux::weather::advance_dry_transport_with_temperature_observations;
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


WeatherObservation make_temperature_observation(
    double longitude,
    ObservationStatus status,
    std::optional<double> value = std::nullopt,
    std::string reason = {}) {
    auto observation = make_observation(longitude, status, value, std::move(reason));
    observation.variable = "air_temperature";
    observation.units = "K";
    return observation;
}

DryTransportState make_temperature_background() {
    return make_state(
        {280.0, 281.0, 282.0, 283.0, 284.0, 285.0, 286.0, 287.0},
        std::vector<double>(8, 0.0),
        std::vector<double>(8, 0.0));
}

void temperature_overlay_applies_valid_values_and_keeps_background_for_gaps() {
    auto config = make_mapping_config();
    const auto background = make_temperature_background();
    const std::vector<WeatherObservation> observations{
        make_temperature_observation(0.0, ObservationStatus::decoded, 291.25),
        make_temperature_observation(-0.001, ObservationStatus::no_data),
        make_temperature_observation(
            0.001, ObservationStatus::ambiguous, std::nullopt,
            "uncertain source value"),
    };
    const auto field = map_weather_observations_to_grid(
        observations, config, "air_temperature", "K");
    const auto result = overlay_air_temperature_observations(
        background, field, config);

    require(result.observation_cells_applied == 1,
            "one decoded temperature cell should be applied");
    require(result.no_data_cells == 6 && result.ambiguous_cells == 1,
            "empty and explicit no-data cells plus ambiguous cell must be counted");
    require(result.background_cells_retained == 7,
            "background must remain for all no-data and ambiguous cells");
    require_near(result.state.air_temperature_k[6], 291.25,
                 "decoded observation must replace its background cell");
    require_near(result.state.air_temperature_k[4], 284.0,
                 "explicit no-data must retain its background value");
    require_near(result.state.air_temperature_k[7], 287.0,
                 "ambiguous value must retain its background value");
    require_near(result.state.air_temperature_k[0], 280.0,
                 "empty cell must retain its background value");
    require_near(background.air_temperature_k[6], 286.0,
                 "overlay must not mutate the input background state");
}

void temperature_overlay_rejects_duplicate_cell_collisions() {
    const auto config = make_mapping_config();
    const auto background = make_temperature_background();
    const std::vector<WeatherObservation> observations{
        make_temperature_observation(0.0, ObservationStatus::decoded, 290.0),
        make_temperature_observation(0.0001, ObservationStatus::decoded, 300.0),
    };
    const auto field = map_weather_observations_to_grid(
        observations, config, "air_temperature", "K");
    const auto result = overlay_air_temperature_observations(
        background, field, config);

    require(result.ambiguous_cells == 1 && result.background_cells_retained == 8,
            "colliding observations must be counted as ambiguous and retain background");
    require_near(result.state.air_temperature_k[6], 286.0,
                 "colliding values must not replace or average the background");
}

void temperature_overlay_rejects_variable_time_and_grid_mismatches() {
    const auto config = make_mapping_config();
    const auto background = make_temperature_background();
    const std::vector<WeatherObservation> observations{
        make_temperature_observation(0.0, ObservationStatus::decoded, 290.0),
    };
    const auto valid_field = map_weather_observations_to_grid(
        observations, config, "air_temperature", "K");
    const auto must_reject = [&](const terraflux::weather::GriddedObservationField& field,
                                 const std::string& case_name) {
        bool rejected = false;
        try {
            (void)overlay_air_temperature_observations(background, field, config);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, case_name + " must be rejected");
    };

    auto field = valid_field;
    field.variable = "radar_reflectivity";
    field.units = "dBZ";
    must_reject(field, "reflectivity field");
    field = valid_field;
    field.valid_time_utc = "2026-11-01T01:00:00Z";
    must_reject(field, "time mismatch");
    field = valid_field;
    field.grid.origin_east_m += 1.0;
    must_reject(field, "grid mismatch");
}


terraflux::weather::GriddedObservationField make_timed_temperature_field(
    const DryTransportConfig& config,
    const std::string& valid_time,
    ObservationStatus status,
    std::optional<double> value = std::nullopt,
    std::string reason = {}) {
    auto observation = make_temperature_observation(
        0.0, status, value, std::move(reason));
    observation.valid_time_utc = valid_time;
    return map_weather_observations_to_grid(
        {observation}, config, "air_temperature", "K");
}

void time_indexed_updates_apply_at_start_middle_and_end() {
    auto config = make_mapping_config();
    config.timestep_seconds = 60.0;
    config.steps = 2;
    const auto initial = make_temperature_background();

    const std::vector<TimeIndexedTemperatureField> updates{
        {0.0, make_timed_temperature_field(
                  config, "2026-11-01T00:00:00Z",
                  ObservationStatus::decoded, 290.0)},
        {60.0, make_timed_temperature_field(
                   config, "2026-11-01T00:01:00Z",
                   ObservationStatus::decoded, 295.0)},
        {120.0, make_timed_temperature_field(
                    config, "2026-11-01T00:02:00Z",
                    ObservationStatus::decoded, 300.0)},
    };

    const auto forecast =
        advance_dry_transport_with_temperature_observations(
            initial, config, updates);
    require_near(forecast.air_temperature_k[6], 300.0,
                 "final-boundary observation must set the returned forecast");
    require_near(forecast.air_temperature_k[4], 284.0,
                 "unobserved cells must continue from their background");
    require_near(forecast.elapsed_seconds, 120.0,
                 "timed updates must not change the configured run duration");
}

void time_indexed_no_data_retains_current_forecast_value() {
    auto config = make_mapping_config();
    config.timestep_seconds = 60.0;
    config.steps = 2;
    const auto initial = make_temperature_background();
    const std::vector<TimeIndexedTemperatureField> updates{
        {0.0, make_timed_temperature_field(
                  config, "2026-11-01T00:00:00Z",
                  ObservationStatus::decoded, 290.0)},
        {60.0, make_timed_temperature_field(
                   config, "2026-11-01T00:01:00Z",
                   ObservationStatus::no_data)},
    };

    const auto forecast =
        advance_dry_transport_with_temperature_observations(
            initial, config, updates);
    require_near(forecast.air_temperature_k[6], 290.0,
                 "no-data update must retain the current forecast value");
}

void time_indexed_updates_reject_bad_times_and_off_step_offsets() {
    auto config = make_mapping_config();
    config.timestep_seconds = 60.0;
    config.steps = 2;
    const auto initial = make_temperature_background();
    const auto must_reject = [&](const std::vector<TimeIndexedTemperatureField>& updates,
                                 const std::string& case_name) {
        bool rejected = false;
        try {
            (void)advance_dry_transport_with_temperature_observations(
                initial, config, updates);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, case_name + " must be rejected");
    };

    must_reject(
        {{0.0, make_timed_temperature_field(
                   config, "not-a-timeZ", ObservationStatus::decoded, 290.0)}},
        "invalid UTC timestamp");
    must_reject(
        {{30.0, make_timed_temperature_field(
                    config, "2026-11-01T00:00:30Z",
                    ObservationStatus::decoded, 290.0)}},
        "off-timestep update");
    must_reject(
        {{60.0, make_timed_temperature_field(
                    config, "2026-11-01T00:02:00Z",
                    ObservationStatus::decoded, 290.0)}},
        "timestamp that disagrees with the scheduled time");
}


void raw_temperature_observations_group_chronologically_and_keep_global_indices() {
    const auto config = make_mapping_config();
    auto later_first = make_temperature_observation(
        0.0, ObservationStatus::decoded, 295.0);
    later_first.valid_time_utc = "2026-11-01T00:01:00Z";
    auto earlier = make_temperature_observation(
        0.0, ObservationStatus::decoded, 290.0);
    earlier.valid_time_utc = "2026-11-01T00:00:00Z";
    auto later_second = make_temperature_observation(
        0.0001, ObservationStatus::decoded, 300.0);
    later_second.valid_time_utc = "2026-11-01T00:01:00Z";

    const std::vector<WeatherObservation> observations{
        later_first, earlier, later_second};
    const auto updates =
        map_air_temperature_observations_to_updates(observations, config);

    require(updates.size() == 2,
            "observations at the same valid time must share one update");
    require_near(updates[0].elapsed_seconds, 0.0,
                 "earlier valid time must be returned first");
    require(updates[0].field.valid_time_utc == "2026-11-01T00:00:00Z",
            "first field must retain its valid time");
    require_near(updates[1].elapsed_seconds, 60.0,
                 "later valid time must have the correct elapsed offset");
    require(updates[1].field.cells[6].status == ObservationStatus::ambiguous,
            "colliding readings in one time group must remain ambiguous");
    require(updates[0].field.source_observation_indices[
                updates[0].field.source_offsets[6]] == 1,
            "first group source index must refer to the original input");
    require(updates[1].field.source_offsets[7] -
                updates[1].field.source_offsets[6] == 2,
            "later group must preserve both colliding source indices");
    require(updates[1].field.source_observation_indices[
                updates[1].field.source_offsets[6]] == 0 &&
                updates[1].field.source_observation_indices[
                    updates[1].field.source_offsets[6] + 1] == 2,
            "later group indexes must refer to original input positions");
}

void raw_temperature_adapter_rejects_mixed_variable_units_and_bad_timestamp() {
    const auto config = make_mapping_config();
    const auto valid = make_temperature_observation(
        0.0, ObservationStatus::decoded, 290.0);
    const auto must_reject = [&](const std::vector<WeatherObservation>& observations,
                                 const std::string& case_name) {
        bool rejected = false;
        try {
            (void)map_air_temperature_observations_to_updates(
                observations, config);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, case_name + " must be rejected");
    };

    must_reject(
        {valid, make_observation(0.001, ObservationStatus::decoded, 30.0)},
        "mixed variables");
    auto wrong_units = valid;
    wrong_units.units = "C";
    must_reject({wrong_units}, "non-kelvin units");
    auto invalid_time = valid;
    invalid_time.valid_time_utc = "2026-02-30T00:00:00Z";
    must_reject({invalid_time}, "invalid calendar timestamp");
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
        temperature_overlay_applies_valid_values_and_keeps_background_for_gaps();
        temperature_overlay_rejects_duplicate_cell_collisions();
        temperature_overlay_rejects_variable_time_and_grid_mismatches();
        time_indexed_updates_apply_at_start_middle_and_end();
        time_indexed_no_data_retains_current_forecast_value();
        time_indexed_updates_reject_bad_times_and_off_step_offsets();
        raw_temperature_observations_group_chronologically_and_keep_global_indices();
        raw_temperature_adapter_rejects_mixed_variable_units_and_bad_timestamp();
    } catch (const std::exception& error) {
        std::cerr << "dry scalar transport test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "weather transport and observation-grid cases passed\n";
    return 0;
}
