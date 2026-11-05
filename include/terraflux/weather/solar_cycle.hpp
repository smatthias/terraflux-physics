#pragma once

#include "terraflux/weather/overturning_box.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace terraflux::weather {

// Solar cycle v1: the ~11-year sunspot cycle, its minima and maxima, and a statistical forecast.
//
// Shape of one cycle (Hathaway, Wilson & Reichmann 1994; Hathaway 2015, Living Rev. Sol. Phys. 12:4), t in months
// after the cycle's onset t0:
//   R(t) = a t^3 / (exp(t^2 / b^2) - c),   c = 0.71,   b = beta * b_H(a),   b_H(a) = 27.12 + 25.15 / (1000 a_v1)^(1/4)
// b_H is Hathaway's width relation, calibrated on the original (v1) sunspot numbers, so it is evaluated with
// a_v1 = 0.6 a (SN v2 = SN v1 / 0.6). Large cycles are narrower and peak sooner: the Waldmeier effect. The peak of
// one cycle lies at t = u* b with u* = 1.0813 and has the value A = a b^3 g*, g* = 0.50380, so a cycle is set by its
// onset, its amplitude A (peak of the 13-month smoothed sunspot number v2) and a width scale beta (1 = Hathaway).
// Successive cycles overlap: the observed minimum lies a few months after the new cycle's onset.
//
// Smoothing: the SILSO 13-month running mean with half weights at both ends (undefined for the first and last six
// months). Minima and maxima are extrema of the smoothed series over +-`half_window` months; near the end of the
// record an extremum counts once `confirm_months` of smoothed data follow it.
//
// Forecast of the cycle in progress: its onset follows from the detected minimum and the mean lead of minima over
// onsets in the history; its amplitude is fitted to the monthly values since the minimum (and its width once it
// is old enough) and combined with an AR(1) prior from the amplitudes of the previous cycles, weighted by the
// errors a backtest of the same method on the past cycles found at the same phase. Later cycles follow with the
// mean onset-to-onset period and the AR(1) amplitude. An ensemble samples every uncertainty.

struct SolarCycleCurve {
    double onset_year{0.0};
    double amplitude{0.0};              // peak of this cycle alone, 13-month smoothed sunspot number (v2)
    double width_scale{1.0};            // beta
    double a{0.0};                      // derived shape parameters
    double b_months{0.0};
    double value(double year) const;
    double peak_year() const;
};

constexpr double kHathawayCurvature = 0.71;
constexpr double kSunspotV1PerV2 = 0.6;
constexpr double kHathawayPeakShape = 1.0813;        // u*
constexpr double kHathawayPeakValue = 0.503803;      // g* = max u^3 / (exp(u^2) - c)

double hathaway_width_months(double a_v1);
// Throws std::invalid_argument for a non-positive amplitude or width scale.
SolarCycleCurve make_solar_cycle_curve(double onset_year, double amplitude, double width_scale = 1.0);
double cycles_value(const std::vector<SolarCycleCurve>& cycles, double year);

// Months are counted from year 0: index = 12 * year + (month - 1); a value stands for the middle of its month.
int month_index_of(double year);
double month_mid_year(int month_index);

struct MonthlySeries {
    int first_month{0};
    std::vector<double> values;
    double year_at(std::size_t k) const { return month_mid_year(first_month + static_cast<int>(k)); }
};

// A table of cycles (as published by SILSO: the dates of the minima and maxima of the smoothed series and the
// maximum value) turned into overlapping curves. The onsets are shifted until the summed curve has its minima at
// the table's dates (the first cycle has no predecessor: its onset is `first_lead_years` before its minimum) and
// the amplitudes are scaled until the maxima have the table's values. The widths follow Hathaway's relation; with
// `calibrate_widths` they are scaled until each maximum falls on the table's date where one is given. Single
// smooth curves cannot represent double peaks: fitting late second peaks (cycles 23 and 24) widens the curves and
// lifts the following minima far above the observed ones, so this is off by default.
struct SolarCycleTableEntry {
    int number{0};
    double minimum_year{0.0};
    double maximum_year{0.0};            // NaN: not given
    double maximum{0.0};                 // smoothed sunspot number at the maximum
};
// Throws std::invalid_argument for unordered or overlapping entries, amplitudes outside 5-600 or maxima outside
// 1.5-9 years after the minimum.
std::vector<SolarCycleCurve> calibrate_cycle_table(const std::vector<SolarCycleTableEntry>& table, double first_lead_years = 0.3, bool calibrate_widths = false);

