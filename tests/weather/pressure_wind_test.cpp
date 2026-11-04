#include "terraflux/weather/pressure_wind.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using terraflux::weather::AtmosphereConfig;
using terraflux::weather::DryTransportConfig;
using terraflux::weather::DryTransportState;
using terraflux::weather::PressureCenter;
using terraflux::weather::PressureSystemsConfig;
using terraflux::weather::PressureWindConfig;
using terraflux::weather::PressureWindState;
using terraflux::weather::TransportGrid2D;
using terraflux::weather::advance_dry_transport;
using terraflux::weather::advance_pressure_wind;
using terraflux::weather::balanced_wind;
using terraflux::weather::build_pressure_field;
using terraflux::weather::coriolis_parameter;
using terraflux::weather::geostrophic_wind;
using terraflux::weather::validate_pressure_wind_config;
using terraflux::weather::validate_pressure_wind_state;

constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(
    double actual,
    double expected,
    const std::string& message,
    double tolerance = 1.0e-12) {
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

TransportGrid2D make_grid(
    std::size_t nx,
    std::size_t ny,
    double spacing,
    double anchor_latitude_deg = 45.0) {
    TransportGrid2D grid;
    grid.nx = nx;
    grid.ny = ny;
    grid.spacing_east_m = spacing;
    grid.spacing_north_m = spacing;
    grid.origin_east_m = -0.5 * static_cast<double>(nx) * spacing;
    grid.origin_north_m = -0.5 * static_cast<double>(ny) * spacing;
    grid.geographic_anchor_longitude_deg = 145.0;
    grid.geographic_anchor_latitude_deg = anchor_latitude_deg;
    return grid;
}

PressureWindConfig make_config(
    const TransportGrid2D& grid,
    double timestep,
    std::size_t steps,
    const std::string& coriolis_mode,
    double friction = 0.0,
    double prescribed_f = 0.0) {
    PressureWindConfig config;
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

// Linear pressure field p = p0 + gx * east + gy * north: its one-sided and
// central differences are both exact.
std::vector<double> linear_pressure(
    const TransportGrid2D& grid,
    double gradient_east,
    double gradient_north) {
    std::vector<double> pressure(grid.nx * grid.ny);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const double east = grid.origin_east_m + (x + 0.5) * grid.spacing_east_m;
            const double north = grid.origin_north_m + (y + 0.5) * grid.spacing_north_m;
            pressure[at(x, y, grid.nx)] =
                101325.0 + gradient_east * east + gradient_north * north;
        }
    }
    return pressure;
}

PressureWindState rest_state(std::vector<double> pressure) {
    PressureWindState state;
    state.pressure_pa = std::move(pressure);
    state.wind_east_m_s.assign(state.pressure_pa.size(), 0.0);
    state.wind_north_m_s.assign(state.pressure_pa.size(), 0.0);
    return state;
}

// 1. Without rotation or friction, air accelerates straight toward low
//    pressure: u(t) = -(1/rho) dp/dx * t, exactly reproduced by the scheme.
void without_coriolis_air_accelerates_straight_down_gradient() {
    const auto grid = make_grid(4, 3, 1000.0);
    const auto config = make_config(grid, 60.0, 10, "none");
    // Pressure rises eastward, so the force points west (toward low pressure).
    const auto state = rest_state(linear_pressure(grid, 1.0e-3, 0.0));
    const auto result = advance_pressure_wind(state, config);
    const double expected_u = -1.0e-3 / 1.2 * 600.0;
    for (std::size_t i = 0; i < result.wind_east_m_s.size(); ++i) {
        require_near(result.wind_east_m_s[i], expected_u,
                     "down-gradient acceleration must be exact for constant forcing");
        require_near(result.wind_north_m_s[i], 0.0,
                     "no cross-gradient motion without Coriolis");
    }
    require(result.wind_east_m_s[0] < 0.0, "air must move toward lower pressure");
    require_near(result.elapsed_seconds, 600.0, "elapsed time must advance by all steps");
}

