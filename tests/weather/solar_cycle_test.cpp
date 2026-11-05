// The solar cycle, its forecast, and the irradiance, UV, flare, climate and disk models (weather.solar_cycle).
#include "terraflux/weather/solar_cycle.hpp"
#include "terraflux/weather/solar_forcing.hpp"
#include "terraflux/weather/solar_irradiance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

// Scans a curve at 0.001-year resolution for its maximum.
std::pair<double, double> scan_peak(const SolarCycleCurve& c) {
    double best = -1.0, year = 0.0;
    for (double y = c.onset_year; y < c.onset_year + 12.0; y += 0.001) {
        const double v = c.value(y);
        if (v > best) { best = v; year = y; }
    }
    return {year, best};
}

// Periodic cycles: constant period and alternating amplitudes (a clean, known truth).
std::vector<SolarCycleTableEntry> periodic_table(std::size_t n, double first_minimum, double period, double a, double b) {
    std::vector<SolarCycleTableEntry> t;
    for (std::size_t k = 0; k < n; ++k) t.push_back({static_cast<int>(k + 1), first_minimum + period * static_cast<double>(k), std::numeric_limits<double>::quiet_NaN(), k % 2 ? b : a});
    return t;
}

// ---- Shape -------------------------------------------------------------------------------------------------------

void cycle_shape() {
    require_near(hathaway_width_months(0.001), 27.12 + 25.15, "b_H(0.001) = 27.12 + 25.15", 1e-9);
    for (double amplitude : {40.0, 116.0, 180.0, 285.0}) {
        const auto c = make_solar_cycle_curve(2000.0, amplitude);
        const auto peak = scan_peak(c);
        require_near(peak.second, amplitude, "the peak of one cycle equals its amplitude", 1e-5 * amplitude);
        require_near(peak.first, c.peak_year(), "the peak lies u* b after the onset", 0.002);
        require_near(c.a * std::pow(c.b_months, 3.0) * kHathawayPeakValue, amplitude, "A = a b^3 g*", 1e-9 * amplitude);
        require_near(c.b_months, hathaway_width_months(kSunspotV1PerV2 * c.a), "b follows Hathaway's relation in the v1 scale", 1e-9);
        require(c.value(1999.9) == 0.0 && c.value(2000.0) == 0.0 && c.value(2000.01) > 0.0, "nothing before the onset");
    }
    // Waldmeier effect: strong cycles rise faster.
    require(make_solar_cycle_curve(0.0, 280.0).peak_year() < make_solar_cycle_curve(0.0, 180.0).peak_year() &&
            make_solar_cycle_curve(0.0, 180.0).peak_year() < make_solar_cycle_curve(0.0, 60.0).peak_year(), "larger cycles peak sooner");
    const auto wide = make_solar_cycle_curve(0.0, 150.0, 1.3), normal = make_solar_cycle_curve(0.0, 150.0);
    // b = beta b_H(a): at the same amplitude a wider curve needs a smaller a, whose b_H is slightly larger.
    require_near(wide.b_months, 1.3 * hathaway_width_months(kSunspotV1PerV2 * wide.a), "the width scale multiplies Hathaway's width", 1e-9);
    require(wide.peak_year() > 1.3 * normal.peak_year(), "the width scale stretches the rise");
    require_near(scan_peak(wide).second, 150.0, "a scaled width keeps the amplitude", 1e-3);
    require_rejected([] { make_solar_cycle_curve(0.0, 0.0); }, "zero amplitude");
    require_rejected([] { make_solar_cycle_curve(0.0, 100.0, 0.1); }, "width scale out of range");
    require_rejected([] { make_solar_cycle_curve(std::nan(""), 100.0); }, "non-finite onset");
    require(month_index_of(2019.9) == 2019 * 12 + 10 && std::abs(month_mid_year(2019 * 12 + 10) - (2019.0 + 10.5 / 12.0)) < 1e-12, "month index and mid-month year");
}

// ---- Smoothing and extrema -----------------------------------------------------------------------------------

