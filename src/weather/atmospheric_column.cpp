#include "terraflux/weather/atmospheric_column.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

using namespace column;
using moist_air::kDryAirGasConstantJkgK;
using moist_air::kEpsilon;
using moist_air::kVirtualTemperatureFactor;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

double exner(double pressure_pa) {
    return std::pow(pressure_pa / kReferencePressurePa, kKappa);
}

double virtual_temperature(double temperature_k, double q) {
    return temperature_k * (1.0 + kVirtualTemperatureFactor * q);
}

// Hydrostatic pressure at the end of an interval over which the virtual
// temperature varies linearly from tv0 to tv1 (exact integral of g/(R_d Tv)).
double hydrostatic_step(double pressure_pa, double tv0, double tv1, double dz) {
    if (std::abs(tv1 - tv0) < 1.0e-9 * tv0) {
        return pressure_pa * std::exp(-kGravityMS2 * dz / (kDryAirGasConstantJkgK * tv0));
    }
    const double gamma = (tv1 - tv0) / dz;
    return pressure_pa * std::pow(tv1 / tv0, -kGravityMS2 / (kDryAirGasConstantJkgK * gamma));
}

// Piecewise-linear interpolation, extrapolating linearly beyond the ends so
// that the half-layers below the first and above the last mid-level follow
// the local profile slope.
double interpolate(const std::vector<double>& x, const std::vector<double>& y, double at) {
    std::size_t i = 1;
    if (at > x[1]) {
        const auto upper = std::upper_bound(x.begin(), x.end(), at);
        i = std::min(static_cast<std::size_t>(upper - x.begin()), x.size() - 1);
    }
    const double w = (at - x[i - 1]) / (x[i] - x[i - 1]);
    return y[i - 1] + w * (y[i] - y[i - 1]);
}

// Pressure at any height, linear in ln p through the interface and
// mid-level values (a 2N + 1 point table built once per ascent).
struct PressureTable {
    std::vector<double> height_m;
    std::vector<double> log_pressure;
    explicit PressureTable(const AtmosphericColumn& c) {
        const std::size_t n = c.height_m.size();
        for (std::size_t k = 0; k < n; ++k) {
            height_m.push_back(c.interface_height_m[k]);
            log_pressure.push_back(std::log(c.interface_pressure_pa[k]));
            height_m.push_back(c.height_m[k]);
            log_pressure.push_back(std::log(c.pressure_pa[k]));
        }
        height_m.push_back(c.interface_height_m[n]);
        log_pressure.push_back(std::log(c.interface_pressure_pa[n]));
    }
    double at(double z) const { return std::exp(interpolate(height_m, log_pressure, z)); }
};

// Saturation adjustment of one layer: condense vapour above saturation or
// evaporate cloud water below it, conserving c_p T + L q. Returns the change
// in cloud water (positive = condensation).
double saturation_adjustment(double& temperature_k, double& q, double& cloud_water, double pressure_pa) {
    const double total_water = q + cloud_water;
    // Saturated solution: c_p T' + L (q_t - q_s(T')) = c_p T + L (q_t - q)
    // Newton iteration for T' with q' = q_s(T').
    double t = temperature_k;
    for (int iteration = 0; iteration < 30; ++iteration) {
        const double qs = saturation_specific_humidity_kg_kg(t, pressure_pa);
        const double f = kSpecificHeatDryAirJkgK * (t - temperature_k) - kLatentHeatVaporisationJkg * (q - qs);
        const double dt = 0.01;
        const double dqs = (saturation_specific_humidity_kg_kg(t + dt, pressure_pa) - qs) / dt;
        const double df = kSpecificHeatDryAirJkgK + kLatentHeatVaporisationJkg * dqs;
        const double step = f / df;
        t -= step;
        if (std::abs(step) < 1.0e-9) {
            break;
        }
    }
    const double qs = saturation_specific_humidity_kg_kg(t, pressure_pa);
    if (qs >= total_water) {
        // Not enough water to saturate: evaporate all cloud water.
        const double new_q = total_water;
        const double new_t = temperature_k - kLatentHeatVaporisationJkg * (new_q - q) / kSpecificHeatDryAirJkgK;
        const double change = -cloud_water;
        temperature_k = new_t;
        q = new_q;
        cloud_water = 0.0;
        return change;
    }
    const double change = q - qs;  // condensation (> 0) or partial evaporation (< 0)
    temperature_k = t;
    q = qs;
    cloud_water = total_water - qs;
    return change;
}

}  // namespace

