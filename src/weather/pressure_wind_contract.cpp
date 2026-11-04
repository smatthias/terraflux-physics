#include "terraflux/weather/pressure_wind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraflux::weather {
namespace {

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void validate_time_stepping(const TimeSteppingConfig& time) {
    if (time.start_time_utc.empty() || time.start_time_utc.back() != 'Z') {
        throw std::invalid_argument("start_time_utc must be a UTC timestamp ending in Z");
    }
    require_finite(time.timestep_seconds, "timestep must be finite");
    if (time.timestep_seconds <= 0.0 || time.steps == 0) {
        throw std::invalid_argument("timestep and step count must be positive");
    }
    const long double duration =
        static_cast<long double>(time.timestep_seconds) *
        static_cast<long double>(time.steps);
    if (duration > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::invalid_argument("configured simulation duration overflows");
    }
}

void validate_atmosphere(const AtmosphereConfig& atmosphere) {
    require_finite(atmosphere.air_density_kg_m3, "air density must be finite");
    if (atmosphere.air_density_kg_m3 <= 0.0) {
        throw std::invalid_argument("air density must be positive");
    }
    require_finite(
        atmosphere.surface_friction_coefficient_s,
        "surface friction coefficient must be finite");
    if (atmosphere.surface_friction_coefficient_s < 0.0) {
        throw std::invalid_argument("surface friction coefficient must not be negative");
    }

    const std::string& mode = atmosphere.coriolis_mode;
    if (mode != "none" && mode != "f_plane" && mode != "beta_plane" &&
        mode != "prescribed") {
        throw std::invalid_argument(
            "coriolis_mode must be one of none, f_plane, beta_plane, prescribed");
    }
    if (mode == "prescribed") {
        require_finite(
            atmosphere.coriolis_parameter_s,
            "prescribed Coriolis parameter must be finite");
    }
    if (mode == "f_plane" || mode == "beta_plane") {
        require_finite(
            atmosphere.earth_angular_speed_rad_s,
            "earth angular speed must be finite");
        if (atmosphere.earth_angular_speed_rad_s < 0.0) {
            throw std::invalid_argument("earth angular speed must not be negative");
        }
    }
    if (mode == "beta_plane") {
        require_finite(atmosphere.earth_radius_m, "earth radius must be finite");
        if (atmosphere.earth_radius_m <= 0.0) {
            throw std::invalid_argument("earth radius must be positive for a beta plane");
        }
    }
}

}  // namespace

void validate_pressure_wind_config(const PressureWindConfig& config) {
    validate_transport_grid(config.grid);
    validate_time_stepping(config.time);
    validate_atmosphere(config.atmosphere);
    if (config.pressure_gradient_boundary != "one_sided" &&
        config.pressure_gradient_boundary != "periodic") {
        throw std::invalid_argument(
            "pressure_gradient_boundary must be one_sided or periodic");
    }

    // Accuracy bound for the trapezoidal Coriolis/friction update: the scheme
    // is unconditionally stable, but the inertial rotation per step must stay
    // resolved. Rejecting, rather than silently substepping, matches the
    // transport model's policy.
    double max_abs_f = 0.0;
    for (double f : coriolis_parameter(config.grid, config.atmosphere)) {
        require_finite(f, "Coriolis parameter must be finite in every cell");
        max_abs_f = std::max(max_abs_f, std::abs(f));
    }
    const double rate = max_abs_f + config.atmosphere.surface_friction_coefficient_s;
    const double number = config.time.timestep_seconds * rate;
    require_finite(number, "Coriolis/friction timestep number is not finite");
    if (number > 1.0) {
        throw std::invalid_argument(
            "timestep exceeds the Coriolis/friction resolution bound timestep * (|f| + k) <= 1");
    }
}

void validate_pressure_wind_state(
    const PressureWindState& state,
    const PressureWindConfig& config) {
    validate_pressure_wind_config(config);
    require_finite(state.elapsed_seconds, "elapsed simulation time must be finite");
    if (state.elapsed_seconds < 0.0) {
        throw std::invalid_argument("elapsed simulation time must not be negative");
    }

    const std::size_t expected = config.grid.nx * config.grid.ny;
    if (state.pressure_pa.size() != expected ||
        state.wind_east_m_s.size() != expected ||
        state.wind_north_m_s.size() != expected) {
        throw std::invalid_argument("pressure wind state arrays must match the grid cell count");
    }
    for (double pressure : state.pressure_pa) {
        if (!std::isfinite(pressure) || pressure <= 0.0) {
            throw std::invalid_argument("pressure must be finite and greater than 0 Pa");
        }
    }
    for (double wind : state.wind_east_m_s) {
        if (!std::isfinite(wind)) {
            throw std::invalid_argument("eastward wind values must be finite");
        }
    }
    for (double wind : state.wind_north_m_s) {
        if (!std::isfinite(wind)) {
            throw std::invalid_argument("northward wind values must be finite");
        }
    }
}

void validate_pressure_systems(
    const PressureSystemsConfig& systems,
    const TransportGrid2D& grid) {
    validate_transport_grid(grid);
    require_finite(systems.background_pressure_pa, "background pressure must be finite");
    if (systems.background_pressure_pa <= 0.0) {
        throw std::invalid_argument("background pressure must be positive");
    }
    for (const PressureCenter& center : systems.centers) {
        if (center.kind != "high" && center.kind != "low") {
            throw std::invalid_argument("pressure center kind must be high or low");
        }
        require_finite(center.east_m, "pressure center east coordinate must be finite");
        require_finite(center.north_m, "pressure center north coordinate must be finite");
        require_finite(center.pressure_anomaly_pa, "pressure anomaly must be finite");
        require_finite(center.radius_m, "pressure center radius must be finite");
        if (center.radius_m <= 0.0) {
            throw std::invalid_argument("pressure center radius must be positive");
        }
        if (center.kind == "high" && center.pressure_anomaly_pa <= 0.0) {
            throw std::invalid_argument("a high pressure center requires a positive anomaly");
        }
        if (center.kind == "low" && center.pressure_anomaly_pa >= 0.0) {
            throw std::invalid_argument("a low pressure center requires a negative anomaly");
        }
    }
}

}  // namespace terraflux::weather
