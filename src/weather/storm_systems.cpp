#include "terraflux/weather/storm_systems.hpp"

#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/pressure_wind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

constexpr double kSeaWaterDensity = 1025.0;
constexpr double kE = 2.718281828459045;

void require_positive(double value, const char* message) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(message);
    }
}

void require_non_negative(double value, const char* message) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(message);
    }
}

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
}

double cell_east(const TransportGrid2D& g, std::size_t i) {
    return g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m;
}

double cell_north(const TransportGrid2D& g, std::size_t i) {
    return g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m;
}

// Cell containing a point, or count when outside the domain.
std::size_t cell_at(const TransportGrid2D& g, double east, double north) {
    const double fx = (east - g.origin_east_m) / g.spacing_east_m;
    const double fy = (north - g.origin_north_m) / g.spacing_north_m;
    if (fx < 0.0 || fy < 0.0 || fx >= static_cast<double>(g.nx) || fy >= static_cast<double>(g.ny)) {
        return g.nx * g.ny;
    }
    return at(static_cast<std::size_t>(fx), static_cast<std::size_t>(fy), g.nx);
}

struct Gradient {
    std::vector<double> east, north;
};

Gradient gradient_of(const std::vector<double>& field, const TransportGrid2D& g) {
    Gradient out;
    out.east.resize(field.size());
    out.north.resize(field.size());
    for (std::size_t y = 0; y < g.ny; ++y) {
        const std::size_t south = y == 0 ? y : y - 1, north = y + 1 == g.ny ? y : y + 1;
        for (std::size_t x = 0; x < g.nx; ++x) {
            const std::size_t west = x == 0 ? x : x - 1, east = x + 1 == g.nx ? x : x + 1;
            out.east[at(x, y, g.nx)] = (field[at(east, y, g.nx)] - field[at(west, y, g.nx)]) /
                                       (static_cast<double>(east - west) * g.spacing_east_m);
            out.north[at(x, y, g.nx)] = (field[at(x, north, g.nx)] - field[at(x, south, g.nx)]) /
                                        (static_cast<double>(north - south) * g.spacing_north_m);
        }
    }
    return out;
}

double density_at(const CoupledWindState& s, std::size_t i) {
    return moist_air_density_kg_m3(s.pressure_pa[i], s.air_temperature_k[i], s.specific_humidity_kg_kg[i]);
}

void update_pressure_and_derived(TropicalCyclone& storm, const CoupledWindState& state, std::size_t cell,
                                 const StormSystemsConfig& config) {
    storm.environment_pressure_pa = state.pressure_pa[cell];
    const double drop = holland_pressure_drop_pa(storm.max_wind_m_s, density_at(state, cell), config.holland_b);
    storm.central_pressure_pa = storm.environment_pressure_pa - drop;
    storm.category = saffir_simpson_category(storm.max_wind_m_s);
    storm.surge_estimate_m = surge_estimate_m(drop, storm.max_wind_m_s, config.surge_wind_coefficient_s2_m);
    storm.core_rain_rate_mm_h = config.rain_core_factor_mm_h_per_m_s * storm.max_wind_m_s;
}

}  // namespace