// 2. One trapezoidal step against hand-computed values (rho = 1, f = 0.5,
//    k = 0.2, dt = 1, dp/dx = -0.01): alpha = 0.1, gamma = 0.25,
//    det = 1.1^2 + 0.25^2 = 1.2725, u1 = 1.1*0.01/det, v1 = -0.25*0.01/det.
void single_step_matches_hand_computed_trapezoidal_update() {
    const auto grid = make_grid(4, 3, 1.0);
    auto config = make_config(grid, 1.0, 1, "prescribed", 0.2, 0.5);
    config.atmosphere.air_density_kg_m3 = 1.0;
    const auto state = rest_state(linear_pressure(grid, -0.01, 0.0));
    const auto result = advance_pressure_wind(state, config);
    // Differencing ~1e5 Pa neighbours to recover a 0.01 Pa/m gradient leaves
    // ~1e-11 relative roundoff in the forcing; the tolerance reflects that.
    for (std::size_t i = 0; i < result.wind_east_m_s.size(); ++i) {
        require_near(result.wind_east_m_s[i], 0.011 / 1.2725,
                     "trapezoidal u after one step", 1.0e-10);
        require_near(result.wind_north_m_s[i], -0.0025 / 1.2725,
                     "trapezoidal v after one step", 1.0e-10);
    }
}

// 3. Coriolis deflects moving air to the right in the Northern Hemisphere and
//    to the left in the Southern Hemisphere.
void coriolis_deflects_right_in_north_and_left_in_south() {
    for (double latitude : {45.0, -45.0}) {
        const auto grid = make_grid(3, 3, 10000.0, latitude);
        const auto config = make_config(grid, 600.0, 6, "f_plane");
        // Pressure falls northward: the pressure-gradient force points north.
        const auto result =
            advance_pressure_wind(rest_state(linear_pressure(grid, 0.0, -2.0e-3)), config);
        const double u = result.wind_east_m_s[4];
        const double v = result.wind_north_m_s[4];
        require(v > 0.0, "air initially moves toward the low pressure to the north");
        if (latitude > 0.0) {
            require(u > 0.0, "Northern Hemisphere flow must turn right (eastward)");
        } else {
            require(u < 0.0, "Southern Hemisphere flow must turn left (westward)");
        }
    }
}

// 4. Frictionless spin-up from rest is an inertial oscillation about the
//    geostrophic wind. The ageostrophic speed is conserved exactly by the
//    trapezoidal rotation, and after half an inertial period the wind peaks
//    near twice the geostrophic value.
void frictionless_spin_up_is_an_inertial_oscillation_about_geostrophic_wind() {
    const double f = 1.0e-4;
    const std::size_t steps = 100;
    const double timestep = kPi / (f * static_cast<double>(steps));
    const auto grid = make_grid(3, 3, 10000.0);
    const auto pressure = linear_pressure(grid, 0.0, -2.0e-3);

    auto config = make_config(grid, timestep, 1, "prescribed", 0.0, f);
    const auto geostrophic = geostrophic_wind(pressure, config);
    const double ug = geostrophic.east_m_s[4];
    const double vg = geostrophic.north_m_s[4];
    require_near(ug, 2.0e-3 / (1.2 * f), "geostrophic wind blows along isobars with low on the left");
    require_near(vg, 0.0, "geostrophic wind has no cross-isobar component");

    auto state = rest_state(pressure);
    const double ageostrophic_speed = std::hypot(ug, vg);
    for (std::size_t step = 0; step < steps; ++step) {
        state = advance_pressure_wind(state, config);
        const double deviation =
            std::hypot(state.wind_east_m_s[4] - ug, state.wind_north_m_s[4] - vg);
        require_near(deviation, ageostrophic_speed,
                     "ageostrophic speed must be conserved without friction",
                     1.0e-9 * ageostrophic_speed);
    }
    require_near(state.wind_east_m_s[4], 2.0 * ug,
                 "wind after half an inertial period must approach twice geostrophic",
                 1.0e-3 * ug);
    require(std::abs(state.wind_north_m_s[4]) < 1.0e-2 * ug,
            "cross-isobar wind must nearly vanish after half an inertial period");
}

