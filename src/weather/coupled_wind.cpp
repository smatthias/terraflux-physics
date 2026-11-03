#include "terraflux/weather/coupled_wind.hpp"

#include "terraflux/weather/moist_air.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
}

// Neighbour indices of a cell under the configured scalar boundary rule.
struct Neighbours {
    std::size_t east, west, north, south;
};

Neighbours neighbours_at(
    std::size_t x, std::size_t y, const TransportGrid2D& grid, bool periodic) {
    const std::size_t east_x = x + 1 == grid.nx ? (periodic ? 0 : x) : x + 1;
    const std::size_t west_x = x == 0 ? (periodic ? grid.nx - 1 : x) : x - 1;
    const std::size_t north_y = y + 1 == grid.ny ? (periodic ? 0 : y) : y + 1;
    const std::size_t south_y = y == 0 ? (periodic ? grid.ny - 1 : y) : y - 1;
    return {at(east_x, y, grid.nx), at(west_x, y, grid.nx),
            at(x, north_y, grid.nx), at(x, south_y, grid.nx)};
}

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

// Explicit five-point Laplacian of a field; with zero_gradient boundaries
// the clamped neighbour equals the cell itself, giving zero flux.
std::vector<double> laplacian(
    const std::vector<double>& field,
    const TransportGrid2D& grid,
    bool periodic,
    double inverse_dx2,
    double inverse_dy2) {
    std::vector<double> result(field.size());
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const Neighbours n = neighbours_at(x, y, grid, periodic);
            const std::size_t i = at(x, y, grid.nx);
            const double value = field[i];
            result[i] = (field[n.east] - 2.0 * value + field[n.west]) * inverse_dx2 +
                        (field[n.north] - 2.0 * value + field[n.south]) * inverse_dy2;
            require_finite(result[i], "diffusion term is not finite");
        }
    }
    return result;
}

// Advective-form first-order upwind tendency -(u dT/dx + v dT/dy) using the
// cell-centred wind. Monotone under the combined stability bound.
std::vector<double> upwind_advection_tendency(
    const std::vector<double>& field,
    const std::vector<double>& wind_east,
    const std::vector<double>& wind_north,
    const TransportGrid2D& grid,
    bool periodic,
    double inverse_dx,
    double inverse_dy) {
    std::vector<double> result(field.size());
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const Neighbours n = neighbours_at(x, y, grid, periodic);
            const std::size_t i = at(x, y, grid.nx);
            const double value = field[i];
            const double u = wind_east[i];
            const double v = wind_north[i];
            const double d_dx = u >= 0.0 ? (value - field[n.west]) * inverse_dx
                                         : (field[n.east] - value) * inverse_dx;
            const double d_dy = v >= 0.0 ? (value - field[n.south]) * inverse_dy
                                         : (field[n.north] - value) * inverse_dy;
            result[i] = -(u * d_dx + v * d_dy);
            require_finite(result[i], "advection term is not finite");
        }
    }
    return result;
}

std::vector<double> layer_density(
    const CoupledWindState& state,
    const CoupledWindConfig& config) {
    if (config.density_mode == "constant") {
        return std::vector<double>(state.pressure_pa.size(), config.atmosphere.air_density_kg_m3);
    }
    std::vector<double> density(state.pressure_pa.size());
    for (std::size_t i = 0; i < density.size(); ++i) {
        density[i] = moist_air_density_kg_m3(
            state.pressure_pa[i], state.air_temperature_k[i], state.specific_humidity_kg_kg[i]);
    }
    return density;
}

double scalar_stability_number(
    const std::vector<double>& wind_east,
    const std::vector<double>& wind_north,
    const CoupledWindConfig& config,
    double inverse_dx,
    double inverse_dy,
    double inverse_dx2,
    double inverse_dy2) {
    const double kappa =
        std::max(config.diffusion.temperature_m2_s, config.diffusion.humidity_m2_s);
    const double diffusion_rate = 2.0 * kappa * (inverse_dx2 + inverse_dy2);
    double maximum = 0.0;
    for (std::size_t i = 0; i < wind_east.size(); ++i) {
        const double rate =
            std::abs(wind_east[i]) * inverse_dx + std::abs(wind_north[i]) * inverse_dy;
        const double number = config.time.timestep_seconds * (rate + diffusion_rate);
        require_finite(number, "scalar stability number is not finite");
        maximum = std::max(maximum, number);
    }
    return maximum;
}

}  // namespace

