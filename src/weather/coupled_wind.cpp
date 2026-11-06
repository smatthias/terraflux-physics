#include "terraflux/weather/coupled_wind.hpp"

#include "terraflux/weather/atmospheric_column.hpp"
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

double terrain_surface_pressure(double sea_level_pa, double elevation_m, double temperature_k) {
    return sea_level_pa * std::exp(-column::kGravityMS2 * elevation_m / (moist_air::kDryAirGasConstantJkgK * temperature_k));
}

double surface_pressure_of(const CoupledWindState& state, const CoupledWindConfig& config, std::size_t i) {
    return config.orography.enabled
        ? terrain_surface_pressure(state.pressure_pa[i], config.orography.elevation_m[i], state.air_temperature_k[i])
        : state.pressure_pa[i];
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
            surface_pressure_of(state, config, i), state.air_temperature_k[i], state.specific_humidity_kg_kg[i]);
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

// ---- orography helpers -----------------------------------------------------

struct Slopes {
    std::vector<double> east, north;
};

Slopes terrain_slopes(const std::vector<double>& h, const TransportGrid2D& g) {
    Slopes out;
    out.east.resize(h.size());
    out.north.resize(h.size());
    for (std::size_t y = 0; y < g.ny; ++y) {
        const std::size_t south = y == 0 ? y : y - 1, north = y + 1 == g.ny ? y : y + 1;
        for (std::size_t x = 0; x < g.nx; ++x) {
            const std::size_t west = x == 0 ? x : x - 1, east = x + 1 == g.nx ? x : x + 1;
            out.east[at(x, y, g.nx)] = (h[at(east, y, g.nx)] - h[at(west, y, g.nx)]) / (static_cast<double>(east - west) * g.spacing_east_m);
            out.north[at(x, y, g.nx)] = (h[at(x, north, g.nx)] - h[at(x, south, g.nx)]) / (static_cast<double>(north - south) * g.spacing_north_m);
        }
    }
    return out;
}

double cell_east_of(const TransportGrid2D& g, std::size_t i) {
    return g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m;
}

double cell_north_of(const TransportGrid2D& g, std::size_t i) {
    return g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m;
}

// The cells that can lie within `radius` of cell i: a box of rows and columns (one cell of margin), visited in
// ascending index order, so that loops restricted to it see the candidates in the same order as a loop over the
// whole grid (identical sums) while skipping the far cells, which the exact distance test would reject anyway.
struct NeighbourBox {
    std::size_t x0, x1, y0, y1;
};
NeighbourBox neighbour_box(const TransportGrid2D& g, std::size_t i, double radius) {
    const long long cx = static_cast<long long>(i % g.nx), cy = static_cast<long long>(i / g.nx);
    const long long rx = static_cast<long long>(std::ceil(radius / g.spacing_east_m)) + 1, ry = static_cast<long long>(std::ceil(radius / g.spacing_north_m)) + 1;
    auto clamp_to = [](long long v, std::size_t n) { return static_cast<std::size_t>(std::max(0LL, std::min(v, static_cast<long long>(n) - 1))); };
    return {clamp_to(cx - rx, g.nx), clamp_to(cx + rx, g.nx), clamp_to(cy - ry, g.ny), clamp_to(cy + ry, g.ny)};
}

// Highest rise of the terrain ahead of the air (downwind half-disc within
// the influence radius); zero when the air faces no obstacle.
double obstacle_ahead(const std::vector<double>& h, const TransportGrid2D& g, std::size_t i, double u, double v, double radius) {
    const double speed = std::hypot(u, v);
    if (speed <= 0.0) return 0.0;
    double rise = 0.0;
    const double e0 = cell_east_of(g, i), n0 = cell_north_of(g, i);
    const NeighbourBox box = neighbour_box(g, i, radius);
    for (std::size_t y = box.y0; y <= box.y1; ++y)
        for (std::size_t j = y * g.nx + box.x0; j <= y * g.nx + box.x1; ++j) {
            if (j == i) continue;
            const double de = cell_east_of(g, j) - e0, dn = cell_north_of(g, j) - n0;
            const double d = std::hypot(de, dn);
            if (d > radius || de * u + dn * v <= 0.0) continue;
            rise = std::max(rise, h[j] - h[i]);
        }
    return rise;
}

