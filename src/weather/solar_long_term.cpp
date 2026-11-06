#include "terraflux/weather/solar_long_term.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

}  // namespace

const char* solar_epoch_kind_name(SolarEpochKind kind) {
    switch (kind) {
        case SolarEpochKind::grand_minimum: return "grand_minimum";
        case SolarEpochKind::grand_maximum: return "grand_maximum";
        case SolarEpochKind::minimum: return "minimum";
        case SolarEpochKind::maximum: return "maximum";
    }
    return "grand_minimum";
}

// ---- Envelope and historical cycles ----------------------------------------------------------------------------

void validate_solar_epochs(const std::vector<SolarEpoch>& epochs, const SolarEnvelopeConfig& c) {
    require(within(c.normal_amplitude, 5.0, 400.0), "long_term.normal_amplitude must lie within 5-400");
    require(within(c.transition_years, 1.0, 60.0), "long_term.transition_years must lie within 1-60");
    require(epochs.size() <= 40, "long_term.epochs holds at most 40 epochs");
    std::set<std::string> names;
    for (std::size_t k = 0; k < epochs.size(); ++k) {
        const auto& e = epochs[k];
        require(!e.name.empty() && names.insert(e.name).second, "long_term epochs need unique non-empty names");
        require(within(e.start_year, -12000.0, 3000.0) && within(e.end_year, -12000.0, 3000.0) && e.end_year > e.start_year, "long_term epochs need start_year < end_year within -12000-3000");
        require(std::isfinite(e.cycle_amplitude) && e.cycle_amplitude <= 400.0, "long_term epoch cycle_amplitude must be at most 400 (<= 0: annotation only)");
        if (e.cycle_amplitude > 0.0)
            for (std::size_t j = 0; j < k; ++j)
                if (epochs[j].cycle_amplitude > 0.0 && e.start_year < epochs[j].end_year && epochs[j].start_year < e.end_year)
                    throw std::invalid_argument("long_term epochs that set an amplitude must not overlap: " + epochs[j].name + " and " + e.name);
    }
}

double solar_envelope(double year, const std::vector<SolarEpoch>& epochs, const SolarEnvelopeConfig& c) {
    // Blended in the logarithm: the transitions into a grand minimum are smooth over orders of magnitude.
    double log_amplitude = std::log(c.normal_amplitude);
    for (const auto& e : epochs) {
        if (!(e.cycle_amplitude > 0.0)) continue;
        const double w = 0.5 * (std::tanh((year - e.start_year) / c.transition_years) - std::tanh((year - e.end_year) / c.transition_years));
        log_amplitude += w * (std::log(e.cycle_amplitude) - std::log(c.normal_amplitude));
    }
    return std::exp(log_amplitude);
}

HistoricalCycles synthesize_historical_cycles(double first_onset, int first_number, double start_year, const std::vector<SolarEpoch>& epochs,
                                              const SolarEnvelopeConfig& envelope, const HistoricalCycleConfig& c) {
    require(std::isfinite(first_onset) && std::isfinite(start_year) && start_year < first_onset - 11.0, "historical cycles need a start at least a cycle before the table");
    require(first_onset - start_year <= 15000.0, "historical cycles are limited to 15000 years");
    require(within(c.period_years, 8.0, 15.0) && within(c.period_sd_years, 0.0, 3.0), "long_term historical period must lie within 8-15 years (spread 0-3)");
    require(within(c.amplitude_log_sd, 0.0, 1.5), "long_term historical amplitude_log_sd must lie within 0-1.5");
    NormalSequence random(c.seed);
    HistoricalCycles backwards;
    double onset = first_onset;
    int number = first_number;
    while (onset > start_year - 16.0) {
        const double period = std::clamp(c.period_years + c.period_sd_years * random.next(), 8.0, 15.0);
        onset -= period;
        --number;
        const double typical = solar_envelope(onset + 4.5, epochs, envelope);
        const double amplitude = std::clamp(typical * std::exp(c.amplitude_log_sd * random.next() - 0.5 * c.amplitude_log_sd * c.amplitude_log_sd), 1.0, 600.0);
        backwards.curves.push_back(make_solar_cycle_curve(onset, amplitude));
        backwards.numbers.push_back(number);
    }
    HistoricalCycles out;
    out.curves.assign(backwards.curves.rbegin(), backwards.curves.rend());
    out.numbers.assign(backwards.numbers.rbegin(), backwards.numbers.rend());
    return out;
}

std::vector<double> annual_cycle_means(const std::vector<SolarCycleCurve>& curves, int first_year, int last_year) {
    std::vector<double> out;
    for (int y = first_year; y <= last_year; ++y) {
        double s = 0.0;
        for (int m = 0; m < 12; ++m) s += cycles_value(curves, y + (m + 0.5) / 12.0);
        out.push_back(s / 12.0);
    }
    return out;
}