double potential_temperature_k(double air_temperature_k, double pressure_pa) {
    if (!std::isfinite(air_temperature_k) || air_temperature_k <= 0.0 ||
        !std::isfinite(pressure_pa) || pressure_pa <= 0.0) {
        throw std::invalid_argument("potential temperature needs positive temperature and pressure");
    }
    return air_temperature_k / exner(pressure_pa);
}

double dry_adiabatic_lapse_rate_k_m() {
    return kGravityMS2 / kSpecificHeatDryAirJkgK;
}

double moist_adiabatic_lapse_rate_k_m(double air_temperature_k, double pressure_pa) {
    const double e_s = saturation_vapour_pressure_pa(air_temperature_k);
    if (!std::isfinite(pressure_pa) || pressure_pa <= e_s) {
        throw std::invalid_argument("moist adiabatic lapse rate needs pressure above the saturation vapour pressure");
    }
    const double r_s = kEpsilon * e_s / (pressure_pa - e_s);  // saturation mixing ratio
    const double t = air_temperature_k;
    const double numerator = kGravityMS2 * (1.0 + kLatentHeatVaporisationJkg * r_s / (kDryAirGasConstantJkgK * t));
    const double denominator =
        kSpecificHeatDryAirJkgK +
        kLatentHeatVaporisationJkg * kLatentHeatVaporisationJkg * r_s * kEpsilon /
            (kDryAirGasConstantJkgK * t * t);
    return numerator / denominator;
}

double dew_point_k(double vapour_pressure) {
    if (!std::isfinite(vapour_pressure) || vapour_pressure <= 0.0) {
        throw std::invalid_argument("dew point needs a positive vapour pressure");
    }
    const double ratio = std::log(vapour_pressure / moist_air::kMagnusA);
    if (ratio >= moist_air::kMagnusB) {
        throw std::invalid_argument("vapour pressure exceeds the Magnus range");
    }
    return moist_air::kMagnusC * ratio / (moist_air::kMagnusB - ratio) + moist_air::kCelsiusOffsetK;
}

double equivalent_potential_temperature_k(
    double air_temperature_k, double pressure_pa, double specific_humidity_kg_kg) {
    const double theta = potential_temperature_k(air_temperature_k, pressure_pa);
    if (specific_humidity_kg_kg <= 0.0) {
        return theta;
    }
    const double r = specific_humidity_kg_kg / (1.0 - specific_humidity_kg_kg);  // mixing ratio
    const double e = vapour_pressure_pa(specific_humidity_kg_kg, pressure_pa);
    const double t_d = dew_point_k(e);
    const double t = air_temperature_k;
    // Bolton (1980) eq. 15: temperature at the lifting condensation level.
    const double t_l = 1.0 / (1.0 / (t_d - 56.0) + std::log(t / t_d) / 800.0) + 56.0;
    // Bolton (1980) eq. 43.
    const double p_hpa = pressure_pa / 100.0;
    return t * std::pow(1000.0 / p_hpa, 0.2854 * (1.0 - 0.28 * r)) *
           std::exp((3376.0 / t_l - 2.54) * r * (1.0 + 0.81 * r));
}

