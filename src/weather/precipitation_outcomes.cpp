#include "terraflux/weather/precipitation_outcomes.hpp"

#include "terraflux/weather/moist_air.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kIceDensity = 900.0;      // kg/m^3
constexpr double kHailDragCoefficient = 0.6;
constexpr double kAirDensityAloft = 1.0;   // kg/m^3, mid-troposphere

void require_positive(double value, const char* message) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(message);
    }
}

void require_fraction(double value, const char* message) {
    if (!std::isfinite(value) || value <= 0.0 || value > 1.0) {
        throw std::invalid_argument(message);
    }
}

// Temperature at the cloud top following the moist adiabat from the base.
double moist_adiabat_top_temperature(
    const AtmosphericColumn& column, double base_m, double base_temperature_k, double top_m) {
    const int sub = std::max(1, static_cast<int>(std::ceil((top_m - base_m) / 50.0)));
    const double h = (top_m - base_m) / sub;
    double z = base_m, t = base_temperature_k;
    for (int i = 0; i < sub; ++i) {
        const double tv_env = virtual_temperature_k(
            column_temperature_at_height_k(column, z), column_humidity_at_height_kg_kg(column, z));
        const double k1 = -moist_adiabatic_lapse_rate_k_m(t, column_pressure_at_height_pa(column, z)) * t / tv_env;
        const double tv_mid = virtual_temperature_k(
            column_temperature_at_height_k(column, z + 0.5 * h), column_humidity_at_height_kg_kg(column, z + 0.5 * h));
        const double k2 = -moist_adiabatic_lapse_rate_k_m(t + 0.5 * h * k1, column_pressure_at_height_pa(column, z + 0.5 * h)) *
                          (t + 0.5 * h * k1) / tv_mid;
        t += h * k2;
        z += h;
    }
    return t;
}

}  // namespace

void validate_precipitation_config(const PrecipitationConfig& c) {
    require_fraction(c.stratiform_efficiency, "stratiform efficiency must be within (0, 1]");
    require_fraction(c.convective_efficiency, "convective efficiency must be within (0, 1]");
    require_fraction(c.convective_updraft_fraction, "convective updraft fraction must be within (0, 1]");
    require_fraction(c.updraft_parcel_factor, "updraft parcel factor must be within (0, 1]");
    require_positive(c.interval_seconds, "interval must be positive");
    if (!std::isfinite(c.snow_wet_bulb_k) || !std::isfinite(c.sleet_wet_bulb_k) || c.snow_wet_bulb_k > c.sleet_wet_bulb_k) {
        throw std::invalid_argument("snow wet-bulb threshold must not exceed the sleet threshold");
    }
    if (!std::isfinite(c.lightning_top_temperature_k) || c.lightning_top_temperature_k >= moist_air::kCelsiusOffsetK) {
        throw std::invalid_argument("lightning requires a cloud-top temperature threshold below 0 C");
    }
    require_positive(c.hail_min_diameter_cm, "minimum hail diameter must be positive");
    if (!std::isfinite(c.hail_melting_cm_per_km) || c.hail_melting_cm_per_km < 0.0) {
        throw std::invalid_argument("hail melting rate must not be negative");
    }
    require_positive(c.hail_max_diameter_cm, "maximum hail diameter must be positive");
    require_positive(c.drizzle_max_rate_mm_h, "drizzle rate cap must be positive");
    require_fraction(c.virga_surface_rh, "virga surface humidity must be within (0, 1]");
    require_positive(c.virga_max_rate_mm_h, "virga rate limit must be positive");
    if (!std::isfinite(c.virga_min_base_m) || c.virga_min_base_m < 0.0) {
        throw std::invalid_argument("virga minimum base must not be negative");
    }
    require_positive(c.minimum_rate_mm_h, "minimum rate must be positive");
}

std::string precipitation_character_for_type(const std::string& type) {
    if (type == "stratus") return "drizzle";
    if (type == "stratocumulus") return "intermittent";
    if (type == "altostratus") return "intermittent";
    if (type == "nimbostratus") return "continuous";
    if (type == "towering_cumulus" || type == "cumulonimbus") return "showers";
    return "none";
}

double hail_terminal_velocity_m_s(double diameter_m) {
    if (!std::isfinite(diameter_m) || diameter_m < 0.0) {
        throw std::invalid_argument("hail diameter must be finite and non-negative");
    }
    return std::sqrt(4.0 * column::kGravityMS2 * kIceDensity * diameter_m /
                     (3.0 * kHailDragCoefficient * kAirDensityAloft));
}