void smoothing() {
    std::vector<double> linear(40), spike(40, 0.0);
    for (std::size_t k = 0; k < linear.size(); ++k) linear[k] = 3.0 + 0.5 * static_cast<double>(k);
    spike[20] = 24.0;
    const auto s = smooth_13_month(linear), p = smooth_13_month(spike);
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (k < 6 || k + 6 >= s.size()) require(std::isnan(s[k]), "undefined in the first and last six months");
        else require_near(s[k], linear[k], "a linear series is unchanged", 1e-12);
    }
    require_near(p[14], 1.0, "end weights are one half of 1/12", 1e-12);
    require_near(p[26], 1.0, "end weights are one half of 1/12 (after)", 1e-12);
    require_near(p[20], 2.0, "inner weights are 1/12", 1e-12);
    require_near(p[15], 2.0, "inner weights are 1/12 (five months away)", 1e-12);
    require(p[13] == 0.0 && p[27] == 0.0, "nothing beyond six months");
    double total = 0.0;
    for (std::size_t k = 6; k + 6 < p.size(); ++k) total += p[k];
    require_near(total, 24.0, "the smoothing conserves the total", 1e-12);
}

void extrema() {
    const auto table = periodic_table(8, 1900.0, 11.0, 150.0, 220.0);
    const auto curves = calibrate_cycle_table(table);
    // The record starts in the decline of the first cycle, like a real one: its first minimum starts cycle 2.
    const auto record = synthesize_sunspot_record(curves, month_index_of(1904.0), 12 * 82, {0.0, 0.0, 1});
    MonthlySeries smoothed{record.first_month, smooth_13_month(record.values)};
    const auto ex = solar_extrema(smoothed.values);
    require(ex.size() >= 13, "minima and maxima of seven cycles");
    for (std::size_t k = 1; k < ex.size(); ++k) require(ex[k].minimum != ex[k - 1].minimum && ex[k].index > ex[k - 1].index, "extrema alternate in time");
    const auto cycles = observed_solar_cycles(smoothed, ex, 2);
    require(cycles.size() == 7 && cycles.front().number == 2, "cycles from minimum to minimum");
    for (std::size_t k = 0; k + 1 < cycles.size(); ++k) {
        // The 13-month smoothing shifts the minimum of an asymmetric dip (slow decline, fast rise) by a month or two.
        require_near(cycles[k].minimum_year, table[k + 1].minimum_year, "minima at the table's dates", 0.2);
        require_near(cycles[k].maximum_value, table[k + 1].maximum, "maxima at the table's values", 0.02 * table[k + 1].maximum);
        require(cycles[k].complete && std::abs(cycles[k].length_years - 11.0) < 0.2, "cycle lengths of eleven years");
        require_near(cycles[k].rise_years + cycles[k].decline_years, cycles[k].length_years, "rise + decline = length", 1e-9);
    }
    require(!cycles.back().complete && std::isnan(cycles.back().length_years), "the last cycle is still running");
    // At the end of a record a minimum needs `confirm` months of smoothed data after it.
    const std::size_t at = cycles[3].minimum_index;
    std::vector<double> cut(smoothed.values.begin(), smoothed.values.begin() + static_cast<std::ptrdiff_t>(at + 5));
    require(solar_extrema(cut, 30, 12).back().minimum == false, "an unconfirmed minimum is not reported");
    cut.assign(smoothed.values.begin(), smoothed.values.begin() + static_cast<std::ptrdiff_t>(at + 14));
    require(solar_extrema(cut, 30, 12).back().minimum && solar_extrema(cut, 30, 12).back().index == at, "a minimum followed by twelve rising months is confirmed");
}

