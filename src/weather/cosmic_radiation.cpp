#include "terraflux/weather/cosmic_radiation.hpp"

#include "terraflux/weather/cosmic_dust.hpp"   // the US Standard Atmosphere

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kProtonRest = 0.938272;        // GeV
constexpr double kMinRigidity = 0.01, kMaxRigidity = 1000.0;
constexpr int kNodes = 1200;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

double node(int k) { return kMinRigidity * std::pow(kMaxRigidity / kMinRigidity, static_cast<double>(k) / (kNodes - 1)); }

// Cumulative integral from each node to the top (trapezoid in ln P): C[k] = int_{P_k}^{Pmax} f(P) dP.
template <typename F>
std::vector<double> cumulative_from_top(F&& f) {
    std::vector<double> c(kNodes, 0.0);
    const double dl = std::log(kMaxRigidity / kMinRigidity) / (kNodes - 1);
    double prev = f(node(kNodes - 1)) * node(kNodes - 1);
    for (int k = kNodes - 2; k >= 0; --k) {
        const double p = node(k), cur = f(p) * p;
        c[k] = c[k + 1] + 0.5 * (cur + prev) * dl;
        prev = cur;
    }
    return c;
}

double integral_above(const std::vector<double>& c, double cutoff) {
    if (cutoff <= kMinRigidity) return c.front();
    if (cutoff >= kMaxRigidity) return 0.0;
    const double x = std::log(cutoff / kMinRigidity) / std::log(kMaxRigidity / kMinRigidity) * (kNodes - 1);
    const int k = std::min(kNodes - 2, static_cast<int>(std::floor(x)));
    const double w = x - k;
    return c[k] + w * (c[k + 1] - c[k]);
}

}  // namespace

// ---- Spectra ------------------------------------------------------------------------------------------------------

double proton_rigidity_gv(double t) { return std::sqrt(std::max(0.0, t) * (std::max(0.0, t) + 2.0 * kProtonRest)); }
double proton_kinetic_gev(double p) { return std::sqrt(p * p + kProtonRest * kProtonRest) - kProtonRest; }

double lis_proton_flux(double t) {
    const double p = proton_rigidity_gv(t);
    if (p <= 0.0) return 0.0;
    return 1.9e4 * std::pow(p, -2.78) / (1.0 + 0.4866 * std::pow(p, -2.51));
}

double force_field_proton_flux(double t, double phi) {
    if (t <= 0.0) return 0.0;
    return lis_proton_flux(t + phi) * t * (t + 2.0 * kProtonRest) / ((t + phi) * (t + phi + 2.0 * kProtonRest));
}

double gcr_flux_per_rigidity(double p, double phi) {
    const double t = proton_kinetic_gev(p);
    return force_field_proton_flux(t, phi) * p / std::sqrt(p * p + kProtonRest * kProtonRest);   // dT/dP
}

// ---- Responses ----------------------------------------------------------------------------------------------------

std::vector<RadiationResponse> default_radiation_responses() {
    return {{"sea_level_neutron", "% of the polar solar-minimum rate", 1.4, 3.0, 3.0, 100.0, 0.4},
            {"flight_dose_12km", "uSv/h", 1.0, 2.8, 3.5, 6.0, 0.4},
            {"pfotzer_ionisation", "ion pairs cm^-3 s^-1", 0.8, 2.5, 3.0, 30.0, 0.4},
            {"cosmogenic_production", "relative", 0.6, 1.5, 2.0, 1.0, 0.4}};
}

void validate_radiation_response(const RadiationResponse& r) {
    require(!r.name.empty(), "radiation responses need a name");
    require(within(r.yield_exponent, -1.0, 4.0) && within(r.threshold_gv, 0.0, 30.0) && within(r.sharpness, 0.1, 10.0),
            "radiation response: yield_exponent within -1-4, threshold_gv within 0-30, sharpness within 0.1-10");
    require(within(r.anchor, 1.0e-12, 1.0e12) && within(r.reference_phi_gv, 0.0, 3.0), "radiation response: anchor positive, reference_phi_gv within 0-3");
}

double response_yield(double p, const RadiationResponse& r) {
    if (p <= 0.0) return 0.0;
    return std::pow(p, r.yield_exponent) * std::exp(-std::pow(r.threshold_gv / p, r.sharpness));
}

double sep_flux_above_pfu(double p, const SepSpectrum& s) {
    if (s.flux_above_10mev_pfu <= 0.0) return 0.0;
    const double r10 = proton_rigidity_gv(0.010);
    return s.flux_above_10mev_pfu * std::exp(-(p - r10) / s.efold_rigidity_gv);
}

double sep_flux_per_rigidity(double p, const SepSpectrum& s) { return sep_flux_above_pfu(p, s) / s.efold_rigidity_gv * 1.0e4; }

