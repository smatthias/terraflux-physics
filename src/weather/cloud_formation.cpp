#include "terraflux/weather/cloud_formation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

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

// Central differences inside, one-sided at the edges (no wrap).
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

}  // namespace

void validate_cloud_formation_config(const CloudFormationConfig& c) {
    validate_column_config(c.column);
    require_positive(c.frontal_gradient_threshold_k_m, "frontal gradient threshold must be positive");
    require_positive(c.advection_threshold_k_s, "advection threshold must be positive");
    require_positive(c.boundary_layer_depth_m, "boundary layer depth must be positive");
    require_positive(c.lifting_time_scale_s, "lifting time scale must be positive");
    require_non_negative(c.mixed_layer_growth_m_per_k, "mixed layer growth must not be negative");
    require_positive(c.warm_front_slope, "warm front slope must be positive");
    require_positive(c.cold_front_slope, "cold front slope must be positive");
    if (c.warm_front_slope > c.cold_front_slope) {
        throw std::invalid_argument("a warm front must be gentler than a cold front");
    }
    require_positive(c.warm_front_reach_m, "warm front reach must be positive");
    require_positive(c.cold_front_reach_m, "cold front reach must be positive");
    require_positive(c.frontal_band_width_m, "frontal band width must be positive");
    require_non_negative(c.cin_trigger_j_kg, "CIN trigger must not be negative");
    require_non_negative(c.cape_threshold_j_kg, "CAPE threshold must not be negative");
    require_positive(c.max_cloud_top_m, "maximum cloud top must be positive");
}

std::string classify_cloud(double base_m, double top_m, bool convective) {
    if (!std::isfinite(base_m) || !std::isfinite(top_m) || top_m <= base_m || base_m < 0.0) {
        return "none";
    }
    const double depth = top_m - base_m;
    if (convective) {
        if (depth >= 6000.0 || top_m >= 8000.0) return "cumulonimbus";
        if (depth >= 2000.0) return "towering_cumulus";
        return "cumulus";
    }
    if (base_m < 2000.0) {
        if (depth >= 3000.0) return "nimbostratus";
        if (depth >= 500.0) return "stratus";
        return "stratocumulus";
    }
    if (base_m < 6000.0) {
        return depth >= 2000.0 ? "altostratus" : "altocumulus";
    }
    return depth >= 1500.0 ? "cirrostratus" : "cirrus";
}

double cloud_fraction_for_type(const std::string& type) {
    if (type == "cumulus") return 0.3;
    if (type == "towering_cumulus") return 0.5;
    if (type == "cumulonimbus") return 0.9;
    if (type == "stratocumulus") return 0.6;
    if (type == "stratus") return 0.9;
    if (type == "nimbostratus") return 1.0;
    if (type == "altocumulus") return 0.5;
    if (type == "altostratus") return 0.9;
    if (type == "cirrus") return 0.3;
    if (type == "cirrostratus") return 0.7;
    return 0.0;
}

double diurnal_surface_heating_k(double elapsed_seconds, double amplitude_k, double phase_seconds) {
    if (!std::isfinite(elapsed_seconds) || !std::isfinite(amplitude_k) || !std::isfinite(phase_seconds) ||
        amplitude_k < 0.0) {
        throw std::invalid_argument("diurnal heating needs finite inputs and a non-negative amplitude");
    }
    return amplitude_k * std::max(0.0, std::sin(2.0 * kPi * (elapsed_seconds - phase_seconds) / 86400.0));
}

