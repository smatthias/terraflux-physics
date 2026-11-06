#include "terraflux/weather/solar_cycle.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace terraflux::weather {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double mean_of(const std::vector<double>& v) {
    if (v.empty()) return kNaN;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double sd_of(const std::vector<double>& v) {
    if (v.size() < 2) return kNaN;
    const double m = mean_of(v);
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

double correlation(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 3) return kNaN;
    const double mx = mean_of(x), my = mean_of(y);
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); syy += (y[i] - my) * (y[i] - my); }
    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt(sxx * syy) : kNaN;
}

// Values of earlier cycles' curves over record months [from, to).
std::vector<double> baseline_of(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<SolarCycleFit>& earlier) {
    std::vector<double> out(to - from, 0.0);
    for (std::size_t k = from; k < to; ++k)
        for (const auto& f : earlier) out[k - from] += f.curve.value(record.year_at(k));
    return out;
}

double sse(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline, const SolarCycleCurve& curve) {
    double s = 0.0;
    for (std::size_t k = from; k < to; ++k) {
        const double r = record.values[k] - baseline[k - from] - curve.value(record.year_at(k));
        s += r * r;
    }
    return s;
}

void check_range(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline) {
    if (from >= to || to > record.values.size()) throw std::invalid_argument("solar cycle fit needs a non-empty range inside the record");
    if (baseline.size() != to - from) throw std::invalid_argument("solar cycle fit baseline must match the fitted range");
}

// Interpolation in a table with NaN gaps: linear between finite entries, held beyond the finite ends.
double table_value(const std::vector<double>& x, const std::vector<double>& y, double at) {
    std::vector<std::pair<double, double>> pts;
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i) if (std::isfinite(y[i])) pts.emplace_back(x[i], y[i]);
    if (pts.empty()) return kNaN;
    if (at <= pts.front().first) return pts.front().second;
    if (at >= pts.back().first) return pts.back().second;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (at <= pts[i].first) {
            const double w = (at - pts[i - 1].first) / (pts[i].first - pts[i - 1].first);
            return pts[i - 1].second + (pts[i].second - pts[i - 1].second) * w;
        }
    }
    return pts.back().second;
}

// Pattern search on (onset, width) from a start point: tries the eight neighbours at the current steps, moves to
// any improvement and halves the steps when none is found. A zero width step searches the onset only. Onset and
// width trade off along a diagonal valley (both shift the peak), which a fixed refinement grid can miss.
template <typename Cost>
void pattern_search(Cost&& cost, double& onset, double& width, double& best, double onset_step, double width_step, double onset_tolerance, double width_tolerance) {
    for (int it = 0; it < 400 && (onset_step > onset_tolerance || width_step > width_tolerance); ++it) {
        bool moved = false;
        for (int i = -1; i <= 1 && !moved; ++i) {
            for (int k = -1; k <= 1 && !moved; ++k) {
                if ((i == 0 && k == 0) || (k != 0 && width_step == 0.0)) continue;
                const double o = onset + i * onset_step, w = width + k * width_step;
                const double c = cost(o, w);
                if (c < best) { best = c; onset = o; width = w; moved = true; }
            }
        }
        if (!moved) { onset_step *= 0.5; width_step *= 0.5; }
    }
}

}  // namespace

// ---- Shape ---------------------------------------------------------------------------------------------------

double hathaway_width_months(double a_v1) {
    if (!(a_v1 > 0.0) || !std::isfinite(a_v1)) throw std::invalid_argument("Hathaway width needs a positive shape parameter");
    return 27.12 + 25.15 / std::pow(1000.0 * a_v1, 0.25);
}

SolarCycleCurve make_solar_cycle_curve(double onset_year, double amplitude, double width_scale) {
    if (!std::isfinite(onset_year)) throw std::invalid_argument("solar cycle onset must be finite");
    if (!(amplitude >= 1.0 && amplitude <= 1000.0)) throw std::invalid_argument("solar cycle amplitude must lie within 1-1000");
    if (!(width_scale >= 0.3 && width_scale <= 3.0)) throw std::invalid_argument("solar cycle width scale must lie within 0.3-3");
    SolarCycleCurve c;
    c.onset_year = onset_year;
    c.amplitude = amplitude;
    c.width_scale = width_scale;
    // A = a b(a)^3 g* with b = beta (27.12 + 25.15 (600 a)^-1/4): Newton's method on f(a) = a b^3 g* - A, which is
    // increasing in a. Started from the fixed-point estimate it converges in a few steps.
    double a = amplitude / (std::pow(50.0 * width_scale, 3.0) * kHathawayPeakValue);
    a = amplitude / (std::pow(width_scale * hathaway_width_months(kSunspotV1PerV2 * a), 3.0) * kHathawayPeakValue);
    for (int it = 0; it < 50; ++it) {
        const double q = std::pow(1000.0 * kSunspotV1PerV2 * a, -0.25);
        const double b = width_scale * (27.12 + 25.15 * q);
        const double db = width_scale * 25.15 * (-0.25) * q / a;
        const double f = a * b * b * b * kHathawayPeakValue - amplitude;
        const double df = (b * b * b + 3.0 * a * b * b * db) * kHathawayPeakValue;
        const double next = std::max(0.5 * a, a - f / df);
        const bool done = std::abs(next - a) <= 1e-13 * a;
        a = next;
        if (done) break;
    }
    c.a = a;
    c.b_months = width_scale * hathaway_width_months(kSunspotV1PerV2 * a);
    return c;
}

double SolarCycleCurve::value(double year) const {
    const double x = (year - onset_year) * 12.0;
    if (!(x > 0.0)) return 0.0;
    const double e = x * x / (b_months * b_months);
    if (e > 600.0) return 0.0;
    return a * x * x * x / (std::exp(e) - kHathawayCurvature);
}