void table_calibration() {
    std::vector<SolarCycleTableEntry> table = {{1, 1900.0, 1904.5, 120.0}, {2, 1911.2, 1914.6, 240.0}, {3, 1921.5, 1926.8, 90.0}, {4, 1932.9, 1937.1, 180.0}};
    const auto curves = calibrate_cycle_table(table);
    for (std::size_t k = 1; k < table.size(); ++k) {
        double best = 1e9, year = 0.0;
        for (double y = table[k].minimum_year - 2.0; y < table[k].minimum_year + 2.0; y += 0.002) {
            const double v = cycles_value(curves, y);
            if (v < best) { best = v; year = y; }
        }
        require_near(year, table[k].minimum_year, "calibrated minima fall on the table's dates", 0.02);
    }
    for (std::size_t k = 0; k < table.size(); ++k) {
        double best = 0.0;
        for (double y = table[k].minimum_year + 1.0; y < table[k].minimum_year + 8.0; y += 0.002) best = std::max(best, cycles_value(curves, y));
        require_near(best, table[k].maximum, "calibrated maxima have the table's values", 0.005 * table[k].maximum);
    }
    const auto shaped = calibrate_cycle_table(table, 0.3, true);
    for (std::size_t k = 0; k < table.size(); ++k) {
        double best = 0.0, year = 0.0;
        for (double y = table[k].minimum_year + 1.0; y < table[k].minimum_year + 8.0; y += 0.002) {
            const double v = cycles_value(shaped, y);
            if (v > best) { best = v; year = y; }
        }
        require_near(year, table[k].maximum_year, "with calibrate_widths the maxima fall on the table's dates", 0.05);
    }
    require_rejected([] { calibrate_cycle_table({{1, 1900.0, 1901.0, 100.0}}); }, "a maximum too soon after the minimum");
    require_rejected([] { calibrate_cycle_table({{1, 1900.0, std::nan(""), 100.0}, {2, 1903.0, std::nan(""), 100.0}}); }, "minima too close");
    require_rejected([] { calibrate_cycle_table({{1, 1900.0, std::nan(""), 900.0}}); }, "an amplitude out of range");

    // The synthetic record: exact without noise, reproducible and non-negative with it.
    const auto clean = synthesize_sunspot_record(curves, month_index_of(1899.0), 480, {0.0, 0.0, 1});
    for (std::size_t k = 0; k < clean.values.size(); ++k) require_near(clean.values[k], cycles_value(curves, clean.year_at(k)), "noise-free record = curves (rounded to 0.1)", 0.05 + 1e-9);
    const auto a = synthesize_sunspot_record(curves, month_index_of(1899.0), 480, {2.5, 0.6, 7}), b = synthesize_sunspot_record(curves, month_index_of(1899.0), 480, {2.5, 0.6, 7});
    require(a.values == b.values, "the same seed gives the same record");
    require(std::all_of(a.values.begin(), a.values.end(), [](double v) { return v >= 0.0; }), "sunspot numbers are never negative");
    double diff = 0.0;
    for (std::size_t k = 0; k < a.values.size(); ++k) diff += std::abs(a.values[k] - clean.values[k]);
    require(diff / static_cast<double>(a.values.size()) > 5.0, "the noise is there");
}

// ---- Fits --------------------------------------------------------------------------------------------------------

void fits() {
    const auto previous = make_solar_cycle_curve(1989.0, 190.0);
    const auto truth = make_solar_cycle_curve(1999.7, 165.0, 1.12);
    MonthlySeries record;
    record.first_month = month_index_of(1998.0);
    for (int k = 0; k < 12 * 14; ++k) record.values.push_back(previous.value(month_mid_year(record.first_month + k)) + truth.value(month_mid_year(record.first_month + k)));
    std::vector<double> base;
    for (std::size_t k = 0; k < record.values.size(); ++k) base.push_back(previous.value(record.year_at(k)));
    const auto amplitude = fit_solar_cycle_amplitude(record, 0, record.values.size(), base, truth.onset_year, truth.width_scale);
    require_near(amplitude.curve.amplitude, 165.0, "the amplitude of noise-free data", 1e-4);
    require(amplitude.rms < 1e-4, "noise-free data fit exactly");
    const auto full = fit_solar_cycle(record, 0, record.values.size(), base, 2000.2, 2.0);
    require_near(full.curve.onset_year, truth.onset_year, "the onset of noise-free data", 0.01);
    require_near(full.curve.amplitude, 165.0, "the amplitude with a free onset and width", 0.5);
    require_near(full.curve.width_scale, 1.12, "the width scale", 0.01);
    const auto shape = fit_solar_cycle_shape(record, 0, record.values.size(), base, truth.onset_year);
    require_near(shape.curve.width_scale, 1.12, "the width scale for a fixed onset", 0.003);
    // Only the first 30 months: a loose onset prior is pulled to the truth, a tight one holds its date.
    const std::size_t early = static_cast<std::size_t>(month_index_of(truth.onset_year + 2.5) - record.first_month);
    std::vector<double> early_base(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(early));
    const auto loose = fit_solar_cycle_onset_prior(record, 0, early, early_base, 2000.2, 0.6, 1.0, false, 1.12);
    require_near(loose.curve.onset_year, truth.onset_year, "a loose prior gives way to the data", 0.02);
    require_near(loose.curve.amplitude, 165.0, "and the amplitude follows", 3.0);
    require(loose.onset_sigma > 0.0 && loose.onset_sigma < 0.6, "the data narrow the onset");
    const auto tight = fit_solar_cycle_onset_prior(record, 0, early, early_base, 2000.2, 0.01, 1e6, false, 1.12);
    require(std::abs(tight.curve.onset_year - 2000.2) < 0.03, "a tight prior holds the onset");
    require(tight.curve.amplitude > 200.0, "a late onset needs a larger amplitude for the same early months");
    require_rejected([&] { fit_solar_cycle_amplitude(record, 5, 5, {}, 2000.0); }, "an empty fit range");
    require_rejected([&] { fit_solar_cycle_amplitude(record, 0, 10, std::vector<double>(3), 2000.0); }, "a baseline that does not match");
}

