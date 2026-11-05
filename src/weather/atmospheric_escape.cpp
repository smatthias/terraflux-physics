#include "terraflux/weather/atmospheric_escape.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBoltzmann = 1.380649e-23;
constexpr double kAmu = 1.66053907e-27;
constexpr double kHydrogenMass = 1.00784 * kAmu;
constexpr double kGM = 3.986004418e14;
constexpr double kEarthRadius = 6.371e6;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

double exobase_radius(const HydrogenEscapeConfig& c) { return kEarthRadius + c.exobase_altitude_m; }
double exobase_area(const HydrogenEscapeConfig& c) { return 4.0 * kPi * exobase_radius(c) * exobase_radius(c); }

// Hydrogen scale height at the exobase (m): the reservoir is n H A.
double scale_height(double t, const HydrogenEscapeConfig& c) {
    const double r = exobase_radius(c);
    return kBoltzmann * t / (kHydrogenMass * kGM / (r * r));
}

double loss_coefficient(double t, double open_area, const HydrogenEscapeConfig& c) {   // m^3/s: escape = n * this
    return exobase_area(c) * (jeans_effusion_velocity_m_s(t, 1.00784, exobase_radius(c)) + charge_exchange_velocity_m_s(c)) + open_area * c.polar_wind_velocity_m_s;
}

HydrogenEscape partition(double n, double t, double open_area, double reservoir, const HydrogenEscapeConfig& c) {
    HydrogenEscape e;
    e.supply_per_s = diffusion_limited_hydrogen_per_s(c);
    e.exobase_density_m3 = n;
    e.jeans_per_s = n * exobase_area(c) * jeans_effusion_velocity_m_s(t, 1.00784, exobase_radius(c));
    e.charge_exchange_per_s = n * exobase_area(c) * charge_exchange_velocity_m_s(c);
    e.polar_wind_per_s = n * open_area * c.polar_wind_velocity_m_s;
    e.total_per_s = e.jeans_per_s + e.charge_exchange_per_s + e.polar_wind_per_s;
    e.reservoir_atoms = reservoir;
    return e;
}

}  // namespace

void validate_hydrogen_escape(const HydrogenEscapeConfig& c) {
    require(within(c.water_ppmv, 0.0, 1.0e4) && within(c.methane_ppmv, 0.0, 1.0e4) && within(c.hydrogen_ppmv, 0.0, 1.0e4),
            "hydrogen: water, methane and hydrogen mixing ratios within 0-1e4 ppmv");
    require(c.water_ppmv + c.methane_ppmv + c.hydrogen_ppmv > 0.0, "hydrogen: the mixing ratios must not all be zero");
    require(within(c.limiting_flux_coefficient, 1.0e12, 1.0e14) && within(c.homopause_altitude_m, 50.0e3, 200.0e3) && within(c.exobase_altitude_m, 200.0e3, 2000.0e3),
            "hydrogen: limiting_flux_coefficient within 1e12-1e14, homopause within 50-200 km, exobase within 200-2000 km");
    require(within(c.jeans_share_at_1000k, 0.01, 0.99) && within(c.polar_wind_velocity_m_s, 0.0, 1.0e4), "hydrogen: jeans_share_at_1000k within 0.01-0.99, polar_wind_velocity_m_s within 0-1e4");
}

double total_hydrogen_mixing_ratio(const HydrogenEscapeConfig& c) { return (c.hydrogen_ppmv + c.water_ppmv + 2.0 * c.methane_ppmv) * 1.0e-6; }

double diffusion_limited_hydrogen_per_s(const HydrogenEscapeConfig& c) {
    const double r = kEarthRadius + c.homopause_altitude_m;
    return 2.0 * c.limiting_flux_coefficient * total_hydrogen_mixing_ratio(c) * 4.0 * kPi * r * r * 1.0e4;   // cm^2
}

double jeans_effusion_velocity_m_s(double t, double mass_amu, double r) {
    require(t > 0.0 && mass_amu > 0.0 && r > 0.0, "Jeans escape needs a positive temperature, mass and radius");
    const double m = mass_amu * kAmu;
    const double lambda = kGM * m / (kBoltzmann * t * r);
    const double v0 = std::sqrt(2.0 * kBoltzmann * t / m);
    return v0 / (2.0 * std::sqrt(kPi)) * (1.0 + lambda) * std::exp(-lambda);
}

double charge_exchange_velocity_m_s(const HydrogenEscapeConfig& c) {
    const double jeans = jeans_effusion_velocity_m_s(1000.0, 1.00784, exobase_radius(c));
    return jeans * (1.0 - c.jeans_share_at_1000k) / c.jeans_share_at_1000k;
}

HydrogenEscape hydrogen_escape_equilibrium(double t, double open_area, const HydrogenEscapeConfig& c) {
    validate_hydrogen_escape(c);
    const double n = diffusion_limited_hydrogen_per_s(c) / loss_coefficient(t, open_area, c);
    return partition(n, t, open_area, n * scale_height(t, c) * exobase_area(c), c);
}

HydrogenEscape step_hydrogen_escape(double reservoir, double t, double open_area, double hours, const HydrogenEscapeConfig& c) {
    const double volume = scale_height(t, c) * exobase_area(c);   // reservoir = n * volume
    const double loss = loss_coefficient(t, open_area, c);
    const double eq = diffusion_limited_hydrogen_per_s(c) * volume / loss;
    const double next = eq + (reservoir - eq) * std::exp(-hours * 3600.0 * loss / volume);
    return partition(next / volume, t, open_area, next, c);
}

void validate_ion_outflow(const IonOutflowConfig& c) {
    require(within(c.reference_rate_per_s, 0.0, 1.0e28) && within(c.kp_coefficient, 0.0, 2.0) && within(c.f107_coefficient, 0.0, 0.1),
            "ion_outflow: reference_rate_per_s within 0-1e28, kp_coefficient within 0-2, f107_coefficient within 0-0.1");
    require(within(c.f107_reference, 50.0, 400.0) && within(c.escape_fraction, 0.0, 1.0) && within(c.helium_rate_per_s, 0.0, 1.0e28),
            "ion_outflow: f107_reference within 50-400, escape_fraction within 0-1, helium_rate_per_s within 0-1e28");
}

double oxygen_upflow_per_s(double kp, double f107, double area_ratio, const IonOutflowConfig& c) {
    return c.reference_rate_per_s * std::exp(c.kp_coefficient * kp + c.f107_coefficient * (f107 - c.f107_reference)) * std::max(0.0, area_ratio);
}

double oxygen_escape_per_s(double kp, double f107, double area_ratio, const IonOutflowConfig& c) { return c.escape_fraction * oxygen_upflow_per_s(kp, f107, area_ratio, c); }

double oxygen_escape_monthly_per_s(double kp, double sd, double f107, double area_ratio, const IonOutflowConfig& c) {
    return oxygen_escape_per_s(kp, f107, area_ratio, c) * std::exp(0.5 * c.kp_coefficient * c.kp_coefficient * sd * sd);
}

double helium_escape_per_s(double open_area_ratio, double f107, const IonOutflowConfig& c) {
    return c.helium_rate_per_s * std::max(0.0, open_area_ratio) * std::sqrt(std::max(0.0, f107) / c.f107_reference);
}

}  // namespace terraflux::weather