double SolarCycleCurve::peak_year() const { return onset_year + kHathawayPeakShape * b_months / 12.0; }

double cycles_value(const std::vector<SolarCycleCurve>& cycles, double year) {
    double s = 0.0;
    for (const auto& c : cycles) s += c.value(year);
    return s;
}

int month_index_of(double year) { return static_cast<int>(std::floor(year * 12.0 + 1e-9)); }
double month_mid_year(int month_index) { return (static_cast<double>(month_index) + 0.5) / 12.0; }

// ---- Cycle table and synthetic record ---------------------------------------------------------------------------

std::vector<SolarCycleCurve> calibrate_cycle_table(const std::vector<SolarCycleTableEntry>& table, double first_lead_years, bool calibrate_widths) {
    if (table.empty() || table.size() > 60) throw std::invalid_argument("the cycle table needs 1-60 cycles");
    if (!(first_lead_years >= 0.0 && first_lead_years <= 2.0)) throw std::invalid_argument("the first cycle's lead must lie within 0-2 years");
    for (std::size_t k = 0; k < table.size(); ++k) {
        const auto& e = table[k];
        if (!std::isfinite(e.minimum_year)) throw std::invalid_argument("cycle table minima must be finite");
        if (!(e.maximum >= 5.0 && e.maximum <= 600.0)) throw std::invalid_argument("cycle table maxima must lie within 5-600");
        if (std::isfinite(e.maximum_year) && !(e.maximum_year >= e.minimum_year + 1.5 && e.maximum_year <= e.minimum_year + 9.0))
            throw std::invalid_argument("cycle table maxima must lie 1.5-9 years after their minimum");
        if (k > 0 && !(e.minimum_year >= table[k - 1].minimum_year + 6.0 && e.minimum_year <= table[k - 1].minimum_year + 18.0))
            throw std::invalid_argument("cycle table minima must follow each other by 6-18 years");
        if (k > 0 && std::isfinite(table[k - 1].maximum_year) && !(table[k - 1].maximum_year < e.minimum_year - 1.0))
            throw std::invalid_argument("a cycle table maximum must lie at least a year before the next minimum");
    }
    std::vector<SolarCycleCurve> curves;
    for (const auto& e : table) curves.push_back(make_solar_cycle_curve(e.minimum_year - first_lead_years, e.maximum, 1.0));
    auto search = [&](double lo, double hi, bool minimum) {
        double best_year = lo, best = minimum ? std::numeric_limits<double>::infinity() : -1.0;
        for (double y = lo; y <= hi; y += 0.005) {
            const double v = cycles_value(curves, y);
            if (minimum ? v < best : v > best) { best = v; best_year = y; }
        }
        return std::make_pair(best_year, best);
    };
    for (int it = 0; it < 60; ++it) {
        double change = 0.0;
        for (std::size_t k = 0; k < table.size(); ++k) {
            const auto& e = table[k];
            auto& c = curves[k];
            double onset = c.onset_year, amplitude = c.amplitude, width = c.width_scale;
            if (k > 0) {
                const auto found = search(e.minimum_year - 2.0, e.minimum_year + 2.0, true);
                onset += e.minimum_year - found.first;
                change = std::max(change, std::abs(e.minimum_year - found.first));
            }
            const double hi = k + 1 < table.size() ? table[k + 1].minimum_year - 0.5 : e.minimum_year + 9.0;
            const auto peak = search(e.minimum_year + 1.0, hi, false);
            amplitude *= e.maximum / peak.second;
            change = std::max(change, std::abs(e.maximum / peak.second - 1.0));
            if (calibrate_widths && std::isfinite(e.maximum_year)) {
                const double ratio = (e.maximum_year - c.onset_year) / (peak.first - c.onset_year);
                width = std::clamp(width * ratio, 0.4, 2.5);
                change = std::max(change, std::abs(e.maximum_year - peak.first));
            }
            c = make_solar_cycle_curve(onset, std::clamp(amplitude, 1.0, 1000.0), width);
        }
        if (change < 1e-3) break;
    }
    return curves;
}

MonthlySeries synthesize_sunspot_record(const std::vector<SolarCycleCurve>& curves, int first_month, std::size_t months, const SunspotNoise& noise) {
    if (!(noise.factor >= 0.0 && noise.factor <= 10.0)) throw std::invalid_argument("sunspot noise factor must lie within 0-10");
    if (!(noise.autocorrelation >= 0.0 && noise.autocorrelation < 1.0)) throw std::invalid_argument("sunspot noise autocorrelation must lie within 0-1");
    if (months == 0 || months > 12 * 600) throw std::invalid_argument("the synthetic record must span 1 month to 600 years");
    MonthlySeries out;
    out.first_month = first_month;
    out.values.resize(months);
    NormalSequence z(noise.seed);
    const double phi = noise.autocorrelation, scale = std::sqrt(1.0 - phi * phi);
    double e = z.next();
    for (std::size_t k = 0; k < months; ++k) {
        if (k > 0) e = phi * e + scale * z.next();
        const double r = cycles_value(curves, out.year_at(k));
        out.values[k] = std::round(std::max(0.0, r + noise.factor * std::sqrt(r + 1.0) * e) * 10.0) / 10.0;
    }
    return out;
}

// ---- Smoothing and extrema -----------------------------------------------------------------------------------

std::vector<double> smooth_13_month(const std::vector<double>& monthly) {
    std::vector<double> out(monthly.size(), kNaN);
    for (std::size_t i = 6; i + 6 < monthly.size(); ++i) {
        double s = 0.5 * (monthly[i - 6] + monthly[i + 6]);
        for (std::size_t j = i - 5; j <= i + 5; ++j) s += monthly[j];
        out[i] = s / 12.0;   // NaN propagates
    }
    return out;
}

