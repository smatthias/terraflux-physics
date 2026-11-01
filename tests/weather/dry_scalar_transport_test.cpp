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

}  // namespace

int main() {
    try {
        constant_temperature_is_preserved();
        periodic_upwind_transport_conserves_grid_sum();
        diffusion_matches_single_explicit_step();
        unstable_timestep_is_rejected();
        divergent_wind_is_rejected();
    } catch (const std::exception& error) {
        std::cerr << "dry scalar transport test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "dry scalar transport analytic cases passed\n";
    return 0;
}