void validate_storm_systems_config(const StormSystemsConfig& c) {
    validate_column_config(c.column);
    require_positive(c.enthalpy_exchange_ratio, "enthalpy exchange ratio must be positive");
    if (!std::isfinite(c.surface_wind_factor) || c.surface_wind_factor <= 0.0 || c.surface_wind_factor > 1.0) {
        throw std::invalid_argument("surface wind factor must be within (0, 1]");
    }
    require_positive(c.sst_threshold_k, "SST threshold must be positive");
    require_positive(c.minimum_coriolis_s, "minimum Coriolis parameter must be positive");
    require_positive(c.mid_level_height_m, "mid-level height must be positive");
    require_positive(c.shear_layer_depth_m, "shear layer depth must be positive");
    require_positive(c.shear_limit_m_s, "shear limit must be positive");
    require_non_negative(c.genesis_gpi_threshold, "genesis threshold must not be negative");
    require_positive(c.genesis_separation_m, "genesis separation must be positive");
    require_positive(c.genesis_max_wind_m_s, "genesis wind must be positive");
    require_positive(c.intensification_time_scale_s, "intensification time scale must be positive");
    require_positive(c.decay_alpha_per_s, "decay rate must be positive");
    require_positive(c.decay_background_wind_m_s, "decay background wind must be positive");
    if (!std::isfinite(c.landfall_reduction) || c.landfall_reduction <= 0.0 || c.landfall_reduction > 1.0) {
        throw std::invalid_argument("landfall reduction must be within (0, 1]");
    }
    require_positive(c.dissipation_wind_m_s, "dissipation wind must be positive");
    require_positive(c.holland_b, "Holland B must be positive");
    require_positive(c.steering_radius_m, "steering radius must be positive");
    require_non_negative(c.steering_factor, "steering factor must not be negative");
    require_non_negative(c.beta_drift_west_m_s, "beta drift must not be negative");
    require_non_negative(c.beta_drift_poleward_m_s, "beta drift must not be negative");
    require_non_negative(c.merge_distance_m, "merge distance must not be negative");
    if (!std::isfinite(c.induction_factor) || c.induction_factor < 0.0 || c.induction_factor > 1.0) {
        throw std::invalid_argument("induction factor must be within [0, 1]");
    }
    require_non_negative(c.genesis_edge_margin_m, "genesis edge margin must not be negative");
    require_non_negative(c.surge_wind_coefficient_s2_m, "surge coefficient must not be negative");
    require_non_negative(c.rain_core_factor_mm_h_per_m_s, "rain core factor must not be negative");
    require_positive(c.substep_seconds, "sub-step must be positive");
    if (c.max_storms == 0) {
        throw std::invalid_argument("at least one storm must be allowed");
    }
}

void validate_surface(const SurfaceConfig& surface, const TransportGrid2D& grid) {
    validate_transport_grid(grid);
    const std::size_t count = grid.nx * grid.ny;
    if (surface.land.size() != count || surface.sea_surface_temperature_k.size() != count) {
        throw std::invalid_argument("surface land mask and SST must match the grid cell count");
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (surface.land[i] > 1) {
            throw std::invalid_argument("land mask values must be 0 or 1");
        }
        if (!std::isfinite(surface.sea_surface_temperature_k[i]) || surface.sea_surface_temperature_k[i] <= 0.0) {
            throw std::invalid_argument("sea-surface temperature must be finite and positive");
        }
    }
}

double potential_intensity_m_s(
    double sst_k, double air_temperature_k, double specific_humidity_kg_kg, double pressure_pa,
    double outflow_temperature_k, const StormSystemsConfig& config) {
    if (!std::isfinite(sst_k) || !std::isfinite(outflow_temperature_k) || outflow_temperature_k <= 0.0) {
        throw std::invalid_argument("potential intensity needs finite temperatures");
    }
    if (sst_k < config.sst_threshold_k || sst_k <= outflow_temperature_k) {
        return 0.0;
    }
    const double cp = column::kSpecificHeatDryAirJkgK, l = column::kLatentHeatVaporisationJkg;
    const double k_sea = cp * sst_k + l * saturation_specific_humidity_kg_kg(sst_k, pressure_pa);
    const double k_air = cp * air_temperature_k + l * specific_humidity_kg_kg;
    const double disequilibrium = std::max(0.0, k_sea - k_air);
    const double efficiency = (sst_k - outflow_temperature_k) / outflow_temperature_k;
    return config.surface_wind_factor * std::sqrt(config.enthalpy_exchange_ratio * efficiency * disequilibrium);
}