FrontAnalysis analyse_fronts(
    const CoupledWindState& state,
    const TransportGrid2D& grid,
    const CloudFormationConfig& config) {
    validate_cloud_formation_config(config);
    validate_transport_grid(grid);
    const std::size_t count = grid.nx * grid.ny;
    if (state.air_temperature_k.size() != count || state.wind_east_m_s.size() != count ||
        state.wind_north_m_s.size() != count) {
        throw std::invalid_argument("front analysis needs temperature and wind on the grid");
    }
    FrontAnalysis f;
    const Gradient t = gradient_of(state.air_temperature_k, grid);
    const Gradient u = gradient_of(state.wind_east_m_s, grid);
    const Gradient v = gradient_of(state.wind_north_m_s, grid);
    f.temperature_gradient_k_m.resize(count);
    f.temperature_advection_k_s.resize(count);
    f.divergence_s.resize(count);
    f.lifting_m_s.resize(count);
    f.front_type.assign(count, "none");
    f.distance_to_front_m.assign(count, kNaN);
    f.nearest_front_index.assign(count, count);
    f.side.assign(count, 0);
    std::vector<std::size_t> front_cells;
    for (std::size_t i = 0; i < count; ++i) {
        f.temperature_gradient_k_m[i] = std::hypot(t.east[i], t.north[i]);
        f.temperature_advection_k_s[i] = -(state.wind_east_m_s[i] * t.east[i] + state.wind_north_m_s[i] * t.north[i]);
        f.divergence_s[i] = u.east[i] + v.north[i];
        f.lifting_m_s[i] = std::max(0.0, -config.boundary_layer_depth_m * f.divergence_s[i]);
        for (double value : {f.temperature_gradient_k_m[i], f.temperature_advection_k_s[i], f.divergence_s[i]}) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument("front analysis produced a non-finite value");
            }
        }
        if (f.temperature_gradient_k_m[i] >= config.frontal_gradient_threshold_k_m) {
            const double advection = f.temperature_advection_k_s[i];
            f.front_type[i] = advection <= -config.advection_threshold_k_s ? "cold"
                            : advection >= config.advection_threshold_k_s ? "warm" : "stationary";
            front_cells.push_back(i);
        }
    }
    f.front_cell_count = front_cells.size();
    if (front_cells.empty()) {
        return f;
    }
    // Nearest frontal cell and which side of it a cell lies on.
    for (std::size_t i = 0; i < count; ++i) {
        const double x = static_cast<double>(i % grid.nx) * grid.spacing_east_m;
        const double y = static_cast<double>(i / grid.nx) * grid.spacing_north_m;
        double best = std::numeric_limits<double>::infinity();
        std::size_t nearest = count;
        for (std::size_t j : front_cells) {
            const double dx = static_cast<double>(j % grid.nx) * grid.spacing_east_m - x;
            const double dy = static_cast<double>(j / grid.nx) * grid.spacing_north_m - y;
            const double d = std::hypot(dx, dy);
            if (d < best) {
                best = d;
                nearest = j;
            }
        }
        f.distance_to_front_m[i] = best;
        f.nearest_front_index[i] = nearest;
        if (f.front_type[i] == "none") {
            f.side[i] = state.air_temperature_k[i] >= state.air_temperature_k[nearest] ? 1 : -1;
        }
    }
    return f;
}