// Monthly sunspot numbers from curves with multiplicative, month-to-month correlated noise:
//   R_m = max(0, R(t_m) + f sqrt(R(t_m) + 1) e_m),   e_m = phi e_{m-1} + sqrt(1 - phi^2) z_m,
// rounded to 0.1 like the published monthly means.
struct SunspotNoise {
    double factor{2.5};
    double autocorrelation{0.6};
    std::uint64_t seed{1};
};
MonthlySeries synthesize_sunspot_record(const std::vector<SolarCycleCurve>& curves, int first_month, std::size_t months, const SunspotNoise& noise);

// SILSO 13-month smoothing; NaN where undefined (the first and last six months, or a NaN in the window).
std::vector<double> smooth_13_month(const std::vector<double>& monthly);

struct SolarExtremum {
    bool minimum{false};
    std::size_t index{0};
    double value{0.0};
};
// Alternating minima and maxima of a smoothed series (NaN entries are skipped). An entry is an extremum when it is
// the lowest (highest) defined value within +-half_window entries; the window must lie fully inside the defined
// range, except at the end, where `confirm` defined entries after it suffice. Of two neighbouring extrema of the
// same kind the more extreme is kept.
std::vector<SolarExtremum> solar_extrema(const std::vector<double>& smoothed, std::size_t half_window = 30, std::size_t confirm = 12);

struct ObservedSolarCycle {
    int number{0};
    std::size_t minimum_index{0};
    double minimum_year{0.0}, minimum_value{0.0};
    bool has_maximum{false};
    std::size_t maximum_index{0};
    double maximum_year{0.0}, maximum_value{0.0};
    bool complete{false};               // the next minimum is known
    std::size_t end_index{0};           // index of the next minimum
    double end_year{0.0};
    double length_years{0.0}, rise_years{0.0}, decline_years{0.0};   // NaN while unknown
};
// Cycles from minimum to minimum; the first detected minimum starts cycle `first_number`.
std::vector<ObservedSolarCycle> observed_solar_cycles(const MonthlySeries& smoothed, const std::vector<SolarExtremum>& extrema, int first_number);

struct SolarCycleFit {
    SolarCycleCurve curve;
    double rms{0.0};                    // of the monthly residuals
    std::size_t months{0};
    double onset_sigma{0.0};            // curvature estimate of the onset's spread (fits with an onset prior only)
};
// Least-squares fits to record.values[from, to) on top of `baseline` (the earlier cycles' curves, aligned with
// [from, to)). The amplitude is linear for a given width b(a) and is iterated with it.
SolarCycleFit fit_solar_cycle_amplitude(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline,
                                        double onset_year, double width_scale = 1.0);
// Amplitude and width scale for a fixed onset.
SolarCycleFit fit_solar_cycle_shape(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline, double onset_year);
// Onset with a Gaussian prior, amplitude and (optionally) width: minimises
//   SSE / noise_variance + ((onset - onset_prior) / onset_sigma)^2
// over onsets within +-3 sigma. For a cycle in progress the minimum only loosely fixes the onset (minima are flat
// and noisy), while the first months of a cycle constrain the onset and the amplitude only together.
SolarCycleFit fit_solar_cycle_onset_prior(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline,
                                          double onset_prior, double onset_sigma, double noise_variance, bool fit_width, double width_scale = 1.0);
// Onset (within +-onset_range_years of the guess), amplitude and width scale.
SolarCycleFit fit_solar_cycle(const MonthlySeries& record, std::size_t from, std::size_t to, const std::vector<double>& baseline,
                              double onset_guess_year, double onset_range_years = 2.0);

