#include "terraflux/weather/earth_climate.hpp"

#include "terraflux/weather/ocean_carbon.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kEarthRadius = 6.371e6;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

}  // namespace

// ---- Forcing ------------------------------------------------------------------------------------------------------

double ghg_band_overlap(double m, double n) {
    require(m >= 0.0 && n >= 0.0, "greenhouse gas concentrations must not be negative");
    const double mn = m * n;
    return 0.47 * std::log(1.0 + 2.01e-5 * std::pow(mn, 0.75) + 5.31e-15 * m * std::pow(mn, 1.52));
}

double methane_forcing_w_m2(double m, double n0, double m0) {
    require(m > 0.0 && m0 > 0.0 && n0 > 0.0, "methane forcing needs positive concentrations");
    return 0.036 * (std::sqrt(m) - std::sqrt(m0)) - (ghg_band_overlap(m, n0) - ghg_band_overlap(m0, n0));
}

double nitrous_oxide_forcing_w_m2(double n, double m0, double n0) {
    require(n > 0.0 && m0 > 0.0 && n0 > 0.0, "nitrous oxide forcing needs positive concentrations");
    return 0.12 * (std::sqrt(n) - std::sqrt(n0)) - (ghg_band_overlap(m0, n) - ghg_band_overlap(m0, n0));
}

double interpolate_points(const std::vector<std::array<double, 2>>& p, double year) {
    require(!p.empty(), "a time series needs at least one point");
    if (year <= p.front()[0]) return p.front()[1];
    if (year >= p.back()[0]) return p.back()[1];
    for (std::size_t k = 1; k < p.size(); ++k)
        if (year <= p[k][0]) return p[k - 1][1] + (year - p[k - 1][0]) / (p[k][0] - p[k - 1][0]) * (p[k][1] - p[k - 1][1]);
    return p.back()[1];
}

// ---- Feedbacks and the response -----------------------------------------------------------------------------------

void validate_feedbacks(const FeedbackSet& f) {
    require(within(f.planck_w_m2_k, 1.0, 6.0) && within(f.water_vapour_lapse_rate_w_m2_k, -1.0, 3.0) && within(f.surface_albedo_w_m2_k, -1.0, 2.0) &&
                within(f.clouds_w_m2_k, -2.0, 2.0) && within(f.other_w_m2_k, -2.0, 2.0),
            "feedbacks: Planck within 1-6, water vapour and lapse rate -1-3, albedo -1-2, clouds and other -2-2 W/m2/K");
    require(net_feedback_w_m2_k(f) >= 0.2, "the net feedback must stay restoring (at least 0.2 W/m2/K)");
}

double net_feedback_w_m2_k(const FeedbackSet& f) {
    return f.planck_w_m2_k - f.water_vapour_lapse_rate_w_m2_k - f.surface_albedo_w_m2_k - f.clouds_w_m2_k - f.other_w_m2_k;
}

FeedbackShares feedback_shares(double t, const FeedbackSet& f) {
    FeedbackShares s;
    s.no_feedback_k = t * net_feedback_w_m2_k(f) / f.planck_w_m2_k;
    s.water_vapour_lapse_rate_k = t * f.water_vapour_lapse_rate_w_m2_k / f.planck_w_m2_k;
    s.surface_albedo_k = t * f.surface_albedo_w_m2_k / f.planck_w_m2_k;
    s.clouds_k = t * f.clouds_w_m2_k / f.planck_w_m2_k;
    s.other_k = t * f.other_w_m2_k / f.planck_w_m2_k;
    return s;
}

TwoLayerState step_two_layer(const TwoLayerState& s, double forcing, double dt, const TwoLayerClimate& c) {
    require(c.upper_heat_capacity > 0.0 && c.deep_heat_capacity > 0.0 && c.exchange_w_m2_k >= 0.0 && c.feedback_w_m2_k > 0.0 && dt > 0.0,
            "two-layer climate needs positive heat capacities and feedback, a non-negative exchange and a positive step");
    // Sub-steps keep the explicit scheme stable for the thin upper layer.
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / (0.2 * c.upper_heat_capacity / (c.feedback_w_m2_k + c.exchange_w_m2_k)))));
    const double h = dt / steps;
    TwoLayerState out = s;
    for (int k = 0; k < steps; ++k) {
        const double exchange = c.exchange_w_m2_k * (out.surface_k - out.deep_k);
        const double du = (forcing - c.feedback_w_m2_k * out.surface_k - exchange) / c.upper_heat_capacity;
        const double dd = exchange / c.deep_heat_capacity;
        out.surface_k += h * du;
        out.deep_k += h * dd;
    }
    return out;
}

double transient_climate_response_k(const TwoLayerClimate& c, double f2x) {
    TwoLayerState s;
    for (int year = 0; year < 70; ++year) s = step_two_layer(s, f2x * (year + 1) / 70.0, 1.0, c);
    return s.surface_k;
}

