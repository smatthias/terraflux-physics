#include "terraflux/weather/transport_model.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraflux::weather {
namespace {

std::size_t cell_count(const TransportGrid2D& grid) {
    if (grid.nx < 2 || grid.ny < 2) {
        throw std::invalid_argument("transport grid dimensions must each be at least 2");
    }
    if (grid.nx > std::numeric_limits<std::size_t>::max() / grid.ny) {
        throw std::invalid_argument("transport grid cell count overflows size_t");
    }
    return grid.nx * grid.ny;
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(field);
    }
}

}  // namespace

void validate_dry_transport_config(const DryTransportConfig& config) {
    (void)cell_count(config.grid);
    if (config.start_time_utc.empty() || config.start_time_utc.back() != 'Z') {
        throw std::invalid_argument("start_time_utc must be a UTC timestamp ending in Z");
    }
    if (config.boundary_condition != "periodic") {
        throw std::invalid_argument("v1 dry transport supports periodic boundaries only");
    }

    require_finite(config.grid.origin_east_m, "grid east origin must be finite");
    require_finite(config.grid.origin_north_m, "grid north origin must be finite");
    require_finite(config.grid.spacing_east_m, "east grid spacing must be finite");
    require_finite(config.grid.spacing_north_m, "north grid spacing must be finite");
    if (config.grid.spacing_east_m <= 0.0 || config.grid.spacing_north_m <= 0.0) {
        throw std::invalid_argument("grid spacing must be positive");
    }

    const long double east_extent =
        static_cast<long double>(config.grid.origin_east_m) +
        static_cast<long double>(config.grid.nx) * config.grid.spacing_east_m;
    const long double north_extent =
        static_cast<long double>(config.grid.origin_north_m) +
        static_cast<long double>(config.grid.ny) * config.grid.spacing_north_m;
    if (!std::isfinite(east_extent) || !std::isfinite(north_extent) ||
        std::abs(east_extent) > std::numeric_limits<double>::max() ||
        std::abs(north_extent) > std::numeric_limits<double>::max()) {
        throw std::invalid_argument("transport grid extent overflows");
    }

    require_finite(
        config.grid.geographic_anchor_longitude_deg,
        "geographic anchor longitude must be finite");
    require_finite(
        config.grid.geographic_anchor_latitude_deg,
        "geographic anchor latitude must be finite");
    if (config.grid.geographic_anchor_longitude_deg < -180.0 ||
        config.grid.geographic_anchor_longitude_deg > 180.0 ||
        config.grid.geographic_anchor_latitude_deg < -90.0 ||
        config.grid.geographic_anchor_latitude_deg > 90.0) {
        throw std::invalid_argument("geographic projection anchor is outside longitude/latitude bounds");
    }

    require_finite(config.timestep_seconds, "timestep must be finite");
    if (config.timestep_seconds <= 0.0 || config.steps == 0) {
        throw std::invalid_argument("timestep and step count must be positive");
    }
    const long double duration =
        static_cast<long double>(config.timestep_seconds) *
        static_cast<long double>(config.steps);
    if (duration > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::invalid_argument("configured simulation duration overflows");
    }

    require_finite(
        config.horizontal_mixing_diffusivity_m2_s,
        "horizontal mixing diffusivity must be finite");
    if (config.horizontal_mixing_diffusivity_m2_s < 0.0) {
        throw std::invalid_argument("horizontal mixing diffusivity must not be negative");
    }
}

void validate_dry_transport_state(
    const DryTransportState& state,
    const DryTransportConfig& config) {
    validate_dry_transport_config(config);
    require_finite(state.elapsed_seconds, "elapsed simulation time must be finite");
    if (state.elapsed_seconds < 0.0) {
        throw std::invalid_argument("elapsed simulation time must not be negative");
    }

    const std::size_t expected = cell_count(config.grid);
    if (state.air_temperature_k.size() != expected ||
        state.wind_east_m_s.size() != expected ||
        state.wind_north_m_s.size() != expected) {
        throw std::invalid_argument("transport state arrays must match the grid cell count");
    }

    for (double temperature : state.air_temperature_k) {
        if (!std::isfinite(temperature) || temperature <= 0.0) {
            throw std::invalid_argument("air temperature must be finite and greater than 0 K");
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

}  // namespace terraflux::weather