CutoffResponse::CutoffResponse(const RadiationResponse& r, double phi, const SepSpectrum* sep, double max_cutoff) : step_(0.05) {
    validate_radiation_response(r);
    require(within(phi, 0.0, 3.0) && within(max_cutoff, 1.0, 100.0), "cutoff response: phi within 0-3 GV, maximum cutoff within 1-100 GV");
    const auto reference = cumulative_from_top([&](double p) { return gcr_flux_per_rigidity(p, r.reference_phi_gv) * response_yield(p, r); });
    const double norm = r.anchor / reference.front();
    const auto g = cumulative_from_top([&](double p) { return gcr_flux_per_rigidity(p, phi) * response_yield(p, r); });
    std::vector<double> s;
    if (sep && sep->flux_above_10mev_pfu > 0.0) {
        require(sep->efold_rigidity_gv > 0.0, "SEP spectra need a positive e-folding rigidity");
        s = cumulative_from_top([&](double p) { return sep_flux_per_rigidity(p, *sep) * response_yield(p, r); });
    }
    const int n = static_cast<int>(std::ceil(max_cutoff / step_)) + 1;
    gcr_.resize(n);
    sep_.assign(n, 0.0);
    for (int k = 0; k < n; ++k) {
        gcr_[k] = norm * integral_above(g, k * step_);
        if (!s.empty()) sep_[k] = norm * integral_above(s, k * step_);
    }
}

double CutoffResponse::lookup(const std::vector<double>& t, double c) const {
    const double x = std::max(0.0, c) / step_;
    const std::size_t k = std::min(t.size() - 2, static_cast<std::size_t>(x));
    const double w = std::min(1.0, x - static_cast<double>(k));
    return t[k] + w * (t[k + 1] - t[k]);
}

double CutoffResponse::gcr(double c) const { return lookup(gcr_, c); }
double CutoffResponse::sep(double c) const { return lookup(sep_, c); }
double CutoffResponse::operator()(double c) const { return gcr(c) + sep(c); }

// ---- SEP energy deposition ----------------------------------------------------------------------------------------

void validate_sep_atmosphere(const SepAtmosphereConfig& c) {
    require(within(c.range_coefficient_g_cm2, 1.0e-4, 0.1) && within(c.range_exponent, 1.0, 2.5) && within(c.mean_secant, 1.0, 5.0),
            "sep_atmosphere: range_coefficient_g_cm2 within 1e-4-0.1, range_exponent within 1-2.5, mean_secant within 1-5");
    require(within(c.ion_pair_energy_ev, 10.0, 100.0) && within(c.nox_per_ion_pair, 0.0, 5.0) && within(c.hox_per_ion_pair, 0.0, 5.0),
            "sep_atmosphere: ion_pair_energy_ev within 10-100, NOx and HOx per ion pair within 0-5");
    require(c.layer_tops_km.size() >= 2 && c.layer_tops_km.size() <= 10, "sep_atmosphere needs 2-10 layer tops");
    for (std::size_t k = 0; k < c.layer_tops_km.size(); ++k)
        require(within(c.layer_tops_km[k], 0.1, 2000.0) && (k == 0 || c.layer_tops_km[k] < c.layer_tops_km[k - 1]), "sep_atmosphere layer tops must decrease (km)");
}

SepDeposition sep_deposition(double cutoff, const SepSpectrum& s, const SepAtmosphereConfig& c) {
    SepDeposition out;
    out.layer_mev_cm2_s.assign(c.layer_tops_km.size(), 0.0);
    if (s.flux_above_10mev_pfu <= 0.0) return out;
    // Slant depths (g/cm^2) of the layer boundaries; the last layer reaches the ground.
    std::vector<double> depth;
    for (double top : c.layer_tops_km) depth.push_back(top >= 200.0 ? 0.0 : standard_air_pressure_pa(top * 1000.0) / 9.80665 * 0.1 * c.mean_secant);
    depth.push_back(standard_air_pressure_pa(0.0) / 9.80665 * 0.1 * c.mean_secant);
    auto remaining = [&](double e0, double x) {      // MeV left after a slant depth x
        const double range = c.range_coefficient_g_cm2 * std::pow(e0, c.range_exponent);
        return x >= range ? 0.0 : std::pow((range - x) / c.range_coefficient_g_cm2, 1.0 / c.range_exponent);
    };
    const double lo = std::max(cutoff, proton_rigidity_gv(0.001)), hi = 20.0;
    if (lo >= hi) return out;
    const int n = 600;
    const double dl = std::log(hi / lo) / n;
    for (int k = 0; k < n; ++k) {
        const double p = lo * std::exp((k + 0.5) * dl);
        const double protons = kPi * sep_flux_per_rigidity(p, s) * 1.0e-4 * p * dl;   // cm^-2 s^-1 through a horizontal surface
        const double e0 = proton_kinetic_gev(p) * 1000.0;
        for (std::size_t layer = 0; layer < c.layer_tops_km.size(); ++layer) {
            const double lost = remaining(e0, depth[layer]) - remaining(e0, depth[layer + 1]);
            out.layer_mev_cm2_s[layer] += protons * lost;
        }
    }
    for (double v : out.layer_mev_cm2_s) out.energy_mev_cm2_s += v;
    out.ion_pairs_cm2_s = out.energy_mev_cm2_s * 1.0e6 / c.ion_pair_energy_ev;
    return out;
}

}  // namespace terraflux::weather
