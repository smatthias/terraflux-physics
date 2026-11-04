#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/pressure_wind.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, const std::string& message, double tolerance = 1.0e-12) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
}

TransportGrid2D make_grid(std::size_t nx, std::size_t ny, double spacing, double latitude = 45.0) {
    TransportGrid2D grid;
    grid.nx = nx;
    grid.ny = ny;
    grid.spacing_east_m = spacing;
    grid.spacing_north_m = spacing;
    grid.origin_east_m = -0.5 * static_cast<double>(nx) * spacing;
    grid.origin_north_m = -0.5 * static_cast<double>(ny) * spacing;
    grid.geographic_anchor_longitude_deg = 145.0;
    grid.geographic_anchor_latitude_deg = latitude;
    return grid;
}

CoupledWindConfig make_config(
    const TransportGrid2D& grid, double timestep, std::size_t steps, const std::string& coriolis_mode,
    double friction = 0.0, double prescribed_f = 0.0) {
    CoupledWindConfig config;
    config.time.start_time_utc = "2026-11-01T00:00:00Z";
    config.time.timestep_seconds = timestep;
    config.time.steps = steps;
    config.grid = grid;
    config.atmosphere.air_density_kg_m3 = 1.2;
    config.atmosphere.surface_friction_coefficient_s = friction;
    config.atmosphere.coriolis_mode = coriolis_mode;
    config.atmosphere.coriolis_parameter_s = prescribed_f;
    return config;
}

std::vector<double> linear_pressure(const TransportGrid2D& grid, double gradient_east, double gradient_north) {
    std::vector<double> pressure(grid.nx * grid.ny);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const double east = grid.origin_east_m + (x + 0.5) * grid.spacing_east_m;
            const double north = grid.origin_north_m + (y + 0.5) * grid.spacing_north_m;
            pressure[at(x, y, grid.nx)] = 100000.0 + gradient_east * east + gradient_north * north;
        }
    }
    return pressure;
}

CoupledWindState rest_state(std::vector<double> pressure, double temperature = 288.0, double humidity = 0.0) {
    CoupledWindState state;
    state.pressure_pa = std::move(pressure);
    const std::size_t count = state.pressure_pa.size();
    state.wind_east_m_s.assign(count, 0.0);
    state.wind_north_m_s.assign(count, 0.0);
    state.air_temperature_k.assign(count, temperature);
    state.specific_humidity_kg_kg.assign(count, humidity);
    return state;
}

// 1. With constant density and no diffusion the coupled wind equals the
//    standalone pressure-wind model, and uniform scalars stay uniform.
void constant_density_reproduces_pressure_wind_and_keeps_uniform_scalars() {
    const auto grid = make_grid(6, 5, 20000.0);
    auto config = make_config(grid, 600.0, 40, "f_plane", 2.0e-5);
    config.density_mode = "constant";
    const auto pressure = linear_pressure(grid, -2.0e-3, 1.0e-3);
    const auto result = advance_coupled_wind(rest_state(pressure, 285.0, 0.006), config);

    PressureWindConfig reference;
    reference.time = config.time;
    reference.grid = grid;
    reference.atmosphere = config.atmosphere;
    PressureWindState reference_state;
    reference_state.pressure_pa = pressure;
    reference_state.wind_east_m_s.assign(pressure.size(), 0.0);
    reference_state.wind_north_m_s.assign(pressure.size(), 0.0);
    const auto expected = advance_pressure_wind(reference_state, reference);
    for (std::size_t i = 0; i < pressure.size(); ++i) {
        require_near(result.wind_east_m_s[i], expected.wind_east_m_s[i], "coupled u equals pressure-wind u", 1.0e-9);
        require_near(result.wind_north_m_s[i], expected.wind_north_m_s[i], "coupled v equals pressure-wind v", 1.0e-9);
        require_near(result.air_temperature_k[i], 285.0, "uniform temperature stays uniform");
        require_near(result.specific_humidity_kg_kg[i], 0.006, "uniform humidity stays uniform");
    }
    require_near(result.elapsed_seconds, 24000.0, "elapsed time advances by all steps");
}

