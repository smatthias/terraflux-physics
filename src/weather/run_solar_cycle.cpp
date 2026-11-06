// terraflux-run-solar-cycle: the sunspot cycle, its minima and maxima, a forecast, and what the cycle does to the
// sunlight, the UV, the flares and the Earth's atmosphere and climate.
//
// Reads a terraflux.solar.cycle-run/v1 request:
//   record       a monthly sunspot record: either given (e.g. converted from SILSO by tools/silso_to_request.py) or
//                synthesised from a table of cycles (minimum and maximum dates, maximum values) with monthly noise
//   forecast     issue date and horizon, ensemble size, backtest phases
//   irradiance   proxy coefficients (F10.7, Mg II, TSI) and UV bands
//   atmosphere   ozone and stratospheric response, erythemal variability
//   uv_sites     places for the noon UV index
//   flares       rate and size distribution
//   climate      two-layer energy balance
//   disk         the rendered solar disk frames
//   butterfly    the time-latitude diagram of the spot zones
// and writes a viewer timeline: solar-disk frames and a `solar` block (terraflux.solar.cycle/v1) with the record,
// the detected cycles, the backtest, the forecast with its ensemble percentiles and turning points, the irradiance,
// UV, flare and climate series, the butterfly diagram, and an annual forcing export that the polar-melt and ocean
// runs can read.
#include "terraflux/weather/solar_cycle.hpp"
#include "terraflux/weather/solar_irradiance.hpp"
#include "terraflux/weather/solar_long_term.hpp"
#include "utc_time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) throw std::invalid_argument(std::string("run request is missing field: ") + key);
    return object.at(key);
}

double required_number(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number()) throw std::invalid_argument(std::string("run request field must be numeric: ") + key);
    const double result = value.get<double>();
    if (!std::isfinite(result)) throw std::invalid_argument(std::string("run request field must be finite: ") + key);
    return result;
}

double optional_number(const Json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    return required_number(object, key);
}

std::string optional_string(const Json& object, const char* key, const std::string& fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) throw std::invalid_argument(std::string("run request field must be a non-empty string: ") + key);
    return value.get<std::string>();
}

bool optional_bool(const Json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    if (!object.at(key).is_boolean()) throw std::invalid_argument(std::string("run request field must be boolean: ") + key);
    return object.at(key).get<bool>();
}

long long optional_integer(const Json& object, const char* key, long long fallback, long long lo, long long hi) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_number_integer() || value.get<long long>() < lo || value.get<long long>() > hi)
        throw std::invalid_argument(std::string("run request field must be an integer within ") + std::to_string(lo) + "-" + std::to_string(hi) + ": " + key);
    return value.get<long long>();
}

// Rejects keys the section does not know: a misspelt parameter must not fall back silently to its default.
void only_keys(const Json& object, const char* section, std::initializer_list<const char*> keys) {
    if (!object.is_object()) throw std::invalid_argument(std::string("run request section must be an object: ") + section);
    const std::set<std::string> allowed(keys.begin(), keys.end());
    for (const auto& item : object.items())
        if (!allowed.count(item.key())) throw std::invalid_argument(std::string("unknown field in ") + section + ": " + item.key());
}

template <typename T>
void read_into(const Json& section, const char* key, T& target) { target = static_cast<T>(optional_number(section, key, static_cast<double>(target))); }

// Significant-digit rounding that lands on the nearest double of the decimal value.
double tidy(double v, int digits = 5) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    const int exponent = static_cast<int>(std::floor(std::log10(std::abs(v)))) - (digits - 1);
    if (exponent < 0) { const double inverse = std::pow(10.0, -exponent); return std::round(v * inverse) / inverse; }
    const double scale = std::pow(10.0, exponent);
    return std::round(v / scale) * scale;
}

// Fixed decimals (for sunspot numbers, dates and other values with a natural resolution).
double fixed(double v, int decimals) {
    if (!std::isfinite(v)) return v;
    const double scale = std::pow(10.0, decimals);
    return std::round(v * scale) / scale;
}

Json number(double v, int digits = 5) { return std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr); }
Json fixed_number(double v, int decimals) { return std::isfinite(v) ? Json(fixed(v, decimals)) : Json(nullptr); }

Json tidy_array(const std::vector<double>& values, int digits = 5) {
    Json out = Json::array();
    for (double v : values) out.push_back(number(v, digits));
    return out;
}

Json fixed_array(const std::vector<double>& values, int decimals) {
    Json out = Json::array();
    for (double v : values) out.push_back(fixed_number(v, decimals));
    return out;
}

std::string month_label(int month_index) {
    const int year = month_index / 12, month = month_index % 12 + 1;
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d", year, month);
    return buffer;
}

// Day of the year at the middle of a month.
double mid_month_day(int month_index) { return (static_cast<double>(month_index % 12) + 0.5) * 365.25 / 12.0; }

// ---- Long-term request (grand minima and maxima) -----------------------------------------------------------------

struct LongTermRequest {
    bool enabled{false};
    int start_year{1000}, horizon_year{2100};
    SolarEnvelopeConfig envelope;
    HistoricalCycleConfig historical;
    std::vector<SolarEpoch> epochs;
    std::size_t episode_window{11};
    int episode_min_years{20};
    double episode_low{25.0}, episode_high{83.0};
    double secular_timescale{15.0};
    int reference_start{1976}, reference_end{2019};
    double secular_low{0.0}, secular_central{0.7}, secular_high{3.5};
    CosmicRayConfig cosmic;
    double volcanic_rise{0.75};
    std::vector<VolcanicEruption> eruptions;
    std::vector<std::pair<double, double>> co2;
    std::string regional_name{"Northern and central Europe, winter (illustrative)"};
    double regional_amplification{2.0}, regional_top_down_k_per_uv_pct{0.1};
    int gm_first_cycle{26}, gm_cycles{6};
    double gm_amplitude{8.0};
    SolarDynamoConfig dynamo;
    std::size_t dynamo_statistics_cycles{100000}, dynamo_realizations{20000}, dynamo_cycles_ahead{10};
    std::uint64_t dynamo_seed{13};
    Json dynamo_targets = Json::object();
    std::string case_epoch;
    int case_from{1600}, case_to{1760};
};

SolarEpochKind epoch_kind(const std::string& s) {
    if (s == "grand_minimum") return SolarEpochKind::grand_minimum;
    if (s == "grand_maximum") return SolarEpochKind::grand_maximum;
    if (s == "minimum") return SolarEpochKind::minimum;
    if (s == "maximum") return SolarEpochKind::maximum;
    throw std::invalid_argument("long_term epoch kind must be grand_minimum, grand_maximum, minimum or maximum");
}

LongTermRequest parse_long_term(const Json& l) {
    LongTermRequest t;
    t.enabled = true;
    only_keys(l, "long_term", {"start_year", "horizon_year", "normal_amplitude", "transition_years", "historical_cycles", "epochs", "episodes", "secular_background",
                               "cosmic_rays", "volcanic", "co2_ppm", "regional_winter", "future", "dynamo", "case_study"});
    t.start_year = static_cast<int>(optional_integer(l, "start_year", t.start_year, -10000, 1800));
    t.horizon_year = static_cast<int>(optional_integer(l, "horizon_year", t.horizon_year, 1900, 2500));
    read_into(l, "normal_amplitude", t.envelope.normal_amplitude);
    read_into(l, "transition_years", t.envelope.transition_years);
    if (l.contains("historical_cycles")) {
        const Json& h = required(l, "historical_cycles");
        only_keys(h, "long_term.historical_cycles", {"period_years", "period_sd_years", "amplitude_log_sd", "lead_years", "seed"});
        read_into(h, "period_years", t.historical.period_years);
        read_into(h, "period_sd_years", t.historical.period_sd_years);
        read_into(h, "amplitude_log_sd", t.historical.amplitude_log_sd);
        read_into(h, "lead_years", t.historical.lead_years);
        t.historical.seed = static_cast<std::uint64_t>(optional_integer(h, "seed", 7, 1, 1LL << 40));
    }
    const Json& epochs = required(l, "epochs");
    if (!epochs.is_array()) throw std::invalid_argument("long_term.epochs must be an array");
    for (const Json& e : epochs) {
        only_keys(e, "long_term.epochs[]", {"name", "kind", "start_year", "end_year", "cycle_amplitude", "evidence"});
        SolarEpoch x;
        x.name = optional_string(e, "name", "");
        x.kind = epoch_kind(optional_string(e, "kind", "grand_minimum"));
        x.start_year = required_number(e, "start_year");
        x.end_year = required_number(e, "end_year");
        x.cycle_amplitude = optional_number(e, "cycle_amplitude", 0.0);
        x.evidence = e.contains("evidence") ? optional_string(e, "evidence", "") : "";
        t.epochs.push_back(x);
    }
    validate_solar_epochs(t.epochs, t.envelope);
    if (l.contains("episodes")) {
        const Json& e = required(l, "episodes");
        only_keys(e, "long_term.episodes", {"window_years", "min_years", "grand_minimum_mean", "grand_maximum_mean"});
        t.episode_window = static_cast<std::size_t>(optional_integer(e, "window_years", 11, 1, 101));
        t.episode_min_years = static_cast<int>(optional_integer(e, "min_years", 20, 1, 500));
        read_into(e, "grand_minimum_mean", t.episode_low);
        read_into(e, "grand_maximum_mean", t.episode_high);
        if (!(t.episode_low >= 0.0 && t.episode_low < t.episode_high && t.episode_high < 500.0)) throw std::invalid_argument("long_term.episodes needs 0 <= grand_minimum_mean < grand_maximum_mean < 500");
    }
    if (l.contains("secular_background")) {
        const Json& s = required(l, "secular_background");
        only_keys(s, "long_term.secular_background", {"timescale_years", "reference_start_year", "reference_end_year", "low", "central", "high"});
        read_into(s, "timescale_years", t.secular_timescale);
        t.reference_start = static_cast<int>(optional_integer(s, "reference_start_year", t.reference_start, 1600, 2500));
        t.reference_end = static_cast<int>(optional_integer(s, "reference_end_year", t.reference_end, 1600, 2500));
        read_into(s, "low", t.secular_low);
        read_into(s, "central", t.secular_central);
        read_into(s, "high", t.secular_high);
    }
    if (!(t.secular_timescale >= 1.0 && t.secular_timescale <= 200.0)) throw std::invalid_argument("long_term.secular_background.timescale_years must lie within 1-200");
    if (!(t.secular_low >= 0.0 && t.secular_low <= t.secular_central && t.secular_central <= t.secular_high && t.secular_high <= 10.0))
        throw std::invalid_argument("long_term.secular_background needs 0 <= low <= central <= high <= 10");
    if (l.contains("cosmic_rays")) {
        const Json& c = required(l, "cosmic_rays");
        only_keys(c, "long_term.cosmic_rays", {"phi_min_mv", "phi_per_sunspot_mv", "background_weight", "production_offset_mv", "production_exponent"});
        read_into(c, "phi_min_mv", t.cosmic.phi_min_mv);
        read_into(c, "phi_per_sunspot_mv", t.cosmic.phi_per_sunspot_mv);
        read_into(c, "background_weight", t.cosmic.background_weight);
        read_into(c, "production_offset_mv", t.cosmic.production_offset_mv);
        read_into(c, "production_exponent", t.cosmic.production_exponent);
    }
    validate_cosmic_rays(t.cosmic);
    if (l.contains("volcanic")) {
        const Json& v = required(l, "volcanic");
        only_keys(v, "long_term.volcanic", {"rise_years", "eruptions", "note"});
        read_into(v, "rise_years", t.volcanic_rise);
        if (!(t.volcanic_rise >= 0.1 && t.volcanic_rise <= 5.0)) throw std::invalid_argument("long_term.volcanic.rise_years must lie within 0.1-5");
        const Json& list = required(v, "eruptions");
        if (!list.is_array() || list.size() > 200) throw std::invalid_argument("long_term.volcanic.eruptions must list at most 200 eruptions");
        for (const Json& e : list) {
            only_keys(e, "long_term.volcanic.eruptions[]", {"name", "year", "peak_forcing_w_m2"});
            VolcanicEruption x{optional_string(e, "name", "eruption"), required_number(e, "year"), required_number(e, "peak_forcing_w_m2")};
            if (!(x.peak_forcing_w_m2 <= 0.0 && x.peak_forcing_w_m2 >= -30.0)) throw std::invalid_argument("volcanic peak forcing must lie within -30-0 W/m2");
            t.eruptions.push_back(x);
        }
    }
    const Json& co2 = required(l, "co2_ppm");
    if (!co2.is_array() || co2.size() < 2) throw std::invalid_argument("long_term.co2_ppm needs [year, ppm] points");
    for (const Json& p : co2) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) throw std::invalid_argument("long_term.co2_ppm points are [year, ppm]");
        const double y = p[0].get<double>(), c = p[1].get<double>();
        if (!t.co2.empty() && y <= t.co2.back().first) throw std::invalid_argument("long_term.co2_ppm years must increase");
        if (!(c >= 150.0 && c <= 2000.0)) throw std::invalid_argument("long_term.co2_ppm values must lie within 150-2000 ppm");
        t.co2.emplace_back(y, c);
    }
    if (l.contains("regional_winter")) {
        const Json& g = required(l, "regional_winter");
        only_keys(g, "long_term.regional_winter", {"name", "global_amplification", "top_down_k_per_uv_pct"});
        t.regional_name = optional_string(g, "name", t.regional_name);
        read_into(g, "global_amplification", t.regional_amplification);
        read_into(g, "top_down_k_per_uv_pct", t.regional_top_down_k_per_uv_pct);
        if (!(t.regional_amplification >= 0.0 && t.regional_amplification <= 5.0 && t.regional_top_down_k_per_uv_pct >= 0.0 && t.regional_top_down_k_per_uv_pct <= 1.0))
            throw std::invalid_argument("long_term.regional_winter needs global_amplification within 0-5 and top_down_k_per_uv_pct within 0-1");
    }
    if (l.contains("future")) {
        const Json& f = required(l, "future");
        only_keys(f, "long_term.future", {"grand_minimum"});
        const Json& g = required(f, "grand_minimum");
        only_keys(g, "long_term.future.grand_minimum", {"first_cycle", "cycles", "amplitude"});
        t.gm_first_cycle = static_cast<int>(optional_integer(g, "first_cycle", t.gm_first_cycle, -100, 1000));
        t.gm_cycles = static_cast<int>(optional_integer(g, "cycles", t.gm_cycles, 1, 30));
        read_into(g, "amplitude", t.gm_amplitude);
        if (!(t.gm_amplitude >= 1.0 && t.gm_amplitude <= 100.0)) throw std::invalid_argument("long_term.future.grand_minimum.amplitude must lie within 1-100");
    }
    if (l.contains("dynamo")) {
        const Json& d = required(l, "dynamo");
        only_keys(d, "long_term.dynamo", {"weak_alpha", "babcock_leighton_noise", "emergence_threshold", "emergence_width", "quenching_field", "floor_noise", "amplitude_scale",
                                          "cycle_mean_fraction", "episode_cycles", "statistics_cycles", "realizations", "cycles_ahead", "seed", "targets"});
        auto& c = t.dynamo;
        read_into(d, "weak_alpha", c.weak_alpha);
        read_into(d, "babcock_leighton_noise", c.babcock_leighton_noise);
        read_into(d, "emergence_threshold", c.emergence_threshold);
        read_into(d, "emergence_width", c.emergence_width);
        read_into(d, "quenching_field", c.quenching_field);
        read_into(d, "floor_noise", c.floor_noise);
        read_into(d, "amplitude_scale", c.amplitude_scale);
        read_into(d, "cycle_mean_fraction", c.cycle_mean_fraction);
        c.episode_cycles = static_cast<std::size_t>(optional_integer(d, "episode_cycles", 2, 1, 10));
        t.dynamo_statistics_cycles = static_cast<std::size_t>(optional_integer(d, "statistics_cycles", 100000, 1000, 2000000));
        t.dynamo_realizations = static_cast<std::size_t>(optional_integer(d, "realizations", 20000, 100, 1000000));
        t.dynamo_cycles_ahead = static_cast<std::size_t>(optional_integer(d, "cycles_ahead", 10, 2, 50));
        t.dynamo_seed = static_cast<std::uint64_t>(optional_integer(d, "seed", 13, 1, 1LL << 40));
        if (d.contains("targets")) {
            t.dynamo_targets = required(d, "targets");
            only_keys(t.dynamo_targets, "long_term.dynamo.targets", {"source", "grand_minima_per_11400_years", "grand_minimum_fraction", "grand_minimum_mean_years",
                                                                    "grand_maxima_per_11400_years", "grand_maximum_fraction", "grand_maximum_mean_years"});
        }
    }
    // The episode thresholds of the dynamo follow the annual ones (decadal means).
    t.dynamo.grand_minimum_mean = t.episode_low;
    t.dynamo.grand_maximum_mean = t.episode_high;
    validate_solar_dynamo(t.dynamo);
    if (l.contains("case_study")) {
        const Json& c = required(l, "case_study");
        only_keys(c, "long_term.case_study", {"epoch", "from_year", "to_year"});
        t.case_epoch = optional_string(c, "epoch", "");
        t.case_from = static_cast<int>(optional_integer(c, "from_year", t.case_from, -10000, 2500));
        t.case_to = static_cast<int>(optional_integer(c, "to_year", t.case_to, -10000, 2500));
        if (std::none_of(t.epochs.begin(), t.epochs.end(), [&](const SolarEpoch& e) { return e.name == t.case_epoch; })) throw std::invalid_argument("long_term.case_study.epoch must name an epoch");
        if (!(t.case_from < t.case_to)) throw std::invalid_argument("long_term.case_study needs from_year < to_year");
    }
    if (!(t.reference_start < t.reference_end)) throw std::invalid_argument("long_term.secular_background needs reference_start_year < reference_end_year");
    return t;
}