// ---- Forecaster --------------------------------------------------------------------------------------------------

void forecaster() {
    // Eleven-year cycles alternating between 140 and 210: noise-free, with exactly known future.
    const auto table = periodic_table(14, 1850.0, 11.0, 140.0, 210.0);
    const auto curves = calibrate_cycle_table(table);
    const double issue = 1850.0 + 11.0 * 11 + 3.0;   // three years into cycle 12
    const int first = month_index_of(1854.0);   // in the decline of cycle 1
    const auto record = synthesize_sunspot_record(curves, first, static_cast<std::size_t>(month_index_of(issue) - first), {0.0, 0.0, 1});
    const SolarCycleForecaster f(record, 2);
    require(f.cycles().size() == 11 && f.cycles().front().number == 2, "eleven cycles detected, numbered from the first minimum");
    const std::vector<double> phases = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
    auto entries = f.backtest(phases);
    require(!entries.empty(), "the backtest covers the past cycles");
    for (const auto& e : entries) {
        require(e.cycle >= 6, "a backtest needs three complete cycles before the issue date");
        if (e.phase_years >= 2.0) {
            require(e.minimum_confirmed && std::isfinite(e.fit_maximum), "two years after the minimum the cycle is fitted");
            require_near(e.fit_maximum, e.observed_maximum, "noise-free cycles are fitted accurately", 0.03 * e.observed_maximum);
        }
        if (e.phase_years == 0.0) require(!e.minimum_confirmed && std::isnan(e.fit_maximum) && std::isfinite(e.predicted_minimum_year), "at the minimum it is not yet detectable");
    }
    const auto model = SolarCycleForecaster::error_model(entries, phases);
    require(std::isnan(model.amplitude_error_fraction[0]) && std::isfinite(model.amplitude_error_fraction[2]), "no fit errors before the minimum is confirmed");
    require_near(model.amplitude_error(0.5), model.amplitude_error_fraction[2], "held below the first fitted phase", 1e-12);
    require_near(model.amplitude_error(9.0), model.amplitude_error_fraction.back(), "held beyond the last phase", 1e-12);
    SolarCycleForecaster::apply_error_model(entries, model);
    const auto summary = SolarCycleForecaster::summarize(entries, phases);
    require(summary.size() == phases.size() && summary[3].fitted == summary[3].forecasts, "a summary row per phase");
    require(summary[3].fit_mae < 5.0, "noise-free backtest errors are small");

    const auto a = f.analyse(issue, &model);
    require(a.current_cycle == 12 && std::abs(a.phase_years - 3.0) < 0.2, "the cycle in progress and its phase");
    require_near(a.fit_amplitude, 210.0, "the cycle in progress is fitted", 6.0);
    require(a.amplitude_sigma > 0.0 && a.amplitude_sigma < a.prior_sigma, "the fit narrows the prior");
    require_near(a.statistics.mean_period_years, 11.0, "the mean period", 0.1);
    require(a.statistics.amplitude_autocorrelation == 0.0, "alternating amplitudes have no positive memory (clamped)");
    const auto fc = f.forecast(a, issue + 20.0, 300, 9);
    require(fc.members.size() == 300 && fc.members.front().size() == fc.months && fc.central.size() == fc.months, "ensemble dimensions");
    for (std::size_t k = 0; k < fc.months; k += 7) {
        std::vector<double> v;
        for (const auto& m : fc.members) v.push_back(m[k]);
        const double p5 = solar_percentile(v, 5.0), p50 = solar_percentile(v, 50.0), p95 = solar_percentile(v, 95.0);
        require(p5 <= p50 && p50 <= p95, "percentiles are ordered");
    }
    // The next minimum and maximum against the truth (cycle 13 of the table).
    const SolarTurningPoint* minimum = nullptr;
    const SolarTurningPoint* maximum = nullptr;
    for (const auto& t : fc.turning_points) {
        if (t.minimum && t.cycle == 13) minimum = &t;
        if (!t.minimum && t.cycle == 13) maximum = &t;
    }
    require(minimum && maximum, "the next minimum and maximum are predicted");
    require_near(minimum->central_year, table[12].minimum_year, "the next minimum", 0.5);
    require(minimum->year_p5 <= minimum->year_median && minimum->year_median <= minimum->year_p95, "minimum dates ordered");
    require(maximum->value_p5 < maximum->value_median && maximum->value_median < maximum->value_p95, "the next cycle's amplitude is uncertain");
    require(maximum->year_p5 < table[12].minimum_year + 6.0 && maximum->year_p95 > table[12].minimum_year + 3.0, "the next maximum range covers the truth");
    require(fc.turning_points.front().cycle == 12 && !fc.turning_points.front().minimum, "the maximum of the cycle in progress comes first");
    require_rejected([&] { f.analyse(1870.0); }, "too few complete cycles before the issue date");
    require_rejected([&] { f.forecast(a, issue + 0.5, 100, 1); }, "a horizon too close to the issue date");
    require_near(solar_percentile({3.0, 1.0, 2.0, 4.0}, 50.0), 2.5, "median by interpolation", 1e-12);
    require_near(solar_percentile({3.0, 1.0, 2.0, 4.0}, 0.0), 1.0, "minimum", 1e-12);
}