std::vector<double> exponential_moving_average(const std::vector<double>& values, double timescale, std::size_t spin) {
    std::vector<double> out(values.size());
    if (values.empty()) return out;
    require(timescale > 0.0 && std::isfinite(timescale), "moving-average time scale must be positive");
    const std::size_t n = std::min(spin == 0 ? std::size_t{1} : spin, values.size());
    double m = std::accumulate(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(n), 0.0) / static_cast<double>(n);
    const double k = 1.0 - std::exp(-1.0 / timescale);
    for (std::size_t i = 0; i < values.size(); ++i) {
        m += (values[i] - m) * k;
        out[i] = m;
    }
    return out;
}

std::vector<double> running_mean(const std::vector<double>& values, std::size_t window) {
    std::vector<double> out(values.size());
    const std::size_t half = window / 2;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::size_t lo = i >= half ? i - half : 0, hi = std::min(values.size() - 1, i + half);
        double s = 0.0;
        for (std::size_t j = lo; j <= hi; ++j) s += values[j];
        out[i] = s / static_cast<double>(hi - lo + 1);
    }
    return out;
}

std::vector<SolarEpisode> detect_solar_episodes(const std::vector<double>& annual, int first_year, double low, double high, std::size_t window, int min_years) {
    require(low < high, "episode thresholds need low < high");
    const auto rm = running_mean(annual, window);
    std::vector<SolarEpisode> out;
    auto scan = [&](bool minimum) {
        std::size_t k = 0;
        while (k < rm.size()) {
            const bool in = minimum ? rm[k] < low : rm[k] > high;
            if (!in) { ++k; continue; }
            std::size_t j = k;
            while (j < rm.size() && (minimum ? rm[j] < low : rm[j] > high)) ++j;
            if (static_cast<int>(j - k) >= min_years) {
                double s = 0.0;
                for (std::size_t i = k; i < j; ++i) s += annual[i];
                out.push_back({minimum, first_year + static_cast<int>(k), first_year + static_cast<int>(j) - 1, s / static_cast<double>(j - k)});
            }
            k = j;
        }
    };
    scan(true);
    scan(false);
    std::sort(out.begin(), out.end(), [](const SolarEpisode& a, const SolarEpisode& b) { return a.start_year < b.start_year; });
    return out;
}

// ---- Cosmic rays, volcanoes, CO2 ----------------------------------------------------------------------------------

void validate_cosmic_rays(const CosmicRayConfig& c) {
    require(within(c.phi_min_mv, 0.0, 1000.0) && within(c.phi_per_sunspot_mv, 0.0, 50.0), "cosmic_rays: phi_min_mv within 0-1000 and phi_per_sunspot_mv within 0-50");
    require(within(c.background_weight, 0.0, 1.0), "cosmic_rays.background_weight must lie within 0-1");
    require(within(c.production_offset_mv, 0.0, 3000.0) && within(c.production_exponent, 0.0, 3.0), "cosmic_rays: production_offset_mv within 0-3000 and production_exponent within 0-3");
}

double modulation_potential_mv(double sunspot_3yr, double background, const CosmicRayConfig& c) {
    return c.phi_min_mv + c.phi_per_sunspot_mv * ((1.0 - c.background_weight) * std::max(0.0, sunspot_3yr) + c.background_weight * std::max(0.0, background));
}

double relative_cosmogenic_production(double phi, double reference, const CosmicRayConfig& c) {
    return std::pow((reference + c.production_offset_mv) / (phi + c.production_offset_mv), c.production_exponent);
}

double volcanic_forcing_w_m2(double year, const std::vector<VolcanicEruption>& eruptions, double rise) {
    double f = 0.0;
    for (const auto& e : eruptions) {
        const double u = year - e.year;
        if (u < 0.0 || u > 12.0 * rise + 10.0) continue;
        f += e.peak_forcing_w_m2 * (u / rise) * std::exp(1.0 - u / rise);
    }
    return f;
}

double annual_volcanic_forcing_w_m2(int year, const std::vector<VolcanicEruption>& eruptions, double rise) {
    double s = 0.0;
    for (int m = 0; m < 12; ++m) s += volcanic_forcing_w_m2(year + (m + 0.5) / 12.0, eruptions, rise);
    return s / 12.0;
}

double co2_forcing_w_m2(double ppm, double reference) { return 5.35 * std::log(ppm / reference); }

// ---- Dynamo map ---------------------------------------------------------------------------------------------------

