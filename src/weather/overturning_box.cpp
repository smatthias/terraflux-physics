#include "terraflux/weather/overturning_box.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraflux::weather {

namespace {

void require_range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(std::string("overturning ") + name + " is out of range");
}

double exchange(const OverturningBoxConfig& c) { return 1.0 / c.north_volume_m3 + 1.0 / c.south_volume_m3; }

// psi(q) for q > 0 (sinking north) and psi_off(u) for the reversed state with |q| = u.
double psi_on(const OverturningBoxConfig& c, double q, double d_star) {
    return (c.flow_constant_m3_s * c.thermal_expansion_per_k * d_star * q / (1.0 + q * c.temperature_relaxation_s * exchange(c)) - q * q) /
           (c.flow_constant_m3_s * c.haline_contraction_per_psu * c.reference_salinity_psu);
}

double psi_off(const OverturningBoxConfig& c, double u, double d_star) {
    return (u * u + c.flow_constant_m3_s * c.thermal_expansion_per_k * d_star * u / (1.0 + u * c.temperature_relaxation_s * exchange(c))) /
           (c.flow_constant_m3_s * c.haline_contraction_per_psu * c.reference_salinity_psu);
}

struct Derivative { double tn, ts, sn, ss; };

Derivative rates(const OverturningBoxState& s, const OverturningBoxConfig& c, double f, double tn_star, double ts_star) {
    const double q = std::abs(overturning_m3_s(s, c));
    const double s0 = c.reference_salinity_psu, tau = c.temperature_relaxation_s;
    return {q * (s.south_temperature_k - s.north_temperature_k) / c.north_volume_m3 + (tn_star - s.north_temperature_k) / tau,
            q * (s.north_temperature_k - s.south_temperature_k) / c.south_volume_m3 + (ts_star - s.south_temperature_k) / tau,
            q * (s.south_salinity_psu - s.north_salinity_psu) / c.north_volume_m3 - f * s0 / c.north_volume_m3,
            q * (s.north_salinity_psu - s.south_salinity_psu) / c.south_volume_m3 + f * s0 / c.south_volume_m3};
}

OverturningBoxState shifted(const OverturningBoxState& s, const Derivative& d, double h) {
    OverturningBoxState out = s;
    out.north_temperature_k += h * d.tn;
    out.south_temperature_k += h * d.ts;
    out.north_salinity_psu += h * d.sn;
    out.south_salinity_psu += h * d.ss;
    return out;
}

}  // namespace

void validate_overturning(const OverturningBoxConfig& c) {
    require_range(c.north_volume_m3, 1.0e12, 1.0e19, "north_volume_m3");
    require_range(c.south_volume_m3, 1.0e12, 1.0e19, "south_volume_m3");
    require_range(c.flow_constant_m3_s, 1.0, 1.0e13, "flow_constant_m3_s");
    require_range(c.thermal_expansion_per_k, 1.0e-6, 1.0e-3, "thermal_expansion_per_k");
    require_range(c.haline_contraction_per_psu, 1.0e-6, 1.0e-2, "haline_contraction_per_psu");
    require_range(c.reference_salinity_psu, 1.0, 45.0, "reference_salinity_psu");
    require_range(c.temperature_relaxation_s, 1.0, 1.0e11, "temperature_relaxation_s");
    require_range(c.density_kg_m3, 900.0, 1100.0, "density_kg_m3");
    require_range(c.heat_capacity_j_kg_k, 3000.0, 4300.0, "heat_capacity_j_kg_k");
}