// ---- Irradiance and atmosphere -----------------------------------------------------------------------------------

void irradiance() {
    const SolarProxyConfig c;
    const auto bands = default_solar_uv_bands();
    validate_solar_proxies(c, bands);
    const auto quiet = solar_irradiance(0.0, c, bands), ref = solar_irradiance(c.reference_sunspot_number, c, bands);
    require_near(quiet.tsi_w_m2, c.tsi_quiet_w_m2, "the quiet Sun", 1e-12);
    require_near(ref.facular_index, 1.0, "the facular index is 1 at the reference maximum", 1e-12);
    require_near(ref.tsi_w_m2 - quiet.tsi_w_m2, c.facular_brightening_w_m2 - c.spot_darkening_w_m2_per_sunspot * c.reference_sunspot_number, "TSI = faculae - spots", 1e-9);
    double previous = quiet.tsi_w_m2;
    for (double sn = 10.0; sn <= 300.0; sn += 10.0) {
        const double tsi = solar_irradiance(sn, c, bands).tsi_w_m2;
        require(tsi > previous, "the Sun is brighter with more spots: the faculae win");
        previous = tsi;
    }
    require(ref.tsi_w_m2 - quiet.tsi_w_m2 > 0.7 && ref.tsi_w_m2 - quiet.tsi_w_m2 < 1.6, "a TSI cycle of about 1 W/m2");
    // The shorter the wavelength, the larger the cycle.
    for (std::size_t b = 1; b < bands.size(); ++b)
        if (bands[b].from_nm >= bands[b - 1].from_nm) require(ref.band_change[b] < ref.band_change[b - 1], "UV variability falls with wavelength");
    require(ref.band_change.back() > (ref.tsi_w_m2 - quiet.tsi_w_m2) / quiet.tsi_w_m2, "even UV-A varies more than the TSI");
    auto bad = c;
    bad.f107_per_sunspot = 0.0;
    require_rejected([&] { validate_solar_proxies(bad, bands); }, "a proxy that does not grow with the sunspot number");
    auto bad_bands = bands;
    bad_bands[1].key = bad_bands[0].key;
    require_rejected([&] { validate_solar_proxies(c, bad_bands); }, "duplicate band keys");

    // UV index: Madronich's formula, the ozone amplification and the solar-cycle change.
    require_near(clear_sky_uv_index(1.0, 300.0), 12.5, "overhead sun and 300 DU", 1e-12);
    require_near(clear_sky_uv_index(1.0, 600.0) / clear_sky_uv_index(1.0, 300.0), std::pow(2.0, -1.23), "twice the ozone", 1e-12);
    require(clear_sky_uv_index(0.0, 300.0) == 0.0 && clear_sky_uv_index(-0.3, 300.0) == 0.0, "no UV at night");
    require_near(earth_sun_distance_factor(3.0), 1.0334, "perihelion", 1e-12);
    const SolarAtmosphereConfig at;
    const SolarUvSite site{"test", 45.0, 330.0, 30.0, 90.0};
    for (double day : {15.0, 172.0, 300.0}) {
        const auto u = solar_site_uv_index(site, day, 0.8, at);
        const double expected = (1.0 + at.erythemal_variability * 0.8) * std::pow(1.0 + at.ozone_change_at_reference * 0.8, -at.radiation_amplification_factor) - 1.0;
        require_near(u.change, expected, "the UV-index change is the same every day", 1e-12);
        require(u.quiet_uv_index > 0.0 && u.cos_zenith > 0.0, "daylight at noon");
    }
    require(solar_site_uv_index(site, 172.0, 0.0, at).uv_index > solar_site_uv_index(site, 355.0, 0.0, at).uv_index * 4.0, "summer noon UV far above winter");
    require(solar_site_uv_index({"pole", 89.0, 300.0, 0.0, 0.0}, 355.0, 1.0, at).uv_index == 0.0, "polar night");
    require(solar_site_uv_index(site, 172.0, 1.0, at).change < 0.0, "with the default ozone response the surface UV falls slightly at maximum");
    require_rejected([] { validate_solar_uv_site({"x", 95.0, 300.0, 0.0, 0.0}); }, "latitude out of range");
}

