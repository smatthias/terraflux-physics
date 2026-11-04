#include "terraflux/weather/pressure_wind.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraflux::weather {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
}

double cell_center_east(const TransportGrid2D& grid, std::size_t x) {
    return grid.origin_east_m +
           (static_cast<double>(x) + 0.5) * grid.spacing_east_m;
}

double cell_center_north(const TransportGrid2D& grid, std::size_t y) {
    return grid.origin_north_m +
           (static_cast<double>(y) + 0.5) * grid.spacing_north_m;
}

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

}  // namespace

std::vector<double> build_pressure_field(
    const PressureSystemsConfig& systems,
    const TransportGrid2D& grid) {
    validate_pressure_systems(systems, grid);
    std::vector<double> pressure(grid.nx * grid.ny);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        const double north = cell_center_north(grid, y);
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const double east = cell_center_east(grid, x);
            long double value = systems.background_pressure_pa;
            for (const PressureCenter& center : systems.centers) {
                const long double de =
                    (static_cast<long double>(east) - center.east_m) / center.radius_m;
                const long double dn =
                    (static_cast<long double>(north) - center.north_m) / center.radius_m;
                value += center.pressure_anomaly_pa * std::exp(-(de * de + dn * dn));
            }
            if (!std::isfinite(value) || value <= 0.0 ||
                value > std::numeric_limits<double>::max()) {
                throw std::invalid_argument(
                    "pressure systems produce a non-finite or non-positive pressure");
            }
            pressure[at(x, y, grid.nx)] = static_cast<double>(value);
        }
    }
    return pressure;
}

std::vector<double> coriolis_parameter(
    const TransportGrid2D& grid,
    const AtmosphereConfig& atmosphere) {
    const std::size_t count = grid.nx * grid.ny;
    const std::string& mode = atmosphere.coriolis_mode;
    if (mode == "none") {
        return std::vector<double>(count, 0.0);
    }
    if (mode == "prescribed") {
        return std::vector<double>(count, atmosphere.coriolis_parameter_s);
    }

    const double latitude_rad =
        grid.geographic_anchor_latitude_deg * kPi / 180.0;
    const double f0 =
        2.0 * atmosphere.earth_angular_speed_rad_s * std::sin(latitude_rad);
    if (mode == "f_plane") {
        return std::vector<double>(count, f0);
    }
    if (mode == "beta_plane") {
        // df/dy = 2 Omega cos(phi0) / R, with y the north distance from the
        // projection anchor at local (0, 0).
        const double beta =
            2.0 * atmosphere.earth_angular_speed_rad_s * std::cos(latitude_rad) /
            atmosphere.earth_radius_m;
        std::vector<double> f(count);
        for (std::size_t y = 0; y < grid.ny; ++y) {
            const double value = f0 + beta * cell_center_north(grid, y);
            require_finite(value, "beta-plane Coriolis parameter is not finite");
            for (std::size_t x = 0; x < grid.nx; ++x) {
                f[at(x, y, grid.nx)] = value;
            }
        }
        return f;
    }
    throw std::invalid_argument(
        "coriolis_mode must be one of none, f_plane, beta_plane, prescribed");
}

