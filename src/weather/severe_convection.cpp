#include "terraflux/weather/severe_convection.hpp"

#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/pressure_wind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

void require_positive(double value, const char* message) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(message);
    }
}

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
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

}  // namespace

void validate_severe_convection_config(const SevereConvectionConfig& c) {
    require_positive(c.deep_shear_depth_m, "deep shear depth must be positive");
    if (!std::isfinite(c.bunkers_deviation_m_s) || c.bunkers_deviation_m_s < 0.0) {
        throw std::invalid_argument("Bunkers deviation must not be negative");
    }
    require_positive(c.geostrophic_air_density_kg_m3, "geostrophic air density must be positive");
    if (!std::isfinite(c.stp_low) || !std::isfinite(c.stp_moderate) || !std::isfinite(c.stp_significant) ||
        c.stp_low < 0.0 || c.stp_low > c.stp_moderate || c.stp_moderate > c.stp_significant) {
        throw std::invalid_argument("STP thresholds must be ordered low <= moderate <= significant");
    }
}

double significant_tornado_parameter(double cape, double lcl, double helicity, double deep_shear, double cin) {
    if (!std::isfinite(cape) || cape < 0.0 || !std::isfinite(deep_shear) || deep_shear < 0.0 ||
        !std::isfinite(cin) || cin < 0.0 || !std::isfinite(helicity)) {
        throw std::invalid_argument("STP needs finite, non-negative CAPE, shear and CIN");
    }
    if (!std::isfinite(lcl)) {
        return 0.0;
    }
    const double cape_term = cape / 1500.0;
    const double lcl_term = lcl < 1000.0 ? 1.0 : lcl > 2000.0 ? 0.0 : (2000.0 - lcl) / 1000.0;
    const double srh_term = std::abs(helicity) / 150.0;
    const double shear_term = deep_shear < 12.5 ? 0.0 : std::min(1.5, deep_shear / 20.0);
    const double cin_term = cin < 50.0 ? 1.0 : cin > 200.0 ? 0.0 : (200.0 - cin) / 150.0;
    return cape_term * lcl_term * srh_term * shear_term * cin_term;
}

std::string tornado_risk_class(double stp, const SevereConvectionConfig& config) {
    if (!std::isfinite(stp) || stp < 0.0) {
        throw std::invalid_argument("STP must be finite and non-negative");
    }
    if (stp < config.stp_low) return "none";
    if (stp < config.stp_moderate) return "low";
    if (stp < config.stp_significant) return "moderate";
    return "significant";
}