// ---- Impacts ------------------------------------------------------------------------------------------------------

double standard_normal_cdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

double heat_extreme_factor(double warming, double sigma, double period) {
    require(sigma > 0.0 && period > 1.0, "heat extremes need a positive spread and a return period above one year");
    const double p = 1.0 / period;
    // Threshold z of the pre-industrial distribution (inverse normal by bisection).
    double lo = -10.0, hi = 10.0;
    for (int k = 0; k < 200; ++k) {
        const double mid = 0.5 * (lo + hi);
        if (1.0 - standard_normal_cdf(mid) > p) lo = mid; else hi = mid;
    }
    const double z = 0.5 * (lo + hi);
    return (1.0 - standard_normal_cdf(z - warming / sigma)) / p;
}

double heavy_rain_frequency_factor(double warming, double factor_per_k) { return std::pow(factor_per_k, warming); }

double september_sea_ice_mkm2(double warming, double extent_ref, double warming_ref, double loss_per_k) {
    return std::max(0.0, extent_ref - loss_per_k * (warming - warming_ref));
}

// ---- Geography of warming -----------------------------------------------------------------------------------------

double warming_pattern_factor(double lat, bool land, const WarmingPattern& p) {
    double f = land ? p.land_factor : 1.0;
    const double north = std::clamp((lat - 50.0) / 16.0, 0.0, 1.0);          // rises to the Arctic value from 50 to 66 N
    f *= 1.0 + (p.arctic_factor - 1.0) * north * north * (3.0 - 2.0 * north);
    if (!land && lat < -45.0) f *= p.southern_ocean_factor;
    return f;
}

// ---- Carbon fluxes ------------------------------------------------------------------------------------------------

double ocean_co2_uptake_mol_m2_s(double sst, double wind, double dpco2, double salinity) {
    const double k = gas_transfer_velocity_m_s(wind, co2_schmidt_number(sst));
    const double k0 = carbonate_constants(sst, salinity).k0;   // mol kg-1 atm-1
    return k * k0 * 1025.0 * dpco2 * 1.0e-6;                    // m/s * mol/m3/atm * atm
}

std::vector<double> global_steady_anomaly(const std::vector<double>& s, std::size_t nx, std::size_t ny, double kdiff, double tau, std::size_t iterations) {
    require(s.size() == nx * ny && nx >= 4 && ny >= 2, "the steady anomaly needs a source per cell of a global grid");
    require(kdiff > 0.0 && tau > 0.0, "the steady anomaly needs a positive diffusivity and relaxation time");
    const double dlat = kPi / static_cast<double>(ny), dlon = 2.0 * kPi / static_cast<double>(nx);
    std::vector<double> c(s.size(), 0.0);
    double mean_source = 0.0, weight = 0.0;
    for (std::size_t j = 0; j < ny; ++j) {
        const double w = std::cos(-kPi / 2.0 + (static_cast<double>(j) + 0.5) * dlat);
        for (std::size_t i = 0; i < nx; ++i) { mean_source += w * s[j * nx + i]; weight += w; }
    }
    mean_source /= weight;
    for (std::size_t it = 0; it < iterations; ++it) {
        for (std::size_t j = 0; j < ny; ++j) {
            const double lat = -kPi / 2.0 + (static_cast<double>(j) + 0.5) * dlat;
            const double cl = std::max(std::cos(lat), 1e-3);
            const double ax = kdiff / (kEarthRadius * kEarthRadius * cl * cl * dlon * dlon);
            const double latn = lat + 0.5 * dlat, lats = lat - 0.5 * dlat;
            const double an = j + 1 < ny ? kdiff * std::cos(latn) / (kEarthRadius * kEarthRadius * cl * dlat * dlat) : 0.0;
            const double as = j > 0 ? kdiff * std::cos(lats) / (kEarthRadius * kEarthRadius * cl * dlat * dlat) : 0.0;
            for (std::size_t i = 0; i < nx; ++i) {
                const std::size_t k = j * nx + i;
                const double east = c[j * nx + (i + 1) % nx], west = c[j * nx + (i + nx - 1) % nx];
                const double north = j + 1 < ny ? c[k + nx] : 0.0, south = j > 0 ? c[k - nx] : 0.0;
                c[k] = (s[k] - mean_source + ax * (east + west) + an * north + as * south) / (2.0 * ax + an + as + 1.0 / tau);
            }
        }
    }
    // Remove the area mean: an anomaly about the global mean.
    double mean = 0.0;
    for (std::size_t j = 0; j < ny; ++j) {
        const double w = std::cos(-kPi / 2.0 + (static_cast<double>(j) + 0.5) * dlat);
        for (std::size_t i = 0; i < nx; ++i) mean += w * c[j * nx + i];
    }
    mean /= weight;
    for (double& x : c) x -= mean;
    return c;
}

}  // namespace terraflux::weather