struct SolarCycleStatistics {
    std::size_t cycles{0};                       // complete cycles used
    double mean_period_years{0.0}, sd_period_years{0.0};        // onset to onset of the fitted curves
    double mean_length_years{0.0}, sd_length_years{0.0};        // minimum to minimum, observed
    double mean_amplitude{0.0}, sd_amplitude{0.0};              // fitted amplitudes
    double amplitude_autocorrelation{0.0};                      // lag 1, clamped to [0, 0.8]
    double mean_lead_years{0.0}, sd_lead_years{0.0};            // observed minimum - fitted onset
    double mean_width_scale{1.0}, sd_width_scale{0.0};
    double waldmeier_correlation{0.0};                          // observed rise time vs observed maximum
    double residual_sd{0.0};                                    // monthly values around the fitted curves
    double residual_autocorrelation{0.0};                       // lag 1, clamped to [0, 0.95]
};

struct SolarForecastSettings {
    std::size_t half_window_months{30};
    std::size_t confirm_months{12};
    std::size_t minimum_history_cycles{3};       // complete cycles needed before a forecast
    double fit_lead_in_years{1.0};               // fits start this long before the minimum
    double shape_fit_phase_years{5.5};           // the width is fitted once the cycle is this old (past its peak:
                                                 // near the peak onset, width and amplitude trade off)
    double onset_search_years{2.0};              // onset range of the complete-cycle fits
};

// Empirical errors of the amplitude fit and of the peak timing against the phase (years since the minimum), from
// a backtest; between phases they are interpolated, beyond the last phase held.
struct SolarErrorModel {
    std::vector<double> phases_years;
    std::vector<double> amplitude_error_fraction;   // RMS relative error of the fitted amplitude (NaN: no fit at that phase)
    std::vector<double> peak_timing_error_years;    // RMS error of the predicted peak date (NaN: none)
    double amplitude_error(double phase_years) const;
    double timing_error(double phase_years) const;
};

// The state of the record at an issue date (only data before it).
struct SolarCycleAnalysis {
    double issue_year{0.0};
    MonthlySeries record, smoothed;
    std::vector<SolarExtremum> extrema;
    std::vector<ObservedSolarCycle> cycles;
    std::vector<SolarCycleFit> fits;             // one per observed cycle (the last one: the cycle in progress)
    SolarCycleStatistics statistics;
    // The cycle in progress (the last observed cycle).
    int current_cycle{0};
    double phase_years{0.0};                     // issue - its minimum
    double onset_prior_year{0.0}, onset_prior_sigma{0.0};   // minimum - mean lead, lead scatter
    double onset_year{0.0}, onset_sigma{0.0};                // fitted
    bool amplitude_fitted{false}, width_fitted{false};
    double fit_amplitude{0.0};                   // NaN when no fit
    double prior_amplitude{0.0}, prior_sigma{0.0};
    double amplitude{0.0}, amplitude_sigma{0.0};   // combined (prior only without an error model)
    double width_scale{1.0}, width_sigma{0.0};
    double fit_sigma{0.0};                       // from the error model (NaN without one)
};

// One predicted turning point: its date and value over the ensemble.
struct SolarTurningPoint {
    bool minimum{false};
    int cycle{0};                                // the cycle that a minimum starts / a maximum belongs to
    double year_median{0.0}, year_p5{0.0}, year_p95{0.0};
    double value_median{0.0}, value_p5{0.0}, value_p95{0.0};
    double central_year{0.0}, central_value{0.0};
    bool beyond_horizon{false};                  // more than 5 % of the members reach the horizon first
};

struct SolarCycleForecast {
    double issue_year{0.0};
    int first_month{0};                          // of the forecast series (two years before the issue)
    std::size_t months{0};
    std::vector<std::vector<double>> members;    // [member][month]
    std::vector<double> central;                 // all parameters at their central values
    std::vector<SolarCycleCurve> central_curves; // every cycle of the central member (fits of the past, then forecast)
    std::vector<int> central_cycle_numbers;
    std::vector<std::vector<SolarCycleCurve>> member_curves;
    std::vector<SolarTurningPoint> turning_points;
    double year_at(std::size_t k) const { return month_mid_year(first_month + static_cast<int>(k)); }
};

