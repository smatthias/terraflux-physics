#include "terraflux/weather/catchment_hydrology.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.81;
constexpr double kWaterDensity = 1000.0;
constexpr double kFillEpsilon = 1.0e-7;       // strict descent across filled flats
constexpr double kLakeTolerance = 0.005;      // filled depth that counts as a lake (m)

// Eight neighbours: index 0 = east, counter-clockwise by 45 degrees.
constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};

void require_finite_nonnegative(double v, const char* name) {
    if (!std::isfinite(v) || v < 0.0) throw std::invalid_argument(std::string("catchment ") + name + " must be finite and >= 0");
}
void require_finite_positive(double v, const char* name) {
    if (!std::isfinite(v) || v <= 0.0) throw std::invalid_argument(std::string("catchment ") + name + " must be finite and > 0");
}
void require_fraction(double v, const char* name) {
    if (!std::isfinite(v) || v < 0.0 || v > 1.0) throw std::invalid_argument(std::string("catchment ") + name + " must lie in [0, 1]");
}

double cell_area(const TransportGrid2D& g) { return g.spacing_east_m * g.spacing_north_m; }

double neighbour_distance(const TransportGrid2D& g, int d) {
    return std::hypot(kDx[d] * g.spacing_east_m, kDy[d] * g.spacing_north_m);
}

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

}  // namespace

const char* flood_class_name(FloodClass c) {
    switch (c) {
        case FloodClass::none: return "none";
        case FloodClass::low: return "low";
        case FloodClass::medium: return "medium";
        case FloodClass::high: return "high";
        case FloodClass::extreme: return "extreme";
    }
    return "none";
}

const char* terminal_kind_name(TerminalKind k) {
    switch (k) {
        case TerminalKind::none: return "none";
        case TerminalKind::ocean: return "ocean";
        case TerminalKind::edge: return "edge";
    }
    return "none";
}

void validate_catchment_config(const CatchmentConfig& c) {
    validate_transport_grid(c.grid);
    if (!std::isfinite(c.timestep_seconds) || c.timestep_seconds <= 0.0 || c.timestep_seconds > 3600.0) {
        throw std::invalid_argument("catchment timestep_seconds must be finite, positive and <= 3600");
    }
    const double min_spacing = std::min(c.grid.spacing_east_m, c.grid.spacing_north_m);
    if (c.timestep_seconds > 2.0 * min_spacing) {
        throw std::invalid_argument("catchment timestep_seconds must not exceed twice the grid spacing in metres (1 m/s channel Courant bound)");
    }
    if (!std::isfinite(c.sea_level_m)) throw std::invalid_argument("catchment sea_level_m must be finite");
    const auto& s = c.soil;
    require_finite_positive(s.regolith_depth_m, "soil.regolith_depth_m");
    require_finite_positive(s.slope_thinning_tan, "soil.slope_thinning_tan");
    if (!std::isfinite(s.minimum_depth_fraction) || s.minimum_depth_fraction <= 0.0 || s.minimum_depth_fraction > 1.0) {
        throw std::invalid_argument("catchment soil.minimum_depth_fraction must lie in (0, 1]");
    }
    if (!std::isfinite(s.porosity) || s.porosity <= 0.0 || s.porosity > 1.0) throw std::invalid_argument("catchment soil.porosity must lie in (0, 1]");
    require_fraction(s.field_capacity_fraction, "soil.field_capacity_fraction");
    require_finite_nonnegative(s.infiltration_max_m_s, "soil.infiltration_max_m_s");
    require_finite_nonnegative(s.infiltration_min_m_s, "soil.infiltration_min_m_s");
    if (s.infiltration_min_m_s > s.infiltration_max_m_s) throw std::invalid_argument("catchment soil.infiltration_min_m_s must not exceed infiltration_max_m_s");
    require_finite_nonnegative(s.percolation_rate_m_s, "soil.percolation_rate_m_s");
    require_finite_nonnegative(s.potential_evapotranspiration_m_s, "soil.potential_evapotranspiration_m_s");
    require_finite_nonnegative(s.detention_storage_m, "soil.detention_storage_m");
    require_fraction(s.initial_saturation, "soil.initial_saturation");
    const auto& g = c.groundwater;
    require_finite_positive(g.aquifer_thickness_m, "groundwater.aquifer_thickness_m");
    if (!std::isfinite(g.specific_yield) || g.specific_yield <= 0.0 || g.specific_yield > 1.0) throw std::invalid_argument("catchment groundwater.specific_yield must lie in (0, 1]");
    require_finite_nonnegative(g.hydraulic_conductivity_m_s, "groundwater.hydraulic_conductivity_m_s");
    require_finite_positive(g.baseflow_time_scale_s, "groundwater.baseflow_time_scale_s");
    require_fraction(g.initial_fill_fraction, "groundwater.initial_fill_fraction");
    const double darcy_number = g.hydraulic_conductivity_m_s * g.aquifer_thickness_m * c.timestep_seconds / (g.specific_yield * min_spacing * min_spacing);
    if (darcy_number > 0.25) {
        throw std::invalid_argument("catchment groundwater explicit stability bound exceeded: K B dt / (S_y L^2) must be <= 0.25");
    }
    const auto& ch = c.channels;
    require_finite_positive(ch.channel_initiation_area_m2, "channels.channel_initiation_area_m2");
    require_finite_positive(ch.manning_overland, "channels.manning_overland");
    require_finite_positive(ch.manning_channel, "channels.manning_channel");
    require_finite_positive(ch.width_coefficient_m, "channels.width_coefficient_m");
    require_finite_nonnegative(ch.width_exponent, "channels.width_exponent");
    require_finite_positive(ch.bankfull_depth_coefficient_m, "channels.bankfull_depth_coefficient_m");
    require_finite_nonnegative(ch.bankfull_depth_exponent, "channels.bankfull_depth_exponent");
    require_finite_positive(ch.minimum_slope, "channels.minimum_slope");
    require_fraction(ch.lake_initial_fill_fraction, "channels.lake_initial_fill_fraction");
    const auto& sn = c.snow;
    if (!std::isfinite(sn.rain_snow_threshold_k) || sn.rain_snow_threshold_k < 250.0 || sn.rain_snow_threshold_k > 290.0) {
        throw std::invalid_argument("catchment snow.rain_snow_threshold_k must lie within 250-290 K");
    }
    require_finite_nonnegative(sn.degree_day_factor_m_k_s, "snow.degree_day_factor_m_k_s");
    require_finite_nonnegative(sn.initial_snow_m, "snow.initial_snow_m");
    const auto& h = c.hazards;
    require_finite_nonnegative(h.erodibility_kg_j, "hazards.erodibility_kg_j");
    require_finite_nonnegative(h.critical_stream_power_w_m2, "hazards.critical_stream_power_w_m2");
    require_finite_nonnegative(h.transport_capacity_kg_j, "hazards.transport_capacity_kg_j");
    require_finite_nonnegative(h.settling_velocity_m_s, "hazards.settling_velocity_m_s");
    require_finite_positive(h.bulk_density_kg_m3, "hazards.bulk_density_kg_m3");
    require_finite_nonnegative(h.cohesion_pa, "hazards.cohesion_pa");
    if (!std::isfinite(h.friction_angle_deg) || h.friction_angle_deg <= 0.0 || h.friction_angle_deg >= 89.0) {
        throw std::invalid_argument("catchment hazards.friction_angle_deg must lie in (0, 89)");
    }
    require_finite_positive(h.soil_unit_weight_n_m3, "hazards.soil_unit_weight_n_m3");
    require_finite_positive(h.water_unit_weight_n_m3, "hazards.water_unit_weight_n_m3");
    if (h.water_unit_weight_n_m3 >= h.soil_unit_weight_n_m3) throw std::invalid_argument("catchment hazards soil unit weight must exceed the water unit weight");
    require_finite_nonnegative(h.minimum_failure_tan, "hazards.minimum_failure_tan");
    require_finite_nonnegative(h.runout_tan, "hazards.runout_tan");
    require_fraction(h.residual_depth_fraction, "hazards.residual_depth_fraction");
    if (!std::isfinite(h.failure_area_fraction) || h.failure_area_fraction <= 0.0 || h.failure_area_fraction > 1.0) {
        throw std::invalid_argument("catchment hazards.failure_area_fraction must lie in (0, 1]");
    }
    if (!std::isfinite(h.restabilisation_factor_of_safety) || h.restabilisation_factor_of_safety < 1.0) {
        throw std::invalid_argument("catchment hazards.restabilisation_factor_of_safety must be >= 1");
    }
    require_finite_nonnegative(h.flood_depth_threshold_m, "hazards.flood_depth_threshold_m");
    require_finite_positive(h.intensity_duration_a_mm_h, "hazards.intensity_duration_a_mm_h");
    if (!std::isfinite(h.intensity_duration_b) || h.intensity_duration_b > 0.0 || h.intensity_duration_b < -2.0) {
        throw std::invalid_argument("catchment hazards.intensity_duration_b must lie in [-2, 0]");
    }
    require_finite_nonnegative(h.dry_spell_reset_s, "hazards.dry_spell_reset_s");
}