// 2. Density feedback: for the same pressure gradient, warm or moist (light)
//    air accelerates faster than cold or dry (dense) air. The first step has
//    no Coriolis or friction, so u1 = -(1/rho) dp/dx dt exactly.
void lighter_air_accelerates_faster_under_the_same_pressure_gradient() {
    const auto grid = make_grid(4, 2, 1000.0);
    const auto config = make_config(grid, 60.0, 1, "none");
    auto state = rest_state(linear_pressure(grid, 1.0e-3, 0.0));
    state.air_temperature_k = {270.0, 300.0, 285.0, 285.0, 270.0, 300.0, 285.0, 285.0};
    state.specific_humidity_kg_kg = {0.0, 0.0, 0.0, 0.015, 0.0, 0.0, 0.0, 0.015};
    const auto result = advance_coupled_wind(state, config);
    for (std::size_t i = 0; i < 8; ++i) {
        const double rho = moist_air_density_kg_m3(
            state.pressure_pa[i], state.air_temperature_k[i], state.specific_humidity_kg_kg[i]);
        require_near(result.wind_east_m_s[i], -1.0e-3 / rho * 60.0,
                     "first-step wind follows the diagnosed density", 1.0e-12);
    }
    require(std::abs(result.wind_east_m_s[1]) > std::abs(result.wind_east_m_s[0]),
            "warm air (300 K) must accelerate faster than cold air (270 K)");
    require(std::abs(result.wind_east_m_s[3]) > std::abs(result.wind_east_m_s[2]),
            "moist air (15 g/kg) must accelerate faster than dry air at the same temperature");
    require_near(std::abs(result.wind_east_m_s[1]) / std::abs(result.wind_east_m_s[0]),
                 300.0 / 270.0 * state.pressure_pa[0] / state.pressure_pa[1],
                 "dry acceleration ratio equals the inverse density ratio", 1.0e-12);
}

// 3. Upwind transport by a steady wind: uniform pressure keeps the initial
//    wind, and one periodic step reproduces the hand-computed values.
void upwind_transport_matches_hand_computed_step() {
    const auto grid = make_grid(4, 2, 1.0);
    auto config = make_config(grid, 0.5, 1, "none");
    config.scalar_boundary = "periodic";
    config.density_mode = "constant";
    auto state = rest_state(std::vector<double>(8, 100000.0));
    state.wind_east_m_s.assign(8, 1.0);
    state.air_temperature_k = {4.0, 3.0, 2.0, 1.0, 4.0, 3.0, 2.0, 1.0};
    state.specific_humidity_kg_kg = {0.004, 0.003, 0.002, 0.001, 0.004, 0.003, 0.002, 0.001};
    const auto result = advance_coupled_wind(state, config);
    const std::vector<double> expected{2.5, 3.5, 2.5, 1.5, 2.5, 3.5, 2.5, 1.5};
    for (std::size_t i = 0; i < 8; ++i) {
        require_near(result.wind_east_m_s[i], 1.0, "uniform pressure keeps the wind");
        require_near(result.air_temperature_k[i], expected[i], "periodic upwind temperature");
        require_near(result.specific_humidity_kg_kg[i], expected[i] / 1000.0, "periodic upwind humidity");
    }
}

// 4. Configurable diffusion: a hand-computed explicit step with zero-gradient
//    edges conserves the grid sum (zero flux), and periodic edges match the
//    transport model's reference values.
void diffusion_step_matches_hand_computed_values_for_both_boundaries() {
    const auto grid = make_grid(4, 2, 1.0);
    auto config = make_config(grid, 0.1, 1, "none");
    config.diffusion.temperature_m2_s = 0.1;
    config.diffusion.humidity_m2_s = 0.1;
    auto state = rest_state(std::vector<double>(8, 100000.0));
    state.air_temperature_k = {4.0, 2.0, 2.0, 2.0, 4.0, 2.0, 2.0, 2.0};
    state.specific_humidity_kg_kg = {0.004, 0.002, 0.002, 0.002, 0.004, 0.002, 0.002, 0.002};

    config.scalar_boundary = "zero_gradient";
    auto result = advance_coupled_wind(state, config);
    const std::vector<double> expected_closed{3.98, 2.02, 2.0, 2.0, 3.98, 2.02, 2.0, 2.0};
    for (std::size_t i = 0; i < 8; ++i) {
        require_near(result.air_temperature_k[i], expected_closed[i], "zero-gradient diffusion value");
        require_near(result.specific_humidity_kg_kg[i], expected_closed[i] / 1000.0, "zero-gradient humidity value");
    }
    require_near(std::accumulate(result.air_temperature_k.begin(), result.air_temperature_k.end(), 0.0), 20.0,
                 "zero-gradient diffusion conserves the grid sum");

    config.scalar_boundary = "periodic";
    result = advance_coupled_wind(state, config);
    const std::vector<double> expected_periodic{3.96, 2.02, 2.0, 2.02, 3.96, 2.02, 2.0, 2.02};
    for (std::size_t i = 0; i < 8; ++i) {
        require_near(result.air_temperature_k[i], expected_periodic[i], "periodic diffusion value");
    }
}