PressureGradient calculate_pressure_gradient(
    const std::vector<double>& pressure_pa,
    const TransportGrid2D& grid,
    const std::string& boundary) {
    validate_transport_grid(grid);
    if (pressure_pa.size() != grid.nx * grid.ny) {
        throw std::invalid_argument("pressure array must match the grid cell count");
    }
    const bool periodic = boundary == "periodic";
    if (!periodic && boundary != "one_sided") {
        throw std::invalid_argument(
            "pressure_gradient_boundary must be one_sided or periodic");
    }
    for (double p : pressure_pa) {
        require_finite(p, "pressure must be finite");
    }

    PressureGradient gradient;
    gradient.east_pa_m.resize(pressure_pa.size());
    gradient.north_pa_m.resize(pressure_pa.size());
    for (std::size_t y = 0; y < grid.ny; ++y) {
        std::size_t south = y == 0 ? (periodic ? grid.ny - 1 : y) : y - 1;
        std::size_t north = y + 1 == grid.ny ? (periodic ? 0 : y) : y + 1;
        const double span_north =
            (periodic ? 2.0 : static_cast<double>(north - south)) *
            grid.spacing_north_m;
        for (std::size_t x = 0; x < grid.nx; ++x) {
            std::size_t west = x == 0 ? (periodic ? grid.nx - 1 : x) : x - 1;
            std::size_t east = x + 1 == grid.nx ? (periodic ? 0 : x) : x + 1;
            const double span_east =
                (periodic ? 2.0 : static_cast<double>(east - west)) *
                grid.spacing_east_m;

            // Long double intermediates avoid overflow on large finite values.
            const double dp_dx = static_cast<double>(
                (static_cast<long double>(pressure_pa[at(east, y, grid.nx)]) -
                 pressure_pa[at(west, y, grid.nx)]) /
                span_east);
            const double dp_dy = static_cast<double>(
                (static_cast<long double>(pressure_pa[at(x, north, grid.nx)]) -
                 pressure_pa[at(x, south, grid.nx)]) /
                span_north);
            require_finite(dp_dx, "pressure gradient overflow");
            require_finite(dp_dy, "pressure gradient overflow");
            gradient.east_pa_m[at(x, y, grid.nx)] = dp_dx;
            gradient.north_pa_m[at(x, y, grid.nx)] = dp_dy;
        }
    }
    return gradient;
}

WindField geostrophic_wind(
    const std::vector<double>& pressure_pa,
    const PressureWindConfig& config) {
    validate_pressure_wind_config(config);
    const auto gradient = calculate_pressure_gradient(
        pressure_pa, config.grid, config.pressure_gradient_boundary);
    const auto f = coriolis_parameter(config.grid, config.atmosphere);
    const double rho = config.atmosphere.air_density_kg_m3;

    WindField wind;
    wind.east_m_s.resize(pressure_pa.size());
    wind.north_m_s.resize(pressure_pa.size());
    for (std::size_t i = 0; i < pressure_pa.size(); ++i) {
        if (f[i] == 0.0) {
            throw std::invalid_argument(
                "geostrophic wind is undefined where the Coriolis parameter is zero");
        }
        const double u = -gradient.north_pa_m[i] / (rho * f[i]);
        const double v = gradient.east_pa_m[i] / (rho * f[i]);
        require_finite(u, "geostrophic wind is not finite");
        require_finite(v, "geostrophic wind is not finite");
        wind.east_m_s[i] = u;
        wind.north_m_s[i] = v;
    }
    return wind;
}

WindField balanced_wind(
    const std::vector<double>& pressure_pa,
    const PressureWindConfig& config) {
    validate_pressure_wind_config(config);
    const auto gradient = calculate_pressure_gradient(
        pressure_pa, config.grid, config.pressure_gradient_boundary);
    const auto f = coriolis_parameter(config.grid, config.atmosphere);
    const double rho = config.atmosphere.air_density_kg_m3;
    const double k = config.atmosphere.surface_friction_coefficient_s;

    WindField wind;
    wind.east_m_s.resize(pressure_pa.size());
    wind.north_m_s.resize(pressure_pa.size());
    for (std::size_t i = 0; i < pressure_pa.size(); ++i) {
        const double denominator = rho * (k * k + f[i] * f[i]);
        if (denominator == 0.0) {
            throw std::invalid_argument(
                "balanced wind is undefined where both friction and the Coriolis parameter are zero");
        }
        const double dp_dx = gradient.east_pa_m[i];
        const double dp_dy = gradient.north_pa_m[i];
        const double u = -(k * dp_dx + f[i] * dp_dy) / denominator;
        const double v = (f[i] * dp_dx - k * dp_dy) / denominator;
        require_finite(u, "balanced wind is not finite");
        require_finite(v, "balanced wind is not finite");
        wind.east_m_s[i] = u;
        wind.north_m_s[i] = v;
    }
    return wind;
}