double manning_velocity_m_s(double depth_m, double slope, double manning_n) {
    if (depth_m <= 0.0 || slope <= 0.0) return 0.0;
    return std::pow(depth_m, 2.0 / 3.0) * std::sqrt(slope) / manning_n;
}

double infinite_slope_factor_of_safety(double slope_tan, double regolith_depth_m, double saturated_fraction, const CatchmentHazardConfig& h) {
    if (slope_tan <= 0.0 || regolith_depth_m <= 0.0) return std::numeric_limits<double>::infinity();
    const double beta = std::atan(slope_tan), cb = std::cos(beta), sb = std::sin(beta);
    const double m = clamp01(saturated_fraction);
    const double phi = h.friction_angle_deg * kPi / 180.0;
    const double numerator = h.cohesion_pa + (h.soil_unit_weight_n_m3 - m * h.water_unit_weight_n_m3) * regolith_depth_m * cb * cb * std::tan(phi);
    const double denominator = h.soil_unit_weight_n_m3 * regolith_depth_m * sb * cb;
    return numerator / denominator;
}

double intensity_duration_threshold_mm_h(double duration_h, const CatchmentHazardConfig& h) {
    if (duration_h <= 0.0) return std::numeric_limits<double>::infinity();
    return h.intensity_duration_a_mm_h * std::pow(duration_h, h.intensity_duration_b);
}

FloodClass classify_flood(double depth_m, double velocity_m_s, const CatchmentHazardConfig& h) {
    if (!(depth_m > h.flood_depth_threshold_m)) return FloodClass::none;
    const double dv = depth_m * std::max(0.0, velocity_m_s);
    if (depth_m > 2.0 || dv > 1.5) return FloodClass::extreme;
    if (depth_m > 1.0 || dv > 0.6) return FloodClass::high;
    if (depth_m > 0.3 || dv > 0.3) return FloodClass::medium;
    return FloodClass::low;
}

double lake_level_for_volume(const CatchmentLake& lake, const CatchmentNetwork& network, double cell_area_m2, double volume_m3) {
    // Stage-volume from the hypsometry of the lake floor: V(level) = sum max(0, level - z_i) A.
    std::vector<double> floors;
    floors.reserve(lake.cells.size());
    for (std::size_t c : lake.cells) floors.push_back(network.elevation_m[c]);
    std::sort(floors.begin(), floors.end());
    if (volume_m3 <= 0.0) return floors.front();
    double accumulated = 0.0;
    for (std::size_t k = 0; k < floors.size(); ++k) {
        const double next = k + 1 < floors.size() ? floors[k + 1] : std::numeric_limits<double>::infinity();
        const double layer = (next - floors[k]) * static_cast<double>(k + 1) * cell_area_m2;
        if (std::isinf(next) || accumulated + layer >= volume_m3) {
            return floors[k] + (volume_m3 - accumulated) / (static_cast<double>(k + 1) * cell_area_m2);
        }
        accumulated += layer;
    }
    return floors.back();
}