// 5. Between a high to the west and a low to the east the balanced wind is
//    not eastward. In the Northern Hemisphere it blows south along the
//    isobars with only a small frictional drift toward the low, turned by
//    atan(k / f).
void wind_between_high_and_low_follows_isobars_with_small_cross_isobar_drift() {
    const double f = 1.0e-4;
    const double k = 2.0e-5;
    const auto grid = make_grid(3, 3, 10000.0);
    const auto config = make_config(grid, 1000.0, 500, "prescribed", k, f);
    // Pressure falls eastward: high to the west, low to the east.
    const auto pressure = linear_pressure(grid, -2.0e-3, 0.0);

    const auto result = advance_pressure_wind(rest_state(pressure), config);
    const auto balance = balanced_wind(pressure, config);
    const auto geostrophic = geostrophic_wind(pressure, config);

    const double u = result.wind_east_m_s[4];
    const double v = result.wind_north_m_s[4];
    const double geostrophic_speed = std::hypot(geostrophic.east_m_s[4], geostrophic.north_m_s[4]);
    require_near(u, balance.east_m_s[4], "time stepping must converge to the balanced u",
                 1.0e-4 * geostrophic_speed);
    require_near(v, balance.north_m_s[4], "time stepping must converge to the balanced v",
                 1.0e-4 * geostrophic_speed);

    require(v < 0.0, "Northern Hemisphere flow between a western high and eastern low is southward");
    require(u > 0.0, "friction adds a drift from high toward low pressure");
    require(std::abs(u) < std::abs(v), "the cross-isobar drift must stay smaller than the along-isobar wind");
    require_near(std::atan2(u, -v), std::atan(k / f),
                 "cross-isobar angle must equal atan(k / f)", 1.0e-4);
    require(std::hypot(u, v) < geostrophic_speed,
            "friction must reduce the speed below geostrophic");
    require_near(std::hypot(u, v), geostrophic_speed * f / std::hypot(f, k),
                 "balanced speed must equal the geostrophic speed scaled by f / sqrt(f^2 + k^2)",
                 1.0e-4 * geostrophic_speed);
}

// 6. Geostrophic circulation around synthetic centers: counterclockwise
//    around a low and clockwise around a high in the Northern Hemisphere,
//    reversed in the Southern Hemisphere.
void geostrophic_flow_circulates_around_lows_and_highs() {
    for (double latitude : {45.0, -45.0}) {
        const auto grid = make_grid(40, 40, 25000.0, latitude);
        auto config = make_config(grid, 600.0, 1, "f_plane");
        config.atmosphere.air_density_kg_m3 = 1.225;
        const double sign = latitude > 0.0 ? 1.0 : -1.0;

        for (const char* kind : {"low", "high"}) {
            PressureSystemsConfig systems;
            systems.centers.push_back(PressureCenter{
                kind, 12500.0, 12500.0, kind[0] == 'l' ? -1500.0 : 1500.0, 400000.0});
            const auto pressure = build_pressure_field(systems, grid);
            const auto wind = geostrophic_wind(pressure, config);
            const double cyclonic = kind[0] == 'l' ? sign : -sign;

            // Center lies on cell (20, 20); inspect cells east and north of it.
            const double v_east_of_center = wind.north_m_s[at(26, 20, grid.nx)];
            const double u_north_of_center = wind.east_m_s[at(20, 26, grid.nx)];
            require(v_east_of_center * cyclonic > 0.0,
                    std::string("flow east of a ") + kind + " must follow the expected rotation sense");
            require(u_north_of_center * cyclonic < 0.0,
                    std::string("flow north of a ") + kind + " must follow the expected rotation sense");
            require(std::abs(wind.east_m_s[at(20, 20, grid.nx)]) < 1.0e-9 &&
                        std::abs(wind.north_m_s[at(20, 20, grid.nx)]) < 1.0e-9,
                    "the wind vanishes at the center of a symmetric pressure system");

            double max_speed = 0.0;
            for (std::size_t i = 0; i < wind.east_m_s.size(); ++i) {
                max_speed = std::max(max_speed, std::hypot(wind.east_m_s[i], wind.north_m_s[i]));
            }
            require(max_speed > 10.0 && max_speed < 40.0,
                    "a 15 hPa, 400 km system at 45 degrees yields synoptic wind speeds");
        }
    }
}