void validate_column_config(const ColumnConfig& config) {
    if (config.layer_count < 2) {
        throw std::invalid_argument("column needs at least 2 layers");
    }
    require_finite(config.layer_thickness_m, "layer thickness must be finite");
    if (config.layer_thickness_m <= 0.0) {
        throw std::invalid_argument("layer thickness must be positive");
    }
    require_finite(config.environment_lapse_rate_k_m, "environment lapse rate must be finite");
    if (std::abs(config.environment_lapse_rate_k_m) > 0.05) {
        throw std::invalid_argument("environment lapse rate magnitude must not exceed 0.05 K/m");
    }
    require_finite(config.tropopause_height_m, "tropopause height must be finite");
    if (config.tropopause_height_m < 0.0) {
        throw std::invalid_argument("tropopause height must not be negative");
    }
    require_finite(config.humidity_scale_height_m, "humidity scale height must be finite");
    if (config.humidity_scale_height_m <= 0.0) {
        throw std::invalid_argument("humidity scale height must be positive");
    }
    require_finite(config.parcel_step_m, "parcel step must be finite");
    if (config.parcel_step_m <= 0.0 || config.parcel_step_m > config.layer_thickness_m) {
        throw std::invalid_argument("parcel step must be positive and no larger than the layer thickness");
    }
    require_finite(config.inversion_base_m, "inversion base must be finite");
    require_finite(config.inversion_depth_m, "inversion depth must be finite");
    require_finite(config.inversion_strength_k, "inversion strength must be finite");
    if (config.inversion_base_m < 0.0 || config.inversion_depth_m < 0.0 || config.inversion_strength_k < 0.0) {
        throw std::invalid_argument("inversion base, depth and strength must not be negative");
    }
    if (config.inversion_strength_k > 0.0 && config.inversion_depth_m <= 0.0) {
        throw std::invalid_argument("an inversion needs a positive depth");
    }
    require_finite(config.lapse_rate_above_inversion_k_m, "lapse rate above the inversion must be finite");
    if (config.lapse_rate_above_inversion_k_m > 0.05) {
        throw std::invalid_argument("lapse rate above the inversion must not exceed 0.05 K/m");
    }
}

void validate_column(const AtmosphericColumn& c) {
    const std::size_t n = c.height_m.size();
    if (n < 2 || c.interface_height_m.size() != n + 1 || c.interface_pressure_pa.size() != n + 1 ||
        c.pressure_pa.size() != n || c.air_temperature_k.size() != n ||
        c.specific_humidity_kg_kg.size() != n || c.cloud_water_kg_kg.size() != n) {
        throw std::invalid_argument("column arrays must describe at least 2 consistent layers");
    }
    for (std::size_t k = 0; k < n; ++k) {
        if (!(c.interface_height_m[k] < c.height_m[k] && c.height_m[k] < c.interface_height_m[k + 1])) {
            throw std::invalid_argument("column layer heights must increase with index");
        }
        if (!(c.interface_pressure_pa[k] > c.pressure_pa[k] && c.pressure_pa[k] > c.interface_pressure_pa[k + 1]) ||
            c.interface_pressure_pa[k + 1] <= 0.0) {
            throw std::invalid_argument("column pressure must decrease with height and stay positive");
        }
        if (!std::isfinite(c.air_temperature_k[k]) || c.air_temperature_k[k] <= 0.0) {
            throw std::invalid_argument("column temperature must be finite and positive");
        }
        if (!std::isfinite(c.specific_humidity_kg_kg[k]) || c.specific_humidity_kg_kg[k] < 0.0 ||
            c.specific_humidity_kg_kg[k] >= 1.0) {
            throw std::invalid_argument("column specific humidity must be within [0, 1)");
        }
        if (!std::isfinite(c.cloud_water_kg_kg[k]) || c.cloud_water_kg_kg[k] < 0.0) {
            throw std::invalid_argument("column cloud water must be finite and non-negative");
        }
    }
}