CatchmentNetwork build_catchment_network(const std::vector<double>& z, const CatchmentConfig& config) {
    validate_catchment_config(config);
    const auto& g = config.grid;
    const std::size_t nx = g.nx, ny = g.ny, count = nx * ny;
    if (z.size() != count) throw std::invalid_argument("catchment elevation array must match the grid");
    for (double v : z) if (!std::isfinite(v)) throw std::invalid_argument("catchment elevations must be finite");
    CatchmentNetwork n;
    n.elevation_m = z;
    n.ocean.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) n.ocean[i] = z[i] <= config.sea_level_m ? 1 : 0;

    // Priority-flood depression filling seeded at the ocean and (if open) the domain edges.
    n.filled_elevation_m = z;
    std::vector<unsigned char> closed(count, 0);
    using Entry = std::pair<double, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    auto is_edge = [&](std::size_t i) { const std::size_t x = i % nx, y = i / nx; return x == 0 || y == 0 || x + 1 == nx || y + 1 == ny; };
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i] || (config.open_edges && is_edge(i))) { closed[i] = 1; open.push({z[i], i}); }
    }
    if (open.empty()) throw std::invalid_argument("catchment requires ocean cells (elevation <= sea level) or open edges to drain to");
    while (!open.empty()) {
        const auto [level, c] = open.top();
        open.pop();
        const long cx = static_cast<long>(c % nx), cy = static_cast<long>(c / nx);
        for (int d = 0; d < 8; ++d) {
            const long x = cx + kDx[d], y = cy + kDy[d];
            if (x < 0 || y < 0 || x >= static_cast<long>(nx) || y >= static_cast<long>(ny)) continue;
            const std::size_t k = static_cast<std::size_t>(y) * nx + static_cast<std::size_t>(x);
            if (closed[k]) continue;
            closed[k] = 1;
            n.filled_elevation_m[k] = n.ocean[k] ? z[k] : std::max(z[k], level + kFillEpsilon);
            open.push({n.filled_elevation_m[k], k});
        }
    }

    // D8 receivers on the filled surface.
    n.downstream.assign(count, -1);
    n.flow_direction.assign(count, 8);
    n.terminal.assign(count, TerminalKind::none);
    n.reach_length_m.assign(count, 0.0);
    n.slope.assign(count, config.channels.minimum_slope);
    n.surface_slope_tan.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t x = i % nx, y = i / nx;
        const std::size_t left = x == 0 ? x : x - 1, right = x + 1 == nx ? x : x + 1, south = y == 0 ? y : y - 1, north = y + 1 == ny ? y : y + 1;
        const double ge = (z[y * nx + right] - z[y * nx + left]) / (static_cast<double>(right - left) * g.spacing_east_m);
        const double gn = (z[north * nx + x] - z[south * nx + x]) / (static_cast<double>(north - south) * g.spacing_north_m);
        n.surface_slope_tan[i] = std::hypot(ge, gn);
        if (n.ocean[i]) continue;
        double best = 0.0;
        int best_dir = -1;
        const long cx = static_cast<long>(x), cy = static_cast<long>(y);
        for (int d = 0; d < 8; ++d) {
            const long xx = cx + kDx[d], yy = cy + kDy[d];
            if (xx < 0 || yy < 0 || xx >= static_cast<long>(nx) || yy >= static_cast<long>(ny)) continue;
            const std::size_t k = static_cast<std::size_t>(yy) * nx + static_cast<std::size_t>(xx);
            const double drop = (n.filled_elevation_m[i] - n.filled_elevation_m[k]) / neighbour_distance(g, d);
            if (drop > best) { best = drop; best_dir = d; }
        }
        if (best_dir < 0) {
            if (!(config.open_edges && is_edge(i))) throw std::logic_error("catchment priority flood left a land cell without a receiver");
            n.terminal[i] = TerminalKind::edge;
            n.reach_length_m[i] = 0.5 * std::min(g.spacing_east_m, g.spacing_north_m);
            n.slope[i] = std::max(config.channels.minimum_slope, n.surface_slope_tan[i]);
            continue;
        }
        const std::size_t k = static_cast<std::size_t>(cy + kDy[best_dir]) * nx + static_cast<std::size_t>(cx + kDx[best_dir]);
        n.downstream[i] = static_cast<long>(k);
        n.flow_direction[i] = static_cast<unsigned char>(best_dir);
        n.reach_length_m[i] = neighbour_distance(g, best_dir);
        n.slope[i] = std::max(config.channels.minimum_slope, best);
        if (n.ocean[k]) n.terminal[i] = TerminalKind::ocean;
    }

    // Upstream-first order: receivers always have a strictly lower filled elevation.
    n.topological_order.clear();
    for (std::size_t i = 0; i < count; ++i) if (!n.ocean[i]) n.topological_order.push_back(i);
    std::stable_sort(n.topological_order.begin(), n.topological_order.end(),
                     [&](std::size_t a, std::size_t b) { return n.filled_elevation_m[a] > n.filled_elevation_m[b]; });

    const double area = cell_area(g);
    n.upstream_area_m2.assign(count, 0.0);
    n.strahler_order.assign(count, 0);
    std::vector<int> max_in(count, 0), count_max(count, 0);
    for (std::size_t i : n.topological_order) {
        n.upstream_area_m2[i] += area;
        n.strahler_order[i] = count_max[i] > 1 ? max_in[i] + 1 : std::max(1, max_in[i]);
        const long d = n.downstream[i];
        if (d >= 0 && !n.ocean[static_cast<std::size_t>(d)]) {
            const auto k = static_cast<std::size_t>(d);
            n.upstream_area_m2[k] += n.upstream_area_m2[i];
            if (n.strahler_order[i] > max_in[k]) { max_in[k] = n.strahler_order[i]; count_max[k] = 1; }
            else if (n.strahler_order[i] == max_in[k]) ++count_max[k];
        }
    }
    n.channel.assign(count, 0);
    n.channel_width_m.assign(count, 0.0);
    n.bankfull_depth_m.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i] || n.upstream_area_m2[i] < config.channels.channel_initiation_area_m2) continue;
        n.channel[i] = 1;
        const double km2 = n.upstream_area_m2[i] / 1.0e6;
        n.channel_width_m[i] = config.channels.width_coefficient_m * std::pow(km2, config.channels.width_exponent);
        n.bankfull_depth_m[i] = config.channels.bankfull_depth_coefficient_m * std::pow(km2, config.channels.bankfull_depth_exponent);
    }
    n.channel_target.assign(count, -1);
    n.distance_to_outlet_m.assign(count, 0.0);
    std::vector<std::size_t> terminal_of(count, count);
    for (auto it = n.topological_order.rbegin(); it != n.topological_order.rend(); ++it) {
        const std::size_t i = *it;
        const long d = n.downstream[i];
        const bool ends = d < 0 || n.ocean[static_cast<std::size_t>(d)];
        n.channel_target[i] = n.channel[i] ? static_cast<long>(i) : (ends ? -1 : n.channel_target[static_cast<std::size_t>(d)]);
        n.distance_to_outlet_m[i] = ends ? n.reach_length_m[i] : n.distance_to_outlet_m[static_cast<std::size_t>(d)] + n.reach_length_m[i];
        terminal_of[i] = ends ? i : terminal_of[static_cast<std::size_t>(d)];
    }

    // Lakes: connected filled cells.
    n.lake_id.assign(count, -1);
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i] || n.lake_id[i] >= 0 || n.filled_elevation_m[i] - z[i] <= kLakeTolerance) continue;
        CatchmentLake lake;
        lake.id = static_cast<int>(n.lakes.size());
        std::vector<std::size_t> stack{i};
        n.lake_id[i] = lake.id;
        while (!stack.empty()) {
            const std::size_t c = stack.back();
            stack.pop_back();
            lake.cells.push_back(c);
            const long cx = static_cast<long>(c % nx), cy = static_cast<long>(c / nx);
            for (int d = 0; d < 8; ++d) {
                const long xx = cx + kDx[d], yy = cy + kDy[d];
                if (xx < 0 || yy < 0 || xx >= static_cast<long>(nx) || yy >= static_cast<long>(ny)) continue;
                const std::size_t k = static_cast<std::size_t>(yy) * nx + static_cast<std::size_t>(xx);
                if (n.ocean[k] || n.lake_id[k] >= 0 || n.filled_elevation_m[k] - z[k] <= kLakeTolerance) continue;
                n.lake_id[k] = lake.id;
                stack.push_back(k);
            }
        }
        std::sort(lake.cells.begin(), lake.cells.end());
        std::size_t lowest = lake.cells.front();
        for (std::size_t c : lake.cells) if (n.filled_elevation_m[c] < n.filled_elevation_m[lowest]) lowest = c;
        lake.spill_elevation_m = n.filled_elevation_m[lowest];
        lake.area_m2 = static_cast<double>(lake.cells.size()) * area;
        for (std::size_t c : lake.cells) lake.capacity_m3 += std::max(0.0, lake.spill_elevation_m - z[c]) * area;
        lake.outlet_cell = n.downstream[lowest];
        lake.outlet_kind = n.terminal[lowest];
        n.lakes.push_back(std::move(lake));
    }

    // Basins: cells sharing a terminal cell, largest first.
    std::vector<long> basin_of_terminal(count, -1);
    std::vector<CatchmentBasin> basins;
    for (std::size_t i : n.topological_order) {
        const std::size_t t = terminal_of[i];
        if (basin_of_terminal[t] < 0) {
            basin_of_terminal[t] = static_cast<long>(basins.size());
            CatchmentBasin b;
            b.outlet_cell = t;
            b.terminal = n.terminal[t] == TerminalKind::none ? TerminalKind::ocean : n.terminal[t];
            basins.push_back(b);
        }
        basins[static_cast<std::size_t>(basin_of_terminal[t])].area_m2 += area;
    }
    std::vector<std::size_t> order(basins.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return basins[a].area_m2 > basins[b].area_m2; });
    std::vector<int> new_id(basins.size());
    for (std::size_t r = 0; r < order.size(); ++r) { new_id[order[r]] = static_cast<int>(r); basins[order[r]].id = static_cast<int>(r); }
    n.basin_id.assign(count, -1);
    for (std::size_t i : n.topological_order) n.basin_id[i] = new_id[static_cast<std::size_t>(basin_of_terminal[terminal_of[i]])];
    for (std::size_t r : order) n.basins.push_back(basins[r]);

    n.regolith_depth_m.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i]) continue;
        const double fraction = std::max(config.soil.minimum_depth_fraction, 1.0 - n.surface_slope_tan[i] / config.soil.slope_thinning_tan);
        n.regolith_depth_m[i] = config.soil.regolith_depth_m * std::min(1.0, fraction);
    }
    return n;
}