void flares() {
    const SolarFlareConfig c;
    validate_solar_flares(c);
    require_near(flare_rate_per_year(c.reference_sunspot_number, kFlareX1, c), c.x_rate_per_year, "N(>=X1) at the reference sunspot number", 1e-9);
    require_near(flare_rate_per_year(2.0 * c.reference_sunspot_number, kFlareX1, c) / c.x_rate_per_year, std::pow(2.0, c.rate_exponent), "rates scale as SN^gamma", 1e-9);
    const double qmax = std::pow(c.max_peak_flux_w_m2 / kFlareX1, 1.0 - c.size_index);
    require_near(flare_rate_per_year(100.0, kFlareM1, c) / flare_rate_per_year(100.0, kFlareX1, c), (std::pow(0.1, 1.0 - c.size_index) - qmax) / (1.0 - qmax), "the power law between M1 and X1", 1e-9);
    require(flare_rate_per_year(0.0, kFlareC1, c) == 0.0 && flare_rate_per_year(100.0, c.max_peak_flux_w_m2, c) == 0.0, "no flares without spots or above the cut-off");
    require_near(flare_probability(2.0, 0.5), 1.0 - std::exp(-1.0), "Poisson probability of at least one", 1e-12);
    // Sampled sizes follow the distribution: the share of X flares among flares >= M1.
    NormalSequence random(17);
    std::size_t x = 0;
    const std::size_t n = 200000;
    for (std::size_t k = 0; k < n; ++k) {
        const double s = sample_flare_peak_flux(random.uniform(), kFlareM1, c);
        require(s >= kFlareM1 && s <= c.max_peak_flux_w_m2, "samples within the power law");
        if (s >= kFlareX1) ++x;
    }
    const double share = flare_rate_per_year(100.0, kFlareX1, c) / flare_rate_per_year(100.0, kFlareM1, c);
    require_near(static_cast<double>(x) / n, share, "sampled share of X flares", 4.0 * std::sqrt(share * (1.0 - share) / n));
    // Mean flare irradiance against a numerical integral over the size distribution.
    double numeric = 0.0;
    const double lo = std::log(kFlareC1), hi = std::log(c.max_peak_flux_w_m2), step = (hi - lo) / 20000.0;
    for (double u = lo + step / 2.0; u < hi; u += step) {
        const double s = std::exp(u), ds = s * step;
        const double density = (flare_rate_per_year(100.0, s - ds / 2.0, c) - flare_rate_per_year(100.0, s + ds / 2.0, c));
        numeric += density * flare_energy_j(s, c);
    }
    numeric /= 365.25 * 86400.0 * 4.0 * kPi * std::pow(1.495978707e11, 2.0);
    require_near(mean_flare_irradiance_w_m2(100.0, kFlareC1, c), numeric, "mean flare irradiance", 1e-3 * numeric);
    require(mean_flare_irradiance_w_m2(180.0, kFlareC1, c) < 1e-2, "flares carry far less energy than the TSI cycle");
    require(flare_class_label(2.34e-4) == "X2.3" && flare_class_label(9.96e-6) == "M1.0" && flare_class_label(4.5e-3) == "X45.0" && flare_class_label(3.2e-7) == "B3.2", "GOES classes");
    require(radio_blackout_level(9e-6) == 0 && radio_blackout_level(1e-5) == 1 && radio_blackout_level(6e-5) == 2 && radio_blackout_level(1e-4) == 3 &&
            radio_blackout_level(1.5e-3) == 4 && radio_blackout_level(2e-3) == 5, "NOAA R scale");
    auto bad = c;
    bad.size_index = 1.0;
    require_rejected([&] { validate_solar_flares(bad); }, "a size index without a finite number of small flares");
    // Poisson draws have the right mean.
    double sum = 0.0;
    for (int k = 0; k < 20000; ++k) sum += sample_poisson(3.7, random);
    require_near(sum / 20000.0, 3.7, "Poisson mean", 0.06);
}