// 7. On an f-plane with periodic central differences the geostrophic wind is
//    discretely divergence-free and therefore accepted by the dry transport
//    model, which otherwise rejects divergent wind.
void periodic_geostrophic_wind_feeds_dry_transport() {
    const auto grid = make_grid(8, 6, 50000.0);
    auto config = make_config(grid, 600.0, 1, "f_plane");
    config.pressure_gradient_boundary = "periodic";
    std::vector<double> pressure(grid.nx * grid.ny);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            pressure[at(x, y, grid.nx)] =
                101325.0 + 100.0 * std::sin(2.0 * kPi * (x + 0.5) / grid.nx) *
                               std::cos(2.0 * kPi * (y + 0.5) / grid.ny);
        }
    }
    const auto wind = geostrophic_wind(pressure, config);

    DryTransportConfig transport;
    transport.start_time_utc = config.time.start_time_utc;
    transport.timestep_seconds = 600.0;
    transport.steps = 3;
    transport.grid = grid;
    DryTransportState state;
    state.air_temperature_k.assign(pressure.size(), 285.0);
    state.wind_east_m_s = wind.east_m_s;
    state.wind_north_m_s = wind.north_m_s;
    const auto result = advance_dry_transport(state, transport);
    for (double temperature : result.air_temperature_k) {
        require_near(temperature, 285.0, "non-divergent geostrophic wind preserves a constant field");
    }
    require(std::hypot(wind.east_m_s[0], wind.north_m_s[0]) > 1.0,
            "the periodic test field must produce a non-trivial wind");
}

// 8. The balanced wind is an exact fixed point of the stepper for any
//    timestep inside the configured bound.
void balanced_wind_is_a_fixed_point_of_the_stepper() {
    const auto grid = make_grid(12, 10, 25000.0);
    const auto config = make_config(grid, 2000.0, 25, "prescribed", 3.0e-5, 1.0e-4);
    PressureSystemsConfig systems;
    systems.centers.push_back(PressureCenter{"low", -60000.0, 20000.0, -1200.0, 150000.0});
    systems.centers.push_back(PressureCenter{"high", 90000.0, -40000.0, 800.0, 120000.0});
    const auto pressure = build_pressure_field(systems, grid);
    const auto balance = balanced_wind(pressure, config);

    PressureWindState state;
    state.pressure_pa = pressure;
    state.wind_east_m_s = balance.east_m_s;
    state.wind_north_m_s = balance.north_m_s;
    const auto result = advance_pressure_wind(state, config);
    for (std::size_t i = 0; i < pressure.size(); ++i) {
        require_near(result.wind_east_m_s[i], balance.east_m_s[i],
                     "balanced u must not drift", 1.0e-9);
        require_near(result.wind_north_m_s[i], balance.north_m_s[i],
                     "balanced v must not drift", 1.0e-9);
    }
}

// 9. Coriolis parameter modes.
void coriolis_parameter_modes_follow_latitude() {
    const auto grid = make_grid(2, 4, 100000.0);
    AtmosphereConfig atmosphere;
    const double omega = atmosphere.earth_angular_speed_rad_s;
    const double f0 = 2.0 * omega * std::sin(45.0 * kPi / 180.0);
    const double beta = 2.0 * omega * std::cos(45.0 * kPi / 180.0) / atmosphere.earth_radius_m;

    atmosphere.coriolis_mode = "f_plane";
    for (double f : coriolis_parameter(grid, atmosphere)) {
        require_near(f, f0, "f-plane uses the anchor latitude", 1.0e-15);
    }
    atmosphere.coriolis_mode = "beta_plane";
    const auto f_beta = coriolis_parameter(grid, atmosphere);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        const double north = grid.origin_north_m + (y + 0.5) * grid.spacing_north_m;
        require_near(f_beta[at(0, y, grid.nx)], f0 + beta * north,
                     "beta plane varies linearly with north distance", 1.0e-15);
    }
    require(f_beta[at(0, 3, grid.nx)] > f_beta[at(0, 0, grid.nx)],
            "f must increase toward the pole on a Northern Hemisphere beta plane");
    atmosphere.coriolis_mode = "none";
    for (double f : coriolis_parameter(grid, atmosphere)) {
        require_near(f, 0.0, "none disables rotation");
    }
    atmosphere.coriolis_mode = "prescribed";
    atmosphere.coriolis_parameter_s = -7.0e-5;
    for (double f : coriolis_parameter(grid, atmosphere)) {
        require_near(f, -7.0e-5, "prescribed mode uses the given constant");
    }
}