namespace {

void check_network(const CatchmentNetwork& n, const CatchmentConfig& config) {
    const std::size_t count = config.grid.nx * config.grid.ny;
    if (n.elevation_m.size() != count || n.downstream.size() != count || n.channel.size() != count || n.lake_id.size() != count ||
        n.topological_order.empty() || n.regolith_depth_m.size() != count) {
        throw std::invalid_argument("catchment network does not match the configuration grid");
    }
}

void diagnose(CatchmentState& s, const CatchmentNetwork& n, const CatchmentConfig& c) {
    const std::size_t count = n.elevation_m.size();
    const double area = cell_area(c.grid);
    s.water_table_head_m.assign(count, 0.0);
    s.lake_depth_m.assign(count, 0.0);
    s.factor_of_safety.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i]) { s.water_table_head_m[i] = c.sea_level_m; continue; }
        s.water_table_head_m[i] = n.elevation_m[i] - c.groundwater.aquifer_thickness_m + s.groundwater_m[i] / c.groundwater.specific_yield;
    }
    s.lake_level_m.assign(n.lakes.size(), 0.0);
    for (std::size_t l = 0; l < n.lakes.size(); ++l) {
        s.lake_level_m[l] = lake_level_for_volume(n.lakes[l], n, area, s.lake_volume_m3[l]);
        for (std::size_t cidx : n.lakes[l].cells) s.lake_depth_m[cidx] = std::max(0.0, s.lake_level_m[l] - n.elevation_m[cidx]);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i] || n.lake_id[i] >= 0 || n.surface_slope_tan[i] < c.hazards.minimum_failure_tan) continue;
        const double depth = s.regolith_depth_m[i];
        const double capacity = c.soil.porosity * depth;
        const double from_soil = capacity > 0.0 ? s.soil_m[i] / capacity : 0.0;
        const double from_table = depth > 0.0 ? (s.water_table_head_m[i] - (n.elevation_m[i] - depth)) / depth : 0.0;
        const double fs = infinite_slope_factor_of_safety(n.surface_slope_tan[i], depth, std::max(from_soil, from_table), c.hazards);
        s.factor_of_safety[i] = std::isfinite(fs) ? std::min(fs, 10.0) : 10.0;
    }
}

}  // namespace