// ---- Request -------------------------------------------------------------------------------------------------------

struct Request {
    Json echo;
    std::string kind;                                // cycle_table | monthly
    std::string data_origin;
    int first_cycle_number{1};
    // cycle_table
    double start_year{0.0};
    std::vector<SolarCycleTableEntry> table;
    bool calibrate_widths{false};
    double first_lead_years{0.3};
    SunspotNoise noise;
    // monthly
    int first_month{0};
    std::vector<double> monthly;
    // forecast
    double issue_year{0.0}, horizon_year{0.0};
    std::size_t members{200};
    std::uint64_t seed{1};
    std::vector<double> phases{0.0, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0};
    SolarForecastSettings settings;
    // physics
    SolarProxyConfig proxies;
    std::vector<SolarUvBand> bands = default_solar_uv_bands();
    SolarAtmosphereConfig atmosphere;
    std::vector<SolarUvSite> sites;
    SolarFlareConfig flares;
    std::uint64_t flare_seed{5};
    SolarClimateConfig climate;
    SolarDiskConfig disk;
    double disk_first_year{0.0}, disk_last_year{0.0};
    int disk_every_months{6};
    std::uint64_t disk_seed{3};
    double butterfly_start_year{0.0};
    int butterfly_step_months{3};
    double butterfly_latitude_step{2.5}, butterfly_max_latitude{45.0};
    LongTermRequest long_term;
};

Request parse_request(const Json& j) {
    Request r;
    r.echo = j;
    only_keys(j, "request", {"schema_version", "record", "forecast", "irradiance", "atmosphere", "uv_sites", "flares", "climate", "disk", "butterfly", "long_term"});

    const Json& rec = required(j, "record");
    r.kind = optional_string(rec, "kind", "cycle_table");
    r.data_origin = optional_string(rec, "data_origin", "unlabelled");
    r.first_cycle_number = static_cast<int>(optional_integer(rec, "first_cycle_number", 1, -100, 1000));
    if (r.kind == "cycle_table") {
        only_keys(rec, "record", {"kind", "data_origin", "first_cycle_number", "start_year", "cycles", "calibrate_widths", "first_lead_years", "noise"});
        r.start_year = required_number(rec, "start_year");
        r.calibrate_widths = optional_bool(rec, "calibrate_widths", false);
        r.first_lead_years = optional_number(rec, "first_lead_years", r.first_lead_years);
        const Json& cycles = required(rec, "cycles");
        if (!cycles.is_array() || cycles.size() < 5) throw std::invalid_argument("record.cycles must list at least five cycles");
        for (const Json& c : cycles) {
            only_keys(c, "record.cycles[]", {"number", "minimum_year", "maximum_year", "maximum", "note"});
            SolarCycleTableEntry e;
            e.number = static_cast<int>(optional_integer(c, "number", 0, -100, 1000));
            e.minimum_year = required_number(c, "minimum_year");
            e.maximum_year = c.contains("maximum_year") ? required_number(c, "maximum_year") : kNaN;
            e.maximum = required_number(c, "maximum");
            if (!r.table.empty() && e.number != r.table.back().number + 1) throw std::invalid_argument("record.cycles must be numbered consecutively");
            r.table.push_back(e);
        }
        if (rec.contains("noise")) {
            const Json& n = required(rec, "noise");
            only_keys(n, "record.noise", {"factor", "autocorrelation", "seed"});
            read_into(n, "factor", r.noise.factor);
            read_into(n, "autocorrelation", r.noise.autocorrelation);
            r.noise.seed = static_cast<std::uint64_t>(optional_integer(n, "seed", 1, 1, 1LL << 40));
        }
        const auto first = std::find_if(r.table.begin(), r.table.end(), [&](const SolarCycleTableEntry& e) { return e.number == r.first_cycle_number; });
        if (first == r.table.end()) throw std::invalid_argument("record.first_cycle_number must be a cycle of the table");
        // The record must start early enough to detect that cycle's minimum (a 30-month window and 6 months of smoothing).
        if (!(r.start_year >= 1600.0 && r.start_year >= r.table.front().minimum_year - 15.0 && r.start_year <= first->minimum_year - 3.0))
            throw std::invalid_argument("record.start_year must lie after 1600, at most 15 years before the first table minimum and at least 3 years before the minimum of first_cycle_number");
    } else if (r.kind == "monthly") {
        only_keys(rec, "record", {"kind", "data_origin", "first_cycle_number", "first_year", "first_month", "values"});
        const int year = static_cast<int>(optional_integer(rec, "first_year", 0, 1600, 3000));
        const int month = static_cast<int>(optional_integer(rec, "first_month", 1, 1, 12));
        if (!rec.contains("first_year")) throw std::invalid_argument("run request is missing field: first_year");
        r.first_month = year * 12 + month - 1;
        const Json& values = required(rec, "values");
        if (!values.is_array() || values.size() < 12 * 40 || values.size() > 12 * 600) throw std::invalid_argument("record.values must hold 40-600 years of monthly values");
        for (const Json& v : values) {
            if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() < 0.0 || v.get<double>() > 1000.0)
                throw std::invalid_argument("record.values must be sunspot numbers within 0-1000 (fill gaps before converting)");
            r.monthly.push_back(v.get<double>());
        }
    } else {
        throw std::invalid_argument("record.kind must be cycle_table or monthly");
    }

    const Json& f = required(j, "forecast");
    only_keys(f, "forecast", {"issue_year", "horizon_year", "members", "seed", "backtest_phases_years", "settings"});
    r.issue_year = required_number(f, "issue_year");
    r.horizon_year = required_number(f, "horizon_year");
    r.members = static_cast<std::size_t>(optional_integer(f, "members", 200, 10, 2000));
    r.seed = static_cast<std::uint64_t>(optional_integer(f, "seed", 1, 1, 1LL << 40));
    if (f.contains("backtest_phases_years")) {
        const Json& p = required(f, "backtest_phases_years");
        if (!p.is_array() || p.empty() || p.size() > 20) throw std::invalid_argument("forecast.backtest_phases_years must list 1-20 phases");
        r.phases.clear();
        for (const Json& v : p) {
            if (!v.is_number() || !(v.get<double>() >= 0.0 && v.get<double>() <= 10.0)) throw std::invalid_argument("backtest phases must lie within 0-10 years");
            if (!r.phases.empty() && v.get<double>() <= r.phases.back()) throw std::invalid_argument("backtest phases must increase");
            r.phases.push_back(v.get<double>());
        }
    }
    if (f.contains("settings")) {
        const Json& s = required(f, "settings");
        only_keys(s, "forecast.settings", {"half_window_months", "confirm_months", "minimum_history_cycles", "fit_lead_in_years", "shape_fit_phase_years", "onset_search_years"});
        r.settings.half_window_months = static_cast<std::size_t>(optional_integer(s, "half_window_months", 30, 6, 60));
        r.settings.confirm_months = static_cast<std::size_t>(optional_integer(s, "confirm_months", 12, 3, 60));
        r.settings.minimum_history_cycles = static_cast<std::size_t>(optional_integer(s, "minimum_history_cycles", 3, 2, 30));
        read_into(s, "fit_lead_in_years", r.settings.fit_lead_in_years);
        read_into(s, "shape_fit_phase_years", r.settings.shape_fit_phase_years);
        read_into(s, "onset_search_years", r.settings.onset_search_years);
    }
    if (!(r.horizon_year >= r.issue_year + 2.0 && r.horizon_year <= r.issue_year + 40.0)) throw std::invalid_argument("forecast.horizon_year must lie 2-40 years after the issue year");

    if (j.contains("irradiance")) {
        const Json& s = required(j, "irradiance");
        only_keys(s, "irradiance", {"f107_base_sfu", "f107_per_sunspot", "f107_per_sunspot_squared", "mgii_quiet", "mgii_per_sfu", "reference_sunspot_number", "tsi_quiet_w_m2",
                                    "facular_brightening_w_m2", "spot_darkening_w_m2_per_sunspot", "uv_bands"});
        auto& p = r.proxies;
        read_into(s, "f107_base_sfu", p.f107_base_sfu);
        read_into(s, "f107_per_sunspot", p.f107_per_sunspot);
        read_into(s, "f107_per_sunspot_squared", p.f107_per_sunspot_squared);
        read_into(s, "mgii_quiet", p.mgii_quiet);
        read_into(s, "mgii_per_sfu", p.mgii_per_sfu);
        read_into(s, "reference_sunspot_number", p.reference_sunspot_number);
        read_into(s, "tsi_quiet_w_m2", p.tsi_quiet_w_m2);
        read_into(s, "facular_brightening_w_m2", p.facular_brightening_w_m2);
        read_into(s, "spot_darkening_w_m2_per_sunspot", p.spot_darkening_w_m2_per_sunspot);
        if (s.contains("uv_bands")) {
            const Json& b = required(s, "uv_bands");
            if (!b.is_array()) throw std::invalid_argument("irradiance.uv_bands must be an array");
            r.bands.clear();
            for (const Json& x : b) {
                only_keys(x, "irradiance.uv_bands[]", {"key", "label", "from_nm", "to_nm", "quiet_w_m2", "variability"});
                SolarUvBand band;
                band.key = optional_string(x, "key", "");
                band.label = optional_string(x, "label", band.key);
                band.from_nm = required_number(x, "from_nm");
                band.to_nm = required_number(x, "to_nm");
                band.quiet_w_m2 = required_number(x, "quiet_w_m2");
                band.variability = required_number(x, "variability");
                r.bands.push_back(band);
            }
        }
    }
    validate_solar_proxies(r.proxies, r.bands);

    if (j.contains("atmosphere")) {
        const Json& s = required(j, "atmosphere");
        only_keys(s, "atmosphere", {"ozone_change_at_reference", "stratosphere_warming_at_reference_k", "erythemal_variability", "radiation_amplification_factor"});
        read_into(s, "ozone_change_at_reference", r.atmosphere.ozone_change_at_reference);
        read_into(s, "stratosphere_warming_at_reference_k", r.atmosphere.stratosphere_warming_at_reference_k);
        read_into(s, "erythemal_variability", r.atmosphere.erythemal_variability);
        read_into(s, "radiation_amplification_factor", r.atmosphere.radiation_amplification_factor);
    }
    validate_solar_atmosphere(r.atmosphere);

    const Json& sites = required(j, "uv_sites");
    if (!sites.is_array() || sites.empty() || sites.size() > 8) throw std::invalid_argument("uv_sites must list 1-8 sites");
    std::set<std::string> names;
    for (const Json& x : sites) {
        only_keys(x, "uv_sites[]", {"name", "latitude_deg", "ozone_du", "ozone_seasonal_amplitude_du", "ozone_peak_day"});
        SolarUvSite s;
        s.name = optional_string(x, "name", "");
        s.latitude_deg = required_number(x, "latitude_deg");
        s.ozone_du = required_number(x, "ozone_du");
        s.ozone_seasonal_amplitude_du = optional_number(x, "ozone_seasonal_amplitude_du", 0.0);
        s.ozone_peak_day = optional_number(x, "ozone_peak_day", 90.0);
        validate_solar_uv_site(s);
        if (!names.insert(s.name).second) throw std::invalid_argument("uv_sites names must be unique");
        r.sites.push_back(s);
    }

    if (j.contains("flares")) {
        const Json& s = required(j, "flares");
        only_keys(s, "flares", {"x_rate_per_year", "reference_sunspot_number", "rate_exponent", "size_index", "max_peak_flux_w_m2", "energy_x1_j", "energy_exponent", "seed"});
        read_into(s, "x_rate_per_year", r.flares.x_rate_per_year);
        read_into(s, "reference_sunspot_number", r.flares.reference_sunspot_number);
        read_into(s, "rate_exponent", r.flares.rate_exponent);
        read_into(s, "size_index", r.flares.size_index);
        read_into(s, "max_peak_flux_w_m2", r.flares.max_peak_flux_w_m2);
        read_into(s, "energy_x1_j", r.flares.energy_x1_j);
        read_into(s, "energy_exponent", r.flares.energy_exponent);
        r.flare_seed = static_cast<std::uint64_t>(optional_integer(s, "seed", 5, 1, 1LL << 40));
    }
    validate_solar_flares(r.flares);

    if (j.contains("climate")) {
        const Json& s = required(j, "climate");
        only_keys(s, "climate", {"albedo", "upper_heat_capacity", "deep_heat_capacity", "feedback_w_m2_k", "exchange_w_m2_k"});
        read_into(s, "albedo", r.climate.albedo);
        read_into(s, "upper_heat_capacity", r.climate.upper_heat_capacity);
        read_into(s, "deep_heat_capacity", r.climate.deep_heat_capacity);
        read_into(s, "feedback_w_m2_k", r.climate.feedback_w_m2_k);
        read_into(s, "exchange_w_m2_k", r.climate.exchange_w_m2_k);
    }
    validate_solar_climate(r.climate);

    const Json& d = required(j, "disk");
    only_keys(d, "disk", {"first_year", "last_year", "every_months", "seed", "cells", "extent_radii", "subsamples", "effective_temperature_k", "limb_darkening", "groups_per_sunspot",
                          "group_area_median_uhem", "group_area_log_sd", "umbra_fraction", "umbra_contrast", "penumbra_contrast", "facular_area_ratio", "facular_contrast_centre",
                          "facular_contrast_limb", "onset_latitude_deg", "drift_efold_months", "zone_width_fraction", "zone_min_width_deg", "max_longitude_deg", "area_per_sunspot_uhem"});
    r.disk_first_year = required_number(d, "first_year");
    r.disk_last_year = required_number(d, "last_year");
    r.disk_every_months = static_cast<int>(optional_integer(d, "every_months", 6, 1, 120));
    r.disk_seed = static_cast<std::uint64_t>(optional_integer(d, "seed", 3, 1, 1LL << 40));
    auto& dc = r.disk;
    dc.cells = static_cast<std::size_t>(optional_integer(d, "cells", static_cast<long long>(dc.cells), 8, 256));
    dc.subsamples = static_cast<std::size_t>(optional_integer(d, "subsamples", static_cast<long long>(dc.subsamples), 1, 16));
    for (auto [key, target] : std::initializer_list<std::pair<const char*, double*>>{
             {"extent_radii", &dc.extent_radii}, {"effective_temperature_k", &dc.effective_temperature_k}, {"limb_darkening", &dc.limb_darkening},
             {"groups_per_sunspot", &dc.groups_per_sunspot}, {"group_area_median_uhem", &dc.group_area_median_uhem}, {"group_area_log_sd", &dc.group_area_log_sd},
             {"umbra_fraction", &dc.umbra_fraction}, {"umbra_contrast", &dc.umbra_contrast}, {"penumbra_contrast", &dc.penumbra_contrast},
             {"facular_area_ratio", &dc.facular_area_ratio}, {"facular_contrast_centre", &dc.facular_contrast_centre}, {"facular_contrast_limb", &dc.facular_contrast_limb},
             {"onset_latitude_deg", &dc.onset_latitude_deg}, {"drift_efold_months", &dc.drift_efold_months}, {"zone_width_fraction", &dc.zone_width_fraction},
             {"zone_min_width_deg", &dc.zone_min_width_deg}, {"max_longitude_deg", &dc.max_longitude_deg}, {"area_per_sunspot_uhem", &dc.area_per_sunspot_uhem}})
        read_into(d, key, *target);
    validate_solar_disk(dc);

    if (j.contains("butterfly")) {
        const Json& b = required(j, "butterfly");
        only_keys(b, "butterfly", {"start_year", "step_months", "latitude_step_deg", "max_latitude_deg"});
        r.butterfly_start_year = optional_number(b, "start_year", 0.0);
        r.butterfly_step_months = static_cast<int>(optional_integer(b, "step_months", 3, 1, 24));
        read_into(b, "latitude_step_deg", r.butterfly_latitude_step);
        read_into(b, "max_latitude_deg", r.butterfly_max_latitude);
    }
    if (!(r.butterfly_latitude_step >= 0.5 && r.butterfly_latitude_step <= 10.0) || !(r.butterfly_max_latitude >= 10.0 && r.butterfly_max_latitude <= 90.0))
        throw std::invalid_argument("butterfly latitudes need a step within 0.5-10 and a maximum within 10-90 degrees");
    if (j.contains("long_term")) {
        r.long_term = parse_long_term(required(j, "long_term"));
        if (r.long_term.horizon_year < static_cast<int>(r.issue_year) + 1) throw std::invalid_argument("long_term.horizon_year must lie after the issue year");
    }
    return r;
}