// 10. Validation rejects unphysical or unresolved configurations.
void invalid_configurations_are_rejected() {
    const auto grid = make_grid(4, 3, 1000.0);
    const auto pressure = linear_pressure(grid, 1.0e-3, 0.0);

    require_rejected([&] {
        validate_pressure_wind_config(make_config(grid, 20000.0, 1, "prescribed", 0.0, 1.0e-4));
    }, "timestep above 1 / (|f| + k) must be rejected");
    require_rejected([&] {
        validate_pressure_wind_config(make_config(grid, 5000.0, 1, "prescribed", 1.5e-4, 1.0e-4));
    }, "friction must count toward the timestep bound");
    require_rejected([&] {
        (void)geostrophic_wind(pressure, make_config(grid, 60.0, 1, "none"));
    }, "geostrophic wind is undefined without rotation");
    require_rejected([&] {
        (void)balanced_wind(pressure, make_config(grid, 60.0, 1, "none"));
    }, "balanced wind is undefined without rotation or friction");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.atmosphere.air_density_kg_m3 = 0.0;
        validate_pressure_wind_config(config);
    }, "zero air density must be rejected");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane", -1.0e-5);
        validate_pressure_wind_config(config);
    }, "negative friction must be rejected");
    require_rejected([&] {
        validate_pressure_wind_config(make_config(grid, 60.0, 1, "gradient_wind"));
    }, "unknown Coriolis mode must be rejected");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.pressure_gradient_boundary = "reflective";
        validate_pressure_wind_config(config);
    }, "unknown gradient boundary must be rejected");
    require_rejected([&] {
        auto config = make_config(grid, 60.0, 1, "f_plane");
        config.time.start_time_utc = "2026-11-01T00:00:00+10:00";
        validate_pressure_wind_config(config);
    }, "non-UTC start time must be rejected");
    require_rejected([&] {
        auto state = rest_state(pressure);
        state.wind_east_m_s.pop_back();
        validate_pressure_wind_state(state, make_config(grid, 60.0, 1, "f_plane"));
    }, "array length mismatch must be rejected");
    require_rejected([&] {
        auto state = rest_state(pressure);
        state.pressure_pa[2] = 0.0;
        validate_pressure_wind_state(state, make_config(grid, 60.0, 1, "f_plane"));
    }, "non-positive pressure must be rejected");
    require_rejected([&] {
        PressureSystemsConfig systems;
        systems.centers.push_back(PressureCenter{"low", 0.0, 0.0, 500.0, 1000.0});
        (void)build_pressure_field(systems, grid);
    }, "a low with a positive anomaly must be rejected");
    require_rejected([&] {
        PressureSystemsConfig systems;
        systems.centers.push_back(PressureCenter{"high", 0.0, 0.0, 500.0, 0.0});
        (void)build_pressure_field(systems, grid);
    }, "a zero radius must be rejected");
    require_rejected([&] {
        PressureSystemsConfig systems;
        systems.centers.push_back(PressureCenter{"low", 0.0, 0.0, -200000.0, 1.0e6});
        (void)build_pressure_field(systems, grid);
    }, "systems driving pressure below zero must be rejected");
}


// ---------------------------------------------------------------------------
// Verification block: Coriolis direction, pressure-gradient acceleration and
// reference magnitudes, checked independently of the solver's own algebra.
// ---------------------------------------------------------------------------