CloudField diagnose_cloud_formation(
    const CoupledWindState& state,
    const TransportGrid2D& grid,
    const CloudFormationConfig& config,
    double surface_heating_k,
    const FrontAnalysis& fronts) {
    validate_cloud_formation_config(config);
    validate_transport_grid(grid);
    if (!std::isfinite(surface_heating_k) || surface_heating_k < 0.0) {
        throw std::invalid_argument("surface heating must be finite and non-negative");
    }
    const std::size_t count = grid.nx * grid.ny;
    if (state.pressure_pa.size() != count || state.air_temperature_k.size() != count ||
        state.specific_humidity_kg_kg.size() != count || fronts.front_type.size() != count) {
        throw std::invalid_argument("cloud diagnosis needs a full state and matching front analysis");
    }

    // Column diagnostics: unheated (frontal lifting of the ambient air) and
    // heated (the convective surface parcel).
    const auto ambient = diagnose_column_field(
        state.pressure_pa, state.air_temperature_k, state.specific_humidity_kg_kg, config.column);
    const auto heated = diagnose_column_field(
        state.pressure_pa, state.air_temperature_k, state.specific_humidity_kg_kg, config.column, surface_heating_k);

    CloudField c;
    c.surface_heating_k = surface_heating_k;
    c.cloud_type.assign(count, "none");
    c.mechanism.assign(count, "none");
    c.trigger.assign(count, "none");
    c.base_m.assign(count, kNaN);
    c.top_m.assign(count, kNaN);
    c.cloud_fraction.assign(count, 0.0);
    c.thunderstorm.assign(count, 0);
    c.lifting_depth_m.resize(count);
    c.heated_cape_j_kg = heated.cape_j_kg;
    c.heated_cin_j_kg = heated.cin_j_kg;
    const double z0 = 0.5 * config.column.layer_thickness_m;  // parcel start (lowest layer midpoint)

    for (std::size_t i = 0; i < count; ++i) {
        c.lifting_depth_m[i] = fronts.lifting_m_s[i] * config.lifting_time_scale_s;
        const bool on_front = fronts.front_type[i] != "none";
        const bool near_front = std::isfinite(fronts.distance_to_front_m[i]) &&
                                fronts.distance_to_front_m[i] <= config.frontal_band_width_m;
        const double lifted_top = z0 + c.lifting_depth_m[i];

        double base = kNaN, top = kNaN;
        bool convective = false;
        std::string mechanism = "none", trigger = "none";

        // 1. Convection released: freely (heated parcel, low CIN) or forced
        //    by lifting that carries the parcel to its level of free convection.
        const bool has_cape = heated.cape_j_kg[i] >= config.cape_threshold_j_kg && std::isfinite(heated.lfc_height_m[i]);
        if (has_cape && heated.cin_j_kg[i] <= config.cin_trigger_j_kg) {
            convective = true;
            base = heated.lcl_height_m[i];
            top = heated.equilibrium_level_m[i];
            // Free convection inside a frontal band is the combined case.
            mechanism = (on_front || near_front) ? "frontal_convective" : "convective";
            trigger = "free";
        } else if (has_cape && lifted_top >= heated.lfc_height_m[i]) {
            convective = true;
            base = heated.lcl_height_m[i];
            top = heated.equilibrium_level_m[i];
            mechanism = (on_front || near_front) ? "frontal_convective" : "convective";
            trigger = (on_front || near_front) ? "frontal" : "convergence";
        }

        // 2. Frontal / layered cloud where convection is not released.
        if (!convective) {
            if ((on_front || (near_front && fronts.side[i] >= 0)) && std::isfinite(ambient.lcl_height_m[i]) &&
                lifted_top > ambient.lcl_height_m[i]) {
                // Lifted warm-sector air at or just ahead of the front.
                base = ambient.lcl_height_m[i];
                top = lifted_top;
                mechanism = "frontal";
                trigger = "frontal";
            } else if (!on_front && fronts.side[i] < 0 && std::isfinite(fronts.distance_to_front_m[i])) {
                // Cold side: warm air overruns the wedge of the nearest front.
                const std::size_t j = fronts.nearest_front_index[i];
                const bool warm_front = fronts.front_type[j] == "warm";
                const double reach = warm_front ? config.warm_front_reach_m : config.cold_front_reach_m;
                if (fronts.distance_to_front_m[i] <= reach) {
                    const double slope = warm_front ? config.warm_front_slope : config.cold_front_slope;
                    const double wedge = fronts.distance_to_front_m[i] * slope;
                    const double lcl_front = std::isfinite(ambient.lcl_height_m[j]) ? ambient.lcl_height_m[j] : 0.0;
                    const double overrun_base = std::max(wedge, lcl_front);
                    const double overrun_top = wedge + fronts.lifting_m_s[j] * config.lifting_time_scale_s;
                    if (overrun_top > overrun_base) {
                        base = overrun_base;
                        top = overrun_top;
                        mechanism = "frontal";
                        trigger = "overrunning";
                    }
                }
            }
        }

        // 3. Fair-weather cumulus from boundary-layer thermals when nothing else:
        //    the heated mixed layer must reach the parcel's condensation level.
        const double mixed_layer_top =
            config.boundary_layer_depth_m + config.mixed_layer_growth_m_per_k * surface_heating_k;
        if (mechanism == "none" && surface_heating_k > 0.0 && std::isfinite(heated.lcl_height_m[i]) &&
            heated.lcl_height_m[i] < mixed_layer_top) {
            convective = true;
            base = heated.lcl_height_m[i];
            top = mixed_layer_top;
            mechanism = "convective";
            trigger = "thermals";
        }

        if (mechanism != "none") {
            top = std::min(top, config.max_cloud_top_m);
            const std::string type = classify_cloud(base, top, convective);
            if (type == "none") {
                mechanism = "none";
                trigger = "none";
                base = top = kNaN;
            } else {
                c.cloud_type[i] = type;
                c.mechanism[i] = mechanism;
                c.trigger[i] = trigger;
                c.base_m[i] = base;
                c.top_m[i] = top;
                c.cloud_fraction[i] = cloud_fraction_for_type(type);
                c.thunderstorm[i] = type == "cumulonimbus" ? 1 : 0;
                ++c.cloudy_cells;
                if (c.thunderstorm[i]) ++c.thunderstorm_cells;
            }
        }
    }
    return c;
}

}  // namespace terraflux::weather