AtmosphericColumn build_hydrostatic_column(
    double surface_pressure_pa,
    double surface_temperature_k,
    double surface_specific_humidity_kg_kg,
    const ColumnConfig& config) {
    validate_column_config(config);
    if (!std::isfinite(surface_pressure_pa) || surface_pressure_pa <= 0.0) {
        throw std::invalid_argument("surface pressure must be finite and positive");
    }
    if (!std::isfinite(surface_temperature_k) || surface_temperature_k <= 0.0) {
        throw std::invalid_argument("surface temperature must be finite and positive");
    }
    if (!std::isfinite(surface_specific_humidity_kg_kg) || surface_specific_humidity_kg_kg < 0.0 ||
        surface_specific_humidity_kg_kg >= 1.0) {
        throw std::invalid_argument("surface specific humidity must be within [0, 1)");
    }
    const auto environment_t = [&](double z) {
        const double zc = std::min(z, config.tropopause_height_m);
        if (config.inversion_strength_k <= 0.0) {
            return surface_temperature_k - config.environment_lapse_rate_k_m * zc;
        }
        // Capping inversion: below the base the environment lapse applies,
        // through the layer the temperature rises by the strength, above it
        // the (steeper) elevated-mixed-layer lapse rate applies.
        const double base = config.inversion_base_m, top = base + config.inversion_depth_m;
        const double above = config.lapse_rate_above_inversion_k_m < 0.0
                                 ? config.environment_lapse_rate_k_m : config.lapse_rate_above_inversion_k_m;
        const double t_base = surface_temperature_k - config.environment_lapse_rate_k_m * std::min(base, zc);
        if (zc <= base) return t_base;
        if (zc <= top) return t_base + config.inversion_strength_k * (zc - base) / config.inversion_depth_m;
        return t_base + config.inversion_strength_k - above * (zc - top);
    };
    const auto environment_q = [&](double z) {
        return surface_specific_humidity_kg_kg * std::exp(-z / config.humidity_scale_height_m);
    };
    const double top = static_cast<double>(config.layer_count) * config.layer_thickness_m;
    if (environment_t(top) <= 0.0) {
        throw std::invalid_argument("environment lapse rate drives the temperature below 0 K within the column");
    }

    AtmosphericColumn c;
    const std::size_t n = config.layer_count;
    c.interface_height_m.resize(n + 1);
    c.interface_pressure_pa.resize(n + 1);
    c.height_m.resize(n);
    c.pressure_pa.resize(n);
    c.air_temperature_k.resize(n);
    c.specific_humidity_kg_kg.resize(n);
    c.cloud_water_kg_kg.assign(n, 0.0);

    c.interface_height_m[0] = 0.0;
    c.interface_pressure_pa[0] = surface_pressure_pa;
    double z = 0.0;
    double p = surface_pressure_pa;
    double tv = virtual_temperature(surface_temperature_k, surface_specific_humidity_kg_kg);
    for (std::size_t k = 0; k < n; ++k) {
        const double half = 0.5 * config.layer_thickness_m;
        const double z_mid = z + half;
        const double z_top = z + config.layer_thickness_m;
        const double t_mid = environment_t(z_mid), q_mid = environment_q(z_mid);
        const double t_top = environment_t(z_top), q_top = environment_q(z_top);
        const double tv_mid = virtual_temperature(t_mid, q_mid);
        const double tv_top = virtual_temperature(t_top, q_top);
        const double p_mid = hydrostatic_step(p, tv, tv_mid, half);
        const double p_top = hydrostatic_step(p_mid, tv_mid, tv_top, half);
        c.height_m[k] = z_mid;
        c.pressure_pa[k] = p_mid;
        c.air_temperature_k[k] = t_mid;
        c.specific_humidity_kg_kg[k] = q_mid;
        c.interface_height_m[k + 1] = z_top;
        c.interface_pressure_pa[k + 1] = p_top;
        z = z_top;
        p = p_top;
        tv = tv_top;
    }
    rediagnose_column(c);
    return c;
}