double genesis_potential_index(double absolute_vorticity_s, double mid_level_rh, double potential_intensity, double shear) {
    if (!std::isfinite(absolute_vorticity_s) || !std::isfinite(mid_level_rh) || !std::isfinite(potential_intensity) ||
        !std::isfinite(shear) || shear < 0.0 || potential_intensity < 0.0 || mid_level_rh < 0.0) {
        throw std::invalid_argument("genesis potential index needs finite, non-negative inputs");
    }
    const double rh_percent = std::min(100.0, 100.0 * mid_level_rh);
    return std::pow(std::abs(1.0e5 * absolute_vorticity_s), 1.5) * std::pow(rh_percent / 50.0, 3.0) *
           std::pow(potential_intensity / 70.0, 3.0) / ((1.0 + 0.1 * shear) * (1.0 + 0.1 * shear));
}

double holland_pressure_drop_pa(double max_wind_m_s, double air_density_kg_m3, double holland_b) {
    if (!std::isfinite(max_wind_m_s) || max_wind_m_s < 0.0 || !std::isfinite(air_density_kg_m3) || air_density_kg_m3 <= 0.0 ||
        !std::isfinite(holland_b) || holland_b <= 0.0) {
        throw std::invalid_argument("Holland relation needs non-negative wind, positive density and B");
    }
    return air_density_kg_m3 * kE * max_wind_m_s * max_wind_m_s / holland_b;
}

double surge_estimate_m(double pressure_drop_pa, double max_wind_m_s, double wind_coefficient_s2_m) {
    if (!std::isfinite(pressure_drop_pa) || pressure_drop_pa < 0.0 || !std::isfinite(max_wind_m_s) || max_wind_m_s < 0.0) {
        throw std::invalid_argument("surge estimate needs non-negative pressure drop and wind");
    }
    return pressure_drop_pa / (kSeaWaterDensity * column::kGravityMS2) + wind_coefficient_s2_m * max_wind_m_s * max_wind_m_s;
}

std::string saffir_simpson_category(double v) {
    if (!std::isfinite(v) || v < 0.0) {
        throw std::invalid_argument("category needs a non-negative wind");
    }
    if (v < 17.0) return "depression";
    if (v < 33.0) return "tropical_storm";
    if (v < 43.0) return "category_1";
    if (v < 50.0) return "category_2";
    if (v < 58.0) return "category_3";
    if (v < 70.0) return "category_4";
    return "category_5";
}

double kaplan_demaria_wind_m_s(double landfall_wind_m_s, double hours_over_land, const StormSystemsConfig& config) {
    if (!std::isfinite(landfall_wind_m_s) || landfall_wind_m_s < 0.0 || !std::isfinite(hours_over_land) || hours_over_land < 0.0) {
        throw std::invalid_argument("decay needs non-negative wind and time");
    }
    return config.decay_background_wind_m_s +
           (config.landfall_reduction * landfall_wind_m_s - config.decay_background_wind_m_s) *
               std::exp(-config.decay_alpha_per_s * 3600.0 * hours_over_land);
}