std::vector<double> build_gaussian_field(
    double background,
    const std::vector<GaussianAnomaly>& anomalies,
    const TransportGrid2D& grid) {
    validate_transport_grid(grid);
    require_finite(background, "gaussian field background must be finite");
    for (const GaussianAnomaly& anomaly : anomalies) {
        require_finite(anomaly.east_m, "anomaly east coordinate must be finite");
        require_finite(anomaly.north_m, "anomaly north coordinate must be finite");
        require_finite(anomaly.amplitude, "anomaly amplitude must be finite");
        require_finite(anomaly.radius_m, "anomaly radius must be finite");
        if (anomaly.radius_m <= 0.0) {
            throw std::invalid_argument("anomaly radius must be positive");
        }
    }
    std::vector<double> field(grid.nx * grid.ny);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        const double north = grid.origin_north_m + (static_cast<double>(y) + 0.5) * grid.spacing_north_m;
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const double east = grid.origin_east_m + (static_cast<double>(x) + 0.5) * grid.spacing_east_m;
            long double value = background;
            for (const GaussianAnomaly& anomaly : anomalies) {
                const long double de = (static_cast<long double>(east) - anomaly.east_m) / anomaly.radius_m;
                const long double dn = (static_cast<long double>(north) - anomaly.north_m) / anomaly.radius_m;
                value += anomaly.amplitude * std::exp(-(de * de + dn * dn));
            }
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<double>::max()) {
                throw std::invalid_argument("gaussian field value is not finite");
            }
            field[at(x, y, grid.nx)] = static_cast<double>(value);
        }
    }
    return field;
}

MoistLayerDiagnostics diagnose_moist_layer(
    const CoupledWindState& state,
    const CoupledWindConfig& config) {
    validate_coupled_wind_state(state, config);
    MoistLayerDiagnostics d;
    const std::size_t count = state.pressure_pa.size();
    d.air_density_kg_m3.resize(count);
    d.virtual_temperature_k.resize(count);
    d.vapour_pressure_pa.resize(count);
    d.saturation_vapour_pressure_pa.resize(count);
    d.relative_humidity_fraction.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double p = state.pressure_pa[i];
        const double t = state.air_temperature_k[i];
        const double q = state.specific_humidity_kg_kg[i];
        d.air_density_kg_m3[i] = moist_air_density_kg_m3(p, t, q);
        d.virtual_temperature_k[i] = virtual_temperature_k(t, q);
        d.vapour_pressure_pa[i] = vapour_pressure_pa(q, p);
        d.saturation_vapour_pressure_pa[i] = saturation_vapour_pressure_pa(t);
        d.relative_humidity_fraction[i] =
            d.vapour_pressure_pa[i] / d.saturation_vapour_pressure_pa[i];
        if (d.relative_humidity_fraction[i] > 1.0) {
            ++d.supersaturated_cells;
        }
    }
    return d;
}

double coupled_scalar_stability_number(
    const CoupledWindState& state,
    const CoupledWindConfig& config) {
    validate_coupled_wind_state(state, config);
    const double inverse_dx = 1.0 / config.grid.spacing_east_m;
    const double inverse_dy = 1.0 / config.grid.spacing_north_m;
    return scalar_stability_number(
        state.wind_east_m_s, state.wind_north_m_s, config, inverse_dx, inverse_dy,
        inverse_dx * inverse_dx, inverse_dy * inverse_dy);
}