// 5. Between a cool, dry high and a warm, moist low, increasing the
//    configured diffusivities smooths the temperature, humidity and wind
//    fields, and the diagnostics stay physically bounded.
void diffusion_smooths_fields_between_high_and_low() {
    const auto grid = make_grid(24, 18, 40000.0, -37.8);
    std::vector<GaussianAnomaly> high{{-300000.0, -40000.0, 350000.0, 0.0}};
    std::vector<GaussianAnomaly> low{{260000.0, 100000.0, 300000.0, 0.0}};
    const auto field = [&](double background, double high_amplitude, double low_amplitude) {
        high[0].amplitude = high_amplitude;
        low[0].amplitude = low_amplitude;
        return build_gaussian_field(background, {high[0], low[0]}, grid);
    };
    auto state = rest_state(field(101500.0, 800.0, -900.0));
    state.air_temperature_k = field(287.0, -4.0, 5.0);
    state.specific_humidity_kg_kg = field(0.007, -0.003, 0.004);

    const auto spread = [](const std::vector<double>& values) {
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        return *hi - *lo;
    };
    const auto roughness = [&](const std::vector<double>& values) {
        double total = 0.0;
        for (std::size_t y = 0; y < grid.ny; ++y) {
            for (std::size_t x = 0; x + 1 < grid.nx; ++x) {
                total += std::abs(values[at(x + 1, y, grid.nx)] - values[at(x, y, grid.nx)]);
            }
        }
        return total;
    };

    // 300 s keeps the spin-up overshoot (up to twice geostrophic) inside the
    // 40 km CFL bound; 600 s is rejected at the overshoot peak.
    auto config = make_config(grid, 300.0, 288, "beta_plane", 2.0e-5);
    const auto plain = advance_coupled_wind(state, config);
    config.diffusion.temperature_m2_s = 20000.0;
    config.diffusion.humidity_m2_s = 20000.0;
    config.diffusion.momentum_m2_s = 20000.0;
    const auto smoothed = advance_coupled_wind(state, config);

    require(spread(smoothed.air_temperature_k) < spread(plain.air_temperature_k),
            "temperature diffusion must reduce the temperature contrast");
    require(spread(smoothed.specific_humidity_kg_kg) < spread(plain.specific_humidity_kg_kg),
            "humidity diffusion must reduce the humidity contrast");
    require(roughness(smoothed.wind_east_m_s) < roughness(plain.wind_east_m_s),
            "momentum diffusion must smooth the wind field");
    require(spread(plain.air_temperature_k) <= spread(state.air_temperature_k) + 1.0e-9 &&
                spread(plain.specific_humidity_kg_kg) <= spread(state.specific_humidity_kg_kg) + 1.0e-12,
            "upwind transport must not create new extrema");

    const auto diagnostics = diagnose_moist_layer(plain, config);
    for (std::size_t i = 0; i < diagnostics.air_density_kg_m3.size(); ++i) {
        require(diagnostics.air_density_kg_m3[i] > 1.1 && diagnostics.air_density_kg_m3[i] < 1.3,
                "diagnosed density must stay near sea-level values");
        require(diagnostics.relative_humidity_fraction[i] > 0.0 &&
                    diagnostics.relative_humidity_fraction[i] < 1.0,
                "relative humidity must remain between 0 and saturation in this scenario");
        require(diagnostics.virtual_temperature_k[i] > plain.air_temperature_k[i],
                "virtual temperature exceeds temperature for moist air");
    }
    require(diagnostics.supersaturated_cells == 0, "no supersaturation expected");
    double max_speed = 0.0;
    for (std::size_t i = 0; i < plain.wind_east_m_s.size(); ++i) {
        max_speed = std::max(max_speed, std::hypot(plain.wind_east_m_s[i], plain.wind_north_m_s[i]));
    }
    require(max_speed > 10.0 && max_speed < 45.0, "synoptic wind speeds expected");
}