void rediagnose_column(AtmosphericColumn& c) {
    validate_column(c);
    const std::size_t n = c.height_m.size();
    c.air_density_kg_m3.resize(n);
    c.potential_temperature_k.resize(n);
    c.relative_humidity_fraction.resize(n);
    c.layer_mass_kg_m2.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        c.air_density_kg_m3[k] = moist_air_density_kg_m3(
            c.pressure_pa[k], c.air_temperature_k[k], c.specific_humidity_kg_kg[k]);
        c.potential_temperature_k[k] = potential_temperature_k(c.air_temperature_k[k], c.pressure_pa[k]);
        c.relative_humidity_fraction[k] = relative_humidity_fraction(
            c.air_temperature_k[k], c.specific_humidity_kg_kg[k], c.pressure_pa[k]);
        c.layer_mass_kg_m2[k] = (c.interface_pressure_pa[k] - c.interface_pressure_pa[k + 1]) / kGravityMS2;
    }
}

double column_pressure_at_height_pa(const AtmosphericColumn& c, double height_m) {
    validate_column(c);
    return PressureTable(c).at(height_m);
}

double column_temperature_at_height_k(const AtmosphericColumn& c, double height_m) {
    validate_column(c);
    return interpolate(c.height_m, c.air_temperature_k, height_m);
}

double column_humidity_at_height_kg_kg(const AtmosphericColumn& c, double height_m) {
    validate_column(c);
    return std::max(0.0, interpolate(c.height_m, c.specific_humidity_kg_kg, height_m));
}

std::optional<double> column_freezing_level_m(const AtmosphericColumn& c) {
    validate_column(c);
    const double t0 = moist_air::kCelsiusOffsetK;
    // Surface value from the first two mid-levels (linear extrapolation).
    const double surface = interpolate(c.height_m, c.air_temperature_k, c.interface_height_m.front());
    if (surface <= t0) {
        return c.air_temperature_k.front() <= t0 || surface <= t0 ? std::optional<double>(0.0) : std::nullopt;
    }
    double previous_z = c.interface_height_m.front(), previous_t = surface;
    for (std::size_t k = 0; k < c.height_m.size(); ++k) {
        const double t = c.air_temperature_k[k];
        if (t <= t0) {
            const double w = (previous_t - t0) / (previous_t - t);
            return previous_z + w * (c.height_m[k] - previous_z);
        }
        previous_z = c.height_m[k];
        previous_t = t;
    }
    return std::nullopt;
}

double column_precipitable_water_mm(const AtmosphericColumn& c) {
    validate_column(c);
    double total = 0.0;
    for (std::size_t k = 0; k < c.height_m.size(); ++k) {
        total += c.specific_humidity_kg_kg[k] * c.layer_mass_kg_m2[k];
    }
    return total;
}

ParcelAscent lift_surface_parcel(const AtmosphericColumn& c, const ColumnConfig& config) {
    validate_column(c);
    return lift_parcel(c, config, c.air_temperature_k[0], c.specific_humidity_kg_kg[0]);
}