CoupledWindState advance_coupled_wind(
    const CoupledWindState& initial,
    const CoupledWindConfig& config) {
    validate_coupled_wind_state(initial, config);

    const auto& grid = config.grid;
    const double inverse_dx = 1.0 / grid.spacing_east_m;
    const double inverse_dy = 1.0 / grid.spacing_north_m;
    const double inverse_dx2 = inverse_dx * inverse_dx;
    const double inverse_dy2 = inverse_dy * inverse_dy;
    require_finite(inverse_dx2, "east grid spacing is too small for this solver");
    require_finite(inverse_dy2, "north grid spacing is too small for this solver");
    const bool periodic = config.scalar_boundary == "periodic";
    const double timestep = config.time.timestep_seconds;
    const double k = config.atmosphere.surface_friction_coefficient_s;
    const double nu = config.diffusion.momentum_m2_s;
    const double kappa_t = config.diffusion.temperature_m2_s;
    const double kappa_q = config.diffusion.humidity_m2_s;

    const long double duration =
        static_cast<long double>(timestep) * static_cast<long double>(config.time.steps);
    const long double final_elapsed =
        static_cast<long double>(initial.elapsed_seconds) + duration;
    if (!std::isfinite(final_elapsed) ||
        final_elapsed > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::invalid_argument("advanced simulation time overflows");
    }

    const auto gradient = calculate_pressure_gradient(
        initial.pressure_pa, grid, config.pressure_gradient_boundary);
    const auto f = coriolis_parameter(grid, config.atmosphere);

    CoupledWindState state = initial;
    const std::size_t count = state.pressure_pa.size();
    WindField wind{state.wind_east_m_s, state.wind_north_m_s};
    std::vector<double> next_temperature(count);
    std::vector<double> next_humidity(count);

    for (std::size_t step = 0; step < config.time.steps; ++step) {
        // 1. Density from the current temperature and humidity.
        const std::vector<double> density = layer_density(state, config);

        // 2. Wind: pressure gradient / density, Coriolis, friction, and
        //    explicit momentum diffusion evaluated on the old wind.
        const WindField* extra = nullptr;
        WindField viscous;
        if (nu > 0.0) {
            viscous.east_m_s = laplacian(wind.east_m_s, grid, periodic, inverse_dx2, inverse_dy2);
            viscous.north_m_s = laplacian(wind.north_m_s, grid, periodic, inverse_dx2, inverse_dy2);
            for (std::size_t i = 0; i < count; ++i) {
                viscous.east_m_s[i] *= nu;
                viscous.north_m_s[i] *= nu;
            }
            extra = &viscous;
        }
        wind = step_pressure_wind(wind, gradient, density, f, k, timestep, extra);

        // 3. Scalars carried by the new wind. The bound depends on the wind,
        //    so it is checked every step.
        const double number = scalar_stability_number(
            wind.east_m_s, wind.north_m_s, config, inverse_dx, inverse_dy, inverse_dx2, inverse_dy2);
        if (number > 1.0) {
            throw std::invalid_argument(
                "timestep exceeds the scalar advection/diffusion stability limit at step " +
                std::to_string(step + 1) + " (stability number " + std::to_string(number) + ")");
        }
        const auto advect_t = upwind_advection_tendency(
            state.air_temperature_k, wind.east_m_s, wind.north_m_s, grid, periodic, inverse_dx, inverse_dy);
        const auto advect_q = upwind_advection_tendency(
            state.specific_humidity_kg_kg, wind.east_m_s, wind.north_m_s, grid, periodic, inverse_dx, inverse_dy);
        const auto diffuse_t = laplacian(state.air_temperature_k, grid, periodic, inverse_dx2, inverse_dy2);
        const auto diffuse_q = laplacian(state.specific_humidity_kg_kg, grid, periodic, inverse_dx2, inverse_dy2);
        for (std::size_t i = 0; i < count; ++i) {
            const double temperature =
                state.air_temperature_k[i] + timestep * (advect_t[i] + kappa_t * diffuse_t[i]);
            const double humidity =
                state.specific_humidity_kg_kg[i] + timestep * (advect_q[i] + kappa_q * diffuse_q[i]);
            if (!std::isfinite(temperature) || temperature <= 0.0) {
                throw std::runtime_error(
                    "coupled update produced a non-finite or non-positive temperature");
            }
            if (!std::isfinite(humidity) || humidity < 0.0 || humidity >= 1.0) {
                throw std::runtime_error(
                    "coupled update produced a non-finite or out-of-range specific humidity");
            }
            next_temperature[i] = temperature;
            next_humidity[i] = humidity;
        }
        state.air_temperature_k.swap(next_temperature);
        state.specific_humidity_kg_kg.swap(next_humidity);
        state.wind_east_m_s = wind.east_m_s;
        state.wind_north_m_s = wind.north_m_s;
    }
    state.elapsed_seconds = static_cast<double>(final_elapsed);
    return state;
}

}  // namespace terraflux::weather