void climate() {
    const SolarClimateConfig c;
    validate_solar_climate(c);
    require_near(solar_radiative_forcing_w_m2(1.0, c), (1.0 - c.albedo) / 4.0, "forcing: TSI spread over the sphere, minus the albedo", 1e-12);
    SolarClimateState s;
    for (int k = 0; k < 12 * 3000; ++k) s = step_solar_climate(s, 1.0, 1.0 / 12.0, c);
    require_near(s.surface_k, 1.0 / c.feedback_w_m2_k, "equilibrium T = F / lambda", 1e-4);
    require_near(s.deep_k, s.surface_k, "the deep ocean catches up", 1e-4);
    // One implicit step satisfies both equations.
    const SolarClimateState s0{0.2, 0.05}, s1 = step_solar_climate(s0, 0.3, 0.5, c);
    require_near(c.upper_heat_capacity * (s1.surface_k - s0.surface_k) / 0.5, 0.3 - c.feedback_w_m2_k * s1.surface_k - c.exchange_w_m2_k * (s1.surface_k - s1.deep_k), "upper layer", 1e-12);
    require_near(c.deep_heat_capacity * (s1.deep_k - s0.deep_k) / 0.5, c.exchange_w_m2_k * (s1.surface_k - s1.deep_k), "deep layer", 1e-12);
    // An eleven-year cycle is damped by the ocean's heat capacity: far below the equilibrium response.
    s = {};
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k < 12 * 300; ++k) {
        s = step_solar_climate(s, 0.1 * std::sin(2.0 * kPi * k / 12.0 / 11.0), 1.0 / 12.0, c);
        if (k > 12 * 200) { lo = std::min(lo, s.surface_k); hi = std::max(hi, s.surface_k); }
    }
    require(hi - lo > 0.01 && hi - lo < 0.2 / c.feedback_w_m2_k * 0.5, "a damped cycle response");
}

void disk() {
    SolarDiskConfig c;
    c.cells = 40;
    validate_solar_disk(c);
    const auto quiet = render_solar_disk({}, c);
    require(quiet.net_change == 0.0 && quiet.brightness_k.size() == c.cells * c.cells, "no regions: the quiet disk");
    for (std::size_t i = 0; i < quiet.brightness_k.size(); ++i)
        require(std::isnan(quiet.brightness_k[i]) ? std::isnan(quiet.quiet_brightness_k[i]) : quiet.brightness_k[i] == quiet.quiet_brightness_k[i], "no regions: every cell quiet");
    const std::size_t n = c.cells, centre = (n / 2) * n + n / 2;
    require(std::isnan(quiet.brightness_k[0]), "the corners are off the disk");
    require(quiet.brightness_k[centre] > quiet.brightness_k[(n / 2) * n + 1] + 300.0, "limb darkening: the centre is hotter than the limb");
    require_near(quiet.brightness_k[centre], c.effective_temperature_k * std::pow(1.0 / (1.0 - c.limb_darkening / 3.0), 0.25), "centre brightness temperature", 5.0);
    // One spot at the disk centre removes its projected area times its contrast, weighted by the central intensity.
    SolarActiveRegion spot{0.0, 0.0, 1000.0, 1};
    auto only_spot = c;
    only_spot.facular_area_ratio = 0.0;
    only_spot.subsamples = 16;
    const auto image = render_solar_disk({spot}, only_spot);
    const double contrast = c.umbra_fraction * c.umbra_contrast + (1.0 - c.umbra_fraction) * c.penumbra_contrast;
    const double expected = -2.0 * 1000e-6 * (1.0 - contrast) / (1.0 - c.limb_darkening / 3.0);
    require_near(image.spot_change, expected, "disk-integrated spot deficit", 0.12 * std::abs(expected));
    require(image.facular_change == 0.0, "no faculae without facular area");
    const auto with_faculae = render_solar_disk({{0.0, 70.0, 1000.0, 1}}, c);
    require(with_faculae.facular_change > 0.0 && with_faculae.spot_change < 0.0, "faculae brighten, spots darken");
    // Sporer's law: young cycles spot at high latitudes, old ones near the equator.
    require(spot_zone_latitude_deg(0.0, c) == c.onset_latitude_deg && spot_zone_latitude_deg(8.0, c) < 10.0, "the spot zone drifts toward the equator");
    const std::vector<SolarCycleCurve> curves = {make_solar_cycle_curve(2000.0, 150.0)};
    const std::vector<int> numbers = {23};
    NormalSequence random(4);
    double count = 0.0, young = 0.0, old = 0.0;
    for (int k = 0; k < 400; ++k) {
        count += static_cast<double>(sample_active_regions(2004.0, 120.0, curves, numbers, random, c).size());
        for (const auto& r : sample_active_regions(2001.5, 60.0, curves, numbers, random, c)) young += std::abs(r.latitude_deg) / 400.0;
        for (const auto& r : sample_active_regions(2008.5, 60.0, curves, numbers, random, c)) old += std::abs(r.latitude_deg) / 400.0;
    }
    require_near(count / 400.0, c.groups_per_sunspot * 120.0, "groups per sunspot number", 0.5);
    require(young > 2.0 * old, "young groups lie at higher latitudes");
    // The butterfly density holds the spot area of the cycle, in two bands at +-lambda.
    std::vector<double> lat;
    for (double l = -60.0; l <= 60.0; l += 0.5) lat.push_back(l);
    const auto d = butterfly_density(2003.0, curves, lat, c);
    const double total = std::accumulate(d.begin(), d.end(), 0.0) * 0.5;
    require_near(total, c.area_per_sunspot_uhem * curves[0].value(2003.0), "butterfly density integrates to the spot area", 0.01 * total);
    const auto peak = std::max_element(d.begin(), d.end()) - d.begin();
    require_near(std::abs(lat[static_cast<std::size_t>(peak)]), spot_zone_latitude_deg(3.0, c), "the bands lie at the zone latitude", 1.0);
}