CalibratedOverturning calibrate_overturning(const OverturningCalibration& cal, OverturningBoxConfig base) {
    require_range(cal.overturning_sv, 0.1, 200.0, "calibration overturning_sv");
    require_range(cal.temperature_difference_k, 0.1, 40.0, "calibration temperature_difference_k");
    require_range(cal.salinity_difference_psu, 0.0, 10.0, "calibration salinity_difference_psu");
    require_range(cal.north_temperature_k, 260.0, 310.0, "calibration north_temperature_k");
    require_range(cal.north_salinity_psu, 1.0, 45.0, "calibration north_salinity_psu");
    const double q = cal.overturning_sv * 1.0e6;
    const double density_term = base.thermal_expansion_per_k * cal.temperature_difference_k - base.haline_contraction_per_psu * cal.salinity_difference_psu;
    if (!(density_term > 0.0)) throw std::invalid_argument("the present state must have the northern water denser (alpha dT > beta dS)");
    base.flow_constant_m3_s = q / density_term;
    validate_overturning(base);
    CalibratedOverturning out;
    out.config = base;
    out.freshwater_sv = q * cal.salinity_difference_psu / base.reference_salinity_psu / 1.0e6;
    // Targets: D* = D (1 + q tau c); the basin-mean temperature is its own target (it relaxes independently).
    const double d_star = cal.temperature_difference_k * (1.0 + q * base.temperature_relaxation_s * exchange(base));
    const double tn = cal.north_temperature_k, ts = tn + cal.temperature_difference_k;
    // Steady north box: |q| (T_S - T_N) / V_N + (T*_N - T_N) / tau = 0.
    out.north_target_k = tn - q * (ts - tn) / base.north_volume_m3 * base.temperature_relaxation_s;
    out.south_target_k = out.north_target_k + d_star;
    if (!(overturning_state_stable(base, q, cal.temperature_difference_k, cal.salinity_difference_psu))) {
        throw std::invalid_argument("the calibrated present state lies on the unstable branch: the salinity difference is too large");
    }
    return out;
}

double overturning_m3_s(const OverturningBoxState& s, const OverturningBoxConfig& c) {
    return c.flow_constant_m3_s * (c.thermal_expansion_per_k * (s.south_temperature_k - s.north_temperature_k) - c.haline_contraction_per_psu * (s.south_salinity_psu - s.north_salinity_psu));
}

double heat_transport_w(const OverturningBoxState& s, const OverturningBoxConfig& c) {
    return c.density_kg_m3 * c.heat_capacity_j_kg_k * overturning_m3_s(s, c) * (s.south_temperature_k - s.north_temperature_k);
}

OverturningBoxState advance_overturning(const OverturningBoxState& s, const OverturningBoxConfig& c, double f, double tn_star, double ts_star, double dt) {
    validate_overturning(c);
    if (!(dt > 0.0) || !std::isfinite(dt) || !std::isfinite(f) || !std::isfinite(tn_star) || !std::isfinite(ts_star)) throw std::invalid_argument("overturning step needs a positive timestep and finite forcing");
    const Derivative k1 = rates(s, c, f, tn_star, ts_star);
    const Derivative k2 = rates(shifted(s, k1, 0.5 * dt), c, f, tn_star, ts_star);
    const Derivative k3 = rates(shifted(s, k2, 0.5 * dt), c, f, tn_star, ts_star);
    const Derivative k4 = rates(shifted(s, k3, dt), c, f, tn_star, ts_star);
    const Derivative mean{(k1.tn + 2.0 * k2.tn + 2.0 * k3.tn + k4.tn) / 6.0, (k1.ts + 2.0 * k2.ts + 2.0 * k3.ts + k4.ts) / 6.0,
                          (k1.sn + 2.0 * k2.sn + 2.0 * k3.sn + k4.sn) / 6.0, (k1.ss + 2.0 * k2.ss + 2.0 * k3.ss + k4.ss) / 6.0};
    OverturningBoxState out = shifted(s, mean, dt);
    out.elapsed_seconds = s.elapsed_seconds + dt;
    return out;
}

double stommel_critical_freshwater_m3_s(double k, double alpha, double beta, double d, double s0) {
    return k * alpha * alpha * d * d / (4.0 * beta * s0);
}