// ---- Derived monthly quantities ------------------------------------------------------------------------------------

struct Derived {
    std::vector<double> f107, tsi, facular, ozone_pct, stratosphere_k, uvi_change_pct, flare_m1, flare_x1, forcing;
    std::vector<std::vector<double>> band_pct;          // [band][month]
};

Derived derive(const std::vector<double>& sn, const Request& r, double reference_tsi) {
    Derived d;
    d.band_pct.assign(r.bands.size(), {});
    for (double s : sn) {
        const auto irr = solar_irradiance(s, r.proxies, r.bands);
        d.f107.push_back(irr.f107_sfu);
        d.tsi.push_back(irr.tsi_w_m2);
        d.facular.push_back(irr.facular_index);
        for (std::size_t b = 0; b < r.bands.size(); ++b) d.band_pct[b].push_back(100.0 * irr.band_change[b]);
        d.ozone_pct.push_back(100.0 * r.atmosphere.ozone_change_at_reference * irr.facular_index);
        d.stratosphere_k.push_back(r.atmosphere.stratosphere_warming_at_reference_k * irr.facular_index);
        // The relative UV-index change is the same at every site and season (the model is multiplicative).
        d.uvi_change_pct.push_back(100.0 * ((1.0 + r.atmosphere.erythemal_variability * irr.facular_index) *
                                            std::pow(1.0 + r.atmosphere.ozone_change_at_reference * irr.facular_index, -r.atmosphere.radiation_amplification_factor) - 1.0));
        d.flare_m1.push_back(flare_rate_per_year(s, kFlareM1, r.flares) / 12.0);
        d.flare_x1.push_back(flare_rate_per_year(s, kFlareX1, r.flares) / 12.0);
        d.forcing.push_back(solar_radiative_forcing_w_m2(irr.tsi_w_m2 - reference_tsi, r.climate));
    }
    return d;
}

struct Flare {
    double peak_flux;
};

// ---- Long-term block -----------------------------------------------------------------------------------------------

double mean_over(const std::vector<double>& v, int first_year, int from, int to) {
    double s = 0.0;
    int n = 0;
    for (int y = std::max(from, first_year); y <= to && y - first_year < static_cast<int>(v.size()); ++y) { s += v[static_cast<std::size_t>(y - first_year)]; ++n; }
    return n ? s / n : kNaN;
}

double interpolate_points(const std::vector<std::pair<double, double>>& p, double x) {
    if (x <= p.front().first) return p.front().second;
    if (x >= p.back().first) return p.back().second;
    std::size_t k = 1;
    while (p[k].first < x) ++k;
    const double w = (x - p[k - 1].first) / (p[k].first - p[k - 1].first);
    return p[k - 1].second + (p[k].second - p[k - 1].second) * w;
}

std::vector<double> run_climate(const std::vector<double>& forcing, const SolarClimateConfig& c) {
    std::vector<double> out;
    SolarClimateState s;
    for (double f : forcing) { s = step_solar_climate(s, f, 1.0, c); out.push_back(s.surface_k); }
    return out;
}