ParcelAscent lift_parcel(
    const AtmosphericColumn& c,
    const ColumnConfig& config,
    double parcel_temperature_k,
    double parcel_specific_humidity_kg_kg) {
    validate_column_config(config);
    validate_column(c);
    if (!std::isfinite(parcel_temperature_k) || parcel_temperature_k <= 0.0 ||
        !std::isfinite(parcel_specific_humidity_kg_kg) || parcel_specific_humidity_kg_kg < 0.0 ||
        parcel_specific_humidity_kg_kg >= 1.0) {
        throw std::invalid_argument("parcel temperature must be positive and humidity within [0, 1)");
    }
    ParcelAscent a;
    const double z0 = c.height_m[0];
    const double top = c.interface_height_m.back();
    const PressureTable table(c);
    const auto pressure_at = [&](const AtmosphericColumn&, double z) { return table.at(z); };
    const double q0 = parcel_specific_humidity_kg_kg;
    const double theta0 = potential_temperature_k(parcel_temperature_k, c.pressure_pa[0]);
    const auto dry_temperature = [&](double z) { return theta0 * exner(pressure_at(c, z)); };
    // The pseudo-adiabat is dT/dp = Gamma_m R_d T / (g p). The parcel sits at
    // the environment pressure p(z), whose hydrostatic slope uses the
    // environment virtual temperature, so in height coordinates
    // dT/dz = -Gamma_m(T, p) * T / Tv_env(z).
    const auto moist_slope = [&](double z, double t) {
        const double tv_env = virtual_temperature(
            interpolate(c.height_m, c.air_temperature_k, z),
            interpolate(c.height_m, c.specific_humidity_kg_kg, z));
        return -moist_adiabatic_lapse_rate_k_m(t, pressure_at(c, z)) * t / tv_env;
    };
    const auto moist_step = [&](double z_from, double t_from, double z_to) {
        // RK2 (midpoint) with sub-steps of at most 25 m.
        const int sub = std::max(1, static_cast<int>(std::ceil((z_to - z_from) / 25.0)));
        const double h = (z_to - z_from) / sub;
        double z = z_from, t = t_from;
        for (int i = 0; i < sub; ++i) {
            const double k1 = moist_slope(z, t);
            const double k2 = moist_slope(z + 0.5 * h, t + 0.5 * h * k1);
            t += h * k2;
            z += h;
        }
        return t;
    };

    bool saturated = q0 >= saturation_specific_humidity_kg_kg(parcel_temperature_k, c.pressure_pa[0]);
    if (saturated) {
        a.lcl_height_m = z0;
    }
    double z = z0;
    double t = parcel_temperature_k;
    const auto record = [&](double height, double temperature) {
        const double p = pressure_at(c, height);
        const double q_parcel = saturated ? std::min(q0, saturation_specific_humidity_kg_kg(temperature, p)) : q0;
        const double t_env = interpolate(c.height_m, c.air_temperature_k, height);
        const double q_env = interpolate(c.height_m, c.specific_humidity_kg_kg, height);
        const double tv_parcel = virtual_temperature(temperature, q_parcel);
        const double tv_env = virtual_temperature(t_env, q_env);
        a.height_m.push_back(height);
        a.pressure_pa.push_back(p);
        a.temperature_k.push_back(temperature);
        a.specific_humidity_kg_kg.push_back(q_parcel);
        a.condensed_water_kg_kg.push_back(q0 - q_parcel);
        a.environment_temperature_k.push_back(t_env);
        a.buoyancy_m_s2.push_back(kGravityMS2 * (tv_parcel - tv_env) / tv_env);
    };
    record(z, t);

    while (z + 1.0e-9 < top) {
        const double z_next = std::min(top, z + config.parcel_step_m);
        if (!saturated) {
            const double t_next = dry_temperature(z_next);
            if (q0 >= saturation_specific_humidity_kg_kg(t_next, pressure_at(c, z_next))) {
                // Locate the LCL on the dry adiabat between z and z_next by bisection.
                double lo = z, hi = z_next;
                for (int i = 0; i < 50; ++i) {
                    const double mid = 0.5 * (lo + hi);
                    if (q0 >= saturation_specific_humidity_kg_kg(dry_temperature(mid), pressure_at(c, mid))) {
                        hi = mid;
                    } else {
                        lo = mid;
                    }
                }
                const double z_lcl = hi;
                a.lcl_height_m = z_lcl;
                saturated = true;
                t = moist_step(z_lcl, dry_temperature(z_lcl), z_next);
            } else {
                t = t_next;
            }
        } else {
            t = moist_step(z, t, z_next);
        }
        z = z_next;
        record(z, t);
    }

    // Level of free convection: first sample at or above the LCL with positive
    // buoyancy. Equilibrium level: top of the last positive area above it.
    if (a.lcl_height_m) {
        const std::size_t n = a.height_m.size();
        std::size_t lfc = n;
        for (std::size_t i = 0; i < n; ++i) {
            if (a.height_m[i] + 1.0e-9 >= *a.lcl_height_m && a.buoyancy_m_s2[i] > 0.0) {
                lfc = i;
                break;
            }
        }
        if (lfc < n) {
            a.lfc_height_m = a.height_m[lfc];
            std::size_t el = lfc;
            for (std::size_t i = lfc; i < n; ++i) {
                if (a.buoyancy_m_s2[i] > 0.0) {
                    el = i;
                }
            }
            a.equilibrium_level_m = a.height_m[el];
            for (std::size_t i = lfc; i < el; ++i) {
                const double dz = a.height_m[i + 1] - a.height_m[i];
                a.cape_j_kg += 0.5 * dz * (std::max(a.buoyancy_m_s2[i], 0.0) + std::max(a.buoyancy_m_s2[i + 1], 0.0));
            }
            for (std::size_t i = 0; i < lfc; ++i) {
                const double dz = a.height_m[i + 1] - a.height_m[i];
                a.cin_j_kg -= 0.5 * dz * (std::min(a.buoyancy_m_s2[i], 0.0) + std::min(a.buoyancy_m_s2[i + 1], 0.0));
            }
            if (a.cape_j_kg <= 0.0) {
                // A single positively buoyant sample has no area: no free convection.
                a.lfc_height_m.reset();
                a.equilibrium_level_m.reset();
                a.cin_j_kg = 0.0;
            }
        }
    }
    a.convective_potential = a.cape_j_kg < 100.0 ? "none"
                           : a.cape_j_kg < 1000.0 ? "weak"
                           : a.cape_j_kg < 2500.0 ? "moderate" : "strong";
    return a;
}