double valley_depth(const std::vector<double>& h, const TransportGrid2D& g, std::size_t i, double radius) {
    double sum = 0.0;
    std::size_t n = 0;
    const double e0 = cell_east_of(g, i), n0 = cell_north_of(g, i);
    const NeighbourBox box = neighbour_box(g, i, radius);
    for (std::size_t y = box.y0; y <= box.y1; ++y)
        for (std::size_t j = y * g.nx + box.x0; j <= y * g.nx + box.x1; ++j) {
            if (std::hypot(cell_east_of(g, j) - e0, cell_north_of(g, j) - n0) <= radius) {
                sum += h[j];
                ++n;
            }
        }
    return n ? std::max(0.0, sum / static_cast<double>(n) - h[i]) : 0.0;
}

}  // namespace

double diurnal_surface_tendency_k_s(double elapsed_seconds, const OrographyConfig& config) {
    const double phase = std::sin(2.0 * 3.14159265358979323846 * (elapsed_seconds - config.diurnal_phase_seconds) / 86400.0);
    return phase >= 0.0 ? config.daytime_heating_k_per_s * phase : -config.nocturnal_cooling_k_per_s * (-phase);
}

OrographyField diagnose_orography(const CoupledWindState& state, const CoupledWindConfig& config) {
    validate_coupled_wind_state(state, config);
    const auto& o = config.orography;
    if (!o.enabled) {
        throw std::invalid_argument("orography diagnostics need orography enabled");
    }
    const auto& g = config.grid;
    const std::size_t count = g.nx * g.ny;
    const Slopes slopes = terrain_slopes(o.elevation_m, g);
    OrographyField f;
    f.slope_east = slopes.east;
    f.slope_north = slopes.north;
    f.upslope_lifting_m_s.resize(count);
    f.obstacle_height_m.resize(count);
    f.froude_number.resize(count);
    f.blocked_fraction.resize(count);
    f.valley_depth_m.resize(count);
    f.cross_barrier_wind_m_s.resize(count);
    f.along_barrier_wind_m_s.resize(count);
    f.ridge_orientation_deg.resize(count);
    f.surface_pressure_pa.resize(count);
    f.cold_pool_k.resize(count);
    f.cold_pool.assign(count, 0);
    f.foehn.assign(count, 0);
    f.foehn_warming_k.assign(count, 0.0);
    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i = 0; i < count; ++i) {
        const double u = state.wind_east_m_s[i], v = state.wind_north_m_s[i];
        const double sx = slopes.east[i], sy = slopes.north[i], slope = std::hypot(sx, sy);
        f.upslope_lifting_m_s[i] = u * sx + v * sy;
        f.obstacle_height_m[i] = obstacle_ahead(o.elevation_m, g, i, u, v, o.obstacle_radius_m);
        const double speed = std::hypot(u, v);
        const double upslope_component = slope > 0.0 ? std::max(0.0, (u * sx + v * sy) / slope) : 0.0;
        const double attack = speed > 0.0 ? (upslope_component / speed) * (upslope_component / speed) : 0.0;
        f.froude_number[i] = f.obstacle_height_m[i] > 0.0 ? upslope_component / (o.brunt_vaisala_s * f.obstacle_height_m[i]) : kNaN;
        f.blocked_fraction[i] = f.obstacle_height_m[i] > 0.0 ? std::clamp(1.0 - f.froude_number[i], 0.0, 1.0) * attack : 0.0;
        f.valley_depth_m[i] = valley_depth(o.elevation_m, g, i, o.influence_radius_m);
        if (slope > 0.0) {
            f.cross_barrier_wind_m_s[i] = (u * sx + v * sy) / slope;
            f.along_barrier_wind_m_s[i] = (-u * sy + v * sx) / slope;
            // Contour direction is perpendicular to the gradient; 0 = north-south, 90 = east-west.
            double angle = std::atan2(sx, sy) * 180.0 / 3.14159265358979323846 + 90.0;
            angle = std::fmod(std::fmod(angle, 180.0) + 180.0, 180.0);
            f.ridge_orientation_deg[i] = angle;
        } else {
            f.cross_barrier_wind_m_s[i] = 0.0;
            f.along_barrier_wind_m_s[i] = 0.0;
            f.ridge_orientation_deg[i] = kNaN;
        }
        f.surface_pressure_pa[i] = terrain_surface_pressure(state.pressure_pa[i], o.elevation_m[i], state.air_temperature_k[i]);
        // Neighbourhood temperature reduced to this cell's height with 6.5 K/km.
        double sum = 0.0;
        std::size_t n = 0;
        const double e0 = cell_east_of(g, i), n0 = cell_north_of(g, i);
        const NeighbourBox box = neighbour_box(g, i, o.influence_radius_m);
        for (std::size_t y = box.y0; y <= box.y1; ++y)
            for (std::size_t j = y * g.nx + box.x0; j <= y * g.nx + box.x1; ++j) {
                if (j == i || std::hypot(cell_east_of(g, j) - e0, cell_north_of(g, j) - n0) > o.influence_radius_m) continue;
                sum += state.air_temperature_k[j] + 0.0065 * (o.elevation_m[j] - o.elevation_m[i]);
                ++n;
            }
        const double reference = n ? sum / static_cast<double>(n) : state.air_temperature_k[i];
        const double anomaly = state.air_temperature_k[i] - reference;
        f.cold_pool_k[i] = std::max(0.0, -anomaly);
        if (f.cold_pool_k[i] >= 1.0 && f.valley_depth_m[i] >= 100.0) {
            f.cold_pool[i] = 1;
            ++f.cold_pool_cells;
        }
        const double rh = relative_humidity_fraction(state.air_temperature_k[i], state.specific_humidity_kg_kg[i], f.surface_pressure_pa[i]);
        if (f.upslope_lifting_m_s[i] < -0.05 && anomaly > 1.0 && rh < 0.5) {
            f.foehn[i] = 1;
            f.foehn_warming_k[i] = anomaly;
            ++f.foehn_cells;
        }
    }
    return f;
}

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
    const auto& oro = config.orography;
    Slopes slopes;
    std::vector<double> valley(count, 0.0);
    if (oro.enabled) {
        slopes = terrain_slopes(oro.elevation_m, grid);
        for (std::size_t i = 0; i < count; ++i) valley[i] = valley_depth(oro.elevation_m, grid, i, oro.influence_radius_m);
        if (state.orographic_precipitation_mm.size() != count) state.orographic_precipitation_mm.assign(count, 0.0);
    }
    double elapsed = initial.elapsed_seconds;

    for (std::size_t step = 0; step < config.time.steps; ++step) {
        // 1. Density from the current temperature and humidity.
        const std::vector<double> density = layer_density(state, config);

        // 2. Wind: pressure gradient / density, Coriolis, friction, and
        //    explicit momentum diffusion evaluated on the old wind.
        const WindField* extra = nullptr;
        WindField viscous;
        if (nu > 0.0 || oro.enabled) {
            viscous.east_m_s.assign(count, 0.0);
            viscous.north_m_s.assign(count, 0.0);
            if (nu > 0.0) {
                viscous.east_m_s = laplacian(wind.east_m_s, grid, periodic, inverse_dx2, inverse_dy2);
                viscous.north_m_s = laplacian(wind.north_m_s, grid, periodic, inverse_dx2, inverse_dy2);
                for (std::size_t i = 0; i < count; ++i) {
                    viscous.east_m_s[i] *= nu;
                    viscous.north_m_s[i] *= nu;
                }
            }
            if (oro.enabled) {
                // Blocking: remove the upslope wind component in proportion to
                // 1 - Fr over the blocking time scale (explicit, dt <= tau).
                for (std::size_t i = 0; i < count; ++i) {
                    const double sx = slopes.east[i], sy = slopes.north[i], slope = std::hypot(sx, sy);
                    if (slope <= 0.0) continue;
                    const double u = wind.east_m_s[i], v = wind.north_m_s[i];
                    const double upslope = (u * sx + v * sy) / slope;
                    if (upslope <= 0.0) continue;
                    const double rise = obstacle_ahead(oro.elevation_m, grid, i, u, v, oro.obstacle_radius_m);
                    if (rise <= 0.0) continue;
                    // Cross-barrier Froude number and angle of attack: flow nearly
                    // parallel to a ridge is not blocked.
                    const double speed = std::hypot(u, v);
                    const double froude = upslope / (oro.brunt_vaisala_s * rise);
                    const double attack = speed > 0.0 ? (upslope / speed) * (upslope / speed) : 0.0;
                    const double blocked = std::clamp(1.0 - froude, 0.0, 1.0) * attack;
                    const double rate = blocked / oro.blocking_time_scale_s * upslope;
                    viscous.east_m_s[i] -= rate * sx / slope;
                    viscous.north_m_s[i] -= rate * sy / slope;
                }
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
        const double surface_tendency = oro.enabled ? diurnal_surface_tendency_k_s(elapsed, oro) : 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            double temperature =
                state.air_temperature_k[i] + timestep * (advect_t[i] + kappa_t * diffuse_t[i]);
            double humidity =
                state.specific_humidity_kg_kg[i] + timestep * (advect_q[i] + kappa_q * diffuse_q[i]);
            if (oro.enabled && std::isfinite(temperature) && temperature > 0.0 && humidity >= 0.0 && humidity < 1.0) {
                // Adiabatic change along the slope: saturated ascent follows the
                // pseudo-adiabat (latent heat and expansion included), descent and
                // unsaturated ascent the dry adiabat.
                const double w = wind.east_m_s[i] * slopes.east[i] + wind.north_m_s[i] * slopes.north[i];
                const double p_before = terrain_surface_pressure(state.pressure_pa[i], oro.elevation_m[i], temperature);
                const bool saturated_ascent = w > 0.0 && humidity >= 0.995 * saturation_specific_humidity_kg_kg(temperature, p_before);
                const double lapse = saturated_ascent ? moist_adiabatic_lapse_rate_k_m(temperature, p_before) : dry_adiabatic_lapse_rate_k_m();
                temperature -= lapse * w * timestep;
                // Diurnal surface heating/cooling, both enhanced in valleys: the
                // sheltered, shallow valley air cools faster at night (pooling)
                // and warms faster by day (the pool is eroded).
                temperature += timestep * surface_tendency * (1.0 + oro.valley_pooling_per_km * valley[i] / 1000.0);
                if (temperature > 0.0) {
                    const double p_s = terrain_surface_pressure(state.pressure_pa[i], oro.elevation_m[i], temperature);
                    if (saturated_ascent) {
                        // Latent heat is already in the moist rate: only remove the excess vapour.
                        const double q_s = saturation_specific_humidity_kg_kg(temperature, p_s);
                        if (humidity > q_s) {
                            state.orographic_precipitation_mm[i] +=
                                (humidity - q_s) * moist_air_density_kg_m3(p_s, temperature, q_s) * oro.boundary_layer_depth_m;
                            humidity = q_s;
                        }
                    } else if (humidity > saturation_specific_humidity_kg_kg(temperature, p_s)) {
                        // Supersaturation from advection or cooling: adjustment conserving c_p T + L q (Newton).
                        const double t0 = temperature, q0 = humidity;
                        double t = t0;
                        for (int iteration = 0; iteration < 20; ++iteration) {
                            const double qs = saturation_specific_humidity_kg_kg(t, p_s);
                            const double residual = column::kSpecificHeatDryAirJkgK * (t - t0) - column::kLatentHeatVaporisationJkg * (q0 - qs);
                            const double dqs = (saturation_specific_humidity_kg_kg(t + 0.01, p_s) - qs) / 0.01;
                            const double step = residual / (column::kSpecificHeatDryAirJkgK + column::kLatentHeatVaporisationJkg * dqs);
                            t -= step;
                            if (std::abs(step) < 1.0e-9) break;
                        }
                        const double q_s = saturation_specific_humidity_kg_kg(t, p_s);
                        const double condensed = std::max(0.0, q0 - q_s);
                        temperature = t;
                        humidity = q0 - condensed;
                        if (w > 0.0) {
                            state.orographic_precipitation_mm[i] +=
                                condensed * moist_air_density_kg_m3(p_s, temperature, humidity) * oro.boundary_layer_depth_m;
                        }
                    }
                }
            }
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
        elapsed += timestep;
    }
    state.elapsed_seconds = static_cast<double>(final_elapsed);
    return state;
}

}  // namespace terraflux::weather
