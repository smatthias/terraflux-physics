#include "terraflux/weather/sea_level.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraflux::weather {

namespace {

void require_range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(std::string("sea-level budget ") + name + " is out of range");
}

}  // namespace

void validate_sea_level_budget(const SeaLevelBudgetConfig& c) {
    require_range(c.thermal_expansion_m_per_k_yr, 0.0, 0.01, "thermal_expansion_m_per_k_yr");
    require_range(c.deep_ocean_time_scale_years, 1.0, 10000.0, "deep_ocean_time_scale_years");
    require_range(c.glacier_m_per_k_yr, 0.0, 0.01, "glacier_m_per_k_yr");
    require_range(c.glacier_store_m, 0.0, 1.0, "glacier_store_m");
    require_range(c.antarctic_base_m_yr, -0.01, 0.05, "antarctic_base_m_yr");
    require_range(c.antarctic_m_per_k_yr, 0.0, 0.05, "antarctic_m_per_k_yr");
    require_range(c.antarctic_threshold_k, -5.0, 10.0, "antarctic_threshold_k");
    require_range(c.land_water_m_yr, -0.005, 0.005, "land_water_m_yr");
    require_range(c.greenland_fingerprint, -2.0, 2.0, "greenland_fingerprint");
    require_range(c.antarctic_fingerprint, -2.0, 2.0, "antarctic_fingerprint");
    require_range(c.glacier_fingerprint, -2.0, 2.0, "glacier_fingerprint");
}

SeaLevelComponents sea_level_budget(const std::vector<int>& years, const std::vector<double>& warming_k, const std::vector<double>& greenland_m,
                                    const SeaLevelBudgetConfig& c) {
    validate_sea_level_budget(c);
    const std::size_t n = years.size();
    if (n < 2 || warming_k.size() != n || greenland_m.size() != n) throw std::invalid_argument("sea-level budget needs annual years with a warming and a Greenland value each");
    std::size_t ref = n;
    for (std::size_t k = 0; k < n; ++k) {
        if (k > 0 && years[k] != years[k - 1] + 1) throw std::invalid_argument("sea-level budget years must be consecutive");
        if (!std::isfinite(warming_k[k]) || std::abs(warming_k[k]) > 30.0 || !std::isfinite(greenland_m[k]) || std::abs(greenland_m[k]) > 20.0) throw std::invalid_argument("sea-level budget inputs must be finite and plausible");
        if (years[k] == c.reference_year) ref = k;
    }
    if (ref == n) throw std::invalid_argument("sea-level budget years must contain the reference year");
    SeaLevelComponents out;
    out.years = years;
    out.warming_k = warming_k;
    for (auto* v : {&out.thermal_expansion_m, &out.glaciers_m, &out.antarctic_m, &out.greenland_m, &out.land_water_m, &out.global_m, &out.regional_m}) v->assign(n, 0.0);
    // Integrate from the first year (taken as the unperturbed state) with annual midpoint steps.
    double e = 0.0, deep = 0.0, g = 0.0, a = 0.0, w = 0.0;
    const double tau = c.deep_ocean_time_scale_years;
    for (std::size_t k = 0; k < n; ++k) {
        if (k > 0) {
            const double t = 0.5 * (warming_k[k] + warming_k[k - 1]);
            // Thermal expansion with the deep-ocean lag (exact over the year for the relaxation).
            const double decay = std::exp(-1.0 / tau);
            const double deep_new = t + (deep - t) * decay;
            e += c.thermal_expansion_m_per_k_yr * (t - 0.5 * (deep + deep_new));
            deep = deep_new;
            if (c.glacier_store_m > 0.0) g += c.glacier_m_per_k_yr * std::max(0.0, t) * std::max(0.0, 1.0 - g / c.glacier_store_m);
            a += c.antarctic_base_m_yr + c.antarctic_m_per_k_yr * std::max(0.0, t - c.antarctic_threshold_k);
            w += c.land_water_m_yr;
        }
        out.thermal_expansion_m[k] = e;
        out.glaciers_m[k] = g;
        out.antarctic_m[k] = a;
        out.land_water_m[k] = w;
        out.greenland_m[k] = greenland_m[k];
    }
    // Count every component from the reference year.
    const double e0 = out.thermal_expansion_m[ref], g0 = out.glaciers_m[ref], a0 = out.antarctic_m[ref], gr0 = out.greenland_m[ref], w0 = out.land_water_m[ref];
    for (std::size_t k = 0; k < n; ++k) {
        out.thermal_expansion_m[k] -= e0;
        out.glaciers_m[k] -= g0;
        out.antarctic_m[k] -= a0;
        out.greenland_m[k] -= gr0;
        out.land_water_m[k] -= w0;
        out.global_m[k] = out.thermal_expansion_m[k] + out.glaciers_m[k] + out.antarctic_m[k] + out.greenland_m[k] + out.land_water_m[k];
        out.regional_m[k] = out.thermal_expansion_m[k] + c.glacier_fingerprint * out.glaciers_m[k] + c.antarctic_fingerprint * out.antarctic_m[k] +
                            c.greenland_fingerprint * out.greenland_m[k] + out.land_water_m[k];
    }
    return out;
}

double gumbel_return_level(double mu, double sigma, double t) {
    if (!(sigma > 0.0) || !(t > 1.0)) throw std::invalid_argument("Gumbel return level needs a positive scale and a return period above one year");
    return mu - sigma * std::log(-std::log(1.0 - 1.0 / t));
}

double gumbel_return_period(double mu, double sigma, double z) {
    if (!(sigma > 0.0)) throw std::invalid_argument("Gumbel return period needs a positive scale");
    const double p = 1.0 - std::exp(-std::exp(-(z - mu) / sigma));   // annual exceedance probability
    return p > 0.0 ? 1.0 / p : std::numeric_limits<double>::infinity();
}

double return_period_after_rise(double mu, double sigma, double t, double rise) {
    return gumbel_return_period(mu + rise, sigma, gumbel_return_level(mu, sigma, t));
}

double uniform_wind_setup_m(double h, double fetch, double tau, double rho, double g) {
    if (!(h > 0.0) || fetch < 0.0) throw std::invalid_argument("wind setup needs a positive depth and a fetch");
    return std::sqrt(h * h + 2.0 * tau * fetch / (rho * g)) - h;
}

double bruun_retreat_m(double rise, double width, double berm, double closure) {
    if (!(width > 0.0) || !(berm + closure > 0.0)) throw std::invalid_argument("Bruun rule needs a positive profile width and height");
    return rise * width / (berm + closure);
}

}  // namespace terraflux::weather