// 6. The scalar stability bound is checked against the evolving wind, so an
//    accelerating flow is rejected at the step where it breaks the bound.
void unresolved_scalar_transport_is_rejected_mid_run() {
    const auto grid = make_grid(4, 2, 10.0);
    auto config = make_config(grid, 1.0, 10, "none");
    config.density_mode = "constant";
    const auto state = rest_state(linear_pressure(grid, -1.0, 0.0));
    // a = 1/1.2 m/s^2: after 10 s the wind is 8.3 m/s, number 0.83; after 13 s it exceeds 1.
    (void)advance_coupled_wind(state, config);
    config.time.steps = 13;
    require_rejected([&] { (void)advance_coupled_wind(state, config); },
                     "accelerating flow must be rejected once |u| dt / dx exceeds 1");
    require_near(coupled_scalar_stability_number(state, config), 0.0, "resting air has zero stability number");
}

// 7. Supersaturation is reported, not removed.
void supersaturation_is_reported() {
    const auto grid = make_grid(3, 2, 1000.0);
    const auto config = make_config(grid, 60.0, 1, "none");
    auto state = rest_state(std::vector<double>(6, 100000.0), 280.0, 0.004);
    state.specific_humidity_kg_kg[4] = 0.012;  // q_s(280 K) is about 6.2 g/kg
    const auto diagnostics = diagnose_moist_layer(state, config);
    require(diagnostics.supersaturated_cells == 1, "one cell must be supersaturated");
    require(diagnostics.relative_humidity_fraction[4] > 1.5, "supersaturated cell reports RH above 1");
    const auto result = advance_coupled_wind(state, config);
    require_near(result.specific_humidity_kg_kg[4], 0.012, "humidity is not condensed away");
}

// 8. Validation rejects invalid coupled configurations and states.
void invalid_configurations_are_rejected() {
    const auto grid = make_grid(4, 3, 1000.0);
    const auto state = rest_state(linear_pressure(grid, 1.0e-3, 0.0));
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.diffusion.humidity_m2_s = -1.0;
        validate_coupled_wind_config(config);
    }, "negative diffusivity must be rejected");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.diffusion.momentum_m2_s = 5000.0;  // 2 * 5000 * 60 * 2 / 1e6 = 1.2
        validate_coupled_wind_config(config);
    }, "momentum diffusion above its explicit bound must be rejected");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.scalar_boundary = "reflective";
        validate_coupled_wind_config(config);
    }, "unknown scalar boundary must be rejected");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.density_mode = "hydrostatic";
        validate_coupled_wind_config(config);
    }, "unknown density mode must be rejected");
    require_rejected([&] {
        validate_coupled_wind_config(make_config(grid, 20000.0, 1, "prescribed", 0.0, 1.0e-4));
    }, "the Coriolis timestep bound still applies");
    require_rejected([&] {
        auto broken = state;
        broken.specific_humidity_kg_kg[1] = 1.0;
        validate_coupled_wind_state(broken, make_config(grid, 60.0, 1, "f_plane"));
    }, "specific humidity of 1 kg/kg must be rejected");
    require_rejected([&] {
        auto broken = state;
        broken.air_temperature_k.pop_back();
        validate_coupled_wind_state(broken, make_config(grid, 60.0, 1, "f_plane"));
    }, "array length mismatch must be rejected");
    require_rejected([&] {
        (void)build_gaussian_field(288.0, {{0.0, 0.0, 0.0, 1.0}}, grid);
    }, "zero anomaly radius must be rejected");
}

}  // namespace

int main() {
    try {
        constant_density_reproduces_pressure_wind_and_keeps_uniform_scalars();
        lighter_air_accelerates_faster_under_the_same_pressure_gradient();
        upwind_transport_matches_hand_computed_step();
        diffusion_step_matches_hand_computed_values_for_both_boundaries();
        diffusion_smooths_fields_between_high_and_low();
        unresolved_scalar_transport_is_rejected_mid_run();
        supersaturation_is_reported();
        invalid_configurations_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "coupled wind test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "coupled wind / moist layer cases passed\n";
    return 0;
}