// A forecast of one past cycle made at its minimum + phase, against what happened.
struct SolarBacktestEntry {
    int cycle{0};
    double phase_years{0.0}, issue_year{0.0};
    bool minimum_confirmed{false};               // the cycle's minimum was detectable at the issue date
    double observed_maximum{0.0}, observed_maximum_year{0.0};
    double fit_maximum{0.0};                     // NaN: no fit possible yet
    double prior_maximum{0.0};
    double prior_sigma{0.0};                     // spread of the AR(1) prior at the issue date
    double combined_maximum{0.0};                // with the error model (filled by apply_error_model)
    double predicted_maximum_year{0.0};          // central curve with the fitted (else prior) amplitude
    double observed_minimum_year{0.0}, predicted_minimum_year{0.0};
};

struct SolarBacktestSummary {
    double phase_years{0.0};
    std::size_t forecasts{0}, fitted{0};
    double fit_mae{0.0}, prior_mae{0.0}, combined_mae{0.0};     // amplitude, smoothed sunspot number (fit: over fitted entries)
    double fit_rms_fraction{0.0};
    double timing_mae_years{0.0}, minimum_mae_years{0.0};
};

class SolarCycleForecaster {
public:
    // `record` is the monthly sunspot number; its first detected minimum starts cycle `first_cycle_number`.
    // Throws std::invalid_argument for negative or non-finite values or a record shorter than four years.
    SolarCycleForecaster(MonthlySeries record, int first_cycle_number, SolarForecastSettings settings = {});

    // Throws std::invalid_argument when fewer than `minimum_history_cycles` complete cycles precede the issue date.
    SolarCycleAnalysis analyse(double issue_year, const SolarErrorModel* errors = nullptr) const;
    SolarCycleForecast forecast(const SolarCycleAnalysis& analysis, double horizon_year, std::size_t members, std::uint64_t seed) const;
    // Forecasts of every cycle that has `minimum_history_cycles` complete cycles before it, issued at its observed
    // minimum + each phase.
    std::vector<SolarBacktestEntry> backtest(const std::vector<double>& phases_years) const;
    static SolarErrorModel error_model(const std::vector<SolarBacktestEntry>& entries, const std::vector<double>& phases_years);
    static void apply_error_model(std::vector<SolarBacktestEntry>& entries, const SolarErrorModel& model);
    static std::vector<SolarBacktestSummary> summarize(const std::vector<SolarBacktestEntry>& entries, const std::vector<double>& phases_years);

    const MonthlySeries& record() const { return record_; }
    const MonthlySeries& smoothed() const { return smoothed_; }
    const std::vector<ObservedSolarCycle>& cycles() const { return cycles_; }   // over the whole record
    const SolarForecastSettings& settings() const { return settings_; }

    // Central forecast of the maximum of a cycle: the local maximum of `curves` within [onset + 1.5, onset + 8] years.
    static std::pair<double, double> curve_maximum(const std::vector<SolarCycleCurve>& curves, const SolarCycleCurve& cycle);

private:
    MonthlySeries record_, smoothed_;
    int first_cycle_number_;
    SolarForecastSettings settings_;
    std::vector<SolarExtremum> extrema_;
    std::vector<ObservedSolarCycle> cycles_;
    mutable std::map<std::pair<std::size_t, std::size_t>, SolarCycleFit> complete_fits_;   // by (minimum, next minimum) index

    const SolarCycleFit& complete_fit(const std::vector<ObservedSolarCycle>& cycles, std::size_t k, const std::vector<SolarCycleFit>& earlier) const;
};

// Percentile (0-100) of unsorted values by linear interpolation between order statistics.
double solar_percentile(std::vector<double> values, double percent);

}  // namespace terraflux::weather