ConvectiveAdjustmentReport adjust_column_convection(AtmosphericColumn& c) {
    rediagnose_column(c);
    ConvectiveAdjustmentReport report;
    const std::size_t n = c.height_m.size();
    std::vector<double> exner_k(n);
    for (std::size_t k = 0; k < n; ++k) {
        exner_k[k] = exner(c.pressure_pa[k]);
    }
    const auto theta = [&](std::size_t k) { return c.air_temperature_k[k] / exner_k[k]; };

    for (report.iterations = 1; report.iterations <= 50; ++report.iterations) {
        bool changed = false;
        // Dry convective adjustment: mix adjacent layers while theta decreases
        // with height, conserving enthalpy sum(m c_p T) and water.
        for (std::size_t sweep = 0; sweep < n * n; ++sweep) {
            bool mixed = false;
            for (std::size_t k = 0; k + 1 < n; ++k) {
                if (theta(k) > theta(k + 1) * (1.0 + 1.0e-12)) {
                    const double m0 = c.layer_mass_kg_m2[k], m1 = c.layer_mass_kg_m2[k + 1];
                    const double theta_bar = (m0 * c.air_temperature_k[k] + m1 * c.air_temperature_k[k + 1]) /
                                             (m0 * exner_k[k] + m1 * exner_k[k + 1]);
                    c.air_temperature_k[k] = theta_bar * exner_k[k];
                    c.air_temperature_k[k + 1] = theta_bar * exner_k[k + 1];
                    const double q_bar = (m0 * c.specific_humidity_kg_kg[k] + m1 * c.specific_humidity_kg_kg[k + 1]) / (m0 + m1);
                    c.specific_humidity_kg_kg[k] = c.specific_humidity_kg_kg[k + 1] = q_bar;
                    const double l_bar = (m0 * c.cloud_water_kg_kg[k] + m1 * c.cloud_water_kg_kg[k + 1]) / (m0 + m1);
                    c.cloud_water_kg_kg[k] = c.cloud_water_kg_kg[k + 1] = l_bar;
                    ++report.dry_mixing_events;
                    mixed = changed = true;
                }
            }
            if (!mixed) {
                break;
            }
        }
        // Saturation adjustment of every layer.
        for (std::size_t k = 0; k < n; ++k) {
            const double qs = saturation_specific_humidity_kg_kg(c.air_temperature_k[k], c.pressure_pa[k]);
            const bool supersaturated = c.specific_humidity_kg_kg[k] > qs * (1.0 + 1.0e-9);
            const bool evaporable = c.cloud_water_kg_kg[k] > 0.0 && c.specific_humidity_kg_kg[k] < qs * (1.0 - 1.0e-9);
            if (!supersaturated && !evaporable) {
                continue;
            }
            const double change = saturation_adjustment(
                c.air_temperature_k[k], c.specific_humidity_kg_kg[k], c.cloud_water_kg_kg[k], c.pressure_pa[k]);
            if (std::abs(change) > 1.0e-12) {
                changed = true;
                if (change > 0.0) {
                    ++report.condensing_layers;
                }
                report.condensed_water_kg_m2 += c.layer_mass_kg_m2[k] * change;
                report.latent_heating_j_m2 += c.layer_mass_kg_m2[k] * kLatentHeatVaporisationJkg * change;
            }
        }
        if (!changed) {
            break;
        }
    }
    rediagnose_column(c);
    return report;
}