StormEnvironment diagnose_storm_environment(
    const CoupledWindState& state, const TransportGrid2D& grid, const SurfaceConfig& surface,
    const StormSystemsConfig& config) {
    validate_storm_systems_config(config);
    validate_surface(surface, grid);
    const std::size_t count = grid.nx * grid.ny;
    if (state.pressure_pa.size() != count || state.air_temperature_k.size() != count ||
        state.specific_humidity_kg_kg.size() != count || state.wind_east_m_s.size() != count ||
        state.wind_north_m_s.size() != count) {
        throw std::invalid_argument("storm environment needs a full coupled state on the grid");
    }
    AtmosphereConfig coriolis_config;
    coriolis_config.coriolis_mode = "f_plane";
    const auto f = coriolis_parameter(grid, coriolis_config);
    const Gradient t = gradient_of(state.air_temperature_k, grid);
    const Gradient u = gradient_of(state.wind_east_m_s, grid);
    const Gradient v = gradient_of(state.wind_north_m_s, grid);

    StormEnvironment e;
    e.potential_intensity_m_s.assign(count, 0.0);
    e.genesis_potential_index.assign(count, 0.0);
    e.vertical_shear_m_s.resize(count);
    e.relative_vorticity_s.resize(count);
    e.absolute_vorticity_s.resize(count);
    e.mid_level_relative_humidity.resize(count);
    e.outflow_temperature_k.resize(count);
    e.enthalpy_disequilibrium_j_kg.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        e.relative_vorticity_s[i] = v.east[i] - u.north[i];
        e.absolute_vorticity_s[i] = f[i] + e.relative_vorticity_s[i];
        // Thermal wind: dV/dz = (g / (f T)) |grad T| over the shear layer.
        const double f_eff = std::max(std::abs(f[i]), config.minimum_coriolis_s);
        e.vertical_shear_m_s[i] = column::kGravityMS2 / (f_eff * state.air_temperature_k[i]) *
                                  std::hypot(t.east[i], t.north[i]) * config.shear_layer_depth_m;
        const auto column = build_hydrostatic_column(
            state.pressure_pa[i], state.air_temperature_k[i], state.specific_humidity_kg_kg[i], config.column);
        const double z_mid = std::min(config.mid_level_height_m, column.interface_height_m.back());
        const double t_mid = column_temperature_at_height_k(column, z_mid);
        const double q_mid = column_humidity_at_height_kg_kg(column, z_mid);
        e.mid_level_relative_humidity[i] = std::min(1.0, relative_humidity_fraction(t_mid, q_mid, column_pressure_at_height_pa(column, z_mid)));
        const double z_out = std::min(config.column.tropopause_height_m, column.interface_height_m.back());
        e.outflow_temperature_k[i] = column_temperature_at_height_k(column, z_out);
        if (surface.land[i] == 0 && std::abs(f[i]) >= config.minimum_coriolis_s) {
            const double sst = surface.sea_surface_temperature_k[i];
            e.potential_intensity_m_s[i] = potential_intensity_m_s(
                sst, state.air_temperature_k[i], state.specific_humidity_kg_kg[i], state.pressure_pa[i],
                e.outflow_temperature_k[i], config);
            if (sst >= config.sst_threshold_k) {
                const double cp = column::kSpecificHeatDryAirJkgK, l = column::kLatentHeatVaporisationJkg;
                e.enthalpy_disequilibrium_j_kg[i] = std::max(0.0, cp * sst + l * saturation_specific_humidity_kg_kg(sst, state.pressure_pa[i]) -
                                                             (cp * state.air_temperature_k[i] + l * state.specific_humidity_kg_kg[i]));
            }
            e.genesis_potential_index[i] = genesis_potential_index(
                e.absolute_vorticity_s[i], e.mid_level_relative_humidity[i], e.potential_intensity_m_s[i], e.vertical_shear_m_s[i]);
        }
    }
    return e;
}