void forcing_export() {
    // decimal years from epoch seconds
    require_near(decimal_year_from_epoch(0.0), 1970.0, "the epoch", 1e-12);
    require_near(decimal_year_from_epoch(946684800.0 + 183.0 * 86400.0), 2000.0 + 183.0 / 366.0, "2 July 2000 (a leap year)", 1e-12);
    require_near(decimal_year_from_epoch(-86400.0 * 365.0), 1969.0, "before the epoch", 1e-12);
    const std::string path = "solar_forcing_test.json";
    {
        std::ofstream f(path);
        f << R"({"solar": {"schema_version": "terraflux.solar.cycle/v1", "forcing_export": {"schema_version": "terraflux.solar.forcing/v1",
              "years": [2000, 2001, 2002], "tsi_w_m2": [1361.5, 1361.9, 1361.3], "surface_k": [0.01, 0.03, -0.02], "reference_tsi_w_m2": 1361.0}}})";
    }
    const auto s = load_solar_forcing(path);
    require(s.years.size() == 3 && s.years.front() == 2000.5, "annual means stand for the middle of their year");
    require_near(s.tsi_at(2001.0), 1361.7, "linear between annual means", 1e-9);
    require_near(s.tsi_anomaly_at(2001.5), 0.9, "the anomaly against the run's mean Sun", 1e-9);
    require_near(s.tsi_at(1990.0), 1361.0, "the mean Sun before the run", 1e-12);
    require_near(s.warming_at(2010.0), 0.0, "no response after the run", 1e-12);
    require_near(s.warming_at(2002.0), 0.005, "response interpolated", 1e-9);
    {
        std::ofstream f(path);
        f << R"({"schema_version": "terraflux.solar.forcing/v1", "years": [2000, 2002], "tsi_w_m2": [1361, 1361], "surface_k": [0, 0], "reference_tsi_w_m2": 1361})";
    }
    require_rejected([&] { load_solar_forcing(path); }, "years must be consecutive");
    {
        std::ofstream f(path);
        f << R"({"schema_version": "terraflux.ocean.circulation/v1"})";
    }
    require_rejected([&] { load_solar_forcing(path); }, "another schema");
    std::remove(path.c_str());
    require_rejected([&] { load_solar_forcing(path); }, "a missing file");
}

}  // namespace

int main() {
    try {
        cycle_shape();
        smoothing();
        extrema();
        table_calibration();
        fits();
        forecaster();
        irradiance();
        flares();
        climate();
        disk();
        forcing_export();
        std::cout << "solar cycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