// V1. The per-cell equations are linear with constant coefficients, so they
//     have the closed form  w(t) = w_b + e^(-k t) R(-f t) (w_0 - w_b), where
//     w = (u, v), w_b is the balanced wind and R rotates counterclockwise.
//     The trapezoidal scheme must reproduce it for both hemispheres.
void scheme_matches_exact_solution_in_both_hemispheres() {
    for (double f : {1.0e-4, -1.0e-4}) {
        const double k = 1.5e-5;
        const double timestep = 0.02 / std::abs(f);
        const std::size_t steps = static_cast<std::size_t>(2.0 * 2.0 * kPi / 0.02);  // two inertial periods
        const auto grid = make_grid(3, 3, 10000.0);
        const auto config = make_config(grid, timestep, steps, "prescribed", k, f);
        const auto pressure = linear_pressure(grid, -1.5e-3, 0.8e-3);
        auto state = rest_state(pressure);
        state.wind_east_m_s.assign(9, 3.0);
        state.wind_north_m_s.assign(9, -2.0);
        const auto balance = balanced_wind(pressure, config);
        const auto result = advance_pressure_wind(state, config);

        const double t = timestep * static_cast<double>(steps);
        const double du = 3.0 - balance.east_m_s[4];
        const double dv = -2.0 - balance.north_m_s[4];
        const double angle = -f * t;
        const double decay = std::exp(-k * t);
        const double expected_u = balance.east_m_s[4] + decay * (std::cos(angle) * du - std::sin(angle) * dv);
        const double expected_v = balance.north_m_s[4] + decay * (std::sin(angle) * du + std::cos(angle) * dv);
        const double scale = std::hypot(du, dv);
        require_near(result.wind_east_m_s[4], expected_u, "u must follow the exact damped inertial solution", 1.0e-3 * scale);
        require_near(result.wind_north_m_s[4], expected_v, "v must follow the exact damped inertial solution", 1.0e-3 * scale);
    }
}

// V2. Direction matrix: for pressure falling toward each compass direction
//     and for both hemispheres, (a) the initial acceleration points from high
//     to low pressure, (b) the first deflection is to the right of the motion
//     north of the equator and to the left south of it, and (c) the
//     frictionless balanced wind keeps low pressure on its left (NH) or right
//     (SH). Signs are taken from cross products, not from eyeballing.
void direction_matrix_holds_for_all_gradients_and_hemispheres() {
    const double gradients[4][2] = {{-2.0e-3, 0.0}, {0.0, -2.0e-3}, {2.0e-3, 0.0}, {0.0, 2.0e-3}};
    for (double latitude : {45.0, -45.0}) {
        for (const auto& gradient : gradients) {
            const auto grid = make_grid(3, 3, 10000.0, latitude);
            const auto pressure = linear_pressure(grid, gradient[0], gradient[1]);
            const double toward_low_east = -gradient[0];
            const double toward_low_north = -gradient[1];
            const double f = coriolis_parameter(grid, make_config(grid, 1.0, 1, "f_plane").atmosphere)[4];
            require((latitude > 0.0) == (f > 0.0), "Coriolis parameter sign must follow the hemisphere");

            // (a) One short step without rotation: pure down-gradient acceleration.
            const auto straight = advance_pressure_wind(rest_state(pressure), make_config(grid, 10.0, 1, "none"));
            const double cross_a = toward_low_east * straight.wind_north_m_s[4] - toward_low_north * straight.wind_east_m_s[4];
            const double dot_a = toward_low_east * straight.wind_east_m_s[4] + toward_low_north * straight.wind_north_m_s[4];
            require(std::abs(cross_a) < 1.0e-12 && dot_a > 0.0, "initial acceleration must point from high toward low pressure");

            // (b) One short step with rotation: the wind turns right (NH) or left (SH)
            //     of the pressure-gradient force, i.e. cross(force, wind) has sign -f.
            const auto turned = advance_pressure_wind(rest_state(pressure), make_config(grid, 600.0, 1, "f_plane"));
            const double cross_b = toward_low_east * turned.wind_north_m_s[4] - toward_low_north * turned.wind_east_m_s[4];
            require(cross_b * f < 0.0, "first deflection must be to the right in the NH and to the left in the SH");

            // (c) Balanced flow: the left-hand normal (k x w) points toward low pressure
            //     in the NH and away from it in the SH; the wind is along the isobars.
            const auto geostrophic = geostrophic_wind(pressure, make_config(grid, 600.0, 1, "f_plane"));
            const double u = geostrophic.east_m_s[4], v = geostrophic.north_m_s[4];
            const double left_dot_low = (-v) * toward_low_east + u * toward_low_north;
            const double along = u * toward_low_east + v * toward_low_north;
            require(left_dot_low * f > 0.0, "geostrophic wind must keep low pressure on its left (NH) / right (SH)");
            require(std::abs(along) < 1.0e-12 * std::hypot(u, v), "geostrophic wind must be parallel to the isobars");
        }
    }
}