void advance_storm_systems(
    StormSystemsState& storms, const std::vector<StormSeed>& seeds,
    const CoupledWindState& state, const TransportGrid2D& grid, const SurfaceConfig& surface,
    const StormEnvironment& environment, const StormSystemsConfig& config,
    double elapsed_seconds, double duration_seconds) {
    validate_storm_systems_config(config);
    validate_surface(surface, grid);
    const std::size_t count = grid.nx * grid.ny;
    if (environment.potential_intensity_m_s.size() != count || state.wind_east_m_s.size() != count) {
        throw std::invalid_argument("storm advance needs an environment and state on the grid");
    }
    if (!std::isfinite(duration_seconds) || duration_seconds < 0.0) {
        throw std::invalid_argument("duration must be finite and non-negative");
    }
    AtmosphereConfig coriolis_config;
    coriolis_config.coriolis_mode = "f_plane";
    const auto f = coriolis_parameter(grid, coriolis_config);
    const double hemisphere = f.empty() || f[0] >= 0.0 ? 1.0 : -1.0;

    const auto distance = [](const TropicalCyclone& a, double east, double north) {
        return std::hypot(a.east_m - east, a.north_m - north);
    };
    const auto add_storm = [&](double east, double north, double wind, double radius, const std::string& kind) {
        const std::size_t cell = cell_at(grid, east, north);
        if (cell == count) {
            throw std::invalid_argument("storm seed lies outside the grid");
        }
        TropicalCyclone storm;
        storm.id = storms.next_id++;
        storm.east_m = east;
        storm.north_m = north;
        storm.max_wind_m_s = wind;
        storm.radius_max_wind_m = radius;
        storm.over_land = surface.land[cell] != 0;
        storm.potential_intensity_m_s = environment.potential_intensity_m_s[cell];
        update_pressure_and_derived(storm, state, cell, config);
        storm.track.emplace_back(east, north);
        storms.storms.push_back(storm);
        storms.events.push_back({kind, elapsed_seconds, storm.id, 0, east, north, wind, storm.category, 0.0});
    };
    for (const StormSeed& seed : seeds) {
        if (!std::isfinite(seed.east_m) || !std::isfinite(seed.north_m) || !std::isfinite(seed.max_wind_m_s) ||
            seed.max_wind_m_s <= 0.0 || !std::isfinite(seed.radius_max_wind_m) || seed.radius_max_wind_m <= 0.0) {
            throw std::invalid_argument("storm seed needs finite position, positive wind and radius");
        }
        if (storms.storms.size() < config.max_storms) {
            add_storm(seed.east_m, seed.north_m, seed.max_wind_m_s, seed.radius_max_wind_m, "seed");
        }
    }

    const std::size_t substeps = duration_seconds > 0.0
        ? static_cast<std::size_t>(std::ceil(duration_seconds / config.substep_seconds)) : 0;
    const double dt = substeps > 0 ? duration_seconds / static_cast<double>(substeps) : 0.0;
    double now = elapsed_seconds;
    for (std::size_t step = 0; step < substeps; ++step) {
        // Genesis at the most favourable ocean cell.
        if (storms.storms.size() < config.max_storms) {
            std::size_t best = count;
            double best_gpi = config.genesis_gpi_threshold;
            for (std::size_t i = 0; i < count; ++i) {
                if (surface.land[i] != 0 || environment.genesis_potential_index[i] < best_gpi) continue;
                const double east = cell_east(grid, i), north = cell_north(grid, i);
                if (east - grid.origin_east_m < config.genesis_edge_margin_m || north - grid.origin_north_m < config.genesis_edge_margin_m ||
                    grid.origin_east_m + static_cast<double>(grid.nx) * grid.spacing_east_m - east < config.genesis_edge_margin_m ||
                    grid.origin_north_m + static_cast<double>(grid.ny) * grid.spacing_north_m - north < config.genesis_edge_margin_m) continue;
                bool isolated = true;
                for (const auto& s : storms.storms) {
                    if (distance(s, cell_east(grid, i), cell_north(grid, i)) < config.genesis_separation_m) { isolated = false; break; }
                }
                if (isolated) { best = i; best_gpi = environment.genesis_potential_index[i]; }
            }
            if (best < count) {
                add_storm(cell_east(grid, best), cell_north(grid, best), config.genesis_max_wind_m_s, 40000.0, "genesis");
                storms.events.back().elapsed_seconds = now;
            }
        }

        // Velocities from steering, beta drift and mutual (Fujiwhara) induction.
        std::vector<double> velocity_east(storms.storms.size(), 0.0), velocity_north(storms.storms.size(), 0.0);
        for (std::size_t a = 0; a < storms.storms.size(); ++a) {
            const auto& s = storms.storms[a];
            double u_sum = 0.0, v_sum = 0.0;
            std::size_t n = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (distance(s, cell_east(grid, i), cell_north(grid, i)) <= config.steering_radius_m) {
                    u_sum += state.wind_east_m_s[i];
                    v_sum += state.wind_north_m_s[i];
                    ++n;
                }
            }
            double ue = n ? config.steering_factor * u_sum / static_cast<double>(n) : 0.0;
            double vn = n ? config.steering_factor * v_sum / static_cast<double>(n) : 0.0;
            ue -= config.beta_drift_west_m_s;
            vn += hemisphere * config.beta_drift_poleward_m_s;
            for (std::size_t b = 0; b < storms.storms.size(); ++b) {
                if (a == b) continue;
                const auto& o = storms.storms[b];
                const double dx = s.east_m - o.east_m, dy = s.north_m - o.north_m, d = std::hypot(dx, dy);
                if (d <= 0.0) continue;
                const double speed = d >= o.radius_max_wind_m ? o.max_wind_m_s * o.radius_max_wind_m / d
                                                               : o.max_wind_m_s * d / o.radius_max_wind_m;
                // Cyclonic tangential direction around the other storm.
                ue += hemisphere * config.induction_factor * speed * (-dy / d);
                vn += hemisphere * config.induction_factor * speed * (dx / d);
            }
            velocity_east[a] = ue;
            velocity_north[a] = vn;
        }

        // Intensity and position update.
        std::vector<TropicalCyclone> survivors;
        for (std::size_t a = 0; a < storms.storms.size(); ++a) {
            TropicalCyclone s = storms.storms[a];
            s.east_m += velocity_east[a] * dt;
            s.north_m += velocity_north[a] * dt;
            s.age_seconds += dt;
            const std::size_t cell = cell_at(grid, s.east_m, s.north_m);
            if (cell == count) {
                storms.events.push_back({"exit", now + dt, s.id, 0, s.east_m, s.north_m, s.max_wind_m_s, s.category, 0.0});
                continue;
            }
            const bool land = surface.land[cell] != 0;
            if (land && !s.over_land) {
                s.max_wind_m_s *= config.landfall_reduction;
                s.time_over_land_seconds = 0.0;
                update_pressure_and_derived(s, state, cell, config);
                storms.events.push_back({"landfall", now + dt, s.id, 0, s.east_m, s.north_m, s.max_wind_m_s, s.category, s.surge_estimate_m});
            }
            s.over_land = land;
            s.potential_intensity_m_s = environment.potential_intensity_m_s[cell];
            if (land) {
                s.time_over_land_seconds += dt;
                const double vb = config.decay_background_wind_m_s;
                s.max_wind_m_s = vb + (s.max_wind_m_s - vb) * std::exp(-config.decay_alpha_per_s * dt);
            } else {
                const double target = s.potential_intensity_m_s *
                                      std::max(0.0, 1.0 - environment.vertical_shear_m_s[cell] / config.shear_limit_m_s);
                s.max_wind_m_s += (target - s.max_wind_m_s) * (1.0 - std::exp(-dt / config.intensification_time_scale_s));
            }
            if (s.max_wind_m_s < config.dissipation_wind_m_s) {
                storms.events.push_back({"dissipation", now + dt, s.id, 0, s.east_m, s.north_m, s.max_wind_m_s, "depression", 0.0});
                continue;
            }
            update_pressure_and_derived(s, state, cell, config);
            s.track.emplace_back(s.east_m, s.north_m);
            survivors.push_back(s);
        }
        storms.storms.swap(survivors);

        // Mergers: overlapping cores combine into the stronger storm.
        for (std::size_t a = 0; a < storms.storms.size(); ++a) {
            for (std::size_t b = a + 1; b < storms.storms.size();) {
                auto& s = storms.storms[a];
                const auto& o = storms.storms[b];
                const double d = distance(s, o.east_m, o.north_m);
                if (d < s.radius_max_wind_m + o.radius_max_wind_m + config.merge_distance_m) {
                    const bool s_stronger = s.max_wind_m_s >= o.max_wind_m_s;
                    const double weight = s.max_wind_m_s + o.max_wind_m_s;
                    TropicalCyclone merged = s_stronger ? s : o;
                    merged.east_m = (s.east_m * s.max_wind_m_s + o.east_m * o.max_wind_m_s) / weight;
                    merged.north_m = (s.north_m * s.max_wind_m_s + o.north_m * o.max_wind_m_s) / weight;
                    merged.max_wind_m_s = std::max(s.max_wind_m_s, o.max_wind_m_s);
                    merged.radius_max_wind_m = std::max(s.radius_max_wind_m, o.radius_max_wind_m);
                    const std::size_t absorbed = s_stronger ? o.id : s.id;
                    const std::size_t cell = cell_at(grid, merged.east_m, merged.north_m);
                    if (cell < count) update_pressure_and_derived(merged, state, cell, config);
                    merged.track.emplace_back(merged.east_m, merged.north_m);
                    storms.events.push_back({"merger", now + dt, merged.id, absorbed, merged.east_m, merged.north_m, merged.max_wind_m_s, merged.category, 0.0});
                    s = merged;
                    storms.storms.erase(storms.storms.begin() + static_cast<std::ptrdiff_t>(b));
                } else {
                    ++b;
                }
            }
        }
        now += dt;
    }
}