double total_storage_m3(const CatchmentState& s, const CatchmentConfig& c) {
    const double area = cell_area(c.grid);
    long double total = 0.0L;
    for (std::size_t i = 0; i < s.surface_m.size(); ++i) {
        total += (static_cast<long double>(s.snow_m[i]) + s.surface_m[i] + s.soil_m[i] + s.groundwater_m[i]) * area + s.channel_m3[i];
    }
    for (double v : s.lake_volume_m3) total += v;
    return static_cast<double>(total);
}

CatchmentState initialize_catchment(const CatchmentNetwork& n, const CatchmentConfig& c) {
    validate_catchment_config(c);
    check_network(n, c);
    const std::size_t count = n.elevation_m.size();
    CatchmentState s;
    s.snow_m.assign(count, 0.0);
    s.surface_m.assign(count, 0.0);
    s.soil_m.assign(count, 0.0);
    s.groundwater_m.assign(count, 0.0);
    s.channel_m3.assign(count, 0.0);
    s.regolith_depth_m = n.regolith_depth_m;
    s.rainfall_rate_m_s.assign(count, 0.0);
    s.overland_velocity_m_s.assign(count, 0.0);
    s.discharge_m3_s.assign(count, 0.0);
    s.channel_depth_m.assign(count, 0.0);
    s.erosion_m.assign(count, 0.0);
    s.deposition_m.assign(count, 0.0);
    s.sediment_flux_kg_s.assign(count, 0.0);
    s.sediment_kg.assign(count, 0.0);
    s.landslide.assign(count, 0);
    s.unstable.assign(count, 0);
    s.storm_duration_s.assign(count, 0.0);
    s.storm_depth_m.assign(count, 0.0);
    s.dry_time_s.assign(count, 0.0);
    s.threshold_exceeded.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i]) continue;
        if (n.lake_id[i] < 0) {
            s.snow_m[i] = c.snow.initial_snow_m;
            s.soil_m[i] = c.soil.initial_saturation * c.soil.porosity * n.regolith_depth_m[i];
        }
        s.groundwater_m[i] = c.groundwater.initial_fill_fraction * c.groundwater.specific_yield * c.groundwater.aquifer_thickness_m;
    }
    s.lake_volume_m3.assign(n.lakes.size(), 0.0);
    for (std::size_t l = 0; l < n.lakes.size(); ++l) s.lake_volume_m3[l] = c.channels.lake_initial_fill_fraction * n.lakes[l].capacity_m3;
    diagnose(s, n, c);
    return s;
}