double critical_freshwater_m3_s(const OverturningBoxConfig& c, double d_star, double* q_at) {
    validate_overturning(c);
    if (!(d_star > 0.0)) { if (q_at) *q_at = 0.0; return 0.0; }
    // psi is unimodal on q > 0: golden-section search for its maximum below the thermal strength k alpha D*.
    double lo = 0.0, hi = c.flow_constant_m3_s * c.thermal_expansion_per_k * d_star;
    const double ratio = (std::sqrt(5.0) - 1.0) / 2.0;
    double a = hi - ratio * (hi - lo), b = lo + ratio * (hi - lo);
    double fa = psi_on(c, a, d_star), fb = psi_on(c, b, d_star);
    for (int it = 0; it < 200; ++it) {
        if (fa < fb) { lo = a; a = b; fa = fb; b = lo + ratio * (hi - lo); fb = psi_on(c, b, d_star); }
        else { hi = b; b = a; fb = fa; a = hi - ratio * (hi - lo); fa = psi_on(c, a, d_star); }
        if (hi - lo < 1.0e-9 * std::max(1.0, hi)) break;
    }
    const double q = 0.5 * (lo + hi);
    if (q_at) *q_at = q;
    return psi_on(c, q, d_star);
}

bool overturning_state_stable(const OverturningBoxConfig& c, double q, double d, double sigma) {
    const double k = c.flow_constant_m3_s, a = c.thermal_expansion_per_k, b = c.haline_contraction_per_psu, cc = exchange(c), tau = c.temperature_relaxation_s;
    const double sign = q >= 0.0 ? 1.0 : -1.0, u = std::abs(q);
    const double j11 = -cc * u - cc * d * sign * k * a - 1.0 / tau;
    const double j12 = cc * d * sign * k * b;
    const double j21 = -cc * sigma * sign * k * a;
    const double j22 = -cc * u + cc * sigma * sign * k * b;
    return (j11 + j22) < 0.0 && (j11 * j22 - j12 * j21) > 0.0;
}

double overturning_recovery_time_s(const OverturningBoxConfig& c, double q, double d, double sigma) {
    const double k = c.flow_constant_m3_s, a = c.thermal_expansion_per_k, b = c.haline_contraction_per_psu, cc = exchange(c), tau = c.temperature_relaxation_s;
    const double sign = q >= 0.0 ? 1.0 : -1.0, u = std::abs(q);
    const double j11 = -cc * u - cc * d * sign * k * a - 1.0 / tau, j12 = cc * d * sign * k * b;
    const double j21 = -cc * sigma * sign * k * a, j22 = -cc * u + cc * sigma * sign * k * b;
    const double trace = j11 + j22, det = j11 * j22 - j12 * j21, disc = trace * trace / 4.0 - det;
    const double largest = disc >= 0.0 ? trace / 2.0 + std::sqrt(disc) : trace / 2.0;
    return largest < 0.0 ? -1.0 / largest : std::numeric_limits<double>::infinity();
}

double equilibrium_recovery_time_s(const OverturningBoxConfig& c, double f, double d_star) {
    for (const auto& e : overturning_equilibria(c, f, d_star)) {
        if (e.overturning_m3_s > 0.0 && e.stable) return overturning_recovery_time_s(c, e.overturning_m3_s, e.temperature_difference_k, e.salinity_difference_psu);
    }
    return std::numeric_limits<double>::infinity();
}

std::vector<OverturningEquilibrium> overturning_equilibria(const OverturningBoxConfig& c, double f, double d_star) {
    validate_overturning(c);
    std::vector<OverturningEquilibrium> out;
    const double tau_c = c.temperature_relaxation_s * exchange(c), s0 = c.reference_salinity_psu;
    auto make = [&](double q) {
        OverturningEquilibrium e;
        e.overturning_m3_s = q;
        e.temperature_difference_k = d_star / (1.0 + std::abs(q) * tau_c);
        e.salinity_difference_psu = f * s0 / std::abs(q);
        e.stable = overturning_state_stable(c, q, e.temperature_difference_k, e.salinity_difference_psu);
        return e;
    };
    auto solve = [&](auto&& fn, double lo, double hi) {
        // fn(lo) and fn(hi) bracket zero.
        double flo = fn(lo);
        for (int it = 0; it < 200; ++it) {
            const double mid = 0.5 * (lo + hi), fm = fn(mid);
            if ((fm < 0.0) == (flo < 0.0)) { lo = mid; flo = fm; } else hi = mid;
            if (hi - lo < 1.0e-10 * std::max(1.0, hi)) break;
        }
        return 0.5 * (lo + hi);
    };
    if (d_star > 0.0) {
        double q_peak = 0.0;
        const double f_crit = critical_freshwater_m3_s(c, d_star, &q_peak);
        const double q_thermal = c.flow_constant_m3_s * c.thermal_expansion_per_k * d_star;
        auto g = [&](double q) { return psi_on(c, q, d_star) - f; };
        if (f > 0.0 && f < f_crit) {
            out.push_back(make(solve(g, q_peak, q_thermal)));          // stable, strong
            out.push_back(make(solve(g, 1.0e-12, q_peak)));            // unstable
        } else if (f <= 0.0 && q_thermal > 0.0) {
            // Without a freshwater deficit (or with net evaporation) only the strong state remains.
            double hi = q_thermal;
            while (g(hi) > 0.0) hi *= 2.0;
            out.push_back(make(solve(g, q_peak, hi)));
        }
    }
    if (f > 0.0) {
        auto g = [&](double u) { return psi_off(c, u, d_star) - f; };
        double hi = 1.0;
        while (g(hi) < 0.0) hi *= 2.0;
        out.push_back(make(-solve(g, 0.0, hi)));
    }
    return out;
}