void validate_solar_dynamo(const SolarDynamoConfig& c) {
    require(within(c.weak_alpha, 0.5, 1.5), "dynamo.weak_alpha must lie within 0.5-1.5");
    require(within(c.babcock_leighton_noise, 0.0, 3.0), "dynamo.babcock_leighton_noise must lie within 0-3");
    require(within(c.emergence_threshold, 0.05, 2.0) && within(c.emergence_width, 0.01, 1.0), "dynamo emergence threshold within 0.05-2 and width within 0.01-1");
    require(within(c.quenching_field, 0.5, 5.0), "dynamo.quenching_field must lie within 0.5-5");
    require(c.babcock_leighton_efficiency() > 0.0, "dynamo: the Babcock-Leighton efficiency 1 + 1/b_q^2 - weak_alpha must be positive");
    require(within(c.floor_noise, 0.0, 0.5), "dynamo.floor_noise must lie within 0-0.5");
    require(within(c.amplitude_scale, 10.0, 600.0), "dynamo.amplitude_scale must lie within 10-600");
    require(within(c.cycle_mean_fraction, 0.1, 1.0), "dynamo.cycle_mean_fraction must lie within 0.1-1");
    require(within(c.grand_minimum_mean, 0.0, 200.0) && within(c.grand_maximum_mean, 0.0, 400.0) && c.grand_minimum_mean < c.grand_maximum_mean, "dynamo episode thresholds need 0 <= grand_minimum_mean < grand_maximum_mean");
    require(c.episode_cycles >= 1 && c.episode_cycles <= 10, "dynamo.episode_cycles must lie within 1-10");
}

double dynamo_emergence(double b, const SolarDynamoConfig& c) { return 0.5 * (1.0 + std::erf((b - c.emergence_threshold) / c.emergence_width)); }
double dynamo_amplitude(double b, const SolarDynamoConfig& c) { return c.amplitude_scale * b * b * dynamo_emergence(b, c); }