CatchmentState advance_catchment(const CatchmentState& previous, const CatchmentForcing& forcing, const CatchmentNetwork& n, const CatchmentConfig& c) {
    validate_catchment_config(c);
    check_network(n, c);
    const std::size_t count = n.elevation_m.size();
    if (previous.surface_m.size() != count || previous.soil_m.size() != count || previous.groundwater_m.size() != count ||
        previous.snow_m.size() != count || previous.channel_m3.size() != count || previous.regolith_depth_m.size() != count ||
        previous.lake_volume_m3.size() != n.lakes.size()) {
        throw std::invalid_argument("catchment state does not match the network");
    }
    if (forcing.precipitation_rate_m_s.size() != count || forcing.air_temperature_k.size() != count) {
        throw std::invalid_argument("catchment forcing arrays must match the grid");
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(forcing.precipitation_rate_m_s[i]) || forcing.precipitation_rate_m_s[i] < 0.0 || forcing.precipitation_rate_m_s[i] > 1.0e-3) {
            throw std::invalid_argument("catchment precipitation rate must be finite, >= 0 and <= 1e-3 m/s");
        }
        if (!std::isfinite(forcing.air_temperature_k[i]) || forcing.air_temperature_k[i] < 150.0 || forcing.air_temperature_k[i] > 350.0) {
            throw std::invalid_argument("catchment air temperature must lie within 150-350 K");
        }
    }
    const double dt = c.timestep_seconds, area = cell_area(c.grid);
    const auto& g = c.grid;
    CatchmentState s = previous;
    s.landslide.assign(count, 0);
    if (s.unstable.size() != count) s.unstable.assign(count, 0);
    s.threshold_exceeded.assign(count, 0);
    s.rainfall_rate_m_s = forcing.precipitation_rate_m_s;
    s.overland_velocity_m_s.assign(count, 0.0);
    s.discharge_m3_s.assign(count, 0.0);
    s.channel_depth_m.assign(count, 0.0);
    s.sediment_flux_kg_s.assign(count, 0.0);
    if (s.sediment_kg.size() != count) s.sediment_kg.assign(count, 0.0);
    const double storage_before = total_storage_m3(previous, c);
    long double precip = 0.0L, et = 0.0L, ocean_out = 0.0L, edge_out = 0.0L, infiltration = 0.0L, percolation = 0.0L, baseflow = 0.0L, melt_total = 0.0L, yield_kg = 0.0L;

    std::vector<double> soil_capacity(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) soil_capacity[i] = c.soil.porosity * s.regolith_depth_m[i];
    const double aquifer_capacity = c.groundwater.specific_yield * c.groundwater.aquifer_thickness_m;

    auto deliver_water = [&](long to, double volume_m3) {
        if (volume_m3 <= 0.0) return;
        if (to < 0) { edge_out += volume_m3; return; }
        const auto k = static_cast<std::size_t>(to);
        if (n.ocean[k]) ocean_out += volume_m3;
        else if (n.lake_id[k] >= 0) s.lake_volume_m3[static_cast<std::size_t>(n.lake_id[k])] += volume_m3;
        else if (n.channel[k]) s.channel_m3[k] += volume_m3;
        else s.surface_m[k] += volume_m3 / area;
    };

    // 1. Precipitation, snow and melt.
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i]) continue;
        const double p = forcing.precipitation_rate_m_s[i] * dt, t = forcing.air_temperature_k[i];
        precip += static_cast<long double>(p) * area;
        if (n.lake_id[i] >= 0) { s.lake_volume_m3[static_cast<std::size_t>(n.lake_id[i])] += p * area; continue; }
        if (t < c.snow.rain_snow_threshold_k) s.snow_m[i] += p; else s.surface_m[i] += p;
        if (t > 273.15 && s.snow_m[i] > 0.0) {
            const double melt = std::min(s.snow_m[i], c.snow.degree_day_factor_m_k_s * (t - 273.15) * dt);
            s.snow_m[i] -= melt;
            s.surface_m[i] += melt;
            melt_total += static_cast<long double>(melt) * area;
        }
        // Rainfall intensity-duration bookkeeping.
        const double rate_mm_h = forcing.precipitation_rate_m_s[i] * 3.6e6;
        if (rate_mm_h > 0.1) { s.storm_duration_s[i] += dt; s.storm_depth_m[i] += p; s.dry_time_s[i] = 0.0; }
        else { s.dry_time_s[i] += dt; if (s.dry_time_s[i] >= c.hazards.dry_spell_reset_s) { s.storm_duration_s[i] = 0.0; s.storm_depth_m[i] = 0.0; } }
        const double hours = s.storm_duration_s[i] / 3600.0;
        if (hours >= 0.5) {
            const double mean_intensity = s.storm_depth_m[i] * 1000.0 / hours;
            if (mean_intensity > intensity_duration_threshold_mm_h(hours, c.hazards)) s.threshold_exceeded[i] = 1;
        }
    }

    // 2. Infiltration, evapotranspiration, percolation (land cells outside lakes).
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i]) continue;
        if (n.lake_id[i] >= 0) {
            auto& volume = s.lake_volume_m3[static_cast<std::size_t>(n.lake_id[i])];
            const double evaporated = std::min(volume, c.soil.potential_evapotranspiration_m_s * dt * area);
            volume -= evaporated;
            et += evaporated;
            continue;
        }
        const double saturation = soil_capacity[i] > 0.0 ? clamp01(s.soil_m[i] / soil_capacity[i]) : 1.0;
        const double capacity_rate = c.soil.infiltration_min_m_s + (c.soil.infiltration_max_m_s - c.soil.infiltration_min_m_s) * (1.0 - saturation);
        const double infiltrated = std::max(0.0, std::min({s.surface_m[i], capacity_rate * dt, soil_capacity[i] - s.soil_m[i]}));
        s.surface_m[i] -= infiltrated;
        s.soil_m[i] += infiltrated;
        infiltration += static_cast<long double>(infiltrated) * area;
        double demand = c.soil.potential_evapotranspiration_m_s * dt;
        const double from_surface = std::min(s.surface_m[i], demand);
        s.surface_m[i] -= from_surface;
        demand -= from_surface;
        const double stress = soil_capacity[i] > 0.0 ? clamp01(s.soil_m[i] / (c.soil.field_capacity_fraction * soil_capacity[i] + 1.0e-12)) : 0.0;
        const double from_soil = std::min(s.soil_m[i], demand * stress);
        s.soil_m[i] -= from_soil;
        et += (static_cast<long double>(from_surface) + from_soil) * area;
        const double held = c.soil.field_capacity_fraction * soil_capacity[i];
        const double drainable = s.soil_m[i] - held;
        if (drainable > 0.0) {
            const double span = std::max(soil_capacity[i] - held, 1.0e-12);
            const double percolated = std::min({drainable, c.soil.percolation_rate_m_s * dt * (drainable / span), std::max(0.0, aquifer_capacity - s.groundwater_m[i])});
            s.soil_m[i] -= percolated;
            s.groundwater_m[i] += percolated;
            percolation += static_cast<long double>(percolated) * area;
        }
    }

    // 3. Groundwater: explicit Darcy exchange on the start-of-step heads, baseflow, springs.
    {
        std::vector<double> head(count, c.sea_level_m), delta(count, 0.0), outgoing(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) if (!n.ocean[i]) head[i] = n.elevation_m[i] - c.groundwater.aquifer_thickness_m + s.groundwater_m[i] / c.groundwater.specific_yield;
        struct Transfer { std::size_t from; long to; double depth; };
        std::vector<Transfer> transfers;
        auto edge = [&](std::size_t a, std::size_t b, double along, double across) {
            if (n.ocean[a] && n.ocean[b]) return;
            if (head[a] == head[b]) return;
            if (head[a] < head[b]) std::swap(a, b);
            if (n.ocean[a]) return;  // no seawater intrusion
            const double thickness = std::min(c.groundwater.aquifer_thickness_m, s.groundwater_m[a] / c.groundwater.specific_yield);
            const double depth = c.groundwater.hydraulic_conductivity_m_s * thickness * (head[a] - head[b]) / along * dt / along;
            (void)across;
            if (depth <= 0.0) return;
            transfers.push_back({a, n.ocean[b] ? -1L : static_cast<long>(b), depth});
            outgoing[a] += depth;
        };
        for (std::size_t y = 0; y < g.ny; ++y) for (std::size_t x = 0; x < g.nx; ++x) {
            const std::size_t i = y * g.nx + x;
            if (x + 1 < g.nx) edge(i, i + 1, g.spacing_east_m, g.spacing_north_m);
            if (y + 1 < g.ny) edge(i, i + g.nx, g.spacing_north_m, g.spacing_east_m);
        }
        for (const auto& t : transfers) {
            const double fraction = outgoing[t.from] > s.groundwater_m[t.from] ? s.groundwater_m[t.from] / outgoing[t.from] : 1.0;
            const double amount = t.depth * fraction;
            delta[t.from] -= amount;
            if (t.to < 0) ocean_out += static_cast<long double>(amount) * area; else delta[static_cast<std::size_t>(t.to)] += amount;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (n.ocean[i]) continue;
            s.groundwater_m[i] = std::max(0.0, s.groundwater_m[i] + delta[i]);
            // Linear-reservoir baseflow to the nearest downstream channel.
            const double base = s.groundwater_m[i] * std::min(1.0, dt / c.groundwater.baseflow_time_scale_s);
            s.groundwater_m[i] -= base;
            baseflow += static_cast<long double>(base) * area;
            const long target = n.lake_id[i] >= 0 ? static_cast<long>(i) : n.channel_target[i];
            if (target >= 0) deliver_water(target, base * area);
            else deliver_water(n.downstream[n.basins[static_cast<std::size_t>(n.basin_id[i])].outlet_cell], base * area);  // no channel: straight to the basin outlet
            // Springs where the aquifer is full.
            if (s.groundwater_m[i] > aquifer_capacity) {
                const double excess = s.groundwater_m[i] - aquifer_capacity;
                s.groundwater_m[i] = aquifer_capacity;
                if (n.lake_id[i] >= 0) s.lake_volume_m3[static_cast<std::size_t>(n.lake_id[i])] += excess * area; else s.surface_m[i] += excess;
            }
        }
    }

    // 4. Routing on start-of-routing stores (Jacobi): overland then channel, one cell per step.
    std::vector<double> overland_out(count, 0.0), channel_out(count, 0.0), travel_fraction(count, 0.0), unit_discharge(count, 0.0), flow_depth(count, 0.0);
    for (std::size_t i : n.topological_order) {
        if (n.lake_id[i] >= 0) continue;
        if (n.channel[i]) {
            // Floodplain water returns while the channel is below bankfull.
            const double bankfull_volume = n.channel_width_m[i] * n.reach_length_m[i] * n.bankfull_depth_m[i];
            const double available = std::max(0.0, s.surface_m[i] - c.soil.detention_storage_m);
            const double returned = std::min(available, std::max(0.0, bankfull_volume - s.channel_m3[i]) / area);
            s.surface_m[i] -= returned;
            s.channel_m3[i] += returned * area;
        }
        const double available = std::max(0.0, s.surface_m[i] - c.soil.detention_storage_m);
        const double v = manning_velocity_m_s(s.surface_m[i], n.slope[i], c.channels.manning_overland);
        s.overland_velocity_m_s[i] = v;
        const double f = std::min(1.0, v * dt / n.reach_length_m[i]);
        overland_out[i] = available * f;
        if (n.channel[i]) {
            const double depth = s.channel_m3[i] / (n.channel_width_m[i] * n.reach_length_m[i]);
            const double vc = manning_velocity_m_s(depth, n.slope[i], c.channels.manning_channel);
            const double q = vc * n.channel_width_m[i] * depth;
            const double fc = std::min(1.0, vc * dt / n.reach_length_m[i]);
            channel_out[i] = std::min(s.channel_m3[i], q * dt);
            travel_fraction[i] = s.channel_m3[i] > 0.0 ? channel_out[i] / s.channel_m3[i] : fc;
            unit_discharge[i] = channel_out[i] / dt / n.channel_width_m[i];
            flow_depth[i] = depth;
            s.channel_depth_m[i] = depth;
            s.discharge_m3_s[i] = channel_out[i] / dt;
        } else {
            travel_fraction[i] = f;
            unit_discharge[i] = v * s.surface_m[i];
            flow_depth[i] = s.surface_m[i];
        }
    }
    for (std::size_t i : n.topological_order) {
        if (n.lake_id[i] >= 0) continue;
        const long to = n.downstream[i];
        if (overland_out[i] > 0.0) {
            // Floodplain water on a channel cell is conveyed downstream like any overland flow.
            s.surface_m[i] -= overland_out[i];
            deliver_water(to, overland_out[i] * area);
        }
        if (channel_out[i] > 0.0) { s.channel_m3[i] -= channel_out[i]; deliver_water(to, channel_out[i]); }
    }
    // Overbank flow on to the floodplain.
    for (std::size_t i : n.topological_order) {
        if (!n.channel[i] || n.lake_id[i] >= 0) continue;
        const double bankfull_volume = n.channel_width_m[i] * n.reach_length_m[i] * n.bankfull_depth_m[i];
        if (s.channel_m3[i] > bankfull_volume) {
            s.surface_m[i] += (s.channel_m3[i] - bankfull_volume) / area;
            s.channel_m3[i] = bankfull_volume;
        }
    }

    // 5. Lakes overflow at their spill level, upstream lakes first.
    {
        std::vector<std::size_t> lake_order(n.lakes.size());
        std::iota(lake_order.begin(), lake_order.end(), 0);
        std::stable_sort(lake_order.begin(), lake_order.end(), [&](std::size_t a, std::size_t b) { return n.lakes[a].spill_elevation_m > n.lakes[b].spill_elevation_m; });
        for (std::size_t l : lake_order) {
            const double excess = s.lake_volume_m3[l] - n.lakes[l].capacity_m3;
            if (excess <= 0.0) continue;
            s.lake_volume_m3[l] = n.lakes[l].capacity_m3;
            deliver_water(n.lakes[l].outlet_cell, excess);
        }
    }

    // 6. Sediment: stream-power detachment and capacity-limited transport with the water.
    {
        std::vector<double> incoming(count, 0.0);
        const double cell_width = std::sqrt(area);
        for (std::size_t i : n.topological_order) {
            if (n.lake_id[i] >= 0) continue;
            const double omega = kWaterDensity * kGravity * unit_discharge[i] * n.slope[i];
            const double excess = std::max(0.0, omega - c.hazards.critical_stream_power_w_m2);
            const double width = n.channel[i] ? n.channel_width_m[i] : cell_width;
            const double effective_area = n.channel[i] ? n.channel_width_m[i] * n.reach_length_m[i] : area;
            const double detachment = c.hazards.erodibility_kg_j * excess * dt * effective_area;
            const double capacity = c.hazards.transport_capacity_kg_j * excess * width * dt;
            double load = s.sediment_kg[i];
            if (load < capacity) {
                const double eroded = std::min(detachment, capacity - load);
                load += eroded;
                s.erosion_m[i] += eroded / (c.hazards.bulk_density_kg_m3 * area);
            } else if (load > capacity) {
                const double settle = std::min(1.0, c.hazards.settling_velocity_m_s * dt / std::max(flow_depth[i], 1.0e-3));
                const double deposited = (load - capacity) * settle;
                load -= deposited;
                s.deposition_m[i] += deposited / (c.hazards.bulk_density_kg_m3 * area);
            }
            const double moved = load * travel_fraction[i];
            s.sediment_flux_kg_s[i] = moved / dt;
            s.sediment_kg[i] = load - moved;
            const long to = n.downstream[i];
            if (moved <= 0.0) continue;
            if (to < 0) yield_kg += moved;
            else {
                const auto k = static_cast<std::size_t>(to);
                if (n.ocean[k]) yield_kg += moved;
                else if (n.lake_id[k] >= 0) s.deposition_m[k] += moved / (c.hazards.bulk_density_kg_m3 * area);
                else incoming[k] += moved;
            }
        }
        for (std::size_t i = 0; i < count; ++i) s.sediment_kg[i] += incoming[i];
    }

    // 7. Slope stability and debris runout.
    s.elapsed_seconds = previous.elapsed_seconds + dt;
    diagnose(s, n, c);
    for (std::size_t i : n.topological_order) {
        if (n.lake_id[i] >= 0 || n.surface_slope_tan[i] < c.hazards.minimum_failure_tan) continue;
        const double fs = s.factor_of_safety[i];
        if (fs >= c.hazards.restabilisation_factor_of_safety) s.unstable[i] = 0;
        if (!(fs < 1.0)) continue;
        const double residual = c.hazards.residual_depth_fraction * n.regolith_depth_m[i];
        if (s.unstable[i] || s.regolith_depth_m[i] <= residual + 1.0e-9) continue;
        // The unstable share of the cell mobilises the regolith above the residual depth.
        const double failed_depth = (s.regolith_depth_m[i] - residual) * c.hazards.failure_area_fraction;
        const double volume = failed_depth * area;
        // Pore water of the failed layer is released to the surface.
        const double water_released = s.soil_m[i] * (failed_depth / s.regolith_depth_m[i]);
        s.soil_m[i] -= water_released;
        s.surface_m[i] += water_released;
        s.regolith_depth_m[i] -= failed_depth;
        s.landslide[i] = 1;
        s.unstable[i] = 1;
        LandslideEvent event;
        event.elapsed_seconds = s.elapsed_seconds;
        event.cell = i;
        event.factor_of_safety = fs;
        event.volume_m3 = volume;
        std::size_t current = i;
        for (std::size_t guard = 0; guard <= count; ++guard) {
            const long next = n.downstream[current];
            if (next < 0) { event.deposited_in = "edge"; event.runout_cell = -1; yield_kg += volume * c.hazards.bulk_density_kg_m3; break; }
            const auto k = static_cast<std::size_t>(next);
            event.runout_length_m += n.reach_length_m[current];
            if (n.ocean[k]) { event.deposited_in = "ocean"; event.runout_cell = next; yield_kg += volume * c.hazards.bulk_density_kg_m3; break; }
            if (n.lake_id[k] >= 0 || n.channel[k] || n.slope[k] < c.hazards.runout_tan) {
                event.deposited_in = n.lake_id[k] >= 0 ? "lake" : n.channel[k] ? "channel" : "slope";
                event.runout_cell = next;
                s.deposition_m[k] += volume / area;
                break;
            }
            current = k;
        }
        s.totals.landslide_count += 1;
        s.totals.landslide_volume_m3 += volume;
        s.events.push_back(event);
    }
    diagnose(s, n, c);

    // 8. Budget.
    s.totals.precipitation_m3 += static_cast<double>(precip);
    s.totals.evapotranspiration_m3 += static_cast<double>(et);
    s.totals.ocean_discharge_m3 += static_cast<double>(ocean_out);
    s.totals.edge_outflow_m3 += static_cast<double>(edge_out);
    s.totals.infiltration_m3 += static_cast<double>(infiltration);
    s.totals.percolation_m3 += static_cast<double>(percolation);
    s.totals.baseflow_m3 += static_cast<double>(baseflow);
    s.totals.snowmelt_m3 += static_cast<double>(melt_total);
    s.totals.sediment_yield_kg += static_cast<double>(yield_kg);
    s.step_precipitation_m3 = static_cast<double>(precip);
    const double storage_after = total_storage_m3(s, c);
    s.balance_error_m3 = static_cast<double>(static_cast<long double>(storage_after) - storage_before - precip + et + ocean_out + edge_out);
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(s.surface_m[i]) || !std::isfinite(s.soil_m[i]) || !std::isfinite(s.groundwater_m[i]) || !std::isfinite(s.channel_m3[i]) ||
            s.surface_m[i] < -1.0e-12 || s.soil_m[i] < -1.0e-12 || s.groundwater_m[i] < -1.0e-12 || s.channel_m3[i] < -1.0e-9) {
            throw std::runtime_error("catchment stores became non-finite or negative");
        }
    }
    return s;
}

}  // namespace terraflux::weather