double hail_diameter_for_updraft_m(double updraft_m_s) {
    if (!std::isfinite(updraft_m_s) || updraft_m_s < 0.0) {
        throw std::invalid_argument("updraft must be finite and non-negative");
    }
    return updraft_m_s * updraft_m_s * 3.0 * kHailDragCoefficient * kAirDensityAloft /
           (4.0 * column::kGravityMS2 * kIceDensity);
}

std::string precipitation_intensity_class(double rate_mm_h) {
    if (!std::isfinite(rate_mm_h) || rate_mm_h < 0.0) {
        throw std::invalid_argument("rate must be finite and non-negative");
    }
    if (rate_mm_h < 0.05) return "none";
    if (rate_mm_h < 2.5) return "light";
    if (rate_mm_h < 10.0) return "moderate";
    if (rate_mm_h < 50.0) return "heavy";
    return "violent";
}

PrecipitationField diagnose_precipitation(
    const CoupledWindState& state,
    const TransportGrid2D& grid,
    const CloudFormationConfig& cloud_config,
    const CloudField& clouds,
    const PrecipitationConfig& config) {
    validate_precipitation_config(config);
    validate_cloud_formation_config(cloud_config);
    validate_transport_grid(grid);
    const std::size_t count = grid.nx * grid.ny;
    if (state.pressure_pa.size() != count || state.air_temperature_k.size() != count ||
        state.specific_humidity_kg_kg.size() != count || clouds.cloud_type.size() != count ||
        clouds.base_m.size() != count || clouds.top_m.size() != count ||
        clouds.heated_cape_j_kg.size() != count || clouds.lifting_depth_m.size() != count) {
        throw std::invalid_argument("precipitation diagnosis needs a full state and matching cloud field");
    }

    PrecipitationField p;
    p.precipitation_type.assign(count, "none");
    p.phase.assign(count, "none");
    p.character.assign(count, "none");
    p.intensity.assign(count, "none");
    p.rate_mm_h.assign(count, 0.0);
    p.duration_fraction.assign(count, 0.0);
    p.accumulation_mm.assign(count, 0.0);
    p.hail_max_diameter_cm.assign(count, 0.0);
    p.lightning.assign(count, 0);
    p.virga.assign(count, 0);
    p.updraft_m_s.assign(count, 0.0);
    p.freezing_level_m.assign(count, kNaN);
    p.surface_wet_bulb_k.assign(count, 0.0);
    p.precipitable_water_mm.assign(count, 0.0);
    p.cloud_base_temperature_k.assign(count, kNaN);
    p.cloud_top_temperature_k.assign(count, kNaN);
    p.hazards.assign(count, {});
    const double t0 = moist_air::kCelsiusOffsetK;

    for (std::size_t i = 0; i < count; ++i) {
        const double ps = state.pressure_pa[i], ts = state.air_temperature_k[i], qs = state.specific_humidity_kg_kg[i];
        const auto column = build_hydrostatic_column(ps, ts, qs, cloud_config.column);
        const auto freezing = column_freezing_level_m(column);
        if (freezing) p.freezing_level_m[i] = *freezing;
        p.surface_wet_bulb_k[i] = wet_bulb_temperature_k(ts, qs, ps);
        p.precipitable_water_mm[i] = column_precipitable_water_mm(column);

        const std::string& genus = clouds.cloud_type[i];
        const std::string character = precipitation_character_for_type(genus);
        if (genus == "none" || !std::isfinite(clouds.base_m[i]) || !std::isfinite(clouds.top_m[i])) {
            continue;
        }
        const double base = clouds.base_m[i], top = clouds.top_m[i];
        const bool convective = genus == "towering_cumulus" || genus == "cumulonimbus";
        const double p_base = column_pressure_at_height_pa(column, base);
        const double p_top = column_pressure_at_height_pa(column, top);
        // Cloud-base temperature: the heated parcel lifted dry-adiabatically
        // for convection, the environment for layered cloud.
        double t_base;
        if (convective) {
            const double theta = potential_temperature_k(ts + clouds.surface_heating_k, ps);
            t_base = theta * std::pow(p_base / column::kReferencePressurePa, column::kKappa);
        } else {
            t_base = column_temperature_at_height_k(column, base);
        }
        const double t_top = moist_adiabat_top_temperature(column, base, t_base, top);
        p.cloud_base_temperature_k[i] = t_base;
        p.cloud_top_temperature_k[i] = t_top;
        if (character == "none") {
            continue;  // cirrus, cirrostratus, altocumulus, cumulus: harmless
        }

        // Lifting through the cloud and the condensation rate.
        double w, sigma, efficiency;
        if (convective) {
            w = config.updraft_parcel_factor * std::sqrt(2.0 * std::max(0.0, clouds.heated_cape_j_kg[i]));
            sigma = config.convective_updraft_fraction;
            efficiency = config.convective_efficiency;
        } else {
            w = (top - base) / cloud_config.lifting_time_scale_s;
            sigma = 1.0;
            efficiency = config.stratiform_efficiency;
        }
        const double q_base = saturation_specific_humidity_kg_kg(t_base, p_base);
        const double q_top = saturation_specific_humidity_kg_kg(t_top, p_top);
        const double rho_base = moist_air_density_kg_m3(p_base, t_base, q_base);
        double rate = sigma * efficiency * rho_base * w * std::max(0.0, q_base - q_top) * 3600.0;
        if (genus == "stratocumulus") rate *= 0.3;  // broken, shallow layer
        if (character == "drizzle" || genus == "stratocumulus") rate = std::min(rate, config.drizzle_max_rate_mm_h);
        if (!std::isfinite(rate)) {
            throw std::invalid_argument("precipitation rate is not finite");
        }
        p.updraft_m_s[i] = w;

        // Virga: light precipitation from a high base into dry air evaporates.
        const double surface_rh = relative_humidity_fraction(ts, qs, ps);
        if (rate > 0.0 && rate <= config.virga_max_rate_mm_h && surface_rh < config.virga_surface_rh &&
            base >= config.virga_min_base_m) {
            p.virga[i] = 1;
            p.hazards[i].push_back("virga");
            rate = 0.0;
        }
        if (rate < config.minimum_rate_mm_h) {
            continue;
        }

        // Phase at the ground.
        const bool warm_cloud = t_top > t0;
        const double t_w = p.surface_wet_bulb_k[i];
        std::string phase = warm_cloud || t_w > config.sleet_wet_bulb_k ? "liquid"
                          : t_w > config.snow_wet_bulb_k ? "mixed" : "solid";
        const bool showers = character == "showers";
        std::string type = phase == "solid" ? (showers ? "snow_showers" : "snow")
                         : phase == "mixed" ? (showers ? "sleet_showers" : "sleet")
                         : showers ? "rain_showers" : character == "drizzle" || (genus == "stratocumulus") ? "drizzle" : "rain";
        const double duration = character == "continuous" || character == "drizzle" ? 1.0
                              : character == "intermittent" ? 0.5 : 0.2;

        // Lightning and hail for cumulonimbus.
        if (genus == "cumulonimbus" && t_top <= config.lightning_top_temperature_k && freezing &&
            *freezing >= base && *freezing <= top) {
            p.lightning[i] = 1;
            p.hazards[i].push_back("thunderstorm");
            const double diameter_cm = std::min(config.hail_max_diameter_cm, 100.0 * hail_diameter_for_updraft_m(w));
            const double melted_cm = diameter_cm - config.hail_melting_cm_per_km * (*freezing / 1000.0);
            if (melted_cm >= config.hail_min_diameter_cm) {
                p.hail_max_diameter_cm[i] = melted_cm;
                p.hazards[i].push_back("hail");
            }
        }

        p.precipitation_type[i] = type;
        p.phase[i] = phase;
        p.character[i] = character;
        p.intensity[i] = precipitation_intensity_class(rate);
        p.rate_mm_h[i] = rate;
        p.duration_fraction[i] = duration;
        p.accumulation_mm[i] = rate * duration * config.interval_seconds / 3600.0;
        if (rate >= 50.0) p.hazards[i].push_back("violent_rain");
        else if (rate >= 10.0) p.hazards[i].push_back("heavy_rain");
        if (character == "continuous" && p.accumulation_mm[i] >= 10.0) p.hazards[i].push_back("prolonged_rain");
        if (phase == "solid") p.hazards[i].push_back("snow");
        if (phase == "mixed") p.hazards[i].push_back("sleet");
        if (type == "drizzle") p.hazards[i].push_back("drizzle");

        ++p.precipitating_cells;
        if (p.lightning[i]) ++p.lightning_cells;
        if (p.hail_max_diameter_cm[i] > 0.0) ++p.hail_cells;
        if (phase == "solid") ++p.snow_cells;
        p.max_rate_mm_h = std::max(p.max_rate_mm_h, rate);
        p.total_accumulation_mm += p.accumulation_mm[i];
    }
    return p;
}

}  // namespace terraflux::weather