SevereConvectionField diagnose_severe_convection(
    const CoupledWindState& state, const TransportGrid2D& grid,
    const CloudFormationConfig& cloud_config, const CloudField& clouds,
    const SevereConvectionConfig& config) {
    validate_severe_convection_config(config);
    validate_cloud_formation_config(cloud_config);
    validate_transport_grid(grid);
    const std::size_t count = grid.nx * grid.ny;
    if (state.pressure_pa.size() != count || state.air_temperature_k.size() != count ||
        state.specific_humidity_kg_kg.size() != count || state.wind_east_m_s.size() != count ||
        state.wind_north_m_s.size() != count || clouds.cloud_type.size() != count) {
        throw std::invalid_argument("severe convection needs a full state and matching cloud field");
    }

    // Geostrophic wind aloft from the pressure field (f-plane at the anchor).
    PressureWindConfig wind_config;
    wind_config.time = {"2000-01-01T00:00:00Z", 1.0, 1};
    wind_config.grid = grid;
    wind_config.atmosphere.air_density_kg_m3 = config.geostrophic_air_density_kg_m3;
    wind_config.atmosphere.coriolis_mode = "f_plane";
    const auto f = coriolis_parameter(grid, wind_config.atmosphere);
    const bool rotating = !f.empty() && std::abs(f[0]) >= 1.0e-6;
    WindField aloft{state.wind_east_m_s, state.wind_north_m_s};
    if (rotating) {
        aloft = geostrophic_wind(state.pressure_pa, wind_config);
    }
    const double hemisphere = rotating && f[0] < 0.0 ? -1.0 : 1.0;
    const Gradient t = gradient_of(state.air_temperature_k, grid);

    const auto ambient = diagnose_column_field(state.pressure_pa, state.air_temperature_k, state.specific_humidity_kg_kg, cloud_config.column);
    const auto heated = diagnose_column_field(state.pressure_pa, state.air_temperature_k, state.specific_humidity_kg_kg,
                                              cloud_config.column, clouds.surface_heating_k);

    SevereConvectionField s;
    s.low_level_shear_m_s.resize(count);
    s.deep_shear_m_s.resize(count);
    s.storm_motion_east_m_s.resize(count);
    s.storm_motion_north_m_s.resize(count);
    s.helicity_m2_s2.resize(count);
    s.significant_tornado_parameter.resize(count);
    s.heated_cape_j_kg = heated.cape_j_kg;
    s.heated_cin_j_kg = heated.cin_j_kg;
    s.cap_strength_j_kg = ambient.cin_j_kg;
    s.lcl_height_m = heated.lcl_height_m;
    s.capped.assign(count, 0);
    s.cap_broken.assign(count, 0);
    s.tornado_risk.assign(count, "none");
    s.tornado_possible.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        const double us = state.wind_east_m_s[i], vs = state.wind_north_m_s[i];
        const double s1_e = aloft.east_m_s[i] - us, s1_n = aloft.north_m_s[i] - vs;
        s.low_level_shear_m_s[i] = std::hypot(s1_e, s1_n);
        // Thermal wind over the deep layer: (g dz / (f T)) k x grad T.
        double tw_e = 0.0, tw_n = 0.0;
        if (rotating) {
            const double factor = column::kGravityMS2 * config.deep_shear_depth_m / (f[i] * state.air_temperature_k[i]);
            tw_e = -factor * t.north[i];
            tw_n = factor * t.east[i];
        }
        const double s6_e = s1_e + tw_e, s6_n = s1_n + tw_n;
        s.deep_shear_m_s[i] = std::hypot(s6_e, s6_n);
        // Bunkers storm motion: mean wind plus a deviation to the right (NH) of the deep shear.
        const double mean_e = 0.5 * (us + aloft.east_m_s[i]), mean_n = 0.5 * (vs + aloft.north_m_s[i]);
        double ce = mean_e, cn = mean_n;
        if (s.deep_shear_m_s[i] > 0.0) {
            ce += hemisphere * config.bunkers_deviation_m_s * (s6_n / s.deep_shear_m_s[i]);
            cn -= hemisphere * config.bunkers_deviation_m_s * (s6_e / s.deep_shear_m_s[i]);
        }
        s.storm_motion_east_m_s[i] = ce;
        s.storm_motion_north_m_s[i] = cn;
        // SRH of the straight 0-1 km hodograph: [S1 x (V_s - c)]_z.
        s.helicity_m2_s2[i] = s1_e * (vs - cn) - s1_n * (us - ce);
        const double lcl = std::isfinite(heated.lcl_height_m[i]) ? heated.lcl_height_m[i] : kNaN;
        s.significant_tornado_parameter[i] = significant_tornado_parameter(
            heated.cape_j_kg[i], lcl, s.helicity_m2_s2[i], s.deep_shear_m_s[i], heated.cin_j_kg[i]);
        s.max_stp = std::max(s.max_stp, s.significant_tornado_parameter[i]);
        s.tornado_risk[i] = tornado_risk_class(s.significant_tornado_parameter[i], config);
        const bool energy = heated.cape_j_kg[i] >= cloud_config.cape_threshold_j_kg;
        if (energy && ambient.cin_j_kg[i] > cloud_config.cin_trigger_j_kg) {
            s.capped[i] = 1;
            ++s.capped_cells;
            if (heated.cin_j_kg[i] <= cloud_config.cin_trigger_j_kg) {
                s.cap_broken[i] = 1;
                ++s.cap_broken_cells;
            }
        }
        if ((s.tornado_risk[i] == "moderate" || s.tornado_risk[i] == "significant") && clouds.cloud_type[i] == "cumulonimbus") {
            s.tornado_possible[i] = 1;
            ++s.tornado_possible_cells;
        }
    }
    return s;
}

}  // namespace terraflux::weather