Json build_long_term(const Request& r, const MonthlySeries& record, const std::vector<SolarCycleCurve>& table_curves, const SolarCycleAnalysis& analysis,
                     const SolarCycleForecast& forecast, int issue_month) {
    const auto& t = r.long_term;
    const auto& st = analysis.statistics;
    const int Y0 = t.start_year, Y1 = t.horizon_year;
    if (Y0 > 1800) throw std::invalid_argument("long_term.start_year must lie before 1800 (the pre-industrial reference ends in 1849)");
    // Curves known before the record: the cycle table (or the fits of a monthly record), and before them the
    // cycles synthesised from the epoch envelope.
    std::vector<SolarCycleCurve> known;
    std::vector<int> known_numbers;
    if (!table_curves.empty()) {
        known = table_curves;
        for (const auto& e : r.table) known_numbers.push_back(e.number);
    } else {
        for (std::size_t k = 0; k < analysis.fits.size(); ++k) { known.push_back(analysis.fits[k].curve); known_numbers.push_back(analysis.cycles[k].number); }
    }
    const double first_onset = known.front().onset_year;
    if (!(Y0 < first_onset - 11.0)) throw std::invalid_argument("long_term.start_year must lie at least a cycle before the first known cycle");
    const HistoricalCycles hist = synthesize_historical_cycles(first_onset, known_numbers.front(), Y0, t.epochs, t.envelope, t.historical);
    std::vector<SolarCycleCurve> before = hist.curves;
    before.insert(before.end(), known.begin(), known.end());
    // The future: the central forecast continued with the record's mean cycle, and a grand-minimum scenario.
    std::vector<SolarCycleCurve> regular = forecast.central_curves;
    std::vector<int> regular_numbers = forecast.central_cycle_numbers;
    while (regular.back().onset_year < Y1 + 2.0) {
        regular.push_back(make_solar_cycle_curve(regular.back().onset_year + st.mean_period_years, std::clamp(st.mean_amplitude, 1.0, 1000.0), st.mean_width_scale));
        regular_numbers.push_back(regular_numbers.back() + 1);
    }
    if (t.gm_first_cycle <= analysis.current_cycle) throw std::invalid_argument("long_term.future.grand_minimum.first_cycle must be a cycle after the one in progress");
    std::vector<SolarCycleCurve> minimum = regular;
    for (std::size_t k = 0; k < regular.size(); ++k)
        if (regular_numbers[k] >= t.gm_first_cycle && regular_numbers[k] < t.gm_first_cycle + t.gm_cycles)
            minimum[k] = make_solar_cycle_curve(regular[k].onset_year, t.gm_amplitude, regular[k].width_scale);

    // Annual series.
    const std::size_t n = static_cast<std::size_t>(Y1 - Y0 + 1);
    std::vector<double> sn(n), sn_gm(n), envelope(n);
    Json source = Json::array();
    for (int y = Y0; y <= Y1; ++y) {
        double a = 0.0, g = 0.0;
        int from_record = 0, from_future = 0;
        for (int m = 0; m < 12; ++m) {
            const int idx = y * 12 + m;
            const double year = month_mid_year(idx);
            double v, vg;
            if (idx < record.first_month) { v = vg = cycles_value(before, year); }
            else if (idx < issue_month) { v = vg = record.values[static_cast<std::size_t>(idx - record.first_month)]; ++from_record; }
            else { v = cycles_value(regular, year); vg = cycles_value(minimum, year); ++from_future; }
            a += v / 12.0;
            g += vg / 12.0;
        }
        const std::size_t k = static_cast<std::size_t>(y - Y0);
        sn[k] = a;
        sn_gm[k] = g;
        envelope[k] = solar_envelope(y + 0.5, t.epochs, t.envelope);
        source.push_back(from_future >= 6 ? "forecast" : from_record >= 6 ? "record" : (y + 0.5 < first_onset ? "envelope" : (table_curves.empty() ? "record_fit" : "table")));
    }
    // The slow background and the irradiance.
    const auto background = exponential_moving_average(sn, t.secular_timescale), background_gm = exponential_moving_average(sn_gm, t.secular_timescale);
    if (t.reference_start < Y0 || t.reference_end > Y1) throw std::invalid_argument("long_term.secular_background reference years must lie within the long-term span");
    const double m_ref = mean_over(background, Y0, t.reference_start, t.reference_end);
    const auto& p = r.proxies;
    const double sn_ref = p.reference_sunspot_number;
    auto tsi_of = [&](double s, double m, double k) { return p.tsi_quiet_w_m2 + p.facular_brightening_w_m2 * (solar_facular_index(s, p) + k * (m - m_ref) / sn_ref) - p.spot_darkening_w_m2_per_sunspot * std::max(0.0, s); };
    std::size_t uv_band = 0;
    for (std::size_t b = 0; b < r.bands.size(); ++b) if (r.bands[b].key == "o2_herzberg") uv_band = b;
    std::vector<double> tsi_low(n), tsi_central(n), tsi_high(n), tsi_gm(n), uv(n), uv_gm(n), ozone(n), strat(n), x_total(n);
    for (std::size_t k = 0; k < n; ++k) {
        tsi_low[k] = tsi_of(sn[k], background[k], t.secular_low);
        tsi_central[k] = tsi_of(sn[k], background[k], t.secular_central);
        tsi_high[k] = tsi_of(sn[k], background[k], t.secular_high);
        tsi_gm[k] = tsi_of(sn_gm[k], background_gm[k], t.secular_central);
        x_total[k] = solar_facular_index(sn[k], p) + t.secular_central * (background[k] - m_ref) / sn_ref;
        const double xg = solar_facular_index(sn_gm[k], p) + t.secular_central * (background_gm[k] - m_ref) / sn_ref;
        uv[k] = 100.0 * r.bands[uv_band].variability * x_total[k];
        uv_gm[k] = 100.0 * r.bands[uv_band].variability * xg;
        ozone[k] = 100.0 * r.atmosphere.ozone_change_at_reference * x_total[k];
        strat[k] = r.atmosphere.stratosphere_warming_at_reference_k * x_total[k];
    }
    // Cosmic rays.
    const auto sn3 = running_mean(sn, 3), sn3_gm = running_mean(sn_gm, 3);
    std::vector<double> phi(n), phi_gm(n), production(n), production_gm(n);
    for (std::size_t k = 0; k < n; ++k) { phi[k] = modulation_potential_mv(sn3[k], background[k], t.cosmic); phi_gm[k] = modulation_potential_mv(sn3_gm[k], background_gm[k], t.cosmic); }
    const double phi_ref = mean_over(phi, Y0, t.reference_start, t.reference_end);
    for (std::size_t k = 0; k < n; ++k) { production[k] = relative_cosmogenic_production(phi[k], phi_ref, t.cosmic); production_gm[k] = relative_cosmogenic_production(phi_gm[k], phi_ref, t.cosmic); }
    // Forcings as anomalies about their pre-industrial means (Y0-1849), and the energy balance of each.
    const int pre_end = 1849;
    std::vector<double> volcanic(n), co2(n);
    for (std::size_t k = 0; k < n; ++k) {
        volcanic[k] = annual_volcanic_forcing_w_m2(Y0 + static_cast<int>(k), t.eruptions, t.volcanic_rise);
        co2[k] = interpolate_points(t.co2, Y0 + static_cast<int>(k) + 0.5);
    }
    const double co2_pre = mean_over(co2, Y0, Y0, pre_end), volcanic_pre = mean_over(volcanic, Y0, Y0, pre_end);
    const double albedo_factor = (1.0 - r.climate.albedo) / 4.0;
    auto solar_forcing = [&](const std::vector<double>& tsi, double pre) { std::vector<double> f(n); for (std::size_t k = 0; k < n; ++k) f[k] = (tsi[k] - pre) * albedo_factor; return f; };
    const double pre_low = mean_over(tsi_low, Y0, Y0, pre_end), pre_central = mean_over(tsi_central, Y0, Y0, pre_end), pre_high = mean_over(tsi_high, Y0, Y0, pre_end);
    const auto f_low = solar_forcing(tsi_low, pre_low), f_central = solar_forcing(tsi_central, pre_central), f_high = solar_forcing(tsi_high, pre_high), f_gm = solar_forcing(tsi_gm, pre_central);
    std::vector<double> f_volcanic(n), f_co2(n);
    for (std::size_t k = 0; k < n; ++k) { f_volcanic[k] = volcanic[k] - volcanic_pre; f_co2[k] = co2_forcing_w_m2(co2[k], co2_pre); }
    const auto t_low = run_climate(f_low, r.climate), t_central = run_climate(f_central, r.climate), t_high = run_climate(f_high, r.climate), t_gm = run_climate(f_gm, r.climate);
    const auto t_volcanic = run_climate(f_volcanic, r.climate), t_co2 = run_climate(f_co2, r.climate);
    std::vector<double> total(n), total_gm(n), regional(n), regional_gm(n);
    const double uv_pre = mean_over(uv, Y0, Y0, pre_end);
    for (std::size_t k = 0; k < n; ++k) {
        total[k] = t_central[k] + t_volcanic[k] + t_co2[k];
        total_gm[k] = t_gm[k] + t_volcanic[k] + t_co2[k];
        regional[k] = t.regional_amplification * total[k] + t.regional_top_down_k_per_uv_pct * (uv[k] - uv_pre);
        regional_gm[k] = t.regional_amplification * total_gm[k] + t.regional_top_down_k_per_uv_pct * (uv_gm[k] - uv_pre);
    }

    // Epochs: what the catalogue's epochs looked like, against the pre-industrial mean.
    auto anomaly = [&](const std::vector<double>& v, int from, int to) { return mean_over(v, Y0, from, to) - mean_over(v, Y0, Y0, pre_end); };
    Json epochs = Json::array();
    for (const auto& e : t.epochs) {
        const int from = std::max(Y0, static_cast<int>(std::ceil(e.start_year))), to = std::min(Y1, static_cast<int>(std::floor(e.end_year)));
        if (from > to) continue;
        epochs.push_back({{"name", e.name}, {"kind", solar_epoch_kind_name(e.kind)}, {"start_year", e.start_year}, {"end_year", e.end_year},
                          {"cycle_amplitude", e.cycle_amplitude > 0.0 ? Json(e.cycle_amplitude) : Json(nullptr)}, {"evidence", e.evidence},
                          {"mean_sunspot", fixed(mean_over(sn, Y0, from, to), 1)},
                          {"tsi_vs_preindustrial_w_m2", {{"low", tidy(anomaly(tsi_low, from, to), 3)}, {"central", tidy(anomaly(tsi_central, from, to), 3)}, {"high", tidy(anomaly(tsi_high, from, to), 3)}}},
                          {"tsi_vs_reference_w_m2", {{"low", tidy(mean_over(tsi_low, Y0, from, to) - mean_over(tsi_low, Y0, t.reference_start, t.reference_end), 3)},
                                                     {"central", tidy(mean_over(tsi_central, Y0, from, to) - mean_over(tsi_central, Y0, t.reference_start, t.reference_end), 3)},
                                                     {"high", tidy(mean_over(tsi_high, Y0, from, to) - mean_over(tsi_high, Y0, t.reference_start, t.reference_end), 3)}}},
                          {"uv_200_242_pct", tidy(anomaly(uv, from, to), 3)}, {"ozone_pct", tidy(anomaly(ozone, from, to), 3)}, {"stratosphere_k", tidy(anomaly(strat, from, to), 3)},
                          {"cosmogenic_production_pct", tidy(100.0 * (mean_over(production, Y0, from, to) / mean_over(production, Y0, Y0, pre_end) - 1.0), 3)},
                          {"temperature_k", {{"solar_low", tidy(anomaly(t_low, from, to), 3)}, {"solar_central", tidy(anomaly(t_central, from, to), 3)}, {"solar_high", tidy(anomaly(t_high, from, to), 3)},
                                             {"volcanic", tidy(anomaly(t_volcanic, from, to), 3)}, {"co2", tidy(anomaly(t_co2, from, to), 3)}, {"total", tidy(anomaly(total, from, to), 3)},
                                             {"regional_winter", tidy(anomaly(regional, from, to), 3)}}}});
    }
    // Episodes detected in the series (up to the last year of the record).
    const int last_record_year = issue_month / 12 - 1;
    std::vector<double> observed(sn.begin(), sn.begin() + std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(n), last_record_year - Y0 + 1));
    Json episodes = Json::array();
    for (const auto& e : detect_solar_episodes(observed, Y0, t.episode_low, t.episode_high, t.episode_window, t.episode_min_years)) {
        std::string matched;
        for (const auto& c : t.epochs)
            if (c.start_year <= e.end_year + 0.5 && e.start_year <= c.end_year &&
                (e.grand_minimum ? (c.kind == SolarEpochKind::grand_minimum || c.kind == SolarEpochKind::minimum) : (c.kind == SolarEpochKind::grand_maximum || c.kind == SolarEpochKind::maximum)))
                matched = c.name;
        episodes.push_back({{"kind", e.grand_minimum ? "grand_minimum" : "grand_maximum"}, {"start_year", e.start_year}, {"end_year", e.end_year},
                            {"years", e.end_year - e.start_year + 1}, {"mean_sunspot", fixed(e.mean_sunspot, 1)}, {"epoch", matched.empty() ? Json(nullptr) : Json(matched)}});
    }
    // Cycles over the whole span.
    Json cycles = Json::array();
    auto add_cycle = [&](const SolarCycleCurve& c, int number, const char* from, double scenario) {
        if (c.peak_year() < Y0 || c.onset_year > Y1) return;
        Json row = {{"number", number}, {"onset_year", fixed(c.onset_year, 2)}, {"peak_year", fixed(c.peak_year(), 2)}, {"amplitude", fixed(c.amplitude, 1)}, {"source", from}};
        if (std::isfinite(scenario)) row["grand_minimum_scenario_amplitude"] = fixed(scenario, 1);
        cycles.push_back(row);
    };
    for (std::size_t k = 0; k < hist.curves.size(); ++k) add_cycle(hist.curves[k], hist.numbers[k], "envelope", kNaN);
    for (std::size_t k = 0; k < known.size(); ++k)
        if (table_curves.empty() ? false : known_numbers[k] < r.first_cycle_number) add_cycle(known[k], known_numbers[k], "table", kNaN);
    for (std::size_t k = 0; k < analysis.fits.size(); ++k) add_cycle(analysis.fits[k].curve, analysis.cycles[k].number, k + 1 == analysis.fits.size() ? "record_fit_in_progress" : "record_fit", kNaN);
    const double forecast_end = forecast.year_at(forecast.months - 1);
    for (std::size_t k = 0; k < regular.size(); ++k)
        if (regular_numbers[k] > analysis.current_cycle)
            add_cycle(regular[k], regular_numbers[k], regular[k].onset_year <= forecast_end ? "forecast_central" : "mean_continuation", minimum[k].amplitude);

    // The future: a grand minimum against the regular continuation.
    Json future_rows = Json::array();
    const int issue_year = issue_month / 12;
    for (int y : {2030, 2040, 2050, 2060, 2075, 2090, 2100}) {
        if (y < issue_year || y > Y1) continue;
        const std::size_t k = static_cast<std::size_t>(y - Y0), k0 = static_cast<std::size_t>(issue_year - Y0);
        future_rows.push_back({{"year", y}, {"sunspot_regular", fixed(sn[k], 1)}, {"sunspot_grand_minimum", fixed(sn_gm[k], 1)}, {"tsi_difference_w_m2", tidy(tsi_gm[k] - tsi_central[k], 3)},
                               {"uv_200_242_difference_pct", tidy(uv_gm[k] - uv[k], 3)}, {"global_difference_k", tidy(total_gm[k] - total[k], 3)},
                               {"regional_winter_difference_k", tidy(regional_gm[k] - regional[k], 3)}, {"co2_warming_since_issue_k", tidy(t_co2[k] - t_co2[k0], 3)}});
    }
    double largest = 0.0;
    int largest_year = Y0;
    for (std::size_t k = 0; k < n; ++k) if (total_gm[k] - total[k] < largest) { largest = total_gm[k] - total[k]; largest_year = Y0 + static_cast<int>(k); }

    // The dynamo map: long-run statistics, an example grand minimum, a synthetic Holocene and the outlook.
    const auto& dc = t.dynamo;
    NormalSequence random(t.dynamo_seed);
    const auto steps = simulate_solar_dynamo(dc, 1.0, t.dynamo_statistics_cycles, random);
    std::vector<double> amps;
    for (const auto& s : steps) amps.push_back(s.amplitude);
    const double years_per_cycle = st.mean_period_years;
    const auto ds = dynamo_statistics(amps, dc, years_per_cycle);
    const auto all_episodes = dynamo_episodes(amps, dc);
    // The example: the first Maunder-like grand minimum (5-8 cycles, spots practically gone in at least one cycle).
    Json example = nullptr;
    for (const auto& e : all_episodes) {
        if (!e.grand_minimum || e.length < 5 || e.length > 8 || e.start < 12 || e.start + e.length + 12 > steps.size()) continue;
        double deepest = 1e9;
        for (std::size_t i = e.start; i < e.start + e.length; ++i) deepest = std::min(deepest, steps[i].amplitude);
        if (deepest > 5.0) continue;
        Json field = Json::array(), emergence = Json::array(), efficiency = Json::array(), amplitude = Json::array();
        for (std::size_t i = e.start - 10; i < e.start + e.length + 10; ++i) {
            field.push_back(fixed(steps[i].field, 3)); emergence.push_back(fixed(steps[i].emergence, 3));
            efficiency.push_back(fixed(steps[i].efficiency, 3)); amplitude.push_back(fixed(steps[i].amplitude, 1));
        }
        example = {{"first_cycle", e.start - 10}, {"episode_start", 10}, {"episode_cycles", e.length}, {"field", field}, {"emergence", emergence}, {"efficiency", efficiency}, {"amplitude", amplitude}};
        break;
    }
    const std::size_t holocene = std::min<std::size_t>(amps.size(), static_cast<std::size_t>(std::ceil(11400.0 / years_per_cycle)));
    Json holocene_amplitudes = Json::array(), holocene_episodes = Json::array();
    for (std::size_t i = 0; i < holocene; ++i) holocene_amplitudes.push_back(fixed(amps[i], 1));
    for (const auto& e : all_episodes)
        if (e.start + e.length <= holocene) holocene_episodes.push_back({{"kind", e.grand_minimum ? "grand_minimum" : "grand_maximum"}, {"start", e.start}, {"cycles", e.length}});
    Json lengths = Json::array();
    for (std::size_t L = 0; L < ds.grand_minimum_lengths.size(); ++L) if (ds.grand_minimum_lengths[L]) lengths.push_back({{"cycles", L}, {"count", ds.grand_minimum_lengths[L]}});
    const std::vector<double> last_two = {analysis.fits[analysis.fits.size() - 2].curve.amplitude, analysis.fits.back().curve.amplitude};
    const auto outlook = dynamo_outlook(dc, last_two, t.dynamo_cycles_ahead, t.dynamo_realizations, t.dynamo_seed + 1);
    Json outlook_rows = Json::array();
    double before_horizon = 0.0;
    const double last_peak = analysis.fits.back().curve.peak_year();
    for (std::size_t k = 0; k < t.dynamo_cycles_ahead; ++k) {
        const int number = analysis.current_cycle + 1 + static_cast<int>(k);
        double peak = last_peak + static_cast<double>(k + 1) * years_per_cycle;
        for (std::size_t j = 0; j < regular.size(); ++j) if (regular_numbers[j] == number) peak = regular[j].peak_year();
        if (peak <= Y1) before_horizon = outlook.grand_minimum_begun[k];
        outlook_rows.push_back({{"cycle", number}, {"peak_year", fixed(peak, 1)}, {"grand_minimum_begun", tidy(outlook.grand_minimum_begun[k], 3)}, {"below_threshold", tidy(outlook.below_threshold[k], 3)},
                                {"amplitude_p5", fixed(outlook.amplitude_p5[k], 1)}, {"amplitude_p50", fixed(outlook.amplitude_p50[k], 1)}, {"amplitude_p95", fixed(outlook.amplitude_p95[k], 1)}});
    }
    const double rate = ds.grand_minima_per_11400_years / 11400.0;
    Json dynamo = {
        {"config", {{"weak_alpha", dc.weak_alpha}, {"babcock_leighton_noise", dc.babcock_leighton_noise}, {"babcock_leighton_efficiency", tidy(dc.babcock_leighton_efficiency(), 5)},
                    {"emergence_threshold", dc.emergence_threshold}, {"emergence_width", dc.emergence_width}, {"quenching_field", dc.quenching_field}, {"floor_noise", dc.floor_noise},
                    {"amplitude_scale", dc.amplitude_scale}, {"cycle_mean_fraction", dc.cycle_mean_fraction}, {"grand_minimum_mean", dc.grand_minimum_mean},
                    {"grand_maximum_mean", dc.grand_maximum_mean}, {"episode_cycles", dc.episode_cycles}, {"seed", t.dynamo_seed}, {"years_per_cycle", fixed(years_per_cycle, 3)}}},
        {"statistics", {{"cycles", ds.cycles}, {"years", fixed(ds.years, 0)}, {"grand_minimum_fraction", tidy(ds.grand_minimum_fraction, 3)},
                        {"grand_minima_per_11400_years", tidy(ds.grand_minima_per_11400_years, 3)}, {"grand_minimum_mean_years", tidy(ds.grand_minimum_mean_years, 3)},
                        {"grand_minimum_longest_years", tidy(ds.grand_minimum_longest_years, 3)}, {"grand_maximum_fraction", tidy(ds.grand_maximum_fraction, 3)},
                        {"grand_maxima_per_11400_years", tidy(ds.grand_maxima_per_11400_years, 3)}, {"grand_maximum_mean_years", tidy(ds.grand_maximum_mean_years, 3)},
                        {"mean_amplitude_outside_minima", fixed(ds.mean_amplitude_outside_minima, 1)}, {"amplitude_autocorrelation", tidy(ds.amplitude_autocorrelation, 3)},
                        {"mean_waiting_years", tidy(ds.mean_waiting_years, 3)}, {"grand_minimum_lengths", lengths}}},
        {"targets", t.dynamo_targets},
        {"example", example},
        {"holocene", {{"amplitudes", holocene_amplitudes}, {"episodes", holocene_episodes}, {"years_per_cycle", fixed(years_per_cycle, 3)}}},
        {"outlook", {{"observed_amplitudes", {fixed(last_two[0], 1), fixed(last_two[1], 1)}}, {"realizations", t.dynamo_realizations}, {"cycles", outlook_rows},
                     {"grand_minimum_by_horizon", tidy(before_horizon, 3)},
                     {"poisson_by_horizon", tidy(1.0 - std::exp(-rate * (Y1 - r.issue_year)), 3)}}},
    };

    // Secular background summary: the TSI of the case-study epoch against the reference period, per scenario.
    Json secular = {{"timescale_years", t.secular_timescale}, {"reference_start_year", t.reference_start}, {"reference_end_year", t.reference_end},
                    {"reference_background_sunspot", fixed(m_ref, 2)}, {"facular_index_per_sunspot", {{"low", t.secular_low}, {"central", t.secular_central}, {"high", t.secular_high}}}};
    auto vec = [&](const std::vector<double>& v, int digits) { return tidy_array(v, digits); };
    Json series = {
        {"sunspot", fixed_array(sn, 1)}, {"decadal_sunspot", fixed_array(running_mean(sn, t.episode_window), 1)}, {"sunspot_grand_minimum_scenario", fixed_array(sn_gm, 1)},
        {"source", source}, {"envelope", fixed_array(envelope, 1)},
        {"background_sunspot", fixed_array(background, 1)},
        {"tsi_w_m2", {{"low", fixed_array(tsi_low, 3)}, {"central", fixed_array(tsi_central, 3)}, {"high", fixed_array(tsi_high, 3)}, {"grand_minimum_scenario", fixed_array(tsi_gm, 3)}}},
        {"uv_200_242_pct", fixed_array(uv, 3)}, {"uv_200_242_grand_minimum_pct", fixed_array(uv_gm, 3)}, {"ozone_pct", fixed_array(ozone, 3)}, {"stratosphere_k", fixed_array(strat, 3)},
        {"modulation_potential_mv", fixed_array(phi, 0)}, {"cosmogenic_production", fixed_array(production, 3)}, {"cosmogenic_production_grand_minimum", fixed_array(production_gm, 3)},
        {"co2_ppm", fixed_array(co2, 1)},
        {"forcing_w_m2", {{"solar_low", vec(f_low, 3)}, {"solar_central", vec(f_central, 3)}, {"solar_high", vec(f_high, 3)}, {"solar_grand_minimum", vec(f_gm, 3)},
                          {"volcanic", vec(f_volcanic, 3)}, {"co2", vec(f_co2, 3)}}},
        {"temperature_k", {{"solar_low", vec(t_low, 3)}, {"solar_central", vec(t_central, 3)}, {"solar_high", vec(t_high, 3)}, {"solar_grand_minimum", vec(t_gm, 3)},
                           {"volcanic", vec(t_volcanic, 3)}, {"co2", vec(t_co2, 3)}, {"total", vec(total, 3)}, {"total_grand_minimum", vec(total_gm, 3)},
                           {"regional_winter", vec(regional, 3)}, {"regional_winter_grand_minimum", vec(regional_gm, 3)}}},
    };
    Json eruptions = Json::array();
    for (const auto& e : t.eruptions) eruptions.push_back({{"name", e.name}, {"year", e.year}, {"peak_forcing_w_m2", e.peak_forcing_w_m2}});
    Json co2_points = Json::array();
    for (const auto& [y, c] : t.co2) co2_points.push_back({y, c});
    return {
        {"start_year", Y0}, {"horizon_year", Y1}, {"preindustrial_end_year", pre_end}, {"issue_year", r.issue_year},
        {"series", series},
        {"epochs", epochs}, {"episodes", episodes}, {"cycles", cycles},
        {"episode_definition", {{"window_years", t.episode_window}, {"min_years", t.episode_min_years}, {"grand_minimum_mean", t.episode_low}, {"grand_maximum_mean", t.episode_high}}},
        {"envelope", {{"normal_amplitude", t.envelope.normal_amplitude}, {"transition_years", t.envelope.transition_years}, {"first_known_onset", fixed(first_onset, 2)}}},
        {"secular_background", secular},
        {"cosmic_rays", {{"phi_min_mv", t.cosmic.phi_min_mv}, {"phi_per_sunspot_mv", t.cosmic.phi_per_sunspot_mv}, {"background_weight", t.cosmic.background_weight},
                         {"production_offset_mv", t.cosmic.production_offset_mv}, {"production_exponent", t.cosmic.production_exponent}, {"reference_phi_mv", fixed(phi_ref, 1)}}},
        {"volcanic", {{"rise_years", t.volcanic_rise}, {"eruptions", eruptions}, {"preindustrial_mean_w_m2", tidy(volcanic_pre, 4)}}},
        {"co2", {{"points", co2_points}, {"preindustrial_mean_ppm", fixed(co2_pre, 2)}}},
        {"regional_winter", {{"name", t.regional_name}, {"global_amplification", t.regional_amplification}, {"top_down_k_per_uv_pct", t.regional_top_down_k_per_uv_pct},
                             {"uv_band", r.bands[uv_band].key}}},
        {"future", {{"grand_minimum", {{"first_cycle", t.gm_first_cycle}, {"cycles", t.gm_cycles}, {"amplitude", t.gm_amplitude}}}, {"rows", future_rows},
                    {"largest_global_difference_k", tidy(largest, 3)}, {"largest_difference_year", largest_year}}},
        {"dynamo", dynamo},
        {"case_study", t.case_epoch.empty() ? Json(nullptr) : Json{{"epoch", t.case_epoch}, {"from_year", t.case_from}, {"to_year", t.case_to}}},
        {"limitations", Json::array({
            "before the cycle table the cycles are synthesised around a catalogue of epochs (a summary of the literature, not a reconstruction): their dates and amplitudes are illustrative",
            "the slow brightening of the quiet Sun is uncertain: low, central and high TSI scenarios span it",
            "the cosmogenic production is a calibrated proxy, not cosmic-ray transport; no geomagnetic-field change",
            "volcanic and CO2 forcings are rounded comparison forcings; no aerosols besides volcanoes, no land use, no orbital forcing",
            "the regional winter response is an illustrative parametrisation of the top-down pathway, not a circulation model",
            "the dynamo map is a conceptual model calibrated to grand-minimum statistics; it makes grand maxima too short and too rare"})},
    };
}

