#include "terraflux/weather/ocean_carbon.hpp"

#include "terraflux/weather/marine_indicators.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kBoltzmannEv = 8.617333262e-5;   // eV/K
constexpr double kAtmosphericO2Kpa = 0.20946 * 101.325;

void check_water(double t, double s) {
    if (!std::isfinite(t) || t < 271.0 || t > 313.15 || !std::isfinite(s) || s < 0.0 || s > 45.0) throw std::invalid_argument("carbonate chemistry needs -2..40 degC and 0..45 psu");
}

}  // namespace

CarbonateConstants carbonate_constants(double t, double salinity) {
    check_water(t, salinity);
    // The empirical fits are made for sea water; brackish water near river mouths is held at 5 psu.
    const double s = std::max(5.0, salinity), sq = std::sqrt(s), lt = std::log(t);
    CarbonateConstants c;
    const double t100 = t / 100.0;
    c.k0 = std::exp(-60.2409 + 93.4517 / t100 + 23.3585 * std::log(t100) + s * (0.023517 - 0.023656 * t100 + 0.0047036 * t100 * t100));
    c.k1 = std::pow(10.0, -(3633.86 / t - 61.2172 + 9.6777 * lt - 0.011555 * s + 0.0001152 * s * s));
    c.k2 = std::pow(10.0, -(471.78 / t + 25.929 - 3.16967 * lt - 0.01781 * s + 0.0001122 * s * s));
    c.kb = std::exp((-8966.90 - 2890.53 * sq - 77.942 * s + 1.728 * s * sq - 0.0996 * s * s) / t + 148.0248 + 137.1942 * sq + 1.62142 * s -
                    (24.4344 + 25.085 * sq + 0.2474 * s) * lt + 0.053105 * sq * t);
    c.kw = std::exp(148.9652 - 13847.26 / t - 23.6521 * lt + (118.67 / t - 5.977 + 1.0495 * lt) * sq - 0.01615 * s);
    const double lg = std::log10(t);
    c.ksp_aragonite = std::pow(10.0, -171.945 - 0.077993 * t + 2903.293 / t + 71.595 * lg + (-0.068393 + 0.0017276 * t + 88.135 / t) * sq - 0.10018 * s + 0.0059415 * s * sq);
    c.ksp_calcite = std::pow(10.0, -171.9065 - 0.077993 * t + 2839.319 / t + 71.595 * lg + (-0.77712 + 0.0028426 * t + 178.34 / t) * sq - 0.07711 * s + 0.0041249 * s * sq);
    c.total_boron = 0.000416 * s / 35.0;
    c.calcium = 0.010282 * s / 35.0;
    return c;
}

CarbonateState solve_carbonate(double dic_umol, double ta_umol, double t, double salinity) {
    check_water(t, salinity);
    if (!std::isfinite(dic_umol) || !std::isfinite(ta_umol) || dic_umol < 0.0 || ta_umol <= 0.0 || dic_umol > 6000.0 || ta_umol > 6000.0) {
        throw std::invalid_argument("carbonate chemistry needs DIC and alkalinity within 0-6000 umol/kg");
    }
    const CarbonateConstants c = carbonate_constants(t, salinity);
    const double dic = dic_umol * 1.0e-6, ta = ta_umol * 1.0e-6;
    // Residual TA(h) - TA, decreasing in h; Newton in h with a bisection bracket in log h.
    auto residual = [&](double h, double* slope) {
        const double d = h * h + c.k1 * h + c.k1 * c.k2, n = c.k1 * h + 2.0 * c.k1 * c.k2;
        const double borate = c.total_boron * c.kb / (c.kb + h);
        const double value = dic * n / d + borate + c.kw / h - h - ta;
        if (slope) *slope = dic * (c.k1 * d - n * (2.0 * h + c.k1)) / (d * d) - c.total_boron * c.kb / ((c.kb + h) * (c.kb + h)) - c.kw / (h * h) - 1.0;
        return value;
    };
    double lo = 1.0e-12, hi = 1.0e-3, h = std::pow(10.0, -8.1);
    if (residual(lo, nullptr) < 0.0 || residual(hi, nullptr) > 0.0) throw std::invalid_argument("carbonate chemistry: no pH between 3 and 12 for this DIC and alkalinity");
    for (int it = 0; it < 100; ++it) {
        double slope = 0.0;
        const double f = residual(h, &slope);
        if (f > 0.0) lo = h; else hi = h;   // f decreases with h: the root is above h when f > 0
        double next = slope < 0.0 ? h - f / slope : 0.0;
        if (!(next > lo && next < hi)) next = std::sqrt(lo * hi);   // bisection in log h
        if (std::abs(next - h) <= 1.0e-13 * h) { h = next; break; }
        h = next;
    }
    const double d = h * h + c.k1 * h + c.k1 * c.k2;
    CarbonateState out;
    out.ph_total = -std::log10(h);
    out.co2_umol_kg = dic_umol * h * h / d;
    out.bicarbonate_umol_kg = dic_umol * c.k1 * h / d;
    out.carbonate_umol_kg = dic_umol * c.k1 * c.k2 / d;
    out.pco2_uatm = out.co2_umol_kg * 1.0e-6 / c.k0 * 1.0e6;
    out.omega_aragonite = c.calcium * out.carbonate_umol_kg * 1.0e-6 / c.ksp_aragonite;
    out.omega_calcite = c.calcium * out.carbonate_umol_kg * 1.0e-6 / c.ksp_calcite;
    return out;
}