std::vector<SolarExtremum> solar_extrema(const std::vector<double>& s, std::size_t half_window, std::size_t confirm) {
    std::vector<SolarExtremum> candidates;
    std::size_t first = s.size(), last = 0;
    for (std::size_t i = 0; i < s.size(); ++i) if (std::isfinite(s[i])) { first = std::min(first, i); last = i; }
    if (first >= s.size() || half_window == 0) return {};
    for (std::size_t i = first; i <= last; ++i) {
        if (!std::isfinite(s[i]) || i < first + half_window) continue;
        std::size_t right = i + half_window;
        if (right > last) {
            if (last - i < confirm) continue;
            right = last;
        }
        bool is_min = true, is_max = true;
        for (std::size_t j = i - half_window; j <= right && (is_min || is_max); ++j) {
            if (j == i || !std::isfinite(s[j])) continue;
            // Ties go to the earlier entry.
            if (s[j] < s[i] || (s[j] == s[i] && j < i)) is_min = false;
            if (s[j] > s[i] || (s[j] == s[i] && j < i)) is_max = false;
        }
        if (is_min) candidates.push_back({true, i, s[i]});
        else if (is_max) candidates.push_back({false, i, s[i]});
    }
    std::vector<SolarExtremum> out;
    for (const auto& c : candidates) {
        if (!out.empty() && out.back().minimum == c.minimum) {
            const bool better = c.minimum ? c.value < out.back().value : c.value > out.back().value;
            if (better) out.back() = c;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::vector<ObservedSolarCycle> observed_solar_cycles(const MonthlySeries& smoothed, const std::vector<SolarExtremum>& extrema, int first_number) {
    std::vector<ObservedSolarCycle> out;
    std::vector<std::size_t> minima;
    for (std::size_t k = 0; k < extrema.size(); ++k) if (extrema[k].minimum) minima.push_back(k);
    for (std::size_t n = 0; n < minima.size(); ++n) {
        const auto& m = extrema[minima[n]];
        ObservedSolarCycle c;
        c.number = first_number + static_cast<int>(n);
        c.minimum_index = m.index;
        c.minimum_year = smoothed.year_at(m.index);
        c.minimum_value = m.value;
        const std::size_t stop = n + 1 < minima.size() ? minima[n + 1] : extrema.size();
        for (std::size_t k = minima[n] + 1; k < stop; ++k) {
            if (!extrema[k].minimum && (!c.has_maximum || extrema[k].value > c.maximum_value)) {
                c.has_maximum = true;
                c.maximum_index = extrema[k].index;
                c.maximum_year = smoothed.year_at(extrema[k].index);
                c.maximum_value = extrema[k].value;
            }
        }
        c.complete = n + 1 < minima.size();
        c.length_years = c.rise_years = c.decline_years = kNaN;
        if (c.complete) {
            c.end_index = extrema[minima[n + 1]].index;
            c.end_year = smoothed.year_at(c.end_index);
            c.length_years = c.end_year - c.minimum_year;
        }
        if (c.has_maximum) c.rise_years = c.maximum_year - c.minimum_year;
        if (c.has_maximum && c.complete) c.decline_years = c.end_year - c.maximum_year;
        out.push_back(c);
    }
    return out;
}

// ---- Fits ------------------------------------------------------------------------------------------------------

SolarCycleFit fit_solar_cycle_amplitude(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline,
                                        double onset_year, double width_scale) {
    check_range(record, from, to, baseline);
    double amplitude = 150.0;
    SolarCycleCurve curve = make_solar_cycle_curve(onset_year, amplitude, width_scale);
    for (int it = 0; it < 40; ++it) {
        // R = A h(t) for the current width: the amplitude is a linear least-squares coefficient.
        double shh = 0.0, shy = 0.0;
        for (std::size_t k = from; k < to; ++k) {
            const double h = curve.value(record.year_at(k)) / curve.amplitude;
            shh += h * h;
            shy += h * (record.values[k] - baseline[k - from]);
        }
        if (!(shh > 1e-12)) break;   // no month after the onset: keep the start value
        const double next = std::clamp(shy / shh, 1.0, 1000.0);
        const bool done = std::abs(next - amplitude) <= 1e-8 * amplitude;
        amplitude = next;
        curve = make_solar_cycle_curve(onset_year, amplitude, width_scale);
        if (done) break;
    }
    SolarCycleFit fit;
    fit.curve = curve;
    fit.months = to - from;
    fit.rms = std::sqrt(sse(record, from, to, baseline, curve) / static_cast<double>(fit.months));
    return fit;
}

SolarCycleFit fit_solar_cycle_shape(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline, double onset_year) {
    check_range(record, from, to, baseline);
    SolarCycleFit best;
    double best_sse = std::numeric_limits<double>::infinity();
    auto trial = [&](double beta) {
        if (beta < 0.5 || beta > 2.0) return;
        SolarCycleFit f = fit_solar_cycle_amplitude(record, from, to, baseline, onset_year, beta);
        const double s = f.rms * f.rms;
        if (s < best_sse) { best_sse = s; best = f; }
    };
    for (int k = 0; k <= 40; ++k) trial(0.6 + 0.025 * k);
    double width = best.curve.width_scale;
    for (double step = 0.0125; step > 2e-4; step *= 0.5) {
        for (bool moved = true; moved;) {
            moved = false;
            for (double w : {width - step, width + step}) {
                const double before = best_sse;
                trial(w);
                if (best_sse < before) { width = best.curve.width_scale; moved = true; }
            }
        }
    }
    return best;
}

SolarCycleFit fit_solar_cycle_onset_prior(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline,
                                          double onset_prior, double onset_sigma, double noise_variance, bool fit_width, double width_scale) {
    check_range(record, from, to, baseline);
    if (!std::isfinite(onset_prior) || !(onset_sigma > 0.0 && onset_sigma <= 5.0)) throw std::invalid_argument("the onset prior needs a finite date and a spread within 0-5 years");
    if (!(noise_variance > 0.0) || !std::isfinite(noise_variance)) throw std::invalid_argument("the onset-prior fit needs a positive noise variance");
    auto cost = [&](const SolarCycleFit& f) {
        const double d = (f.curve.onset_year - onset_prior) / onset_sigma;
        return f.rms * f.rms * static_cast<double>(f.months) / noise_variance + d * d;
    };
    auto at = [&](double onset, double beta) {
        return fit_width ? (beta > 0.0 ? fit_solar_cycle_amplitude(record, from, to, baseline, onset, beta) : fit_solar_cycle_shape(record, from, to, baseline, onset))
                         : fit_solar_cycle_amplitude(record, from, to, baseline, onset, width_scale);
    };
    SolarCycleFit best;
    double best_cost = std::numeric_limits<double>::infinity();
    for (int i = -15; i <= 15; ++i) {
        const SolarCycleFit f = at(onset_prior + onset_sigma * i / 5.0, -1.0);
        const double c = cost(f);
        if (c < best_cost) { best_cost = c; best = f; }
    }
    // Refine the onset (and the width).
    double onset = best.curve.onset_year, width = best.curve.width_scale;
    auto refine = [&](double o, double w) {
        if (w < 0.5 || w > 2.0 || std::abs(o - onset_prior) > 3.5 * onset_sigma) return std::numeric_limits<double>::infinity();
        const SolarCycleFit f = fit_solar_cycle_amplitude(record, from, to, baseline, o, w);
        const double c = cost(f);
        if (c < best_cost) best = f;
        return c;
    };
    double current = best_cost;
    pattern_search(refine, onset, width, current, onset_sigma / 10.0, fit_width ? 0.0125 : 0.0, onset_sigma / 1000.0, 1e-4);
    best_cost = current;
    // Spread of the onset from the curvature of the profile cost.
    const double h = onset_sigma / 10.0, t = best.curve.onset_year, beta = best.curve.width_scale;
    const double up = cost(fit_solar_cycle_amplitude(record, from, to, baseline, t + h, beta));
    const double down = cost(fit_solar_cycle_amplitude(record, from, to, baseline, t - h, beta));
    const double second = up + down - 2.0 * best_cost;
    best.onset_sigma = second > 0.0 ? std::clamp(h * std::sqrt(2.0 / second), 0.02, onset_sigma) : onset_sigma;
    return best;
}

SolarCycleFit fit_solar_cycle(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline,
                              double onset_guess_year, double onset_range_years) {
    check_range(record, from, to, baseline);
    if (!(onset_range_years >= 0.0 && onset_range_years <= 5.0)) throw std::invalid_argument("solar cycle onset range must lie within 0-5 years");
    SolarCycleFit best;
    double best_sse = std::numeric_limits<double>::infinity();
    auto trial = [&](double onset, double beta) {
        if (beta < 0.5 || beta > 2.0) return;
        SolarCycleFit f = fit_solar_cycle_amplitude(record, from, to, baseline, onset, beta);
        const double s = f.rms * f.rms;
        if (s < best_sse) { best_sse = s; best = f; }
    };
    const int steps = static_cast<int>(std::round(onset_range_years * 12.0));
    for (int i = -steps; i <= steps; ++i)
        for (int k = 0; k <= 20; ++k) trial(onset_guess_year + i / 12.0, 0.6 + 0.05 * k);
    double onset = best.curve.onset_year, beta = best.curve.width_scale;
    auto cost = [&](double o, double w) {
        if (w < 0.5 || w > 2.0 || std::abs(o - onset_guess_year) > onset_range_years + 0.5) return std::numeric_limits<double>::infinity();
        const SolarCycleFit f = fit_solar_cycle_amplitude(record, from, to, baseline, o, w);
        const double c = f.rms * f.rms;
        if (c < best_sse) best = f;
        return c;
    };
    double current = best_sse;
    pattern_search(cost, onset, beta, current, 1.0 / 24.0, 0.025, 1e-4, 1e-4);
    return best;
}

// ---- Error model -----------------------------------------------------------------------------------------------

double SolarErrorModel::amplitude_error(double phase_years) const { return table_value(phases_years, amplitude_error_fraction, phase_years); }
double SolarErrorModel::timing_error(double phase_years) const { return table_value(phases_years, peak_timing_error_years, phase_years); }

double solar_percentile(std::vector<double> values, double percent) {
    if (values.empty()) return kNaN;
    std::sort(values.begin(), values.end());
    const double pos = std::clamp(percent, 0.0, 100.0) / 100.0 * static_cast<double>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = std::min(values.size() - 1, lo + 1);
    return values[lo] + (values[hi] - values[lo]) * (pos - static_cast<double>(lo));
}

// ---- Forecaster ------------------------------------------------------------------------------------------------

SolarCycleForecaster::SolarCycleForecaster(MonthlySeries record, int first_cycle_number, SolarForecastSettings settings)
    : record_(std::move(record)), first_cycle_number_(first_cycle_number), settings_(settings) {
    if (record_.values.size() < 48) throw std::invalid_argument("the sunspot record must span at least four years");
    for (double v : record_.values)
        if (!std::isfinite(v) || v < 0.0) throw std::invalid_argument("sunspot numbers must be finite and non-negative");
    if (settings_.half_window_months < 6 || settings_.half_window_months > 60) throw std::invalid_argument("half_window_months must lie within 6-60");
    if (settings_.confirm_months < 3 || settings_.confirm_months > settings_.half_window_months) throw std::invalid_argument("confirm_months must lie within 3 and the half window");
    if (settings_.minimum_history_cycles < 2) throw std::invalid_argument("minimum_history_cycles must be at least 2");
    if (!(settings_.fit_lead_in_years >= 0.0 && settings_.fit_lead_in_years <= 3.0)) throw std::invalid_argument("fit_lead_in_years must lie within 0-3");
    if (!(settings_.shape_fit_phase_years >= 1.0 && settings_.shape_fit_phase_years <= 12.0)) throw std::invalid_argument("shape_fit_phase_years must lie within 1-12");
    if (!(settings_.onset_search_years > 0.0 && settings_.onset_search_years <= 4.0)) throw std::invalid_argument("onset_search_years must lie within 0-4");
    smoothed_.first_month = record_.first_month;
    smoothed_.values = smooth_13_month(record_.values);
    extrema_ = solar_extrema(smoothed_.values, settings_.half_window_months, settings_.confirm_months);
    cycles_ = observed_solar_cycles(smoothed_, extrema_, first_cycle_number_);
}

const SolarCycleFit& SolarCycleForecaster::complete_fit(const std::vector<ObservedSolarCycle>& cycles, std::size_t k, const std::vector<SolarCycleFit>& earlier) const {
    const auto& c = cycles[k];
    const auto key = std::make_pair(c.minimum_index, c.end_index);
    const auto found = complete_fits_.find(key);
    if (found != complete_fits_.end()) return found->second;
    const double start = c.minimum_year - settings_.fit_lead_in_years;
    const std::size_t from = static_cast<std::size_t>(std::max(0, month_index_of(start) - record_.first_month));
    const std::size_t to = c.end_index + 1;
    const auto base = baseline_of(record_, from, to, earlier);
    return complete_fits_[key] = fit_solar_cycle(record_, from, to, base, c.minimum_year - 0.3, settings_.onset_search_years);
}

SolarCycleAnalysis SolarCycleForecaster::analyse(double issue_year, const SolarErrorModel* errors) const {
    SolarCycleAnalysis a;
    a.issue_year = issue_year;
    const int issue_month = month_index_of(issue_year);
    const int available = issue_month - record_.first_month;   // months strictly before the issue month
    if (available < 48) throw std::invalid_argument("the forecast needs at least four years of record before the issue date");
    const std::size_t n = std::min<std::size_t>(record_.values.size(), static_cast<std::size_t>(available));
    a.record.first_month = record_.first_month;
    a.record.values.assign(record_.values.begin(), record_.values.begin() + static_cast<std::ptrdiff_t>(n));
    a.smoothed.first_month = record_.first_month;
    a.smoothed.values = smooth_13_month(a.record.values);
    a.extrema = solar_extrema(a.smoothed.values, settings_.half_window_months, settings_.confirm_months);
    a.cycles = observed_solar_cycles(a.smoothed, a.extrema, first_cycle_number_);
    std::size_t complete = 0;
    for (const auto& c : a.cycles) if (c.complete) ++complete;
    if (a.cycles.empty() || complete < settings_.minimum_history_cycles)
        throw std::invalid_argument("the forecast needs " + std::to_string(settings_.minimum_history_cycles) + " complete cycles before the issue date");

    // Complete cycles: onset, amplitude and width, each on top of the earlier ones.
    for (std::size_t k = 0; k + 1 < a.cycles.size(); ++k) a.fits.push_back(complete_fit(a.cycles, k, a.fits));

    // Statistics of the complete cycles.
    auto& st = a.statistics;
    st.cycles = a.fits.size();
    std::vector<double> periods, lengths, amplitudes, leads, widths, rises, maxima;
    for (std::size_t k = 0; k < a.fits.size(); ++k) {
        const auto& f = a.fits[k].curve;
        const auto& c = a.cycles[k];
        if (k + 1 < a.fits.size()) periods.push_back(a.fits[k + 1].curve.onset_year - f.onset_year);
        lengths.push_back(c.length_years);
        amplitudes.push_back(f.amplitude);
        leads.push_back(c.minimum_year - f.onset_year);
        widths.push_back(f.width_scale);
        if (c.has_maximum) { rises.push_back(c.rise_years); maxima.push_back(c.maximum_value); }
    }
    st.mean_period_years = mean_of(periods);
    st.sd_period_years = sd_of(periods);
    st.mean_length_years = mean_of(lengths);
    st.sd_length_years = sd_of(lengths);
    st.mean_amplitude = mean_of(amplitudes);
    st.sd_amplitude = sd_of(amplitudes);
    double num = 0.0, den = 0.0;
    for (std::size_t k = 0; k < amplitudes.size(); ++k) {
        den += (amplitudes[k] - st.mean_amplitude) * (amplitudes[k] - st.mean_amplitude);
        if (k + 1 < amplitudes.size()) num += (amplitudes[k] - st.mean_amplitude) * (amplitudes[k + 1] - st.mean_amplitude);
    }
    st.amplitude_autocorrelation = den > 0.0 ? std::clamp(num / den, 0.0, 0.8) : 0.0;
    st.mean_lead_years = mean_of(leads);
    st.sd_lead_years = sd_of(leads);
    st.mean_width_scale = std::clamp(mean_of(widths), 0.5, 2.0);
    st.sd_width_scale = sd_of(widths);
    st.waldmeier_correlation = correlation(rises, maxima);
    // Residuals of the monthly values around the fitted curves of the complete cycles.
    {
        double r2 = 0.0, lag = 0.0;
        std::size_t count = 0;
        const std::size_t end = a.cycles[a.fits.size() - 1].end_index + 1;
        double previous = 0.0;
        for (std::size_t k = 0; k < end; ++k) {
            const double year = a.record.year_at(k);
            if (year < a.fits.front().curve.onset_year) continue;
            double model = 0.0;
            for (const auto& f : a.fits) model += f.curve.value(year);
            const double r = a.record.values[k] - model;
            if (count > 0) lag += r * previous;
            r2 += r * r;
            previous = r;
            ++count;
        }
        st.residual_sd = count > 1 ? std::sqrt(r2 / static_cast<double>(count)) : 10.0;
        st.residual_autocorrelation = r2 > 0.0 ? std::clamp(lag / r2, 0.0, 0.95) : 0.0;
    }
    if (!std::isfinite(st.sd_period_years)) st.sd_period_years = 1.0;
    if (!std::isfinite(st.mean_period_years)) st.mean_period_years = st.mean_length_years;
    if (!std::isfinite(st.sd_lead_years)) st.sd_lead_years = 0.2;
    if (!std::isfinite(st.sd_width_scale)) st.sd_width_scale = 0.15;
    if (!std::isfinite(st.sd_amplitude)) st.sd_amplitude = 0.3 * st.mean_amplitude;

    // The cycle in progress.
    const auto& L = a.cycles.back();
    a.current_cycle = L.number;
    a.phase_years = issue_year - L.minimum_year;
    a.onset_prior_year = L.minimum_year - st.mean_lead_years;
    a.onset_prior_sigma = std::max(0.15, st.sd_lead_years);
    const double start = std::min(L.minimum_year, a.onset_prior_year) - settings_.fit_lead_in_years;
    const std::size_t from = static_cast<std::size_t>(std::max(0, month_index_of(start) - record_.first_month));
    const std::size_t to = a.record.values.size();
    const auto base = baseline_of(a.record, from, to, a.fits);
    // Correlated monthly noise carries fewer independent values: inflate its variance by (1 + phi) / (1 - phi).
    const double phi = st.residual_autocorrelation;
    const double noise_variance = st.residual_sd * st.residual_sd * (1.0 + phi) / (1.0 - phi);
    a.width_fitted = a.phase_years >= settings_.shape_fit_phase_years;
    const SolarCycleFit fit = fit_solar_cycle_onset_prior(a.record, from, to, base, a.onset_prior_year, a.onset_prior_sigma, noise_variance, a.width_fitted, st.mean_width_scale);
    a.onset_year = fit.curve.onset_year;
    a.onset_sigma = fit.onset_sigma;
    a.amplitude_fitted = true;
    a.fit_amplitude = fit.curve.amplitude;
    a.width_scale = fit.curve.width_scale;

    // AR(1) prior from the previous cycle's amplitude.
    const double rho = st.amplitude_autocorrelation;
    a.prior_amplitude = st.mean_amplitude + rho * (a.fits.back().curve.amplitude - st.mean_amplitude);
    a.prior_sigma = st.sd_amplitude * std::sqrt(1.0 - rho * rho);

    a.fit_sigma = kNaN;
    if (errors) {
        const double frac = errors->amplitude_error(a.phase_years);
        if (std::isfinite(frac) && frac > 0.0) a.fit_sigma = frac * a.fit_amplitude;
    }
    if (std::isfinite(a.fit_sigma)) {
        const double wf = 1.0 / (a.fit_sigma * a.fit_sigma), wp = 1.0 / (a.prior_sigma * a.prior_sigma);
        a.amplitude = (wf * a.fit_amplitude + wp * a.prior_amplitude) / (wf + wp);
        a.amplitude_sigma = 1.0 / std::sqrt(wf + wp);
    } else {
        a.amplitude = a.fit_amplitude;
        a.amplitude_sigma = a.prior_sigma;
    }
    a.amplitude = std::clamp(a.amplitude, 1.0, 1000.0);
    // Width: the timing error of the peak, converted to the width scale (t_peak = onset + u* b).
    const double peak_lag_years = kHathawayPeakShape * fit.curve.b_months / 12.0;
    const double timing = errors ? errors->timing_error(a.phase_years) : kNaN;
    a.width_sigma = std::isfinite(timing) ? a.width_scale * timing / peak_lag_years : st.sd_width_scale;
    SolarCycleFit current;
    current.curve = make_solar_cycle_curve(a.onset_year, a.amplitude, a.width_scale);
    current.months = fit.months;
    current.rms = std::sqrt(sse(a.record, from, to, base, current.curve) / static_cast<double>(std::max<std::size_t>(1, current.months)));
    a.fits.push_back(current);
    return a;
}

std::pair<double, double> SolarCycleForecaster::curve_maximum(const std::vector<SolarCycleCurve>& curves, const SolarCycleCurve& cycle) {
    double best_year = cycle.peak_year(), best = -1.0;
    for (int m = month_index_of(cycle.onset_year + 1.5); month_mid_year(m) <= cycle.onset_year + 8.0; ++m) {
        const double v = cycles_value(curves, month_mid_year(m));
        if (v > best) { best = v; best_year = month_mid_year(m); }
    }
    return {best_year, best};
}

SolarCycleForecast SolarCycleForecaster::forecast(const SolarCycleAnalysis& a, double horizon_year, std::size_t members, std::uint64_t seed) const {
    if (!(horizon_year > a.issue_year + 1.0 && horizon_year <= a.issue_year + 60.0)) throw std::invalid_argument("the forecast horizon must lie 1-60 years after the issue date");
    if (members < 10 || members > 5000) throw std::invalid_argument("the forecast ensemble needs 10-5000 members");
    SolarCycleForecast out;
    out.issue_year = a.issue_year;
    out.first_month = month_index_of(a.issue_year) - 24;
    out.months = static_cast<std::size_t>(month_index_of(horizon_year) - out.first_month + 1);
    const auto& st = a.statistics;
    const double rho = st.amplitude_autocorrelation;
    const double innovation = st.sd_amplitude * std::sqrt(1.0 - rho * rho);
    const double mean_width = st.mean_width_scale;
    const double period_lo = std::max(7.0, st.mean_period_years - 2.5 * st.sd_period_years), period_hi = st.mean_period_years + 2.5 * st.sd_period_years;
    const std::vector<SolarCycleFit> past(a.fits.begin(), a.fits.end() - 1);

    // One member: curves of every cycle; z == nullptr gives the central member.
    auto build = [&](NormalSequence* z, std::vector<int>& numbers) {
        auto draw = [&]() { return z ? z->next() : 0.0; };
        std::vector<SolarCycleCurve> curves;
        numbers.clear();
        for (std::size_t k = 0; k < past.size(); ++k) { curves.push_back(past[k].curve); numbers.push_back(a.cycles[k].number); }
        double amplitude = std::clamp(a.amplitude + a.amplitude_sigma * draw(), 5.0, 600.0);
        double onset = a.onset_year + a.onset_sigma * draw();
        double width = std::clamp(a.width_scale + a.width_sigma * draw(), 0.5, 2.0);
        int number = a.current_cycle;
        curves.push_back(make_solar_cycle_curve(onset, amplitude, width));
        numbers.push_back(number);
        while (onset < horizon_year + 2.0) {
            const double period = std::clamp(st.mean_period_years + st.sd_period_years * draw(), period_lo, period_hi);
            onset += period;
            amplitude = std::clamp(st.mean_amplitude + rho * (amplitude - st.mean_amplitude) + innovation * draw(), 5.0, 600.0);
            width = std::clamp(mean_width + st.sd_width_scale * draw(), 0.5, 2.0);
            curves.push_back(make_solar_cycle_curve(onset, amplitude, width));
            numbers.push_back(++number);
        }
        return curves;
    };
    auto series = [&](const std::vector<SolarCycleCurve>& curves) {
        std::vector<double> v(out.months);
        for (std::size_t k = 0; k < out.months; ++k) v[k] = cycles_value(curves, out.year_at(k));
        return v;
    };
    out.central_curves = build(nullptr, out.central_cycle_numbers);
    out.central = series(out.central_curves);
    NormalSequence z(seed);
    std::vector<int> numbers;
    for (std::size_t m = 0; m < members; ++m) {
        out.member_curves.push_back(build(&z, numbers));
        out.members.push_back(series(out.member_curves.back()));
    }

    // Turning points: the maximum of the cycle in progress (unless observed), then for every later cycle the
    // minimum that starts it and its maximum.
    const double end_year = out.year_at(out.months - 1);
    const std::size_t current_index = past.size();
    const bool current_max_observed = a.cycles.back().has_maximum;
    auto turning = [&](bool minimum, std::size_t cycle_index) {
        SolarTurningPoint tp;
        tp.minimum = minimum;
        tp.cycle = out.central_cycle_numbers[cycle_index];
        std::vector<double> years, values;
        std::size_t late = 0;
        auto locate = [&](const std::vector<SolarCycleCurve>& curves, double& year, double& value) {
            const auto& c = curves[cycle_index];
            const double lo = minimum ? c.onset_year - 1.5 : c.onset_year + 1.5, hi = minimum ? c.onset_year + 2.5 : c.onset_year + 8.0;
            if (hi > end_year) return false;
            value = minimum ? std::numeric_limits<double>::infinity() : -1.0;
            for (int m = month_index_of(lo); month_mid_year(m) <= hi; ++m) {
                const double v = cycles_value(curves, month_mid_year(m));
                if (minimum ? v < value : v > value) { value = v; year = month_mid_year(m); }
            }
            return true;
        };
        for (const auto& curves : out.member_curves) {
            if (cycle_index >= curves.size()) { ++late; continue; }
            double y = 0.0, v = 0.0;
            if (locate(curves, y, v)) { years.push_back(y); values.push_back(v); } else ++late;
        }
        tp.beyond_horizon = static_cast<double>(late) > 0.05 * static_cast<double>(out.member_curves.size());
        if (!years.empty()) {
            tp.year_median = solar_percentile(years, 50.0); tp.year_p5 = solar_percentile(years, 5.0); tp.year_p95 = solar_percentile(years, 95.0);
            tp.value_median = solar_percentile(values, 50.0); tp.value_p5 = solar_percentile(values, 5.0); tp.value_p95 = solar_percentile(values, 95.0);
        } else {
            tp.year_median = tp.year_p5 = tp.year_p95 = tp.value_median = tp.value_p5 = tp.value_p95 = kNaN;
        }
        double y = kNaN, v = kNaN;
        if (!locate(out.central_curves, y, v)) { y = kNaN; v = kNaN; }
        tp.central_year = y;
        tp.central_value = v;
        return tp;
    };
    if (!current_max_observed) out.turning_points.push_back(turning(false, current_index));
    for (std::size_t j = current_index + 1; j < out.central_curves.size(); ++j) {
        if (out.central_curves[j].onset_year - 1.5 > end_year) break;
        out.turning_points.push_back(turning(true, j));
        if (out.central_curves[j].onset_year + 1.5 > end_year) break;
        out.turning_points.push_back(turning(false, j));
    }
    return out;
}

std::vector<SolarBacktestEntry> SolarCycleForecaster::backtest(const std::vector<double>& phases_years) const {
    std::vector<SolarBacktestEntry> out;
    const double record_end = record_.year_at(record_.values.size() - 1);
    for (std::size_t k = settings_.minimum_history_cycles + 1; k < cycles_.size(); ++k) {
        const auto& target = cycles_[k];
        if (!target.has_maximum) continue;
        // The observed maximum must be confirmed by the full record.
        for (double phase : phases_years) {
            const double issue = target.minimum_year + phase;
            if (issue > record_end) continue;
            SolarCycleAnalysis a;
            try { a = analyse(issue, nullptr); } catch (const std::invalid_argument&) { continue; }
            SolarBacktestEntry e;
            e.cycle = target.number;
            e.phase_years = phase;
            e.issue_year = issue;
            e.observed_maximum = target.maximum_value;
            e.observed_maximum_year = target.maximum_year;
            e.observed_minimum_year = target.minimum_year;
            e.predicted_minimum_year = kNaN;
            const auto& st = a.statistics;
            std::vector<SolarCycleCurve> curves;
            for (std::size_t j = 0; j + 1 < a.fits.size(); ++j) curves.push_back(a.fits[j].curve);
            if (a.current_cycle == target.number) {
                e.minimum_confirmed = true;
                curves.push_back(make_solar_cycle_curve(a.onset_year, a.fit_amplitude, a.width_scale));
                const auto fitted = curve_maximum(curves, curves.back());
                e.fit_maximum = fitted.second;
                e.predicted_maximum_year = fitted.first;
                curves.back() = make_solar_cycle_curve(a.onset_year, std::clamp(a.prior_amplitude, 1.0, 1000.0), a.width_scale);
                e.prior_maximum = curve_maximum(curves, curves.back()).second;
            } else if (a.current_cycle + 1 == target.number) {
                // The minimum is not yet detectable: the cycle in progress is the previous one, the target follows it.
                e.minimum_confirmed = false;
                e.fit_maximum = kNaN;
                curves.push_back(a.fits.back().curve);
                const double rho = st.amplitude_autocorrelation;
                const double prior = std::clamp(st.mean_amplitude + rho * (a.fits.back().curve.amplitude - st.mean_amplitude), 1.0, 1000.0);
                curves.push_back(make_solar_cycle_curve(a.onset_year + st.mean_period_years, prior, st.mean_width_scale));
                const auto predicted = curve_maximum(curves, curves.back());
                e.prior_maximum = predicted.second;
                e.predicted_maximum_year = predicted.first;
                // The minimum between them.
                double best = std::numeric_limits<double>::infinity();
                const double onset = curves.back().onset_year;
                for (int m = month_index_of(onset - 1.5); month_mid_year(m) <= onset + 2.5; ++m) {
                    const double v = cycles_value(curves, month_mid_year(m));
                    if (v < best) { best = v; e.predicted_minimum_year = month_mid_year(m); }
                }
            } else {
                continue;
            }
            e.combined_maximum = e.minimum_confirmed ? e.fit_maximum : e.prior_maximum;
            // The prior's spread, for the later combination.
            e.prior_sigma = a.prior_sigma;
            out.push_back(e);
        }
    }
    return out;
}

SolarErrorModel SolarCycleForecaster::error_model(const std::vector<SolarBacktestEntry>& entries, const std::vector<double>& phases_years) {
    SolarErrorModel m;
    m.phases_years = phases_years;
    for (double p : phases_years) {
        double sa = 0.0, st = 0.0;
        std::size_t n = 0;
        for (const auto& e : entries) {
            if (std::abs(e.phase_years - p) > 1e-9 || !std::isfinite(e.fit_maximum)) continue;
            const double rel = (e.fit_maximum - e.observed_maximum) / e.observed_maximum;
            sa += rel * rel;
            st += (e.predicted_maximum_year - e.observed_maximum_year) * (e.predicted_maximum_year - e.observed_maximum_year);
            ++n;
        }
        m.amplitude_error_fraction.push_back(n >= 2 ? std::sqrt(sa / static_cast<double>(n)) : kNaN);
        m.peak_timing_error_years.push_back(n >= 2 ? std::sqrt(st / static_cast<double>(n)) : kNaN);
    }
    return m;
}

void SolarCycleForecaster::apply_error_model(std::vector<SolarBacktestEntry>& entries, const SolarErrorModel& model) {
    for (auto& e : entries) {
        if (!std::isfinite(e.fit_maximum)) { e.combined_maximum = e.prior_maximum; continue; }
        const double frac = model.amplitude_error(e.phase_years);
        if (!(std::isfinite(frac) && frac > 0.0) || !(e.prior_sigma > 0.0)) { e.combined_maximum = e.fit_maximum; continue; }
        const double sf = frac * e.fit_maximum;
        const double wf = 1.0 / (sf * sf), wp = 1.0 / (e.prior_sigma * e.prior_sigma);
        e.combined_maximum = (wf * e.fit_maximum + wp * e.prior_maximum) / (wf + wp);
    }
}

std::vector<SolarBacktestSummary> SolarCycleForecaster::summarize(const std::vector<SolarBacktestEntry>& entries, const std::vector<double>& phases_years) {
    std::vector<SolarBacktestSummary> out;
    for (double p : phases_years) {
        SolarBacktestSummary s;
        s.phase_years = p;
        double fit = 0.0, prior = 0.0, comb = 0.0, rel2 = 0.0, timing = 0.0, minimum = 0.0;
        std::size_t minima = 0;
        for (const auto& e : entries) {
            if (std::abs(e.phase_years - p) > 1e-9) continue;
            ++s.forecasts;
            prior += std::abs(e.prior_maximum - e.observed_maximum);
            comb += std::abs(e.combined_maximum - e.observed_maximum);
            timing += std::abs(e.predicted_maximum_year - e.observed_maximum_year);
            if (std::isfinite(e.fit_maximum)) {
                ++s.fitted;
                fit += std::abs(e.fit_maximum - e.observed_maximum);
                const double rel = (e.fit_maximum - e.observed_maximum) / e.observed_maximum;
                rel2 += rel * rel;
            }
            if (std::isfinite(e.predicted_minimum_year)) { ++minima; minimum += std::abs(e.predicted_minimum_year - e.observed_minimum_year); }
        }
        const double n = static_cast<double>(std::max<std::size_t>(1, s.forecasts)), nf = static_cast<double>(std::max<std::size_t>(1, s.fitted));
        s.prior_mae = s.forecasts ? prior / n : kNaN;
        s.combined_mae = s.forecasts ? comb / n : kNaN;
        s.timing_mae_years = s.forecasts ? timing / n : kNaN;
        s.fit_mae = s.fitted ? fit / nf : kNaN;
        s.fit_rms_fraction = s.fitted ? std::sqrt(rel2 / nf) : kNaN;
        s.minimum_mae_years = minima ? minimum / static_cast<double>(minima) : kNaN;
        out.push_back(s);
    }
    return out;
}

}  // namespace terraflux::weather