EarlyWarning early_warning_indicators(const std::vector<double>& x, std::size_t window) {
    if (window < 4) throw std::invalid_argument("early-warning window needs at least 4 samples");
    EarlyWarning out;
    out.variance.assign(x.size(), 0.0);
    out.autocorrelation.assign(x.size(), 0.0);
    if (x.size() < window) return out;
    for (std::size_t end = window; end <= x.size(); ++end) {
        const std::size_t start = end - window;
        // Linear detrending by least squares over the window.
        double st = 0.0, sx = 0.0, stt = 0.0, stx = 0.0;
        for (std::size_t k = 0; k < window; ++k) { const double t = static_cast<double>(k); st += t; sx += x[start + k]; stt += t * t; stx += t * x[start + k]; }
        const double n = static_cast<double>(window);
        const double slope = (n * stx - st * sx) / (n * stt - st * st), intercept = (sx - slope * st) / n;
        std::vector<double> r(window);
        double mean = 0.0;
        for (std::size_t k = 0; k < window; ++k) { r[k] = x[start + k] - (intercept + slope * static_cast<double>(k)); mean += r[k]; }
        mean /= n;
        double var = 0.0, cov = 0.0;
        for (std::size_t k = 0; k < window; ++k) var += (r[k] - mean) * (r[k] - mean);
        for (std::size_t k = 0; k + 1 < window; ++k) cov += (r[k] - mean) * (r[k + 1] - mean);
        out.variance[end - 1] = var / n;
        out.autocorrelation[end - 1] = var > 0.0 ? cov / var : 0.0;
    }
    for (std::size_t k = 0; k + 1 < window; ++k) { out.variance[k] = out.variance[window - 1]; out.autocorrelation[k] = out.autocorrelation[window - 1]; }
    return out;
}

double kendall_tau(const std::vector<double>& v) {
    long long concordant = 0, discordant = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        for (std::size_t j = i + 1; j < v.size(); ++j) {
            if (v[j] > v[i]) ++concordant; else if (v[j] < v[i]) ++discordant;
        }
    }
    const long long pairs = static_cast<long long>(v.size()) * static_cast<long long>(v.size() - 1) / 2;
    return pairs > 0 ? static_cast<double>(concordant - discordant) / static_cast<double>(pairs) : 0.0;
}

NormalSequence::NormalSequence(std::uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

double NormalSequence::uniform() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    const std::uint64_t r = state_ * 0x2545F4914F6CDD1DULL;
    return (static_cast<double>(r >> 11) + 0.5) / 9007199254740992.0;   // (0, 1)
}

double NormalSequence::next() {
    if (has_spare_) { has_spare_ = false; return spare_; }
    const double u1 = uniform(), u2 = uniform();
    const double r = std::sqrt(-2.0 * std::log(u1)), t = 2.0 * 3.14159265358979323846 * u2;
    spare_ = r * std::sin(t);
    has_spare_ = true;
    return r * std::cos(t);
}

}  // namespace terraflux::weather