double dic_at_equilibrium(double pco2, double ta, double t, double s) {
    if (!std::isfinite(pco2) || pco2 <= 0.0 || pco2 > 20000.0) throw std::invalid_argument("equilibrium DIC needs a pCO2 within 0-20000 uatm");
    // pCO2 rises monotonically with DIC at fixed alkalinity: bisection.
    double lo = 0.5 * ta, hi = std::min(5999.0, 1.5 * ta + 500.0);
    for (int it = 0; it < 80; ++it) {
        const double mid = 0.5 * (lo + hi);
        (solve_carbonate(mid, ta, t, s).pco2_uatm < pco2 ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

double revelle_factor(double dic, double ta, double t, double s) {
    const double step = 1.0e-3 * dic;
    const double p0 = solve_carbonate(dic - step, ta, t, s).pco2_uatm, p1 = solve_carbonate(dic + step, ta, t, s).pco2_uatm;
    const double p = solve_carbonate(dic, ta, t, s).pco2_uatm;
    return ((p1 - p0) / p) / (2.0 * step / dic);
}

double co2_schmidt_number(double t) {
    const double c = std::max(-2.0, std::min(40.0, t - 273.15));
    return 2116.8 - 136.25 * c + 4.7353 * c * c - 0.092307 * c * c * c + 0.0007555 * c * c * c * c;
}

double o2_schmidt_number(double t) {
    const double c = std::max(-2.0, std::min(40.0, t - 273.15));
    return 1920.4 - 135.6 * c + 5.2122 * c * c - 0.10939 * c * c * c + 0.00093777 * c * c * c * c;
}

double gas_transfer_velocity_m_s(double wind, double schmidt) {
    if (!std::isfinite(wind) || wind < 0.0 || !(schmidt > 0.0)) throw std::invalid_argument("gas transfer needs a wind speed and a positive Schmidt number");
    return 0.251 * wind * wind * std::pow(schmidt / 660.0, -0.5) / 3.6e5;   // cm/h -> m/s
}

double oxygen_partial_pressure_kpa(double o2, double t, double s) {
    const double saturation = oxygen_solubility_umol_kg(std::max(271.0, std::min(313.0, t)), std::max(0.0, std::min(42.0, s)));
    return std::max(0.0, o2) / saturation * kAtmosphericO2Kpa;
}

double metabolic_index(double o2, double t, double s, const MetabolicIndexConfig& c) {
    if (!(c.critical_po2_kpa > 0.0) || !(c.reference_temperature_k > 0.0)) throw std::invalid_argument("metabolic index needs a positive critical pO2 and reference temperature");
    const double critical = c.critical_po2_kpa * std::exp(-c.temperature_sensitivity_ev / kBoltzmannEv * (1.0 / t - 1.0 / c.reference_temperature_k));
    return oxygen_partial_pressure_kpa(o2, t, s) / critical;
}

}  // namespace terraflux::weather
