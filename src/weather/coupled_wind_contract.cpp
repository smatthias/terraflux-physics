#include "terraflux/weather/coupled_wind.hpp"

#include <cmath>
#include <stdexcept>

namespace terraflux::weather {
namespace {

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void require_diffusivity(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " diffusivity must be finite");
    }
    if (value < 0.0) {
        throw std::invalid_argument(std::string(name) + " diffusivity must not be negative");
    }
}

}  // namespace

void validate_coupled_wind_config(const CoupledWindConfig& config) {
    // Reuse the pressure-wind rules for time, grid, atmosphere, gradient
    // boundary and the Coriolis/friction timestep bound.
    PressureWindConfig wind_config;
    wind_config.time = config.time;
    wind_config.grid = config.grid;
    wind_config.atmosphere = config.atmosphere;
    wind_config.pressure_gradient_boundary = config.pressure_gradient_boundary;
    validate_pressure_wind_config(wind_config);

    require_diffusivity(config.diffusion.temperature_m2_s, "temperature");
    require_diffusivity(config.diffusion.humidity_m2_s, "humidity");
    require_diffusivity(config.diffusion.momentum_m2_s, "momentum");

    if (config.scalar_boundary != "zero_gradient" && config.scalar_boundary != "periodic") {
        throw std::invalid_argument("scalar_boundary must be zero_gradient or periodic");
    }
    if (config.density_mode != "diagnosed" && config.density_mode != "constant") {
        throw std::invalid_argument("density_mode must be diagnosed or constant");
    }

    // Explicit momentum diffusion bound; the scalar bound also depends on the
    // evolving wind and is checked every step by advance_coupled_wind.
    const double inverse_dx2 = 1.0 / (config.grid.spacing_east_m * config.grid.spacing_east_m);
    const double inverse_dy2 = 1.0 / (config.grid.spacing_north_m * config.grid.spacing_north_m);
    require_finite(inverse_dx2, "east grid spacing is too small for this solver");
    require_finite(inverse_dy2, "north grid spacing is too small for this solver");
    const double momentum_number =
        config.time.timestep_seconds * 2.0 * config.diffusion.momentum_m2_s *
        (inverse_dx2 + inverse_dy2);
    require_finite(momentum_number, "momentum diffusion number is not finite");
    if (momentum_number > 1.0) {
        throw std::invalid_argument(
            "timestep exceeds the explicit momentum diffusion bound 2 nu dt (1/dx^2 + 1/dy^2) <= 1");
    }
}

void validate_coupled_wind_state(
    const CoupledWindState& state,
    const CoupledWindConfig& config) {
    validate_coupled_wind_config(config);
    require_finite(state.elapsed_seconds, "elapsed simulation time must be finite");
    if (state.elapsed_seconds < 0.0) {
        throw std::invalid_argument("elapsed simulation time must not be negative");
    }

    const std::size_t expected = config.grid.nx * config.grid.ny;
    if (state.pressure_pa.size() != expected ||
        state.wind_east_m_s.size() != expected ||
        state.wind_north_m_s.size() != expected ||
        state.air_temperature_k.size() != expected ||
        state.specific_humidity_kg_kg.size() != expected) {
        throw std::invalid_argument("coupled wind state arrays must match the grid cell count");
    }
    for (double pressure : state.pressure_pa) {
        if (!std::isfinite(pressure) || pressure <= 0.0) {
            throw std::invalid_argument("pressure must be finite and greater than 0 Pa");
        }
    }
    for (double wind : state.wind_east_m_s) {
        require_finite(wind, "eastward wind values must be finite");
    }
    for (double wind : state.wind_north_m_s) {
        require_finite(wind, "northward wind values must be finite");
    }
    for (double temperature : state.air_temperature_k) {
        if (!std::isfinite(temperature) || temperature <= 0.0) {
            throw std::invalid_argument("air temperature must be finite and greater than 0 K");
        }
    }
    for (double q : state.specific_humidity_kg_kg) {
        if (!std::isfinite(q) || q < 0.0 || q >= 1.0) {
            throw std::invalid_argument("specific humidity must be finite and within [0, 1) kg/kg");
        }
    }
}

}  // namespace terraflux::weather