ColumnFieldDiagnostics diagnose_column_field(
    const std::vector<double>& surface_pressure_pa,
    const std::vector<double>& surface_temperature_k,
    const std::vector<double>& surface_specific_humidity_kg_kg,
    const ColumnConfig& config,
    double parcel_heating_k) {
    validate_column_config(config);
    if (!std::isfinite(parcel_heating_k) || parcel_heating_k < 0.0) {
        throw std::invalid_argument("parcel heating must be finite and non-negative");
    }
    const std::size_t count = surface_pressure_pa.size();
    if (surface_temperature_k.size() != count || surface_specific_humidity_kg_kg.size() != count) {
        throw std::invalid_argument("column field inputs must have the same cell count");
    }
    ColumnFieldDiagnostics d;
    d.lcl_height_m.assign(count, kNaN);
    d.lfc_height_m.assign(count, kNaN);
    d.equilibrium_level_m.assign(count, kNaN);
    d.cape_j_kg.assign(count, 0.0);
    d.cin_j_kg.assign(count, 0.0);
    d.convective_potential.assign(count, "none");
    d.condensed_water_kg_m2.assign(count, 0.0);
    d.column_mass_kg_m2.assign(count, 0.0);
    d.top_pressure_pa.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        auto column = build_hydrostatic_column(
            surface_pressure_pa[i], surface_temperature_k[i], surface_specific_humidity_kg_kg[i], config);
        const auto report = adjust_column_convection(column);
        const auto ascent = lift_parcel(
            column, config, column.air_temperature_k[0] + parcel_heating_k, column.specific_humidity_kg_kg[0]);
        if (ascent.lcl_height_m) d.lcl_height_m[i] = *ascent.lcl_height_m;
        if (ascent.lfc_height_m) d.lfc_height_m[i] = *ascent.lfc_height_m;
        if (ascent.equilibrium_level_m) d.equilibrium_level_m[i] = *ascent.equilibrium_level_m;
        d.cape_j_kg[i] = ascent.cape_j_kg;
        d.cin_j_kg[i] = ascent.cin_j_kg;
        d.convective_potential[i] = ascent.convective_potential;
        d.condensed_water_kg_m2[i] = report.condensed_water_kg_m2;
        d.column_mass_kg_m2[i] = (column.interface_pressure_pa.front() - column.interface_pressure_pa.back()) / kGravityMS2;
        d.top_pressure_pa[i] = column.interface_pressure_pa.back();
    }
    return d;
}

}  // namespace terraflux::weather