StormFields storm_fields(
    const StormSystemsState& storms, const TransportGrid2D& grid, const SurfaceConfig& surface,
    const StormSystemsConfig& config) {
    validate_storm_systems_config(config);
    validate_surface(surface, grid);
    const std::size_t count = grid.nx * grid.ny;
    StormFields fields;
    fields.wind_speed_m_s.assign(count, 0.0);
    fields.pressure_anomaly_pa.assign(count, 0.0);
    fields.rain_rate_mm_h.assign(count, 0.0);
    fields.surge_m.assign(count, 0.0);
    const auto coastal = [&](std::size_t i) {
        if (surface.land[i] == 0) return false;
        const std::size_t x = i % grid.nx, y = i / grid.nx;
        const std::size_t xs[] = {x == 0 ? x : x - 1, x + 1 == grid.nx ? x : x + 1};
        const std::size_t ys[] = {y == 0 ? y : y - 1, y + 1 == grid.ny ? y : y + 1};
        return surface.land[at(xs[0], y, grid.nx)] == 0 || surface.land[at(xs[1], y, grid.nx)] == 0 ||
               surface.land[at(x, ys[0], grid.nx)] == 0 || surface.land[at(x, ys[1], grid.nx)] == 0;
    };
    for (const auto& s : storms.storms) {
        const double drop = s.environment_pressure_pa - s.central_pressure_pa;
        for (std::size_t i = 0; i < count; ++i) {
            const double r = std::hypot(cell_east(grid, i) - s.east_m, cell_north(grid, i) - s.north_m);
            const double rr = std::max(r, 1.0);
            const double wind = r <= s.radius_max_wind_m ? s.max_wind_m_s * r / s.radius_max_wind_m
                                                         : s.max_wind_m_s * s.radius_max_wind_m / r;
            fields.wind_speed_m_s[i] = std::max(fields.wind_speed_m_s[i], wind);
            fields.pressure_anomaly_pa[i] -= drop * (1.0 - std::exp(-std::pow(s.radius_max_wind_m / rr, config.holland_b)));
            fields.rain_rate_mm_h[i] = std::max(fields.rain_rate_mm_h[i], s.core_rain_rate_mm_h * std::exp(-r / (3.0 * s.radius_max_wind_m)));
            if (coastal(i) && r <= 6.0 * s.radius_max_wind_m) {
                fields.surge_m[i] = std::max(fields.surge_m[i], s.surge_estimate_m * std::exp(-r / (2.0 * s.radius_max_wind_m)));
            }
        }
    }
    return fields;
}

}  // namespace terraflux::weather