// V3. Reference magnitudes: f at 45 N is 1.0313e-4 1/s, beta at 45 N is
//     1.619e-11 1/(m s), and 1 hPa per 100 km at 45 N with rho = 1.2 kg/m3
//     gives a geostrophic speed of 8.08 m/s (textbook value).
void reference_magnitudes_match_textbook_values() {
    AtmosphereConfig atmosphere;
    atmosphere.coriolis_mode = "f_plane";
    require_near(coriolis_parameter(make_grid(2, 2, 1.0, 45.0), atmosphere)[0], 1.0313e-4, "f at 45 N", 1.0e-8);
    require_near(coriolis_parameter(make_grid(2, 2, 1.0, 0.0), atmosphere)[0], 0.0, "f at the equator", 1.0e-20);
    require_near(coriolis_parameter(make_grid(2, 2, 1.0, 90.0), atmosphere)[0], 2.0 * atmosphere.earth_angular_speed_rad_s, "f at the pole", 1.0e-12);
    atmosphere.coriolis_mode = "beta_plane";
    const auto grid = make_grid(2, 2, 100000.0, 45.0);  // rows at north = -50 km and +50 km
    const auto f = coriolis_parameter(grid, atmosphere);
    require_near((f[2] - f[0]) / 100000.0, 1.619e-11, "beta at 45 N", 2.0e-14);
    const auto south = coriolis_parameter(make_grid(2, 2, 100000.0, -45.0), atmosphere);
    require(south[2] > south[0] && south[0] < 0.0, "f increases northward in both hemispheres (toward the pole in the NH, toward the equator in the SH)");

    const auto reference_grid = make_grid(3, 3, 10000.0, 45.0);
    auto config = make_config(reference_grid, 600.0, 1, "f_plane");
    config.atmosphere.air_density_kg_m3 = 1.2;
    const auto wind = geostrophic_wind(linear_pressure(reference_grid, 1.0e-3, 0.0), config);  // 1 hPa per 100 km
    require_near(std::hypot(wind.east_m_s[4], wind.north_m_s[4]), 8.08, "1 hPa/100 km gives about 8 m/s at 45 N", 0.01);
    // Pressure rising eastward puts the low to the west; in the NH the flow
    // keeps the low on its left, so it blows northward.
    require_near(wind.north_m_s[4], 8.08, "low to the west drives northward geostrophic flow in the NH", 0.01);
    require_near(-1.0e-3 / 1.2, -8.33e-4, "1 hPa/100 km is an acceleration of 8.3e-4 m/s^2", 1.0e-6);
}

}  // namespace

int main() {
    try {
        without_coriolis_air_accelerates_straight_down_gradient();
        single_step_matches_hand_computed_trapezoidal_update();
        coriolis_deflects_right_in_north_and_left_in_south();
        frictionless_spin_up_is_an_inertial_oscillation_about_geostrophic_wind();
        wind_between_high_and_low_follows_isobars_with_small_cross_isobar_drift();
        geostrophic_flow_circulates_around_lows_and_highs();
        periodic_geostrophic_wind_feeds_dry_transport();
        balanced_wind_is_a_fixed_point_of_the_stepper();
        coriolis_parameter_modes_follow_latitude();
        invalid_configurations_are_rejected();
        scheme_matches_exact_solution_in_both_hemispheres();
        direction_matrix_holds_for_all_gradients_and_hemispheres();
        reference_magnitudes_match_textbook_values();
    } catch (const std::exception& error) {
        std::cerr << "pressure wind test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "pressure-gradient wind cases passed\n";
    return 0;
}