double dynamo_field_for_amplitude(double amplitude, const SolarDynamoConfig& c) {
    double lo = 0.0, hi = 10.0;
    if (!(amplitude > 0.0)) return 0.0;
    if (amplitude >= dynamo_amplitude(hi, c)) return hi;
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        (dynamo_amplitude(mid, c) < amplitude ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

namespace {
double dynamo_step(double b, double efficiency, double floor, const SolarDynamoConfig& c) {
    return std::abs(c.babcock_leighton_efficiency() * efficiency * dynamo_emergence(b, c) + c.weak_alpha) * b / (1.0 + (b / c.quenching_field) * (b / c.quenching_field)) + c.floor_noise * floor;
}
}  // namespace

std::vector<SolarDynamoStep> simulate_solar_dynamo(const SolarDynamoConfig& c, double initial_field, std::size_t cycles, NormalSequence& random) {
    require(within(initial_field, 0.0, 10.0), "dynamo initial field must lie within 0-10");
    std::vector<SolarDynamoStep> out;
    out.reserve(cycles);
    double b = initial_field;
    for (std::size_t n = 0; n < cycles; ++n) {
        SolarDynamoStep s;
        s.field = b;
        s.emergence = dynamo_emergence(b, c);
        s.amplitude = dynamo_amplitude(b, c);
        s.efficiency = 1.0 + c.babcock_leighton_noise * random.next();
        const double floor = std::abs(random.next());
        out.push_back(s);
        b = std::min(10.0, dynamo_step(b, s.efficiency, floor, c));
    }
    return out;
}

std::vector<SolarCycleEpisode> dynamo_episodes(const std::vector<double>& amplitudes, const SolarDynamoConfig& c) {
    std::vector<SolarCycleEpisode> out;
    for (bool minimum : {true, false}) {
        std::size_t k = 0;
        auto in = [&](std::size_t i) { const double m = c.cycle_mean_fraction * amplitudes[i]; return minimum ? m < c.grand_minimum_mean : m > c.grand_maximum_mean; };
        while (k < amplitudes.size()) {
            if (!in(k)) { ++k; continue; }
            std::size_t j = k;
            while (j < amplitudes.size() && in(j)) ++j;
            if (j - k >= c.episode_cycles) out.push_back({minimum, k, j - k});
            k = j;
        }
    }
    std::sort(out.begin(), out.end(), [](const SolarCycleEpisode& a, const SolarCycleEpisode& b) { return a.start < b.start; });
    return out;
}

SolarDynamoStatistics dynamo_statistics(const std::vector<double>& a, const SolarDynamoConfig& c, double years_per_cycle) {
    SolarDynamoStatistics s;
    s.cycles = a.size();
    s.years = static_cast<double>(a.size()) * years_per_cycle;
    if (a.empty()) return s;
    const auto episodes = dynamo_episodes(a, c);
    std::size_t gm_cycles = 0, gx_cycles = 0, gm_count = 0, gx_count = 0, longest = 0;
    std::vector<bool> in_gm(a.size(), false);
    double waiting = 0.0;
    std::size_t waits = 0, last_end = 0;
    bool seen = false;
    for (const auto& e : episodes) {
        if (e.grand_minimum) {
            gm_cycles += e.length; ++gm_count; longest = std::max(longest, e.length);
            if (s.grand_minimum_lengths.size() <= e.length) s.grand_minimum_lengths.resize(e.length + 1, 0);
            ++s.grand_minimum_lengths[e.length];
            for (std::size_t i = e.start; i < e.start + e.length; ++i) in_gm[i] = true;
            if (seen) { waiting += static_cast<double>(e.start - last_end); ++waits; }
            last_end = e.start + e.length;
            seen = true;
        } else {
            gx_cycles += e.length; ++gx_count;
        }
    }
    const double n = static_cast<double>(a.size());
    s.grand_minimum_fraction = static_cast<double>(gm_cycles) / n;
    s.grand_maximum_fraction = static_cast<double>(gx_cycles) / n;
    s.grand_minima_per_11400_years = static_cast<double>(gm_count) * 11400.0 / s.years;
    s.grand_maxima_per_11400_years = static_cast<double>(gx_count) * 11400.0 / s.years;
    s.grand_minimum_mean_years = gm_count ? static_cast<double>(gm_cycles) / static_cast<double>(gm_count) * years_per_cycle : kNaN;
    s.grand_maximum_mean_years = gx_count ? static_cast<double>(gx_cycles) / static_cast<double>(gx_count) * years_per_cycle : kNaN;
    s.grand_minimum_longest_years = static_cast<double>(longest) * years_per_cycle;
    s.mean_waiting_years = waits ? waiting / static_cast<double>(waits) * years_per_cycle : kNaN;
    double sum = 0.0;
    std::size_t outside = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if (!in_gm[i]) { sum += a[i]; ++outside; }
    s.mean_amplitude_outside_minima = outside ? sum / static_cast<double>(outside) : kNaN;
    const double mean = std::accumulate(a.begin(), a.end(), 0.0) / n;
    double num = 0.0, den = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        den += (a[i] - mean) * (a[i] - mean);
        if (i + 1 < a.size()) num += (a[i] - mean) * (a[i + 1] - mean);
    }
    s.amplitude_autocorrelation = den > 0.0 ? num / den : 0.0;
    return s;
}

SolarDynamoOutlook dynamo_outlook(const SolarDynamoConfig& c, const std::vector<double>& observed, std::size_t cycles_ahead, std::size_t realizations, std::uint64_t seed) {
    require(!observed.empty(), "the dynamo outlook needs at least one observed cycle amplitude");
    require(cycles_ahead >= 1 && cycles_ahead <= 100 && realizations >= 10 && realizations <= 1000000, "the dynamo outlook needs 1-100 cycles and 10-1000000 realizations");
    auto low = [&](double amplitude) { return c.cycle_mean_fraction * amplitude < c.grand_minimum_mean; };
    std::size_t observed_run = 0;
    for (auto it = observed.rbegin(); it != observed.rend() && low(*it); ++it) ++observed_run;
    const double b0 = dynamo_field_for_amplitude(observed.back(), c);
    SolarDynamoOutlook out;
    out.realizations = realizations;
    std::vector<std::vector<double>> amplitudes(cycles_ahead, std::vector<double>(realizations));
    std::vector<std::size_t> begun(cycles_ahead, 0), below(cycles_ahead, 0);
    NormalSequence random(seed);
    for (std::size_t r = 0; r < realizations; ++r) {
        double b = b0;
        std::size_t run = observed_run;
        bool started = observed_run >= c.episode_cycles;
        for (std::size_t k = 0; k < cycles_ahead; ++k) {
            const double efficiency = 1.0 + c.babcock_leighton_noise * random.next();
            b = std::min(10.0, dynamo_step(b, efficiency, std::abs(random.next()), c));
            const double a = dynamo_amplitude(b, c);
            amplitudes[k][r] = a;
            run = low(a) ? run + 1 : 0;
            if (low(a)) ++below[k];
            if (run >= c.episode_cycles) started = true;
            if (started) ++begun[k];
        }
    }
    for (std::size_t k = 0; k < cycles_ahead; ++k) {
        out.grand_minimum_begun.push_back(static_cast<double>(begun[k]) / static_cast<double>(realizations));
        out.below_threshold.push_back(static_cast<double>(below[k]) / static_cast<double>(realizations));
        out.amplitude_p5.push_back(solar_percentile(amplitudes[k], 5.0));
        out.amplitude_p50.push_back(solar_percentile(amplitudes[k], 50.0));
        out.amplitude_p95.push_back(solar_percentile(amplitudes[k], 95.0));
    }
    return out;
}

}  // namespace terraflux::weather