WindField step_pressure_wind(
    const WindField& wind,
    const PressureGradient& gradient,
    const std::vector<double>& air_density_kg_m3,
    const std::vector<double>& coriolis_parameter_s,
    double surface_friction_coefficient_s,
    double timestep_seconds,
    const WindField* extra_acceleration_m_s2) {
    const std::size_t count = wind.east_m_s.size();
    if (wind.north_m_s.size() != count || gradient.east_pa_m.size() != count ||
        gradient.north_pa_m.size() != count || air_density_kg_m3.size() != count ||
        coriolis_parameter_s.size() != count ||
        (extra_acceleration_m_s2 != nullptr &&
         (extra_acceleration_m_s2->east_m_s.size() != count ||
          extra_acceleration_m_s2->north_m_s.size() != count))) {
        throw std::invalid_argument("wind step arrays must all match the grid cell count");
    }

    // With alpha = k dt / 2 and gamma = f dt / 2 the update solves
    //   (1 + alpha) u1 - gamma v1 = (1 - alpha) u0 + gamma v0 + dt a
    //   gamma u1 + (1 + alpha) v1 = (1 - alpha) v0 - gamma u0 + dt b
    // where a = -(1/rho) dp/dx + extra_east and b = -(1/rho) dp/dy + extra_north.
    const double alpha = 0.5 * surface_friction_coefficient_s * timestep_seconds;
    const double one_plus_alpha = 1.0 + alpha;
    const double one_minus_alpha = 1.0 - alpha;

    WindField result;
    result.east_m_s.resize(count);
    result.north_m_s.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double rho = air_density_kg_m3[i];
        if (!std::isfinite(rho) || rho <= 0.0) {
            throw std::invalid_argument("air density must be finite and positive in every cell");
        }
        double acceleration_east = -gradient.east_pa_m[i] / rho;
        double acceleration_north = -gradient.north_pa_m[i] / rho;
        if (extra_acceleration_m_s2 != nullptr) {
            acceleration_east += extra_acceleration_m_s2->east_m_s[i];
            acceleration_north += extra_acceleration_m_s2->north_m_s[i];
        }
        const double forcing_east = acceleration_east * timestep_seconds;
        const double forcing_north = acceleration_north * timestep_seconds;
        require_finite(forcing_east, "wind acceleration is not finite");
        require_finite(forcing_north, "wind acceleration is not finite");

        const double u0 = wind.east_m_s[i];
        const double v0 = wind.north_m_s[i];
        const double g = 0.5 * coriolis_parameter_s[i] * timestep_seconds;
        const double rhs_u = one_minus_alpha * u0 + g * v0 + forcing_east;
        const double rhs_v = one_minus_alpha * v0 - g * u0 + forcing_north;
        const double determinant = one_plus_alpha * one_plus_alpha + g * g;
        const double u1 = (one_plus_alpha * rhs_u + g * rhs_v) / determinant;
        const double v1 = (one_plus_alpha * rhs_v - g * rhs_u) / determinant;
        if (!std::isfinite(u1) || !std::isfinite(v1)) {
            throw std::runtime_error(
                "pressure wind update produced a non-finite wind component");
        }
        result.east_m_s[i] = u1;
        result.north_m_s[i] = v1;
    }
    return result;
}

PressureWindState advance_pressure_wind(
    const PressureWindState& initial,
    const PressureWindConfig& config) {
    validate_pressure_wind_state(initial, config);

    const auto gradient = calculate_pressure_gradient(
        initial.pressure_pa, config.grid, config.pressure_gradient_boundary);
    const auto f = coriolis_parameter(config.grid, config.atmosphere);
    const std::vector<double> density(
        initial.pressure_pa.size(), config.atmosphere.air_density_kg_m3);
    const double k = config.atmosphere.surface_friction_coefficient_s;
    const double timestep = config.time.timestep_seconds;

    const long double duration =
        static_cast<long double>(timestep) *
        static_cast<long double>(config.time.steps);
    const long double final_elapsed =
        static_cast<long double>(initial.elapsed_seconds) + duration;
    if (!std::isfinite(final_elapsed) ||
        final_elapsed > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::invalid_argument("advanced simulation time overflows");
    }

    WindField wind{initial.wind_east_m_s, initial.wind_north_m_s};
    for (std::size_t step = 0; step < config.time.steps; ++step) {
        wind = step_pressure_wind(wind, gradient, density, f, k, timestep);
    }

    PressureWindState result = initial;
    result.wind_east_m_s = std::move(wind.east_m_s);
    result.wind_north_m_s = std::move(wind.north_m_s);
    result.elapsed_seconds = static_cast<double>(final_elapsed);
    return result;
}

}  // namespace terraflux::weather