void print_usage(const char* executable) { std::cerr << "Usage: " << executable << " <run-request.json> <output-timeline.json>\n"; }

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) { print_usage(argv[0]); return 2; }
    try {
        std::ifstream request_file(argv[1]);
        if (!request_file) throw std::runtime_error(std::string("cannot open run request: ") + argv[1]);
        Json request;
        try { request = Json::parse(request_file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid run request JSON: ") + error.what()); }
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.solar.cycle-run/v1") throw std::invalid_argument("schema_version must be terraflux.solar.cycle-run/v1");
        const Request r = parse_request(request);

        // ---- The record ----------------------------------------------------------------------------------------
        const int issue_month = month_index_of(r.issue_year);
        MonthlySeries record;
        std::vector<SolarCycleCurve> table_curves;
        if (r.kind == "cycle_table") {
            table_curves = calibrate_cycle_table(r.table, r.first_lead_years, r.calibrate_widths);
            const int first = month_index_of(r.start_year);
            if (issue_month - first < 12 * 40) throw std::invalid_argument("the record (start_year to issue_year) must span at least 40 years");
            if (r.issue_year > r.table.back().minimum_year + 14.0) throw std::invalid_argument("forecast.issue_year must lie within 14 years of the last table minimum (the record is synthesised from the table)");
            record = synthesize_sunspot_record(table_curves, first, static_cast<std::size_t>(issue_month - first), r.noise);
        } else {
            record.first_month = r.first_month;
            const int available = static_cast<int>(r.monthly.size());
            const int months = std::min(available, issue_month - r.first_month);
            if (months < 12 * 40) throw std::invalid_argument("the record before forecast.issue_year must span at least 40 years");
            if (issue_month - r.first_month > available + 1) throw std::invalid_argument("forecast.issue_year must not lie more than a month after the end of the record");
            record.values.assign(r.monthly.begin(), r.monthly.begin() + months);
        }
        const std::size_t months = record.values.size();
        const SolarCycleForecaster forecaster(record, r.first_cycle_number, r.settings);
        const auto& cycles = forecaster.cycles();
        if (r.kind == "cycle_table") {
            // The numbering starts at the first detected minimum: it must be the table's cycle first_cycle_number.
            const auto entry = std::find_if(r.table.begin(), r.table.end(), [&](const SolarCycleTableEntry& e) { return e.number == r.first_cycle_number; });
            if (cycles.empty() || std::abs(cycles.front().minimum_year - entry->minimum_year) > 2.5)
                throw std::invalid_argument("the first minimum detected in the synthetic record is not the table minimum of cycle first_cycle_number: adjust start_year");
        }

        // ---- Backtest, error model, analysis and forecast -----------------------------------------------------
        auto backtest = forecaster.backtest(r.phases);
        const SolarErrorModel errors = SolarCycleForecaster::error_model(backtest, r.phases);
        SolarCycleForecaster::apply_error_model(backtest, errors);
        const auto summary = SolarCycleForecaster::summarize(backtest, r.phases);
        const SolarCycleAnalysis analysis = forecaster.analyse(r.issue_year, &errors);
        const SolarCycleForecast forecast = forecaster.forecast(analysis, r.horizon_year, r.members, r.seed);
        const std::size_t lead = static_cast<std::size_t>(issue_month - forecast.first_month);   // forecast months before the issue
        const std::size_t ahead = forecast.months - lead;

        // Record model: the fitted curves of the analysis (complete cycles and the cycle in progress).
        std::vector<SolarCycleCurve> fitted;
        std::vector<int> fitted_numbers;
        for (std::size_t k = 0; k < analysis.fits.size(); ++k) { fitted.push_back(analysis.fits[k].curve); fitted_numbers.push_back(analysis.cycles[k].number); }
        std::vector<double> model(months);
        for (std::size_t k = 0; k < months; ++k) model[k] = cycles_value(fitted, record.year_at(k));

        // ---- Irradiance, UV, flares and climate over the record and the forecast ---------------------------------
        // Reference TSI: the mean over the complete cycles of the record (the forcing averages to zero over them).
        const auto& complete_first = cycles.front();
        std::size_t complete_end = complete_first.minimum_index;
        for (const auto& c : cycles) if (c.complete) complete_end = c.end_index;
        double reference_tsi = 0.0;
        for (std::size_t k = complete_first.minimum_index; k < complete_end; ++k) reference_tsi += solar_irradiance(record.values[k], r.proxies, r.bands).tsi_w_m2;
        reference_tsi /= static_cast<double>(std::max<std::size_t>(1, complete_end - complete_first.minimum_index));
        const Derived rec = derive(record.values, r, reference_tsi);
        // The energy balance over the record.
        std::vector<double> surface(months), deep(months);
        SolarClimateState state;
        for (std::size_t k = 0; k < months; ++k) {
            state = step_solar_climate(state, rec.forcing[k], 1.0 / 12.0, r.climate);
            surface[k] = state.surface_k;
            deep[k] = state.deep_k;
        }
        const SolarClimateState issue_state = state;

        // Synthetic flares of the record (Poisson months, power-law sizes).
        NormalSequence flare_random(r.flare_seed);
        std::vector<std::vector<Flare>> record_flares(months);
        std::vector<double> m1_count(months), x1_count(months), largest(months);
        for (std::size_t k = 0; k < months; ++k) {
            const int n = sample_poisson(rec.flare_m1[k], flare_random);
            double big = 0.0, x = 0.0;
            for (int i = 0; i < n; ++i) {
                const double s = sample_flare_peak_flux(flare_random.uniform(), kFlareM1, r.flares);
                record_flares[k].push_back({s});
                big = std::max(big, s);
                if (s >= kFlareX1) x += 1.0;
            }
            m1_count[k] = n;
            x1_count[k] = x;
            largest[k] = n ? big : kNaN;
        }

        // Forecast percentiles (from the issue month): monotone quantities follow the sunspot percentiles.
        auto column = [&](std::size_t k) { std::vector<double> v; for (const auto& m : forecast.members) v.push_back(m[lead + k]); return v; };
        std::map<std::string, std::vector<double>> q;   // sunspot percentiles
        for (const auto& [name, p] : std::initializer_list<std::pair<const char*, double>>{{"p5", 5.0}, {"p25", 25.0}, {"p50", 50.0}, {"p75", 75.0}, {"p95", 95.0}})
            for (std::size_t k = 0; k < ahead; ++k) q[name].push_back(solar_percentile(column(k), p));
        std::vector<double> central(forecast.central.begin() + static_cast<std::ptrdiff_t>(lead), forecast.central.end());
        const Derived f5 = derive(q["p5"], r, reference_tsi), f50 = derive(q["p50"], r, reference_tsi), f95 = derive(q["p95"], r, reference_tsi);
        // The climate of every member, continued from the record's state at the issue date.
        std::vector<std::vector<double>> member_surface;
        for (const auto& m : forecast.members) {
            SolarClimateState s = issue_state;
            std::vector<double> out;
            for (std::size_t k = lead; k < forecast.months; ++k) {
                s = step_solar_climate(s, solar_radiative_forcing_w_m2(solar_irradiance(m[k], r.proxies, r.bands).tsi_w_m2 - reference_tsi, r.climate), 1.0 / 12.0, r.climate);
                out.push_back(s.surface_k);
            }
            member_surface.push_back(out);
        }
        std::vector<double> surface_p5, surface_p50, surface_p95;
        for (std::size_t k = 0; k < ahead; ++k) {
            std::vector<double> v;
            for (const auto& m : member_surface) v.push_back(m[k]);
            surface_p5.push_back(solar_percentile(v, 5.0));
            surface_p50.push_back(solar_percentile(v, 50.0));
            surface_p95.push_back(solar_percentile(v, 95.0));
        }
        // Annual flare outlook per member, from the first full calendar year after the issue date.
        Json annual = Json::array();
        for (int year = (issue_month + 11) / 12; year * 12 <= issue_month + static_cast<int>(ahead) - 12; ++year) {
            std::vector<double> sn_mean, x1, m1, p_x10, p_x1_month;
            for (const auto& m : forecast.members) {
                double s = 0.0, a = 0.0, b = 0.0, c = 0.0;
                for (int mo = 0; mo < 12; ++mo) {
                    const int idx = year * 12 + mo - forecast.first_month;
                    const double v = m[static_cast<std::size_t>(idx)];
                    s += v / 12.0;
                    a += flare_rate_per_year(v, kFlareX1, r.flares) / 12.0;
                    b += flare_rate_per_year(v, kFlareM1, r.flares) / 12.0;
                    c += flare_rate_per_year(v, kFlareX10, r.flares) / 12.0;
                }
                sn_mean.push_back(s);
                x1.push_back(a);
                m1.push_back(b);
                p_x10.push_back(flare_probability(c, 1.0));
            }
            annual.push_back({{"year", year}, {"sunspot_p50", fixed_number(solar_percentile(sn_mean, 50.0), 1)}, {"sunspot_p5", fixed_number(solar_percentile(sn_mean, 5.0), 1)},
                              {"sunspot_p95", fixed_number(solar_percentile(sn_mean, 95.0), 1)}, {"x1_expected_p50", number(solar_percentile(x1, 50.0), 3)},
                              {"x1_expected_p5", number(solar_percentile(x1, 5.0), 3)}, {"x1_expected_p95", number(solar_percentile(x1, 95.0), 3)},
                              {"m1_expected_p50", number(solar_percentile(m1, 50.0), 3)}, {"x10_probability_p50", number(solar_percentile(p_x10, 50.0), 3)},
                              {"x10_probability_p5", number(solar_percentile(p_x10, 5.0), 3)}, {"x10_probability_p95", number(solar_percentile(p_x10, 95.0), 3)}});
        }

        // ---- JSON: record -------------------------------------------------------------------------------------------
        auto bands_json = [&](const Derived& d, int digits) {
            Json out = Json::object();
            for (std::size_t b = 0; b < r.bands.size(); ++b) out[r.bands[b].key] = tidy_array(d.band_pct[b], digits);
            return out;
        };
        Json record_json = {
            {"first_month", month_label(record.first_month)}, {"first_year", fixed(record.year_at(0), 4)}, {"months", months},
            {"sunspot_number", fixed_array(record.values, 1)}, {"smoothed", fixed_array(analysis.smoothed.values, 1)},
            {"model", fixed_array(model, 1)},
            {"f107_sfu", fixed_array(rec.f107, 1)}, {"tsi_w_m2", fixed_array(rec.tsi, 3)}, {"facular_index", fixed_array(rec.facular, 3)},
            {"uv_change_pct", bands_json(rec, 4)}, {"ozone_change_pct", fixed_array(rec.ozone_pct, 3)}, {"stratosphere_k", fixed_array(rec.stratosphere_k, 3)},
            {"uv_index_change_pct", fixed_array(rec.uvi_change_pct, 3)},
            {"flares", {{"m1_expected", tidy_array(rec.flare_m1, 3)}, {"x1_expected", tidy_array(rec.flare_x1, 3)}, {"m1_count", m1_count}, {"x1_count", x1_count},
                        {"largest_w_m2", tidy_array(largest, 3)}}},
            {"forcing_w_m2", tidy_array(rec.forcing, 3)}, {"surface_k", tidy_array(surface, 3)}, {"deep_k", tidy_array(deep, 3)},
        };

        // Cycles: detected in the record (smoothed series), with the fits and, for a table, the input.
        Json cycles_json = Json::array();
        for (std::size_t k = 0; k < cycles.size(); ++k) {
            const auto& c = cycles[k];
            Json row = {{"number", c.number}, {"minimum_year", fixed(c.minimum_year, 3)}, {"minimum", fixed(c.minimum_value, 1)}, {"complete", c.complete},
                        {"maximum_year", c.has_maximum ? fixed_number(c.maximum_year, 3) : Json(nullptr)}, {"maximum", c.has_maximum ? fixed_number(c.maximum_value, 1) : Json(nullptr)},
                        {"end_year", c.complete ? fixed_number(c.end_year, 3) : Json(nullptr)}, {"length_years", fixed_number(c.length_years, 2)},
                        {"rise_years", fixed_number(c.rise_years, 2)}, {"decline_years", fixed_number(c.decline_years, 2)}};
            if (k < analysis.fits.size() && analysis.cycles[k].minimum_index == c.minimum_index) {
                const auto& f = analysis.fits[k].curve;
                row["fit"] = {{"onset_year", fixed(f.onset_year, 3)}, {"amplitude", fixed(f.amplitude, 1)}, {"width_scale", fixed(f.width_scale, 3)},
                              {"peak_year", fixed(f.peak_year(), 3)}, {"rms", fixed(analysis.fits[k].rms, 2)}, {"in_progress", k + 1 == analysis.fits.size()}};
            }
            if (r.kind == "cycle_table") {
                const auto e = std::find_if(r.table.begin(), r.table.end(), [&](const SolarCycleTableEntry& x) { return x.number == c.number; });
                if (e != r.table.end())
                    row["table"] = {{"minimum_year", e->minimum_year}, {"maximum_year", std::isfinite(e->maximum_year) ? Json(e->maximum_year) : Json(nullptr)}, {"maximum", e->maximum}};
            }
            cycles_json.push_back(row);
        }
        const auto& st = analysis.statistics;
        Json statistics = {{"complete_cycles", st.cycles}, {"mean_period_years", fixed(st.mean_period_years, 3)}, {"sd_period_years", fixed(st.sd_period_years, 3)},
                           {"mean_length_years", fixed(st.mean_length_years, 3)}, {"sd_length_years", fixed(st.sd_length_years, 3)},
                           {"mean_amplitude", fixed(st.mean_amplitude, 1)}, {"sd_amplitude", fixed(st.sd_amplitude, 1)}, {"amplitude_autocorrelation", fixed(st.amplitude_autocorrelation, 3)},
                           {"mean_lead_years", fixed(st.mean_lead_years, 3)}, {"sd_lead_years", fixed(st.sd_lead_years, 3)},
                           {"mean_width_scale", fixed(st.mean_width_scale, 3)}, {"sd_width_scale", fixed(st.sd_width_scale, 3)},
                           {"waldmeier_correlation", fixed_number(st.waldmeier_correlation, 3)}, {"residual_sd", fixed(st.residual_sd, 2)},
                           {"residual_autocorrelation", fixed(st.residual_autocorrelation, 3)}};

        // Backtest.
        Json entries = Json::array();
        for (const auto& e : backtest)
            entries.push_back({{"cycle", e.cycle}, {"phase_years", e.phase_years}, {"issue_year", fixed(e.issue_year, 3)}, {"minimum_confirmed", e.minimum_confirmed},
                               {"observed_maximum", fixed(e.observed_maximum, 1)}, {"observed_maximum_year", fixed(e.observed_maximum_year, 3)},
                               {"fit_maximum", fixed_number(e.fit_maximum, 1)}, {"prior_maximum", fixed(e.prior_maximum, 1)}, {"combined_maximum", fixed(e.combined_maximum, 1)},
                               {"predicted_maximum_year", fixed(e.predicted_maximum_year, 3)}, {"observed_minimum_year", fixed(e.observed_minimum_year, 3)},
                               {"predicted_minimum_year", fixed_number(e.predicted_minimum_year, 3)}});
        Json summaries = Json::array();
        for (const auto& s : summary)
            summaries.push_back({{"phase_years", s.phase_years}, {"forecasts", s.forecasts}, {"fitted", s.fitted}, {"fit_mae", fixed_number(s.fit_mae, 1)},
                                 {"prior_mae", fixed_number(s.prior_mae, 1)}, {"combined_mae", fixed_number(s.combined_mae, 1)}, {"fit_rms_fraction", fixed_number(s.fit_rms_fraction, 3)},
                                 {"timing_mae_years", fixed_number(s.timing_mae_years, 2)}, {"minimum_mae_years", fixed_number(s.minimum_mae_years, 2)}});
        Json error_json = {{"phases_years", errors.phases_years}, {"amplitude_error_fraction", fixed_array(errors.amplitude_error_fraction, 4)},
                           {"peak_timing_error_years", fixed_array(errors.peak_timing_error_years, 3)}};

        // Forecast.
        Json turning = Json::array();
        for (const auto& t : forecast.turning_points)
            turning.push_back({{"kind", t.minimum ? "minimum" : "maximum"}, {"cycle", t.cycle}, {"year_median", fixed_number(t.year_median, 3)}, {"year_p5", fixed_number(t.year_p5, 3)},
                               {"year_p95", fixed_number(t.year_p95, 3)}, {"value_median", fixed_number(t.value_median, 1)}, {"value_p5", fixed_number(t.value_p5, 1)},
                               {"value_p95", fixed_number(t.value_p95, 1)}, {"central_year", fixed_number(t.central_year, 3)}, {"central_value", fixed_number(t.central_value, 1)},
                               {"beyond_horizon", t.beyond_horizon}});
        auto triple = [&](const std::vector<double>& a, const std::vector<double>& b, const std::vector<double>& c, int digits) {
            return Json{{"p5", tidy_array(a, digits)}, {"p50", tidy_array(b, digits)}, {"p95", tidy_array(c, digits)}};
        };
        Json band_forecast = Json::object();
        for (std::size_t b = 0; b < r.bands.size(); ++b) band_forecast[r.bands[b].key] = triple(f5.band_pct[b], f50.band_pct[b], f95.band_pct[b], 3);
        std::vector<double> px5, px50, px95;
        for (std::size_t k = 0; k < ahead; ++k) {
            px5.push_back(flare_probability(f5.flare_x1[k], 1.0));
            px50.push_back(flare_probability(f50.flare_x1[k], 1.0));
            px95.push_back(flare_probability(f95.flare_x1[k], 1.0));
        }
        Json current = {{"cycle", analysis.current_cycle}, {"minimum_year", fixed(analysis.cycles.back().minimum_year, 3)}, {"phase_years", fixed(analysis.phase_years, 3)},
                        {"maximum_observed", analysis.cycles.back().has_maximum}, {"onset_prior_year", fixed(analysis.onset_prior_year, 3)},
                        {"onset_prior_sigma_years", fixed(analysis.onset_prior_sigma, 3)}, {"onset_year", fixed(analysis.onset_year, 3)}, {"onset_sigma_years", fixed(analysis.onset_sigma, 3)},
                        {"fit_amplitude", fixed(analysis.fit_amplitude, 1)}, {"fit_sigma", fixed_number(analysis.fit_sigma, 1)}, {"width_fitted", analysis.width_fitted},
                        {"prior_amplitude", fixed(analysis.prior_amplitude, 1)}, {"prior_sigma", fixed(analysis.prior_sigma, 1)},
                        {"amplitude", fixed(analysis.amplitude, 1)}, {"amplitude_sigma", fixed(analysis.amplitude_sigma, 1)},
                        {"width_scale", fixed(analysis.width_scale, 3)}, {"width_sigma", fixed(analysis.width_sigma, 3)}};
        Json central_cycles = Json::array();
        for (std::size_t k = 0; k < forecast.central_curves.size(); ++k) {
            const auto& c = forecast.central_curves[k];
            if (forecast.central_cycle_numbers[k] < analysis.current_cycle) continue;
            central_cycles.push_back({{"cycle", forecast.central_cycle_numbers[k]}, {"onset_year", fixed(c.onset_year, 3)}, {"amplitude", fixed(c.amplitude, 1)},
                                      {"width_scale", fixed(c.width_scale, 3)}, {"peak_year", fixed(c.peak_year(), 3)}});
        }
        Json forecast_json = {
            {"issue_year", r.issue_year}, {"issue_month", month_label(issue_month)}, {"first_month", month_label(issue_month)}, {"first_year", fixed(month_mid_year(issue_month), 4)},
            {"months", ahead}, {"horizon_year", r.horizon_year}, {"members", r.members}, {"seed", r.seed},
            {"sunspot", {{"p5", fixed_array(q["p5"], 1)}, {"p25", fixed_array(q["p25"], 1)}, {"p50", fixed_array(q["p50"], 1)}, {"p75", fixed_array(q["p75"], 1)},
                         {"p95", fixed_array(q["p95"], 1)}, {"central", fixed_array(central, 1)}}},
            {"f107_sfu", triple(f5.f107, f50.f107, f95.f107, 4)}, {"tsi_w_m2", {{"p5", fixed_array(f5.tsi, 3)}, {"p50", fixed_array(f50.tsi, 3)}, {"p95", fixed_array(f95.tsi, 3)}}},
            {"uv_change_pct", band_forecast}, {"ozone_change_pct", triple(f5.ozone_pct, f50.ozone_pct, f95.ozone_pct, 3)},
            {"stratosphere_k", triple(f5.stratosphere_k, f50.stratosphere_k, f95.stratosphere_k, 3)},
            // The UV index falls where the ozone response wins: its 5th percentile belongs to the most active Sun.
            {"uv_index_change_pct", triple(f95.uvi_change_pct, f50.uvi_change_pct, f5.uvi_change_pct, 3)},
            {"flares", {{"m1_expected", triple(f5.flare_m1, f50.flare_m1, f95.flare_m1, 3)}, {"x1_expected", triple(f5.flare_x1, f50.flare_x1, f95.flare_x1, 3)},
                        {"x1_probability", triple(px5, px50, px95, 3)}}},
            {"surface_k", triple(surface_p5, surface_p50, surface_p95, 3)},
            {"turning_points", turning}, {"current_cycle", current}, {"central_cycles", central_cycles}, {"annual", annual},
        };

        // ---- Irradiance, UV and atmosphere summaries -----------------------------------------------------------
        const auto quiet = solar_irradiance(0.0, r.proxies, r.bands), reference = solar_irradiance(r.proxies.reference_sunspot_number, r.proxies, r.bands);
        Json bands_meta = Json::array();
        double uv_change_w = 0.0;
        for (std::size_t b = 0; b < r.bands.size(); ++b) {
            bands_meta.push_back({{"key", r.bands[b].key}, {"label", r.bands[b].label}, {"from_nm", r.bands[b].from_nm}, {"to_nm", r.bands[b].to_nm}, {"quiet_w_m2", r.bands[b].quiet_w_m2},
                                  {"variability", r.bands[b].variability}, {"reference_w_m2", tidy(reference.band_w_m2[b], 5)}});
        }
        // The UV share of the TSI change: non-overlapping bands that tile 100-400 nm.
        for (const char* key : {"uv_c", "uv_b", "uv_a"})
            for (std::size_t b = 0; b < r.bands.size(); ++b)
                if (r.bands[b].key == key) uv_change_w += reference.band_w_m2[b] - quiet.band_w_m2[b];
        const double tsi_change = reference.tsi_w_m2 - quiet.tsi_w_m2;
        Json sites_json = Json::array();
        for (const auto& s : r.sites) {
            Json quiet_months = Json::array(), ozone_months = Json::array(), reference_months = Json::array();
            for (int m = 0; m < 12; ++m) {
                const auto u = solar_site_uv_index(s, mid_month_day(m), 1.0, r.atmosphere);
                quiet_months.push_back(fixed(u.quiet_uv_index, 2));
                reference_months.push_back(fixed(u.uv_index, 2));
                ozone_months.push_back(fixed(u.ozone_du / (1.0 + r.atmosphere.ozone_change_at_reference), 1));
            }
            sites_json.push_back({{"name", s.name}, {"latitude_deg", s.latitude_deg}, {"ozone_du", s.ozone_du}, {"ozone_seasonal_amplitude_du", s.ozone_seasonal_amplitude_du},
                                  {"ozone_peak_day", s.ozone_peak_day}, {"quiet_noon_uv_index_by_month", quiet_months}, {"reference_noon_uv_index_by_month", reference_months},
                                  {"ozone_du_by_month", ozone_months}});
        }
        const double reference_uvi_change = (1.0 + r.atmosphere.erythemal_variability) * std::pow(1.0 + r.atmosphere.ozone_change_at_reference, -r.atmosphere.radiation_amplification_factor) - 1.0;
        Json irradiance_json = {
            {"proxies", {{"f107_base_sfu", r.proxies.f107_base_sfu}, {"f107_per_sunspot", r.proxies.f107_per_sunspot}, {"f107_per_sunspot_squared", r.proxies.f107_per_sunspot_squared},
                         {"mgii_quiet", r.proxies.mgii_quiet}, {"mgii_per_sfu", r.proxies.mgii_per_sfu}, {"reference_sunspot_number", r.proxies.reference_sunspot_number},
                         {"tsi_quiet_w_m2", r.proxies.tsi_quiet_w_m2}, {"facular_brightening_w_m2", r.proxies.facular_brightening_w_m2},
                         {"spot_darkening_w_m2_per_sunspot", r.proxies.spot_darkening_w_m2_per_sunspot}}},
            {"bands", bands_meta},
            {"reference", {{"sunspot_number", r.proxies.reference_sunspot_number}, {"tsi_change_w_m2", tidy(tsi_change, 4)}, {"facular_w_m2", tidy(reference.facular_w_m2, 4)},
                           {"spot_w_m2", tidy(reference.spot_w_m2, 4)}, {"uv_change_w_m2", tidy(uv_change_w, 4)}, {"uv_share_of_tsi_change", tsi_change > 0.0 ? Json(tidy(uv_change_w / tsi_change, 3)) : Json(nullptr)},
                           {"f107_sfu", tidy(reference.f107_sfu, 4)}, {"mgii", tidy(reference.mgii, 5)}}},
            {"reference_tsi_w_m2", tidy(reference_tsi, 8)},
        };
        Json atmosphere_json = {
            {"ozone_change_at_reference", r.atmosphere.ozone_change_at_reference}, {"stratosphere_warming_at_reference_k", r.atmosphere.stratosphere_warming_at_reference_k},
            {"erythemal_variability", r.atmosphere.erythemal_variability}, {"radiation_amplification_factor", r.atmosphere.radiation_amplification_factor},
            {"uv_index_change_at_reference_pct", tidy(100.0 * reference_uvi_change, 4)}, {"sites", sites_json},
        };

        // ---- Flares ---------------------------------------------------------------------------------------------
        std::vector<std::pair<double, int>> all;
        for (std::size_t k = 0; k < months; ++k) for (const auto& fl : record_flares[k]) all.push_back({fl.peak_flux, record.first_month + static_cast<int>(k)});
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
        Json top = Json::array();
        for (std::size_t i = 0; i < std::min<std::size_t>(10, all.size()); ++i)
            top.push_back({{"month", month_label(all[i].second)}, {"peak_flux_w_m2", tidy(all[i].first, 3)}, {"class", flare_class_label(all[i].first)}, {"r_scale", radio_blackout_level(all[i].first)}});
        Json annual_record = Json::array();
        for (int year = record.first_month / 12; year * 12 + 11 < record.first_month + static_cast<int>(months); ++year) {
            if (year * 12 < record.first_month) continue;
            double sn = 0.0, ex = 0.0, em = 0.0, cx = 0.0, cm = 0.0;
            for (int mo = 0; mo < 12; ++mo) {
                const std::size_t k = static_cast<std::size_t>(year * 12 + mo - record.first_month);
                sn += record.values[k] / 12.0; ex += rec.flare_x1[k]; em += rec.flare_m1[k]; cx += x1_count[k]; cm += m1_count[k];
            }
            annual_record.push_back({{"year", year}, {"sunspot_number", fixed(sn, 1)}, {"x1_expected", tidy(ex, 3)}, {"m1_expected", tidy(em, 3)}, {"x1_count", cx}, {"m1_count", cm}});
        }
        Json rates = Json::array();
        for (double sn : {10.0, 50.0, 100.0, 150.0, 200.0, 250.0}) {
            rates.push_back({{"sunspot_number", sn}, {"c1_per_year", tidy(flare_rate_per_year(sn, kFlareC1, r.flares), 4)}, {"m1_per_year", tidy(flare_rate_per_year(sn, kFlareM1, r.flares), 4)},
                             {"m5_per_year", tidy(flare_rate_per_year(sn, kFlareM5, r.flares), 4)}, {"x1_per_year", tidy(flare_rate_per_year(sn, kFlareX1, r.flares), 4)},
                             {"x10_per_year", tidy(flare_rate_per_year(sn, kFlareX10, r.flares), 4)}, {"x20_per_year", tidy(flare_rate_per_year(sn, kFlareX20, r.flares), 4)},
                             {"x10_probability_per_year", tidy(flare_probability(flare_rate_per_year(sn, kFlareX10, r.flares), 1.0), 4)},
                             {"mean_irradiance_w_m2", tidy(mean_flare_irradiance_w_m2(sn, kFlareC1, r.flares), 3)}});
        }
        const double flare_power = mean_flare_irradiance_w_m2(r.proxies.reference_sunspot_number, kFlareC1, r.flares);
        const double au2 = 4.0 * kPi * 1.495978707e11 * 1.495978707e11;
        Json flares_json = {
            {"config", {{"x_rate_per_year", r.flares.x_rate_per_year}, {"reference_sunspot_number", r.flares.reference_sunspot_number}, {"rate_exponent", r.flares.rate_exponent},
                        {"size_index", r.flares.size_index}, {"max_peak_flux_w_m2", r.flares.max_peak_flux_w_m2}, {"energy_x1_j", r.flares.energy_x1_j},
                        {"energy_exponent", r.flares.energy_exponent}, {"seed", r.flare_seed}}},
            {"r_scale", Json::array({{{"level", 1}, {"threshold_w_m2", kFlareM1}, {"class", "M1"}}, {{"level", 2}, {"threshold_w_m2", kFlareM5}, {"class", "M5"}},
                                     {{"level", 3}, {"threshold_w_m2", kFlareX1}, {"class", "X1"}}, {{"level", 4}, {"threshold_w_m2", kFlareX10}, {"class", "X10"}},
                                     {{"level", 5}, {"threshold_w_m2", kFlareX20}, {"class", "X20"}}})},
            {"rates", rates},
            {"energy", {{"x1_j", tidy(flare_energy_j(kFlareX1, r.flares), 3)}, {"x10_j", tidy(flare_energy_j(kFlareX10, r.flares), 3)},
                        {"x10_fluence_at_earth_j_m2", tidy(flare_energy_j(kFlareX10, r.flares) / au2, 3)},
                        {"mean_irradiance_at_reference_w_m2", tidy(flare_power, 3)}, {"tsi_change_at_reference_w_m2", tidy(tsi_change, 4)},
                        {"ratio_to_tsi_change", tsi_change > 0.0 ? Json(tidy(flare_power / tsi_change, 3)) : Json(nullptr)}}},
            {"record_largest", top}, {"record_annual", annual_record},
        };

        // ---- Climate ----------------------------------------------------------------------------------------------
        // Peak-to-peak response of the last complete cycles: smoothed surface anomaly around maximum and minimum.
        const auto smooth_surface = smooth_13_month(surface);
        Json climate_cycles = Json::array();
        for (const auto& c : cycles) {
            if (!c.complete || !c.has_maximum) continue;
            double hi = -1e9, lo = 1e9;
            for (std::size_t k = c.minimum_index; k <= c.end_index && k < months; ++k) {
                if (!std::isfinite(smooth_surface[k])) continue;
                hi = std::max(hi, smooth_surface[k]);
                lo = std::min(lo, smooth_surface[k]);
            }
            climate_cycles.push_back({{"cycle", c.number}, {"peak_to_peak_k", tidy(hi - lo, 3)}, {"maximum", fixed(c.maximum_value, 1)}});
        }
        Json climate_json = {
            {"config", {{"albedo", r.climate.albedo}, {"upper_heat_capacity", r.climate.upper_heat_capacity}, {"deep_heat_capacity", r.climate.deep_heat_capacity},
                        {"feedback_w_m2_k", r.climate.feedback_w_m2_k}, {"exchange_w_m2_k", r.climate.exchange_w_m2_k}}},
            {"equilibrium_k_per_w_m2", tidy(1.0 / r.climate.feedback_w_m2_k, 4)},
            {"forcing_at_reference_w_m2", tidy(solar_radiative_forcing_w_m2(tsi_change, r.climate), 4)},
            {"cycles", climate_cycles},
        };

        // ---- Butterfly diagram ------------------------------------------------------------------------------------
        std::vector<double> latitudes;
        for (double l = -r.butterfly_max_latitude; l <= r.butterfly_max_latitude + 1e-9; l += r.butterfly_latitude_step) latitudes.push_back(fixed(l, 3));
        const double butterfly_start = r.butterfly_start_year > 0.0 ? std::max(r.butterfly_start_year, record.year_at(0)) : record.year_at(0);
        Json b_years = Json::array(), density = Json::array();
        // Past: the fitted curves; future: the central forecast.
        std::vector<SolarCycleCurve> future;
        for (std::size_t k = 0; k < forecast.central_curves.size(); ++k) if (forecast.central_cycle_numbers[k] >= analysis.current_cycle) future.push_back(forecast.central_curves[k]);
        std::vector<SolarCycleCurve> past(fitted.begin(), fitted.end() - 1);
        std::vector<SolarCycleCurve> all_curves = past;
        all_curves.insert(all_curves.end(), future.begin(), future.end());
        for (int m = month_index_of(butterfly_start); month_mid_year(m) <= r.horizon_year; m += r.butterfly_step_months) {
            const double y = month_mid_year(m);
            b_years.push_back(fixed(y, 3));
            density.push_back(tidy_array(butterfly_density(y, all_curves, latitudes, r.disk), 3));
        }
        Json butterfly_json = {{"years", b_years}, {"latitudes_deg", latitudes}, {"density_uhem_per_deg", density}, {"issue_year", r.issue_year},
                               {"drift", {{"onset_latitude_deg", r.disk.onset_latitude_deg}, {"efold_months", r.disk.drift_efold_months}}}};

        // ---- Disk frames -------------------------------------------------------------------------------------------
        const int first_frame = month_index_of(r.disk_first_year), last_frame = month_index_of(r.disk_last_year);
        if (first_frame < record.first_month || last_frame > forecast.first_month + static_cast<int>(forecast.months) - 1 || last_frame <= first_frame)
            throw std::invalid_argument("disk.first_year and disk.last_year must lie within the record and the forecast, in order");
        if ((last_frame - first_frame) / r.disk_every_months + 1 > 240) throw std::invalid_argument("the disk frames are limited to 240");
        if ((last_frame - first_frame) / r.disk_every_months + 1 < 2) throw std::invalid_argument("the disk needs at least two frames");
        const auto& dc = r.disk;
        const double cell_m = 2.0 * dc.extent_radii * dc.solar_radius_m / static_cast<double>(dc.cells);
        const Json grid_json = {{"nx", dc.cells}, {"ny", dc.cells}, {"origin_east_m", -dc.extent_radii * dc.solar_radius_m}, {"origin_north_m", -dc.extent_radii * dc.solar_radius_m},
                                {"spacing_east_m", cell_m}, {"spacing_north_m", cell_m}, {"geographic_anchor_longitude_deg", 0.0}, {"geographic_anchor_latitude_deg", 0.0},
                                {"coordinate_system", "local_azimuthal_equidistant_m"}, {"array_order", "row_major_y_then_x"}};
        const int start_year = first_frame / 12;
        const std::string start_time = std::to_string(start_year) + "-01-01T00:00:00Z";
        const auto start_days = terraflux::weather::detail::days_from_civil(start_year, 1, 1);
        NormalSequence disk_random(r.disk_seed);
        Json frames = Json::array(), disk_frames = Json::array();
        const std::size_t count = dc.cells * dc.cells;
        // The forecast/v1 fields are placeholders (there is no Earth grid here), written as integers to stay small.
        const std::vector<int> placeholder_air(count, 288), zeros(count, 0);
        std::vector<double> quiet_map;
        for (int m = first_frame; m <= last_frame; m += r.disk_every_months) {
            const bool observed = m < issue_month;
            const double year = month_mid_year(m);
            double sn = 0.0, smoothed = kNaN;
            std::vector<SolarCycleCurve> curves;
            std::vector<int> numbers;
            if (observed) {
                const std::size_t k = static_cast<std::size_t>(m - record.first_month);
                sn = record.values[k];
                smoothed = analysis.smoothed.values[k];
                curves = fitted;
                numbers = fitted_numbers;
            } else {
                const std::size_t k = static_cast<std::size_t>(m - forecast.first_month);
                sn = forecast.central[k];
                smoothed = sn;
                curves = forecast.central_curves;
                numbers = forecast.central_cycle_numbers;
            }
            const auto regions = sample_active_regions(year, sn, curves, numbers, disk_random, dc);
            const auto image = render_solar_disk(regions, dc);
            if (quiet_map.empty()) quiet_map = image.quiet_brightness_k;
            // The month's synthetic flares, located in the regions in proportion to their area.
            Json flares = Json::array();
            if (observed) {
                double total_area = 0.0;
                for (const auto& g : regions) total_area += g.area_uhem;
                for (const auto& fl : record_flares[static_cast<std::size_t>(m - record.first_month)]) {
                    int where = -1;
                    if (total_area > 0.0) {
                        double u = disk_random.uniform() * total_area;
                        for (where = 0; where + 1 < static_cast<int>(regions.size()) && u > regions[static_cast<std::size_t>(where)].area_uhem; ++where) u -= regions[static_cast<std::size_t>(where)].area_uhem;
                    }
                    flares.push_back({{"peak_flux_w_m2", tidy(fl.peak_flux, 3)}, {"class", flare_class_label(fl.peak_flux)}, {"r_scale", radio_blackout_level(fl.peak_flux)}, {"region", where}});
                }
            }
            Json regions_json = Json::array();
            for (const auto& g : regions)
                regions_json.push_back({{"latitude_deg", fixed(g.latitude_deg, 2)}, {"longitude_deg", fixed(g.longitude_deg, 2)}, {"area_uhem", fixed(g.area_uhem, 0)}, {"cycle", g.cycle}});
            const int y = m / 12, mo = m % 12 + 1;
            const double elapsed = static_cast<double>(terraflux::weather::detail::days_from_civil(y, static_cast<unsigned>(mo), 15) - start_days) * 86400.0;
            frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", start_time}, {"elapsed_seconds", elapsed}, {"grid", grid_json},
                              {"observation_updates", Json::array()},
                              {"fields", {{"air_temperature", {{"units", "K"}, {"values", placeholder_air}}}, {"wind_east", {{"units", "m/s"}, {"values", zeros}}},
                                          {"wind_north", {{"units", "m/s"}, {"values", zeros}}}}}});
            disk_frames.push_back({{"month", month_label(m)}, {"year", fixed(year, 4)}, {"elapsed_seconds", elapsed}, {"source", observed ? "record" : "forecast"},
                                   {"sunspot_number", fixed(sn, 1)}, {"smoothed", fixed_number(smoothed, 1)}, {"regions", regions_json}, {"flares", flares},
                                   {"spot_ppm", fixed(1e6 * image.spot_change, 0)}, {"facular_ppm", fixed(1e6 * image.facular_change, 0)}, {"net_ppm", fixed(1e6 * image.net_change, 0)},
                                   {"brightness_k", fixed_array(image.brightness_k, 0)}});
        }
        Json disk_json = {{"cells", dc.cells}, {"extent_radii", dc.extent_radii}, {"solar_radius_m", dc.solar_radius_m}, {"effective_temperature_k", dc.effective_temperature_k},
                          {"limb_darkening", dc.limb_darkening}, {"every_months", r.disk_every_months}, {"seed", r.disk_seed}, {"quiet_brightness_k", fixed_array(quiet_map, 0)},
                          {"frames", disk_frames}};

        // ---- Annual forcing export (record, then the forecast median) ------------------------------------------
        Json export_years = Json::array(), export_tsi = Json::array(), export_surface = Json::array(), export_source = Json::array();
        for (int year = record.first_month / 12; (year + 1) * 12 - 1 <= forecast.first_month + static_cast<int>(forecast.months) - 1; ++year) {
            if (year * 12 < record.first_month) continue;
            double tsi = 0.0, t = 0.0;
            int from_record = 0;
            for (int mo = 0; mo < 12; ++mo) {
                const int m = year * 12 + mo;
                if (m < issue_month) {
                    const std::size_t k = static_cast<std::size_t>(m - record.first_month);
                    tsi += rec.tsi[k]; t += surface[k]; ++from_record;
                } else {
                    const std::size_t k = static_cast<std::size_t>(m - issue_month);
                    tsi += f50.tsi[k]; t += surface_p50[k];
                }
            }
            export_years.push_back(year);
            export_tsi.push_back(fixed(tsi / 12.0, 3));
            export_surface.push_back(tidy(t / 12.0, 3));
            export_source.push_back(from_record == 12 ? "record" : from_record == 0 ? "forecast" : "mixed");
        }
        Json export_json = {{"schema_version", "terraflux.solar.forcing/v1"}, {"years", export_years}, {"tsi_w_m2", export_tsi}, {"surface_k", export_surface},
                            {"source", export_source}, {"reference_tsi_w_m2", tidy(reference_tsi, 8)},
                            {"note", "annual means: the record, then the forecast median (TSI) and the median of the members' energy balance (surface)"}};

        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"solar", {
                {"schema_version", "terraflux.solar.cycle/v1"},
                {"method", "hathaway_cycles_onset_prior_fit_ar1_ensemble_proxy_irradiance"},
                {"source_kind", "engine_output"},
                {"record_kind", r.kind},
                {"data_origin", r.data_origin},
                {"request", r.echo},
                {"record", record_json},
                {"cycles", cycles_json},
                {"statistics", statistics},
                {"backtest", {{"phases_years", r.phases}, {"entries", entries}, {"summary", summaries}, {"error_model", error_json},
                              {"note", "the error model that weights fit and prior comes from these same cycles: the combined column is partly in-sample"}}},
                {"forecast", forecast_json},
                {"irradiance", irradiance_json},
                {"atmosphere", atmosphere_json},
                {"flares", flares_json},
                {"climate", climate_json},
                {"butterfly", butterfly_json},
                {"disk", disk_json},
                {"forcing_export", export_json},
                {"limitations", Json::array({
                    "the sunspot record is synthesised from a cycle table unless a monthly record is given; synthetic months carry noise, not observations",
                    "single smooth curves per cycle: no double peaks, no hemispheric asymmetry; forecasts of cycles not yet started are climatology with a weak AR(1) memory (no polar-field precursor)",
                    "irradiance, UV, ozone and UV index follow linear proxies of the sunspot number; the UV variability differs by about a factor of two between instruments and reconstructions",
                    "flares are a stochastic rate model: the counts and the largest flares of the record are synthetic draws, not a flare list",
                    "the climate response is a two-layer global energy balance without the stratospheric (top-down) or ocean-dynamics pathways that observations suggest may amplify it",
                    "the disk shows an illustrative day: region positions, sizes and flares are random draws consistent with the cycle, not solar observations"})},
            }},
        };
        if (r.long_term.enabled) output["solar"]["long_term"] = build_long_term(r, record, table_curves, analysis, forecast, issue_month);
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error(std::string("cannot write output: ") + argv[2]);
        out << output.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
