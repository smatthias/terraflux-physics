// terraflux-run-earth-climate: which drivers change the climate most, what the change does, where CO2 comes from,
// goes to and hits hardest, and how the TerraFlux models are coupled.
//
// Reads a terraflux.earth.climate-run/v1 request:
//   years, climate         the period, the two-layer energy balance and its feedbacks
//   history, scenarios     concentrations and indices of the forcing agents (shared history, then scenarios)
//   agents                 the 2019 values and uncertainty ranges of the agents given by indices
//   impacts                land/Arctic amplification, heat extremes, heavy rain, sea ice, ocean chemistry, sea level
//   map, co2_map           the global grid (land mask), the CO2 sources and sinks and the impact hotspots
//   links                  optional outputs of the other TerraFlux runs: the solar cycle (solar and volcanic forcing),
//                          cosmic dust, the solar wind, polar melt (Greenland, overturning) and the coupled forecast
// and writes a viewer timeline (frames every `frame_every` years: the local warming of the central scenario as the
// air temperature) and an `earth_climate` block (terraflux.earth.climate/v1).
#include "terraflux/weather/earth_climate.hpp"
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/ocean_carbon.hpp"
#include "terraflux/weather/sea_level.hpp"
#include "terraflux/weather/solar_long_term.hpp"
#include "terraflux/weather/storm_systems.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kEarthRadius = 6.371e6;
constexpr double kYear = 365.25 * 86400.0;
constexpr double kGtCo2PerPpm = 7.82;            // 2.13 GtC per ppm
constexpr double kCo2MolarMass = 0.04401;

// ---- JSON helpers -------------------------------------------------------------------------------------------------

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) throw std::invalid_argument(std::string("run request is missing field: ") + key);
    return object.at(key);
}

double required_number(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number() || !std::isfinite(value.get<double>())) throw std::invalid_argument(std::string("run request field must be a finite number: ") + key);
    return value.get<double>();
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

long long optional_integer(const Json& object, const char* key, long long fallback, long long lo, long long hi) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_number_integer() || value.get<long long>() < lo || value.get<long long>() > hi)
        throw std::invalid_argument(std::string("run request field must be an integer within ") + std::to_string(lo) + "-" + std::to_string(hi) + ": " + key);
    return value.get<long long>();
}

void only_keys(const Json& object, const std::string& section, const std::set<std::string>& allowed) {
    if (!object.is_object()) throw std::invalid_argument("run request section must be an object: " + section);
    for (const auto& item : object.items())
        if (!allowed.count(item.key())) throw std::invalid_argument("unknown field in " + section + ": " + item.key());
}

void read_numbers(const Json& root, const char* section, std::initializer_list<std::pair<const char*, double*>> fields) {
    if (!root.contains(section)) return;
    const Json& s = root.at(section);
    std::set<std::string> allowed{"note"};
    for (const auto& f : fields) allowed.insert(f.first);
    only_keys(s, section, allowed);
    for (const auto& f : fields) *f.second = optional_number(s, f.first, *f.second);
}

double tidy(double v, int digits = 4) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*g", digits, v);
    return std::strtod(buffer, nullptr);
}

double fixed(double v, int decimals) {
    const double s = std::pow(10.0, decimals);
    const double r = std::round(v * s) / s;
    return r == 0.0 ? 0.0 : r;
}

Json number(double v, int digits = 4) { return std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr); }

Json tidy_array(const std::vector<double>& values, int digits = 4) {
    Json out = Json::array();
    for (double v : values) out.push_back(number(v, digits));
    return out;
}

Json fixed_array(const std::vector<double>& values, int decimals) {
    Json out = Json::array();
    for (double v : values) out.push_back(std::isfinite(v) ? Json(fixed(v, decimals)) : Json(nullptr));
    return out;
}

using Points = std::vector<std::array<double, 2>>;

Points parse_points(const Json& list, const std::string& label, double lo, double hi) {
    if (!list.is_array() || list.empty() || list.size() > 200) throw std::invalid_argument(label + " needs 1-200 [year, value] points");
    Points out;
    for (const Json& p : list) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) throw std::invalid_argument(label + " points are [year, value]");
        out.push_back({p[0].get<double>(), p[1].get<double>()});
        if (!(out.back()[1] >= lo && out.back()[1] <= hi)) throw std::invalid_argument(label + " values must lie within " + std::to_string(lo) + "-" + std::to_string(hi));
        if (out.size() > 1 && !(out.back()[0] > out[out.size() - 2][0])) throw std::invalid_argument(label + " years must increase");
    }
    return out;
}

// ---- The request --------------------------------------------------------------------------------------------------

// The agents. Series: CO2, CH4, N2O (concentrations); the others from indices times their 2019 value.
const std::vector<std::string> kSeries = {"co2_ppm", "ch4_ppb", "n2o_ppb", "halocarbon_w_m2", "ozone_index", "aerosol_index", "land_use_index", "aviation_index"};
struct SeriesRange { double lo, hi; };
const std::map<std::string, SeriesRange> kSeriesRange = {{"co2_ppm", {150.0, 3000.0}}, {"ch4_ppb", {300.0, 10000.0}}, {"n2o_ppb", {150.0, 1000.0}}, {"halocarbon_w_m2", {0.0, 3.0}},
                                                         {"ozone_index", {0.0, 5.0}}, {"aerosol_index", {0.0, 5.0}}, {"land_use_index", {0.0, 5.0}}, {"aviation_index", {0.0, 10.0}}};

struct Agent {
    std::string key, label, group;
    double value_2019{0.0};          // for index-based agents
    double low{1.0}, high{1.0};      // uncertainty factors of the forcing
};

struct Scenario {
    std::string key, name, polar_name;
    std::map<std::string, Points> series;   // history + scenario points
};

struct Region { std::string name; std::vector<std::array<double, 4>> boxes; double fossil{0.0}, cumulative{0.0}, land_use{0.0}; };
struct Box { std::string name; std::vector<std::array<double, 4>> boxes; double value{0.0}; };

struct Request {
    Json echo;
    int first{1850}, last{2100}, today{2024}, ref0{1850}, ref1{1900}, frame_every{10};
    TwoLayerClimate climate;
    FeedbackSet feedbacks;
    double ecs_low{2.5}, ecs_high{4.0};
    double f2x{3.71};
    std::map<std::string, Agent> agents;
    std::vector<Scenario> scenarios;
    std::string central{"ssp245"};
    // Impacts.
    WarmingPattern pattern;
    double heat_sigma_k{1.87}, heavy_rain_per_k{1.3};
    double ice_extent_ref{4.7}, ice_warming_ref{1.2}, ice_loss_per_k{2.9};
    double sst_c{18.0}, polar_sst_c{2.0}, tropical_sst_c{28.5}, alkalinity{2300.0}, ocean_factor{0.8}, observed_rise_m{0.2};
    SeaLevelBudgetConfig sea_level;
    double greenland_m_yr_per_k{0.0006}, greenland_threshold_k{0.8};
    // Map.
    std::size_t nx{90}, ny{45};
    std::vector<unsigned char> land;
    std::string land_source;
    std::vector<Box> aerosol_regions;
    // CO2 map.
    std::vector<Region> regions;
    std::vector<Box> land_use, deserts, ice, upwelling, hotspots;
    double land_sink{11.6}, ocean_sink{10.6}, fossil_total{37.0}, land_use_total{4.1}, cumulative_total{2500.0};
    Points wind_profile{{0.0, 5.0}, {10.0, 6.5}, {20.0, 7.5}, {30.0, 6.5}, {45.0, 9.5}, {55.0, 10.5}, {65.0, 9.0}, {80.0, 7.0}};
    Points dpco2_profile{{0.0, -30.0}, {15.0, -10.0}, {30.0, 5.0}, {45.0, 25.0}, {55.0, 35.0}, {65.0, 30.0}, {80.0, 15.0}};   // air - sea (uatm)
    Points sst_profile{{0.0, 27.5}, {20.0, 26.0}, {30.0, 21.0}, {40.0, 15.0}, {50.0, 9.0}, {60.0, 4.0}, {70.0, 0.0}, {80.0, -1.5}};
    double tcre_k_per_1000gt{0.45};
    double transport_diffusivity{1.5e6}, transport_relaxation_days{60.0}, transport_mixing_fraction{0.25};
    // Links.
    std::string solar_path, dust_path, wind_path, polar_path, forecast_path;
};

std::vector<std::array<double, 4>> parse_boxes(const Json& list, const std::string& label) {
    if (!list.is_array() || list.empty() || list.size() > 20) throw std::invalid_argument(label + " needs 1-20 boxes [south, north, west, east]");
    std::vector<std::array<double, 4>> out;
    for (const Json& b : list) {
        if (!b.is_array() || b.size() != 4 || !std::all_of(b.begin(), b.end(), [](const Json& v) { return v.is_number(); }))
            throw std::invalid_argument(label + " boxes are [south, north, west, east] in degrees");
        std::array<double, 4> x{b[0].get<double>(), b[1].get<double>(), b[2].get<double>(), b[3].get<double>()};
        if (!(x[0] < x[1] && x[0] >= -90.0 && x[1] <= 90.0 && x[2] >= -180.0 && x[3] <= 180.0 && x[2] != x[3]))
            throw std::invalid_argument(label + " boxes need south < north within +-90 and longitudes within +-180");
        out.push_back(x);
    }
    return out;
}

std::vector<Box> parse_box_list(const Json& j, const char* key, double lo, double hi) {
    std::vector<Box> out;
    if (!j.contains(key)) return out;
    const Json& list = j.at(key);
    if (!list.is_array() || list.size() > 40) throw std::invalid_argument(std::string(key) + " must list at most 40 entries");
    for (const Json& x : list) {
        only_keys(x, key, {"name", "boxes", "value", "note"});
        Box b{optional_string(x, "name", "area"), parse_boxes(required(x, "boxes"), key), optional_number(x, "value", 1.0)};
        if (!(b.value >= lo && b.value <= hi)) throw std::invalid_argument(std::string(key) + " values must lie within " + std::to_string(lo) + "-" + std::to_string(hi));
        out.push_back(b);
    }
    return out;
}

bool in_box(const std::array<double, 4>& b, double lat, double lon) {
    if (lat < b[0] || lat > b[1]) return false;
    return b[2] < b[3] ? lon >= b[2] && lon <= b[3] : lon >= b[2] || lon <= b[3];   // boxes may cross the date line
}
bool in_boxes(const std::vector<std::array<double, 4>>& boxes, double lat, double lon) {
    return std::any_of(boxes.begin(), boxes.end(), [&](const auto& b) { return in_box(b, lat, lon); });
}

Request parse_request(const Json& j) {
    Request r;
    r.echo = j;
    only_keys(j, "request", {"schema_version", "years", "climate", "history", "scenarios", "central_scenario", "agents", "impacts", "map", "co2_map", "links"});
    if (j.contains("years")) {
        const Json& y = j.at("years");
        only_keys(y, "years", {"first", "last", "today", "reference_first", "reference_last", "frame_every", "note"});
        r.first = static_cast<int>(optional_integer(y, "first", r.first, 1750, 2000));
        r.last = static_cast<int>(optional_integer(y, "last", r.last, 2030, 2300));
        r.today = static_cast<int>(optional_integer(y, "today", r.today, 2000, 2030));
        r.ref0 = static_cast<int>(optional_integer(y, "reference_first", r.ref0, 1750, 2000));
        r.ref1 = static_cast<int>(optional_integer(y, "reference_last", r.ref1, 1750, 2000));
        r.frame_every = static_cast<int>(optional_integer(y, "frame_every", r.frame_every, 1, 50));
    }
    if (!(r.first <= r.ref0 && r.ref0 <= r.ref1 && r.ref1 < r.today && r.today < r.last && r.today >= 2020 && (r.last - r.first) / r.frame_every + 1 <= 120))
        throw std::invalid_argument("years: first <= reference_first <= reference_last < today (2020 or later) < last, at most 120 frames");
    if (j.contains("climate")) {
        const Json& c = j.at("climate");
        only_keys(c, "climate", {"upper_heat_capacity", "deep_heat_capacity", "exchange_w_m2_k", "doubling_forcing_w_m2", "feedbacks", "ecs_low_k", "ecs_high_k", "note"});
        r.climate.upper_heat_capacity = optional_number(c, "upper_heat_capacity", r.climate.upper_heat_capacity);
        r.climate.deep_heat_capacity = optional_number(c, "deep_heat_capacity", r.climate.deep_heat_capacity);
        r.climate.exchange_w_m2_k = optional_number(c, "exchange_w_m2_k", r.climate.exchange_w_m2_k);
        r.f2x = optional_number(c, "doubling_forcing_w_m2", r.f2x);
        r.ecs_low = optional_number(c, "ecs_low_k", r.ecs_low);
        r.ecs_high = optional_number(c, "ecs_high_k", r.ecs_high);
        read_numbers(c, "feedbacks", {{"planck", &r.feedbacks.planck_w_m2_k}, {"water_vapour_lapse_rate", &r.feedbacks.water_vapour_lapse_rate_w_m2_k},
                                      {"surface_albedo", &r.feedbacks.surface_albedo_w_m2_k}, {"clouds", &r.feedbacks.clouds_w_m2_k}, {"other", &r.feedbacks.other_w_m2_k}});
    }
    validate_feedbacks(r.feedbacks);
    r.climate.feedback_w_m2_k = net_feedback_w_m2_k(r.feedbacks);
    if (!(r.climate.upper_heat_capacity > 1.0 && r.climate.upper_heat_capacity < 50.0 && r.climate.deep_heat_capacity > 10.0 && r.climate.deep_heat_capacity < 1000.0 &&
          r.climate.exchange_w_m2_k >= 0.0 && r.climate.exchange_w_m2_k <= 3.0 && r.f2x > 2.0 && r.f2x < 6.0 && r.ecs_low > 1.0 && r.ecs_low < r.ecs_high && r.ecs_high < 8.0))
        throw std::invalid_argument("climate: heat capacities 1-50 and 10-1000 W yr/m2/K, exchange 0-3, doubling forcing 2-6 W/m2, 1 < ecs_low_k < ecs_high_k < 8");

    // Agents given by indices (2019 value) and the uncertainty factors of every agent.
    const std::vector<Agent> defaults = {
        {"co2", "CO2", "greenhouse gases", 0.0, 0.88, 1.12}, {"ch4", "Methane", "greenhouse gases", 0.0, 0.8, 1.2}, {"n2o", "Nitrous oxide", "greenhouse gases", 0.0, 0.86, 1.14},
        {"halocarbons", "Halocarbons", "greenhouse gases", 1.0, 0.8, 1.2}, {"stratospheric_water", "Stratospheric water vapour (from CH4)", "greenhouse gases", 0.15, 0.0, 2.0},
        {"ozone", "Tropospheric ozone", "air pollution", 0.47, 0.5, 1.5}, {"aerosols", "Aerosols (direct and cloud effects)", "air pollution", -1.1, 0.36, 1.55},
        {"black_carbon_snow", "Black carbon on snow", "air pollution", 0.08, 0.0, 2.0}, {"land_use", "Land use (surface albedo)", "land", -0.20, 0.5, 1.5},
        {"contrails", "Aviation contrails", "transport", 0.057, 0.3, 1.7}, {"solar", "The Sun (irradiance)", "natural", 0.0, 0.5, 1.5},
        {"volcanic", "Volcanic eruptions", "natural", 0.0, 0.75, 1.25}};
    for (const auto& a : defaults) r.agents[a.key] = a;
    if (j.contains("agents")) {
        const Json& list = j.at("agents");
        if (!list.is_object()) throw std::invalid_argument("agents must be an object keyed by agent");
        for (const auto& item : list.items()) {
            if (!r.agents.count(item.key())) throw std::invalid_argument("unknown agent: " + item.key());
            only_keys(item.value(), "agents." + item.key(), {"value_2019_w_m2", "low_factor", "high_factor", "note"});
            auto& a = r.agents[item.key()];
            a.value_2019 = optional_number(item.value(), "value_2019_w_m2", a.value_2019);
            a.low = optional_number(item.value(), "low_factor", a.low);
            a.high = optional_number(item.value(), "high_factor", a.high);
            if (!(a.low >= 0.0 && a.low <= a.high && a.high <= 5.0 && std::abs(a.value_2019) <= 5.0))
                throw std::invalid_argument("agents need 0 <= low_factor <= high_factor <= 5 and |value_2019_w_m2| <= 5");
        }
    }
    // Concentrations and indices.
    const Json& history = required(j, "history");
    only_keys(history, "history", {"co2_ppm", "ch4_ppb", "n2o_ppb", "halocarbon_w_m2", "ozone_index", "aerosol_index", "land_use_index", "aviation_index", "note"});
    std::map<std::string, Points> hist;
    for (const auto& k : kSeries) hist[k] = parse_points(required(history, k.c_str()), "history." + k, kSeriesRange.at(k).lo, kSeriesRange.at(k).hi);
    const Json& scenarios = required(j, "scenarios");
    if (!scenarios.is_array() || scenarios.empty() || scenarios.size() > 6) throw std::invalid_argument("scenarios must list 1-6 scenarios");
    std::set<std::string> keys;
    for (const Json& x : scenarios) {
        std::set<std::string> allowed{"key", "name", "polar_scenario", "note"};
        for (const auto& k : kSeries) allowed.insert(k);
        only_keys(x, "scenarios[]", allowed);
        Scenario s{optional_string(x, "key", "scenario"), optional_string(x, "name", "scenario"), x.contains("polar_scenario") ? optional_string(x, "polar_scenario", "") : "", {}};
        if (!keys.insert(s.key).second) throw std::invalid_argument("scenario keys must be unique");
        for (const auto& k : kSeries) {
            Points p = hist[k];
            if (x.contains(k)) {
                const Points future = parse_points(x.at(k), "scenario " + s.key + "." + k, kSeriesRange.at(k).lo, kSeriesRange.at(k).hi);
                for (const auto& q : future) if (q[0] > p.back()[0]) p.push_back(q);
            }
            s.series[k] = p;
        }
        r.scenarios.push_back(s);
    }
    r.central = optional_string(j, "central_scenario", r.scenarios.size() > 1 ? r.scenarios[1].key : r.scenarios[0].key);
    if (!keys.count(r.central)) throw std::invalid_argument("central_scenario must name one of the scenarios");

    if (j.contains("impacts")) {
        const Json& im = j.at("impacts");
        only_keys(im, "impacts", {"land_factor", "arctic_factor", "southern_ocean_factor", "north_atlantic_cooling_per_sv_k", "heat_sigma_k", "heavy_rain_factor_per_k",
                                  "sea_ice_extent_reference_mkm2", "sea_ice_reference_warming_k", "sea_ice_loss_per_k_mkm2", "ocean", "sea_level", "note"});
        r.pattern.land_factor = optional_number(im, "land_factor", r.pattern.land_factor);
        r.pattern.arctic_factor = optional_number(im, "arctic_factor", r.pattern.arctic_factor);
        r.pattern.southern_ocean_factor = optional_number(im, "southern_ocean_factor", r.pattern.southern_ocean_factor);
        r.pattern.north_atlantic_cooling_per_sv_k = optional_number(im, "north_atlantic_cooling_per_sv_k", r.pattern.north_atlantic_cooling_per_sv_k);
        r.heat_sigma_k = optional_number(im, "heat_sigma_k", r.heat_sigma_k);
        r.heavy_rain_per_k = optional_number(im, "heavy_rain_factor_per_k", r.heavy_rain_per_k);
        r.ice_extent_ref = optional_number(im, "sea_ice_extent_reference_mkm2", r.ice_extent_ref);
        r.ice_warming_ref = optional_number(im, "sea_ice_reference_warming_k", r.ice_warming_ref);
        r.ice_loss_per_k = optional_number(im, "sea_ice_loss_per_k_mkm2", r.ice_loss_per_k);
        read_numbers(im, "ocean", {{"surface_temperature_c", &r.sst_c}, {"polar_temperature_c", &r.polar_sst_c}, {"tropical_temperature_c", &r.tropical_sst_c},
                                   {"alkalinity_umol_kg", &r.alkalinity}, {"warming_factor", &r.ocean_factor}});
        auto& sl = r.sea_level;
        read_numbers(im, "sea_level", {{"thermal_expansion_m_per_k_yr", &sl.thermal_expansion_m_per_k_yr}, {"deep_ocean_time_scale_years", &sl.deep_ocean_time_scale_years},
                                       {"glacier_m_per_k_yr", &sl.glacier_m_per_k_yr}, {"glacier_store_m", &sl.glacier_store_m}, {"antarctic_base_m_yr", &sl.antarctic_base_m_yr},
                                       {"antarctic_m_per_k_yr", &sl.antarctic_m_per_k_yr}, {"antarctic_threshold_k", &sl.antarctic_threshold_k}, {"land_water_m_yr", &sl.land_water_m_yr},
                                       {"greenland_m_per_k_yr", &r.greenland_m_yr_per_k}, {"greenland_threshold_k", &r.greenland_threshold_k},
                                       {"observed_rise_1900_reference_m", &r.observed_rise_m}});
    }
    if (!(r.pattern.land_factor >= 1.0 && r.pattern.land_factor <= 2.5 && r.pattern.arctic_factor >= 1.0 && r.pattern.arctic_factor <= 5.0 && r.pattern.southern_ocean_factor > 0.0 &&
          r.pattern.southern_ocean_factor <= 1.5 && r.pattern.north_atlantic_cooling_per_sv_k >= 0.0 && r.pattern.north_atlantic_cooling_per_sv_k <= 1.0 && r.heat_sigma_k > 0.2 &&
          r.heat_sigma_k < 5.0 && r.heavy_rain_per_k >= 1.0 && r.heavy_rain_per_k <= 2.0 && r.alkalinity > 1500.0 && r.alkalinity < 3000.0 && r.ocean_factor > 0.3 && r.ocean_factor <= 1.5))
        throw std::invalid_argument("impacts: land_factor 1-2.5, arctic_factor 1-5, southern_ocean_factor 0-1.5, cooling per Sv 0-1 K, heat_sigma_k 0.2-5, heavy rain factor 1-2, alkalinity 1500-3000, ocean warming factor 0.3-1.5");
    r.sea_level.reference_year = 2020;
    validate_sea_level_budget(r.sea_level);

    const Json& mp = required(j, "map");
    only_keys(mp, "map", {"cells_east", "cells_north", "land_mask", "land_mask_source", "aerosol_regions", "note"});
    r.nx = static_cast<std::size_t>(optional_integer(mp, "cells_east", 90, 24, 360));
    r.ny = static_cast<std::size_t>(optional_integer(mp, "cells_north", 45, 12, 180));
    const Json& rows = required(mp, "land_mask");
    if (!rows.is_array() || rows.size() != r.ny) throw std::invalid_argument("map.land_mask needs one string per row (north first)");
    r.land.assign(r.nx * r.ny, 0);
    for (std::size_t row = 0; row < r.ny; ++row) {
        if (!rows[row].is_string() || rows[row].get_ref<const std::string&>().size() != r.nx) throw std::invalid_argument("map.land_mask rows need one character per cell");
        const std::string& line = rows[row].get_ref<const std::string&>();
        for (std::size_t col = 0; col < r.nx; ++col) {
            if (line[col] != '#' && line[col] != '.') throw std::invalid_argument("map.land_mask characters must be '#' (land) or '.' (sea)");
            r.land[(r.ny - 1 - row) * r.nx + col] = line[col] == '#';
        }
    }
    r.land_source = optional_string(mp, "land_mask_source", "unlabelled");
    r.aerosol_regions = parse_box_list(mp, "aerosol_regions", 0.0, 10.0);

    const Json& co = required(j, "co2_map");
    only_keys(co, "co2_map", {"regions", "land_use", "deserts", "ice", "upwelling", "hotspots", "sinks", "ocean", "transport", "tcre_k_per_1000_gt", "note"});
    const Json& regions = required(co, "regions");
    if (!regions.is_array() || regions.empty() || regions.size() > 60) throw std::invalid_argument("co2_map.regions must list 1-60 regions");
    for (const Json& x : regions) {
        only_keys(x, "co2_map.regions[]", {"name", "boxes", "fossil_gt_co2_yr", "cumulative_gt_co2", "land_use_gt_co2_yr", "note"});
        Region g{optional_string(x, "name", "region"), parse_boxes(required(x, "boxes"), "co2_map.regions"), optional_number(x, "fossil_gt_co2_yr", 0.0),
                 optional_number(x, "cumulative_gt_co2", 0.0), optional_number(x, "land_use_gt_co2_yr", 0.0)};
        if (!(g.fossil >= 0.0 && g.fossil <= 50.0 && g.cumulative >= 0.0 && g.cumulative <= 5000.0 && g.land_use >= 0.0 && g.land_use <= 10.0))
            throw std::invalid_argument("co2_map regions: fossil within 0-50 Gt/yr, cumulative within 0-5000 Gt, land use within 0-10 Gt/yr");
        r.regions.push_back(g);
    }
    r.land_use = parse_box_list(co, "land_use", 0.0, 10.0);
    r.deserts = parse_box_list(co, "deserts", 0.0, 1.0);
    r.ice = parse_box_list(co, "ice", 0.0, 1.0);
    r.upwelling = parse_box_list(co, "upwelling", -200.0, 200.0);
    r.hotspots = parse_box_list(co, "hotspots", 0.0, 1.0);
    read_numbers(co, "sinks", {{"land_gt_co2_yr", &r.land_sink}, {"ocean_gt_co2_yr", &r.ocean_sink}, {"fossil_total_gt_co2_yr", &r.fossil_total},
                               {"land_use_total_gt_co2_yr", &r.land_use_total}, {"cumulative_total_gt_co2", &r.cumulative_total}});
    if (co.contains("ocean")) {
        const Json& o = co.at("ocean");
        only_keys(o, "co2_map.ocean", {"wind_profile", "air_minus_sea_pco2_profile", "sst_profile", "note"});
        if (o.contains("wind_profile")) r.wind_profile = parse_points(o.at("wind_profile"), "co2_map.ocean.wind_profile", 0.0, 30.0);
        if (o.contains("air_minus_sea_pco2_profile")) r.dpco2_profile = parse_points(o.at("air_minus_sea_pco2_profile"), "co2_map.ocean.air_minus_sea_pco2_profile", -200.0, 200.0);
        if (o.contains("sst_profile")) r.sst_profile = parse_points(o.at("sst_profile"), "co2_map.ocean.sst_profile", -2.0, 35.0);
    }
    read_numbers(co, "transport", {{"diffusivity_m2_s", &r.transport_diffusivity}, {"relaxation_days", &r.transport_relaxation_days}, {"mixing_fraction", &r.transport_mixing_fraction}});
    r.tcre_k_per_1000gt = optional_number(co, "tcre_k_per_1000_gt", r.tcre_k_per_1000gt);
    if (!(r.land_sink >= 0.0 && r.ocean_sink >= 0.0 && r.fossil_total > 0.0 && r.land_use_total >= 0.0 && r.cumulative_total > 0.0 && r.transport_diffusivity > 0.0 &&
          r.transport_relaxation_days > 0.0 && r.transport_mixing_fraction > 0.0 && r.transport_mixing_fraction <= 1.0 && r.tcre_k_per_1000gt > 0.0 && r.tcre_k_per_1000gt < 2.0))
        throw std::invalid_argument("co2_map: non-negative sinks, positive totals, transport and TCRE within 0-2 K per 1000 Gt");
    if (j.contains("links")) {
        const Json& l = j.at("links");
        only_keys(l, "links", {"solar_cycle_path", "cosmic_dust_path", "solar_wind_path", "polar_melt_path", "forecast_path", "note"});
        r.solar_path = l.contains("solar_cycle_path") ? optional_string(l, "solar_cycle_path", "") : "";
        r.dust_path = l.contains("cosmic_dust_path") ? optional_string(l, "cosmic_dust_path", "") : "";
        r.wind_path = l.contains("solar_wind_path") ? optional_string(l, "solar_wind_path", "") : "";
        r.polar_path = l.contains("polar_melt_path") ? optional_string(l, "polar_melt_path", "") : "";
        r.forecast_path = l.contains("forecast_path") ? optional_string(l, "forecast_path", "") : "";
    }
    return r;
}

Json load_json(const std::string& path, const char* what) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument(std::string("cannot open the ") + what + ": " + path);
    try { return Json::parse(file); } catch (const Json::exception& e) { throw std::invalid_argument(std::string("invalid ") + what + " JSON " + path + ": " + e.what()); }
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
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.earth.climate-run/v1") throw std::invalid_argument("schema_version must be terraflux.earth.climate-run/v1");
        const Request r = parse_request(request);
        const int years = r.last - r.first + 1;
        auto year_at = [&](int k) { return r.first + k; };

        // ---- Links to the other runs ----------------------------------------------------------------------------------
        std::vector<double> solar(years, 0.0), volcanic(years, 0.0);
        Json links = Json::object();
        if (!r.solar_path.empty()) {
            const Json doc = load_json(r.solar_path, "solar-cycle timeline");
            if (!doc.contains("solar") || !doc["solar"].contains("long_term")) throw std::invalid_argument(r.solar_path + " has no solar.long_term (run the solar cycle with a long_term section)");
            const Json& lt = doc["solar"]["long_term"];
            const int start = lt.at("start_year").get<int>();
            const Json& sc = lt.at("series").at("forcing_w_m2").at("solar_central");
            const Json& vo = lt.at("series").at("forcing_w_m2").at("volcanic");
            for (int k = 0; k < years; ++k) {
                const int idx = year_at(k) - start;
                if (idx >= 0 && idx < static_cast<int>(sc.size())) { solar[k] = sc[static_cast<std::size_t>(idx)].get<double>(); volcanic[k] = vo[static_cast<std::size_t>(idx)].get<double>(); }
            }
            // No eruption is known for the future: after today the volcanic forcing is its pre-industrial mean (zero).
            for (int k = 0; k < years; ++k) if (year_at(k) > r.today) volcanic[k] = 0.0;
            links["solar_cycle"] = {{"path", r.solar_path}, {"solar_cycle_forcing_w_m2", doc["solar"]["climate"]["forcing_at_reference_w_m2"]},
                                    {"equilibrium_k_per_w_m2", doc["solar"]["climate"]["equilibrium_k_per_w_m2"]}};
        }
        double dust_bound = 0.0, cloud_constrained = 0.0, cloud_hypothesis = 0.0, magnetospheric_w_m2 = 0.0, gcr_w_m2 = 0.0;
        if (!r.dust_path.empty()) {
            const Json doc = load_json(r.dust_path, "cosmic-dust timeline");
            const Json& c = doc.at("cosmic_dust").at("scenarios").at("central");
            dust_bound = c.at("smoke").at("forcing_bound_w_m2").get<double>();
            links["cosmic_dust"] = {{"path", r.dust_path}, {"smoke_forcing_bound_w_m2", dust_bound}, {"kinetic_power_w_m2", c.at("energy").at("per_m2_w")},
                                    {"water_t_per_year", c.at("water").at("in_t_per_year")}};
        }
        if (!r.wind_path.empty()) {
            const Json doc = load_json(r.wind_path, "solar-wind timeline");
            const Json& b = doc.at("solar_wind").at("budgets");
            cloud_constrained = b.at("clouds").value("forcing_constrained_w_m2", 0.0);
            cloud_hypothesis = b.at("clouds").value("forcing_hypothesis_w_m2", 0.0);
            magnetospheric_w_m2 = b.at("energy").at("magnetospheric_input_w").get<double>() / (4.0 * kPi * kEarthRadius * kEarthRadius);
            gcr_w_m2 = b.at("energy").value("gcr_energy_w_m2", 0.0);
            links["solar_wind"] = {{"path", r.wind_path}, {"cloud_forcing_constrained_w_m2", cloud_constrained}, {"cloud_forcing_hypothesis_w_m2", cloud_hypothesis},
                                   {"magnetospheric_input_w_m2", tidy(magnetospheric_w_m2)}, {"cosmic_ray_energy_w_m2", tidy(gcr_w_m2)}, {"escape_kg_s", b.at("mean_escape").at("total_kg_s")},
                                   {"thermosphere_density_storm_ratio", b.at("storm").at("max_density_400km_ratio")},
                                   {"storm_upper_ozone_loss_pct", b.at("storm").value("min_ozone_upper_pct", 0.0)}};
        }
        struct PolarRun { std::vector<double> years, greenland_m, amoc_sv, north_atlantic_k; double amoc0{17.0}; };
        std::map<std::string, PolarRun> polar;
        if (!r.polar_path.empty()) {
            const Json doc = load_json(r.polar_path, "polar-melt timeline");
            const Json& p = doc.at("polar");
            for (const Json& s : p.at("scenarios")) {
                PolarRun run;
                for (const Json& y : p.at("output_years")) run.years.push_back(y.get<double>());
                const Json& ser = s.at("series");
                const double g0 = ser.at("greenland_sea_level_equivalent_m")[0].get<double>();
                for (std::size_t k = 0; k < run.years.size(); ++k) {
                    run.greenland_m.push_back(g0 - ser.at("greenland_sea_level_equivalent_m")[k].get<double>());
                    run.amoc_sv.push_back(ser.at("overturning_sv")[k].get<double>());
                    run.north_atlantic_k.push_back(ser.at("north_atlantic_temperature_change_k")[k].get<double>());
                }
                run.amoc0 = run.amoc_sv.front();
                polar[s.at("name").get<std::string>()] = run;
            }
            links["polar_melt"] = {{"path", r.polar_path}, {"scenarios", [&] { Json n = Json::array(); for (const auto& [k, v] : polar) n.push_back(k); return n; }()}};
        }
        if (!r.forecast_path.empty()) {
            const Json doc = load_json(r.forecast_path, "earth forecast timeline");
            const Json& f = doc.at("earth_forecast");
            links["forecast"] = {{"path", r.forecast_path}, {"couplings", f.value("couplings", Json(nullptr))}, {"verification", f.at("verification").at("lead_days")},
                                 {"useful_hours", f.at("verification").at("useful_hours")}};
        }
        auto polar_at = [&](const PolarRun& run, const std::vector<double>& series, double year) {
            if (year <= run.years.front()) return series.front();
            for (std::size_t k = 1; k < run.years.size(); ++k)
                if (year <= run.years[k]) return series[k - 1] + (year - run.years[k - 1]) / (run.years[k] - run.years[k - 1]) * (series[k] - series[k - 1]);
            return series.back();
        };

        // ---- Forcing of every agent, every scenario ------------------------------------------------------------------
        const std::vector<std::string> agent_order = {"co2", "ch4", "n2o", "halocarbons", "stratospheric_water", "ozone", "aerosols", "black_carbon_snow", "land_use", "contrails", "solar", "volcanic"};
        struct ScenarioRun {
            std::map<std::string, std::vector<double>> forcing;       // per agent
            std::vector<double> total_forcing;
            std::map<std::string, std::vector<double>> warming;       // per agent (linear response)
            std::vector<double> warming_total, deep_total, warming_low, warming_high;
            std::vector<double> co2;
        };
        std::map<std::string, ScenarioRun> runs;
        // Reference-period mean of each agent's forcing: the warming is counted from the pre-industrial state.
        auto respond = [&](const std::vector<double>& forcing, const TwoLayerClimate& c, std::vector<double>& deep) {
            std::vector<double> t(years);
            deep.assign(years, 0.0);
            TwoLayerState s;
            for (int k = 0; k < years; ++k) { s = step_two_layer(s, forcing[k], 1.0, c); t[k] = s.surface_k; deep[k] = s.deep_k; }
            return t;
        };
        auto reference_mean = [&](const std::vector<double>& v) {
            double sum = 0.0;
            int n = 0;
            for (int k = 0; k < years; ++k) if (year_at(k) >= r.ref0 && year_at(k) <= r.ref1) { sum += v[k]; ++n; }
            return n ? sum / n : 0.0;
        };
        TwoLayerClimate low_ecs = r.climate, high_ecs = r.climate;
        low_ecs.feedback_w_m2_k = r.f2x / r.ecs_low;
        high_ecs.feedback_w_m2_k = r.f2x / r.ecs_high;
        const double ch4_0 = 722.0, n2o_0 = 270.0, co2_0 = 278.0;
        for (const auto& sc : r.scenarios) {
            ScenarioRun run;
            for (const auto& a : agent_order) run.forcing[a].assign(years, 0.0);
            run.co2.resize(years);
            for (int k = 0; k < years; ++k) {
                const double y = year_at(k) + 0.5;
                auto at = [&](const char* key) { return interpolate_points(sc.series.at(key), y); };
                const double co2 = at("co2_ppm"), ch4 = at("ch4_ppb"), n2o = at("n2o_ppb");
                run.co2[k] = co2;
                run.forcing["co2"][k] = co2_forcing_w_m2(co2, co2_0);
                run.forcing["ch4"][k] = methane_forcing_w_m2(ch4, n2o_0, ch4_0);
                run.forcing["n2o"][k] = nitrous_oxide_forcing_w_m2(n2o, ch4_0, n2o_0);
                run.forcing["halocarbons"][k] = r.agents.at("halocarbons").value_2019 * at("halocarbon_w_m2");
                run.forcing["stratospheric_water"][k] = r.agents.at("stratospheric_water").value_2019 * std::max(0.0, run.forcing["ch4"][k]);
                run.forcing["ozone"][k] = r.agents.at("ozone").value_2019 * at("ozone_index");
                run.forcing["aerosols"][k] = r.agents.at("aerosols").value_2019 * at("aerosol_index");
                run.forcing["black_carbon_snow"][k] = r.agents.at("black_carbon_snow").value_2019 * at("aerosol_index");
                run.forcing["land_use"][k] = r.agents.at("land_use").value_2019 * at("land_use_index");
                run.forcing["contrails"][k] = r.agents.at("contrails").value_2019 * at("aviation_index");
                run.forcing["solar"][k] = solar[k];
                run.forcing["volcanic"][k] = volcanic[k];
            }
            run.total_forcing.assign(years, 0.0);
            for (const auto& a : agent_order) {
                // Forcing counted from the reference period (the climate was in balance with it).
                const double base = reference_mean(run.forcing[a]);
                for (double& f : run.forcing[a]) f -= base;
                for (int k = 0; k < years; ++k) run.total_forcing[k] += run.forcing[a][k];
            }
            std::vector<double> deep;
            for (const auto& a : agent_order) run.warming[a] = respond(run.forcing[a], r.climate, deep);
            run.warming_total = respond(run.total_forcing, r.climate, run.deep_total);
            run.warming_low = respond(run.total_forcing, low_ecs, deep);
            run.warming_high = respond(run.total_forcing, high_ecs, deep);
            // Count the warming from the reference period.
            auto rebase = [&](std::vector<double>& v) { const double b = reference_mean(v); for (double& x : v) x -= b; };
            for (const auto& a : agent_order) rebase(run.warming[a]);
            rebase(run.warming_total);
            rebase(run.warming_low);
            rebase(run.warming_high);
            runs[sc.key] = run;
        }
        const int k_today = r.today - r.first, k_last = years - 1;
        auto k_of = [&](int year) { return std::clamp(year - r.first, 0, k_last); };

        // ---- Driver ranking -----------------------------------------------------------------------------------------
        auto driver_rows = [&](const ScenarioRun& run, int k) {
            Json rows = Json::array();
            for (const auto& key : agent_order) {
                const auto& a = r.agents.at(key);
                const double t = run.warming.at(key)[k], f = run.forcing.at(key)[k];
                const double lo = std::min(t * a.low, t * a.high), hi = std::max(t * a.low, t * a.high);
                rows.push_back({{"key", key}, {"label", a.label}, {"group", a.group}, {"forcing_w_m2", tidy(f, 3)}, {"warming_k", tidy(t, 3)}, {"warming_low_k", tidy(lo, 3)},
                                {"warming_high_k", tidy(hi, 3)}, {"share", tidy(t / std::max(1e-9, run.warming_total[k]), 3)}});
            }
            // The space drivers: too small to run through the energy balance; shown with their bounds.
            const double lambda_eq = 1.0 / r.climate.feedback_w_m2_k;
            if (!r.dust_path.empty()) rows.push_back({{"key", "cosmic_dust"}, {"label", "Cosmic dust (meteoric smoke)"}, {"group", "space"}, {"forcing_w_m2", 0.0}, {"warming_k", 0.0},
                                                      {"warming_low_k", tidy(-dust_bound * lambda_eq, 3)}, {"warming_high_k", tidy(dust_bound * lambda_eq, 3)}, {"share", 0.0}});
            if (!r.wind_path.empty()) {
                rows.push_back({{"key", "cosmic_rays_clouds"}, {"label", "Cosmic rays and clouds (solar-cycle swing)"}, {"group", "space"}, {"forcing_w_m2", 0.0}, {"warming_k", 0.0},
                                {"warming_low_k", tidy(-cloud_constrained * lambda_eq, 3)}, {"warming_high_k", tidy(cloud_constrained * lambda_eq, 3)}, {"share", 0.0},
                                {"hypothesis_k", tidy(cloud_hypothesis * lambda_eq, 3)}});
                rows.push_back({{"key", "solar_wind_energy"}, {"label", "Solar-wind energy (aurora, Joule heating)"}, {"group", "space"}, {"forcing_w_m2", tidy(magnetospheric_w_m2, 3)},
                                {"warming_k", tidy(magnetospheric_w_m2 * lambda_eq, 3)}, {"warming_low_k", 0.0}, {"warming_high_k", tidy(magnetospheric_w_m2 * lambda_eq, 3)}, {"share", 0.0}});
            }
            std::vector<Json> v(rows.begin(), rows.end());
            std::stable_sort(v.begin(), v.end(), [](const Json& a, const Json& b) {
                const double x = std::max(std::abs(a["warming_k"].get<double>()), std::abs(a["warming_high_k"].get<double>()));
                const double y = std::max(std::abs(b["warming_k"].get<double>()), std::abs(b["warming_high_k"].get<double>()));
                return std::abs(a["warming_k"].get<double>()) != std::abs(b["warming_k"].get<double>()) ? std::abs(a["warming_k"].get<double>()) > std::abs(b["warming_k"].get<double>()) : x > y;
            });
            Json out = Json::array();
            for (auto& x : v) out.push_back(x);
            return out;
        };

        // ---- Impacts --------------------------------------------------------------------------------------------------
        StormSystemsConfig storm_config;
        auto potential_intensity = [&](double ocean_warming) {
            const double sst = 273.15 + r.tropical_sst_c + ocean_warming, air = sst - 1.0, p = 101000.0;
            const double q = 0.8 * saturation_specific_humidity_kg_kg(air, p);
            return potential_intensity_m_s(sst, air, q, p, 200.0, storm_config);
        };
        const double pi_0 = potential_intensity(0.0);
        auto carbonate = [&](double ppm, double temp_c) {
            const double t = 273.15 + temp_c;
            const double dic = dic_at_equilibrium(ppm, r.alkalinity, t, 35.0);
            return solve_carbonate(dic, r.alkalinity, t, 35.0);
        };
        const auto chem_0 = carbonate(co2_0, r.sst_c);
        std::map<std::string, Json> impact_series;
        std::map<std::string, std::vector<double>> sea_level_total;
        for (const auto& sc : r.scenarios) {
            const auto& run = runs.at(sc.key);
            Json s = Json::object();
            std::vector<double> land(years), arctic(years), heat50(years), heat10(years), rain(years), ice(years), ph(years), omega(years), omega_polar(years), pi(years), ohc(years);
            std::vector<double> amoc(years, NAN), greenland(years, 0.0);
            const PolarRun* pr = polar.count(sc.polar_name) ? &polar.at(sc.polar_name) : nullptr;
            for (int k = 0; k < years; ++k) {
                const double t = run.warming_total[k], y = year_at(k) + 0.5;
                land[k] = r.pattern.land_factor * t;
                arctic[k] = r.pattern.arctic_factor * t;
                heat50[k] = heat_extreme_factor(land[k], r.heat_sigma_k, 50.0);
                heat10[k] = heat_extreme_factor(land[k], r.heat_sigma_k, 10.0);
                rain[k] = heavy_rain_frequency_factor(t, r.heavy_rain_per_k);
                ice[k] = september_sea_ice_mkm2(t, r.ice_extent_ref, r.ice_warming_ref, r.ice_loss_per_k);
                const auto chem = carbonate(run.co2[k], r.sst_c + r.ocean_factor * t);
                ph[k] = chem.ph_total;
                omega[k] = chem.omega_aragonite;
                omega_polar[k] = carbonate(run.co2[k], r.polar_sst_c + 0.5 * r.ocean_factor * t).omega_aragonite;
                pi[k] = (potential_intensity(r.ocean_factor * t) / pi_0 - 1.0) * 100.0;
                // Ocean heat content (ZJ) of the two layers.
                const double area = 4.0 * kPi * kEarthRadius * kEarthRadius;
                ohc[k] = (r.climate.upper_heat_capacity * t + r.climate.deep_heat_capacity * (run.deep_total[k] - reference_mean(run.deep_total))) * area * kYear / 1.0e21;
                if (pr) {
                    amoc[k] = polar_at(*pr, pr->amoc_sv, y);
                    greenland[k] = polar_at(*pr, pr->greenland_m, y);
                }
            }
            // Sea level from the reference year on (rise still to come), plus the observed rise since 1900.
            std::vector<int> sl_years;
            std::vector<double> sl_warming, sl_greenland;
            for (int year = 2020; year <= r.last; ++year) {
                sl_years.push_back(year);
                sl_warming.push_back(run.warming_total[k_of(year)]);
                double g = pr ? greenland[k_of(year)] : 0.0;
                if (!pr) {
                    double acc = 0.0;
                    for (int yy = 2020; yy <= year; ++yy) acc += r.greenland_m_yr_per_k * std::max(0.0, run.warming_total[k_of(yy)] - r.greenland_threshold_k);
                    g = acc;
                }
                sl_greenland.push_back(g);
            }
            const auto sl = sea_level_budget(sl_years, sl_warming, sl_greenland, r.sea_level);
            std::vector<double> sea(years, NAN), sea_thermal(years, NAN), sea_glaciers(years, NAN), sea_antarctic(years, NAN), sea_greenland(years, NAN);
            for (std::size_t m = 0; m < sl.years.size(); ++m) {
                const int k = k_of(sl.years[m]);
                sea[k] = sl.global_m[m] + r.observed_rise_m;
                sea_thermal[k] = sl.thermal_expansion_m[m];
                sea_glaciers[k] = sl.glaciers_m[m];
                sea_antarctic[k] = sl.antarctic_m[m];
                sea_greenland[k] = sl.greenland_m[m];
            }
            sea_level_total[sc.key] = sea;
            s["global_k"] = tidy_array(run.warming_total, 4);
            s["global_low_k"] = tidy_array(run.warming_low, 4);
            s["global_high_k"] = tidy_array(run.warming_high, 4);
            s["land_k"] = tidy_array(land, 4);
            s["arctic_k"] = tidy_array(arctic, 4);
            s["total_forcing_w_m2"] = tidy_array(run.total_forcing, 4);
            s["co2_ppm"] = tidy_array(run.co2, 4);
            s["ocean_heat_zj"] = tidy_array(ohc, 4);
            s["heat_50yr_factor"] = tidy_array(heat50, 4);
            s["heat_10yr_factor"] = tidy_array(heat10, 4);
            s["heavy_rain_factor"] = tidy_array(rain, 4);
            s["september_ice_mkm2"] = tidy_array(ice, 4);
            s["ph"] = tidy_array(ph, 5);
            s["aragonite_saturation"] = tidy_array(omega, 4);
            s["aragonite_saturation_polar"] = tidy_array(omega_polar, 4);
            s["hurricane_potential_intensity_change_pct"] = tidy_array(pi, 4);
            s["overturning_sv"] = tidy_array(amoc, 4);
            s["sea_level_since_1900_m"] = tidy_array(sea, 4);
            s["sea_level_thermal_m"] = tidy_array(sea_thermal, 4);
            s["sea_level_glaciers_m"] = tidy_array(sea_glaciers, 4);
            s["sea_level_antarctic_m"] = tidy_array(sea_antarctic, 4);
            s["sea_level_greenland_m"] = tidy_array(sea_greenland, 4);
            Json agents = Json::object();
            for (const auto& a : agent_order) agents[a] = {{"forcing_w_m2", tidy_array(run.forcing.at(a), 3)}, {"warming_k", tidy_array(run.warming.at(a), 3)}};
            s["agents"] = agents;
            impact_series[sc.key] = s;
        }

        // ---- Impact table: today and the end of the century, per scenario ------------------------------------------
        auto value = [&](const std::string& scen, const char* key, int k) { const Json& v = impact_series.at(scen)[key][static_cast<std::size_t>(k)]; return v.is_null() ? NAN : v.get<double>(); };
        struct ImpactDef { const char* key; const char* label; const char* unit; const char* kind; bool irreversible; double baseline; };
        const std::vector<ImpactDef> impacts = {
            {"global_k", "Global surface warming", "K", "warming", false, 0.0},
            {"land_k", "Warming over land", "K", "warming", false, 0.0},
            {"arctic_k", "Arctic warming", "K", "warming", false, 0.0},
            {"heat_50yr_factor", "Hot extremes that came once in 50 years", "times as often", "frequency", false, 1.0},
            {"heat_10yr_factor", "Hot extremes that came once in 10 years", "times as often", "frequency", false, 1.0},
            {"heavy_rain_factor", "Heavy rain that came once in 10 years", "times as often", "frequency", false, 1.0},
            {"hurricane_potential_intensity_change_pct", "Potential intensity of hurricanes", "%", "change", false, 0.0},
            {"september_ice_mkm2", "Arctic sea ice in September", "million km²", "level", false, 0.0},
            {"sea_level_since_1900_m", "Sea level since 1900", "m", "level", true, 0.0},
            {"ocean_heat_zj", "Heat stored in the ocean", "ZJ", "level", true, 0.0},
            {"ph", "Ocean surface pH", "pH", "level", true, 0.0},
            {"aragonite_saturation_polar", "Aragonite saturation of polar water", "Ω", "level", true, 0.0},
            {"overturning_sv", "Atlantic overturning (AMOC)", "Sv", "level", true, 0.0}};
        Json impact_table = Json::array();
        for (const auto& im : impacts) {
            Json row = {{"key", im.key}, {"label", im.label}, {"unit", im.unit}, {"kind", im.kind}, {"irreversible_on_human_time_scales", im.irreversible}};
            row["preindustrial"] = number(value(r.central, im.key, k_of(r.ref0)));
            row["today"] = number(value(r.central, im.key, k_today));
            Json by = Json::object();
            for (const auto& sc : r.scenarios) by[sc.key] = {{"y2050", number(value(sc.key, im.key, k_of(2050)))}, {"y2100", number(value(sc.key, im.key, k_of(2100)))}};
            row["scenarios"] = by;
            impact_table.push_back(row);
        }

        // ---- Feedbacks --------------------------------------------------------------------------------------------
        const double ecs = r.f2x / r.climate.feedback_w_m2_k, tcr = transient_climate_response_k(r.climate, r.f2x);
        const auto shares_today = feedback_shares(runs.at(r.central).warming_total[k_today], r.feedbacks);
        const auto shares_2100 = feedback_shares(runs.at(r.central).warming_total[k_last], r.feedbacks);
        auto shares_json = [](const FeedbackShares& s) {
            return Json{{"no_feedback_k", tidy(s.no_feedback_k, 3)}, {"water_vapour_lapse_rate_k", tidy(s.water_vapour_lapse_rate_k, 3)}, {"surface_albedo_k", tidy(s.surface_albedo_k, 3)},
                        {"clouds_k", tidy(s.clouds_k, 3)}, {"other_k", tidy(s.other_k, 3)}};
        };

        // ---- The map: local warming by driver, per frame, per scenario ------------------------------------------------
        const std::size_t nx = r.nx, ny = r.ny, n = nx * ny;
        const double dlon = 360.0 / nx, dlat = 180.0 / ny;
        std::vector<double> lat(n), lon(n), area(n), pattern(n), aerosol_pattern(n), atlantic(n);
        double area_total = 0.0, pattern_mean = 0.0, aerosol_mean = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            lat[i] = -90.0 + (static_cast<double>(i / nx) + 0.5) * dlat;
            lon[i] = -180.0 + (static_cast<double>(i % nx) + 0.5) * dlon;
            area[i] = std::cos(lat[i] * kDeg);
            area_total += area[i];
            pattern[i] = warming_pattern_factor(lat[i], r.land[i] != 0, r.pattern);
            double boost = 0.0;
            for (const auto& b : r.aerosol_regions) if (in_boxes(b.boxes, lat[i], lon[i])) boost = std::max(boost, b.value);
            aerosol_pattern[i] = pattern[i] * (1.0 + boost);
            const double dx = (lon[i] + 35.0) / 20.0, dy = (lat[i] - 55.0) / 10.0;
            atlantic[i] = std::exp(-dx * dx - dy * dy);
        }
        for (std::size_t i = 0; i < n; ++i) { pattern_mean += pattern[i] * area[i] / area_total; aerosol_mean += aerosol_pattern[i] * area[i] / area_total; }
        for (std::size_t i = 0; i < n; ++i) { pattern[i] /= pattern_mean; aerosol_pattern[i] /= aerosol_mean; }
        std::vector<int> frame_years;
        for (int y = r.first; y <= r.last; y += r.frame_every) frame_years.push_back(y);
        if (frame_years.back() != r.last) frame_years.push_back(r.last);
        const double metres_per_degree = 2.0 * kPi * kEarthRadius / 360.0;
        const Json grid = {{"nx", nx}, {"ny", ny}, {"origin_east_m", -180.0 * metres_per_degree}, {"origin_north_m", -90.0 * metres_per_degree},
                           {"spacing_east_m", dlon * metres_per_degree}, {"spacing_north_m", dlat * metres_per_degree}, {"geographic_anchor_longitude_deg", 0.0},
                           {"geographic_anchor_latitude_deg", 0.0}, {"coordinate_system", "local_azimuthal_equidistant_m"}, {"array_order", "row_major_y_then_x"}};
        Json frames = Json::array(), map_frames = Json::array();
        const std::vector<int> zeros(n, 0);
        for (int year : frame_years) {
            const int k = k_of(year);
            Json local_by = Json::object();
            std::vector<double> central_local, aerosol_local(n, 0.0), overturning_local(n, 0.0);
            for (const auto& sc : r.scenarios) {
                const auto& run = runs.at(sc.key);
                const PolarRun* pr = polar.count(sc.polar_name) ? &polar.at(sc.polar_name) : nullptr;
                const double amoc_drop = pr ? std::max(0.0, pr->amoc0 - polar_at(*pr, pr->amoc_sv, year + 0.5)) : 0.0;
                std::vector<double> local(n, 0.0);
                for (std::size_t i = 0; i < n; ++i) {
                    for (std::size_t a = 0; a < agent_order.size(); ++a)
                        local[i] += run.warming.at(agent_order[a])[k] * (agent_order[a] == "aerosols" ? aerosol_pattern[i] : pattern[i]);
                    const double overturning = -r.pattern.north_atlantic_cooling_per_sv_k * amoc_drop * atlantic[i] * (r.land[i] ? 0.5 : 1.0);
                    local[i] += overturning;
                    // The regional terms of the central scenario: aerosol cooling (strongest where the air is polluted)
                    // and the cooling of a weaker Atlantic overturning.
                    if (sc.key == r.central) {
                        aerosol_local[i] = run.warming.at("aerosols")[k] * aerosol_pattern[i];
                        overturning_local[i] = overturning;
                    }
                }
                local_by[sc.key] = fixed_array(local, 1);
                if (sc.key == r.central) central_local = local;
            }
            std::vector<double> air(n);
            for (std::size_t i = 0; i < n; ++i) air[i] = 288.0 + central_local[i];
            const double elapsed = (year - r.first) * 365.25 * 86400.0;
            frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", std::to_string(r.first) + "-07-01T00:00:00Z"}, {"elapsed_seconds", elapsed},
                              {"grid", grid}, {"observation_updates", Json::array()},
                              {"fields", {{"air_temperature", {{"units", "K"}, {"values", fixed_array(air, 1)}}}, {"wind_east", {{"units", "m/s"}, {"values", zeros}}},
                                          {"wind_north", {{"units", "m/s"}, {"values", zeros}}}}}});
            map_frames.push_back({{"year", year}, {"elapsed_seconds", elapsed}, {"local_warming_k", local_by}, {"aerosol_k", fixed_array(aerosol_local, 2)},
                                  {"overturning_k", fixed_array(overturning_local, 2)}});
        }

        // ---- CO2: sources, sinks, transport, impacts ------------------------------------------------------------------
        std::vector<double> fossil(n, 0.0), land_use(n, 0.0), land_sink(n, 0.0), ocean_flux(n, 0.0), cumulative(n, 0.0);
        std::vector<int> region_of(n, -1);
        // Fossil and cumulative emissions of each region, spread over its land (ocean cells take them when a region has no land).
        for (std::size_t g = 0; g < r.regions.size(); ++g) {
            const auto& reg = r.regions[g];
            double a_land = 0.0, a_all = 0.0;
            for (std::size_t i = 0; i < n; ++i) if (in_boxes(reg.boxes, lat[i], lon[i])) { a_all += area[i]; if (r.land[i]) a_land += area[i]; }
            if (a_all <= 0.0) throw std::invalid_argument("co2_map region " + reg.name + " covers no cell of the grid");
            for (std::size_t i = 0; i < n; ++i) {
                if (!in_boxes(reg.boxes, lat[i], lon[i]) || (a_land > 0.0 && !r.land[i])) continue;
                const double share = area[i] / (a_land > 0.0 ? a_land : a_all);
                fossil[i] += reg.fossil * share;
                cumulative[i] += reg.cumulative * share;
                land_use[i] += reg.land_use * share;
                if (region_of[i] < 0) region_of[i] = static_cast<int>(g);
            }
        }
        // The rest of the world's fossil emissions (not in a region): spread over the remaining land.
        double named_fossil = 0.0;
        for (const auto& reg : r.regions) named_fossil += reg.fossil;
        const double rest_fossil = std::max(0.0, r.fossil_total - named_fossil);
        {
            double a = 0.0;
            for (std::size_t i = 0; i < n; ++i) if (r.land[i] && region_of[i] < 0 && std::abs(lat[i]) < 60.0) a += area[i];
            for (std::size_t i = 0; i < n; ++i) if (r.land[i] && region_of[i] < 0 && std::abs(lat[i]) < 60.0 && a > 0.0) fossil[i] += rest_fossil * area[i] / a;
        }
        // Land-use emissions (deforestation).
        for (const auto& b : r.land_use) {
            double a = 0.0;
            for (std::size_t i = 0; i < n; ++i) if (r.land[i] && in_boxes(b.boxes, lat[i], lon[i])) a += area[i];
            for (std::size_t i = 0; i < n; ++i) if (r.land[i] && in_boxes(b.boxes, lat[i], lon[i]) && a > 0.0) land_use[i] += b.value * area[i] / a;
        }
        // The land sink: a productivity proxy by latitude, none over deserts and ice.
        {
            std::vector<double> w(n, 0.0);
            double sum = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                if (!r.land[i]) continue;
                const double a = std::abs(lat[i]);
                double p = a < 15.0 ? 2.5 : a < 30.0 ? 1.0 : a < 50.0 ? 1.5 : a < 70.0 ? 1.2 : 0.15;
                for (const auto& d : r.deserts) if (in_boxes(d.boxes, lat[i], lon[i])) p *= 1.0 - d.value;
                for (const auto& d : r.ice) if (in_boxes(d.boxes, lat[i], lon[i])) p *= 1.0 - d.value;
                w[i] = p * area[i];
                sum += w[i];
            }
            for (std::size_t i = 0; i < n; ++i) land_sink[i] = sum > 0.0 ? -r.land_sink * w[i] / sum : 0.0;
        }
        // The ocean: Wanninkhof gas exchange with a zonal pCO2 difference and upwelling regions, scaled to the sink.
        {
            double sum = 0.0;
            std::vector<double> raw(n, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                if (r.land[i]) continue;
                const double a = std::abs(lat[i]);
                double dp = interpolate_points(r.dpco2_profile, a);
                for (const auto& u : r.upwelling) if (in_boxes(u.boxes, lat[i], lon[i])) dp = u.value;
                const double sst = 273.15 + std::max(-1.5, interpolate_points(r.sst_profile, a));
                raw[i] = ocean_co2_uptake_mol_m2_s(sst, interpolate_points(r.wind_profile, a), dp) * area[i];   // mol/s per unit area weight
                sum += raw[i];
            }
            for (std::size_t i = 0; i < n; ++i) ocean_flux[i] = sum > 0.0 ? -r.ocean_sink * raw[i] / sum : 0.0;   // negative: uptake (Gt/yr per cell)
        }
        // Per-area fluxes (t CO2 per km2 per year) and the concentration anomaly.
        std::vector<double> cell_km2(n), emission_density(n), sink_density(n), net_density(n), source_ppm_s(n);
        const double air_column_mol = 101325.0 / 9.80665 / 0.02896;
        for (std::size_t i = 0; i < n; ++i) {
            cell_km2[i] = kEarthRadius * kEarthRadius * dlon * kDeg * (std::sin((lat[i] + 0.5 * dlat) * kDeg) - std::sin((lat[i] - 0.5 * dlat) * kDeg)) / 1.0e6;
            emission_density[i] = (fossil[i] + land_use[i]) * 1.0e9 / cell_km2[i];
            sink_density[i] = (land_sink[i] + ocean_flux[i]) * 1.0e9 / cell_km2[i];
            net_density[i] = emission_density[i] + sink_density[i];
            const double mol_m2_s = net_density[i] * 1.0e3 / 1.0e6 / kCo2MolarMass / kYear;   // t/km2/yr -> mol/m2/s
            source_ppm_s[i] = mol_m2_s / (air_column_mol * r.transport_mixing_fraction) * 1.0e6;
        }
        const auto ppm_anomaly = global_steady_anomaly(source_ppm_s, nx, ny, r.transport_diffusivity, r.transport_relaxation_days * 86400.0);
        // Impacts of 100 ppm more, per cell: local warming (transient), pH drop and aragonite saturation.
        const double transient_per_100ppm = tcr * co2_forcing_w_m2(520.0, 420.0) / r.f2x;
        std::vector<double> warming_100(n), ph_drop(n, NAN), omega_now(n, NAN), omega_2100(n, NAN), impact_score(n, 0.0);
        const double co2_now = runs.at(r.central).co2[k_today], co2_2100 = runs.at(r.central).co2[k_last];
        for (std::size_t i = 0; i < n; ++i) {
            warming_100[i] = transient_per_100ppm * pattern[i];
            if (!r.land[i]) {
                const double sst = std::max(-1.5, interpolate_points(r.sst_profile, std::abs(lat[i])));
                const auto now = carbonate(co2_now, sst), plus = carbonate(co2_now + 100.0, sst);
                ph_drop[i] = now.ph_total - plus.ph_total;
                omega_now[i] = now.omega_aragonite;
                omega_2100[i] = carbonate(co2_2100, sst + r.ocean_factor * runs.at(r.central).warming_total[k_last]).omega_aragonite;
                // Sea: acidification (corrosive water) and loss of shell-building saturation.
                impact_score[i] = std::clamp(ph_drop[i] / 0.1, 0.0, 1.0) * 0.5 + std::clamp((1.8 - omega_2100[i]) / 1.3, 0.0, 1.0) * 0.5;
            } else {
                impact_score[i] = std::clamp((warming_100[i] - 0.3) / 0.9, 0.0, 1.0);
            }
        }
        // Regions ranked: annual and cumulative emissions, and the warming the cumulative emissions cause (TCRE).
        Json region_rows = Json::array();
        for (std::size_t g = 0; g < r.regions.size(); ++g) {
            const auto& reg = r.regions[g];
            // Centre of the cells that carry the region's emissions: its land, or all its cells when the grid sees no land.
            double clat = 0.0, clon = 0.0, w = 0.0;
            for (int pass = 0; pass < 2 && w <= 0.0; ++pass)
                for (std::size_t i = 0; i < n; ++i)
                    if (in_boxes(reg.boxes, lat[i], lon[i]) && (pass == 1 || r.land[i])) { clat += lat[i] * area[i]; clon += lon[i] * area[i]; w += area[i]; }
            region_rows.push_back({{"name", reg.name}, {"fossil_gt_co2_yr", reg.fossil}, {"land_use_gt_co2_yr", reg.land_use}, {"cumulative_gt_co2", reg.cumulative},
                                   {"share_of_annual", tidy((reg.fossil + reg.land_use) / (r.fossil_total + r.land_use_total), 3)}, {"share_of_cumulative", tidy(reg.cumulative / r.cumulative_total, 3)},
                                   {"warming_k", tidy(reg.cumulative * r.tcre_k_per_1000gt / 1000.0, 3)}, {"latitude_deg", w > 0 ? Json(tidy(clat / w, 3)) : Json(nullptr)},
                                   {"longitude_deg", w > 0 ? Json(tidy(clon / w, 3)) : Json(nullptr)}});
        }
        std::vector<Json> rr(region_rows.begin(), region_rows.end());
        std::stable_sort(rr.begin(), rr.end(), [](const Json& a, const Json& b) { return a["fossil_gt_co2_yr"].get<double>() + a["land_use_gt_co2_yr"].get<double>() > b["fossil_gt_co2_yr"].get<double>() + b["land_use_gt_co2_yr"].get<double>(); });
        region_rows = Json::array();
        for (auto& x : rr) region_rows.push_back(x);
        // Sinks and impact hotspots as named areas.
        Json sink_rows = Json::array(), hotspot_rows = Json::array();
        auto area_sum = [&](const std::vector<std::array<double, 4>>& boxes, const std::vector<double>& field, bool land_only, bool sea_only) {
            double s = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                if (!in_boxes(boxes, lat[i], lon[i]) || (land_only && !r.land[i]) || (sea_only && r.land[i])) continue;
                s += field[i];
            }
            return s;
        };
        const std::vector<std::pair<std::string, std::vector<std::array<double, 4>>>> sink_areas = {
            {"Amazon rainforest", {{-15.0, 5.0, -75.0, -45.0}}}, {"Congo basin", {{-10.0, 8.0, 10.0, 32.0}}}, {"Southeast Asian forests", {{-10.0, 20.0, 95.0, 150.0}}},
            {"Boreal forests (Eurasia)", {{50.0, 70.0, 20.0, 180.0}}}, {"Boreal forests (North America)", {{50.0, 70.0, -170.0, -55.0}}},
            {"Southern Ocean", {{-65.0, -40.0, -180.0, 180.0}}}, {"North Atlantic", {{40.0, 70.0, -70.0, 10.0}}}, {"North Pacific", {{35.0, 60.0, 140.0, -125.0}}},
            {"Equatorial Pacific (outgassing)", {{-10.0, 10.0, 160.0, -80.0}}}};
        for (const auto& [name, boxes] : sink_areas) {
            const double land_part = area_sum(boxes, land_sink, true, false), ocean_part = area_sum(boxes, ocean_flux, false, true);
            sink_rows.push_back({{"name", name}, {"land_gt_co2_yr", tidy(land_part, 3)}, {"ocean_gt_co2_yr", tidy(ocean_part, 3)}, {"net_gt_co2_yr", tidy(land_part + ocean_part, 3)}});
        }
        for (const auto& h : r.hotspots) {
            double sum = 0.0, w = 0.0, warm = 0.0, ww = 0.0, dph = 0.0, wp = 0.0, om = 0.0, clat = 0.0, clon = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                if (!in_boxes(h.boxes, lat[i], lon[i])) continue;
                sum += impact_score[i] * area[i]; w += area[i]; clat += lat[i] * area[i]; clon += lon[i] * area[i];
                warm += warming_100[i] * area[i]; ww += area[i];
                if (!r.land[i]) { dph += ph_drop[i] * area[i]; om += omega_2100[i] * area[i]; wp += area[i]; }
            }
            hotspot_rows.push_back({{"name", h.name}, {"score", w > 0 ? Json(tidy(sum / w, 3)) : Json(nullptr)}, {"warming_per_100ppm_k", ww > 0 ? Json(tidy(warm / ww, 3)) : Json(nullptr)},
                                    {"ph_drop_per_100ppm", wp > 0 ? Json(tidy(dph / wp, 3)) : Json(nullptr)}, {"aragonite_2100", wp > 0 ? Json(tidy(om / wp, 3)) : Json(nullptr)},
                                    {"latitude_deg", w > 0 ? Json(tidy(clat / w, 3)) : Json(nullptr)}, {"longitude_deg", w > 0 ? Json(tidy(clon / w, 3)) : Json(nullptr)}});
        }
        std::vector<Json> hs(hotspot_rows.begin(), hotspot_rows.end());
        std::stable_sort(hs.begin(), hs.end(), [](const Json& a, const Json& b) { return (a["score"].is_null() ? 0.0 : a["score"].get<double>()) > (b["score"].is_null() ? 0.0 : b["score"].get<double>()); });
        hotspot_rows = Json::array();
        for (auto& x : hs) hotspot_rows.push_back(x);
        double land_total = 0.0, ocean_total = 0.0, fossil_sum = 0.0, land_use_sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) { land_total += land_sink[i]; ocean_total += ocean_flux[i]; fossil_sum += fossil[i]; land_use_sum += land_use[i]; }
        const double growth_gt = fossil_sum + land_use_sum + land_total + ocean_total;
        // NH - SH difference of the surface anomaly.
        double nh = 0.0, sh = 0.0, wn = 0.0, ws = 0.0;
        for (std::size_t i = 0; i < n; ++i) { if (lat[i] > 0) { nh += ppm_anomaly[i] * area[i]; wn += area[i]; } else { sh += ppm_anomaly[i] * area[i]; ws += area[i]; } }

        // ---- The coupling matrix --------------------------------------------------------------------------------
        const auto& central_run = runs.at(r.central);
        const double t_today = central_run.warming_total[k_today], t_2100 = central_run.warming_total[k_last];
        const double co2_forcing_today = central_run.forcing.at("co2")[k_today];
        Json couplings = Json::array();
        auto link = [&](const char* from, const char* to, const std::string& mechanism, double magnitude, const char* unit, const char* strength, const char* source) {
            couplings.push_back({{"from", from}, {"to", to}, {"mechanism", mechanism}, {"magnitude", number(magnitude, 3)}, {"unit", unit}, {"strength", strength}, {"source", source}});
        };
        link("carbon", "atmosphere", "CO2 traps heat (5.35 ln C/C0)", co2_forcing_today, "W/m² today", "dominant", "earth climate");
        link("atmosphere", "water_vapour_clouds", "warmer air holds more water vapour; clouds change", r.feedbacks.water_vapour_lapse_rate_w_m2_k + r.feedbacks.clouds_w_m2_k, "W/m² per K (amplifying)", "strong", "earth climate");
        link("atmosphere", "ice", "melting snow and sea ice darken the surface", r.feedbacks.surface_albedo_w_m2_k, "W/m² per K (amplifying)", "moderate", "earth climate");
        link("atmosphere", "ocean", "the ocean takes up heat and slows the warming", r.climate.exchange_w_m2_k, "W/m² per K", "strong", "earth climate");
        link("ocean", "sea_level", "warmer water expands", r.sea_level.thermal_expansion_m_per_k_yr * 1000.0, "mm/yr per K", "moderate", "sea level");
        link("ice", "sea_level", "glaciers and ice sheets melt", (r.sea_level.glacier_m_per_k_yr + r.greenland_m_yr_per_k + r.sea_level.antarctic_m_per_k_yr) * 1000.0, "mm/yr per K", "strong (over centuries)", "sea level, polar melt");
        link("atmosphere", "ice", "Arctic sea ice retreats", r.ice_loss_per_k, "million km² per K", "strong", "earth climate");
        link("carbon", "ocean", "CO2 dissolves and acidifies the sea", chem_0.ph_total - carbonate(co2_0 + 100.0, r.sst_c).ph_total, "pH per +100 ppm", "strong", "ocean carbon");
        link("atmosphere", "extremes", "hot extremes become more frequent", heat_extreme_factor(r.pattern.land_factor, r.heat_sigma_k, 50.0), "× per K (1-in-50-year heat)", "strong", "earth climate");
        link("atmosphere", "extremes", "warm air holds 7 % more water per K: heavier rain", (r.heavy_rain_per_k - 1.0) * 100.0, "% more frequent per K", "moderate", "earth climate");
        link("ocean", "extremes", "warmer seas raise the potential intensity of hurricanes", potential_intensity(1.0) / pi_0 * 100.0 - 100.0, "% per K of sea warming", "moderate", "storm systems");
        if (!polar.empty()) {
            const auto& pr = polar.count(r.scenarios[std::min<std::size_t>(1, r.scenarios.size() - 1)].polar_name) ? polar.at(r.scenarios[std::min<std::size_t>(1, r.scenarios.size() - 1)].polar_name) : polar.begin()->second;
            link("ice", "ocean", "Greenland meltwater and warming weaken the Atlantic overturning", pr.amoc0 - pr.amoc_sv.back(), "Sv lost by the end of the polar run", "strong (tipping risk)", "polar melt");
            link("ocean", "atmosphere", "a weaker overturning cools the North Atlantic region", r.pattern.north_atlantic_cooling_per_sv_k, "K per Sv", "regional", "polar melt, earth climate");
        }
        if (!r.solar_path.empty()) {
            const Json doc = load_json(r.solar_path, "solar-cycle timeline");
            link("sun", "atmosphere", "the 11-year cycle of the Sun's brightness", doc["solar"]["climate"]["forcing_at_reference_w_m2"].get<double>(), "W/m² peak to peak", "weak", "solar cycle");
            link("sun", "atmosphere", "UV heats the stratosphere and makes ozone", doc["solar"]["irradiance"]["reference"]["uv_share_of_tsi_change"].get<double>() * 100.0, "% of the TSI change is UV", "regional (stratosphere)", "solar cycle");
        }
        if (!r.wind_path.empty()) {
            link("space", "atmosphere", "cosmic rays ionise the air; cloud link constrained", cloud_constrained, "W/m² over a cycle (at most)", "negligible", "solar wind");
            link("space", "upper_atmosphere", "solar wind heats the thermosphere in storms", links["solar_wind"]["thermosphere_density_storm_ratio"].get<double>(), "× air density at 400 km", "strong (upper atmosphere only)", "solar wind");
            link("space", "atmosphere", "solar protons destroy polar ozone above 35 km (the storm of the solar-wind run)", -links["solar_wind"]["storm_upper_ozone_loss_pct"].get<double>(),
                 "% of the upper polar ozone, for weeks", "regional", "solar wind");
        }
        if (!r.dust_path.empty()) link("space", "atmosphere", "meteoric smoke from cosmic dust", dust_bound, "W/m² (at most)", "negligible", "cosmic dust");
        if (!r.forecast_path.empty()) {
            const Json doc = load_json(r.forecast_path, "earth forecast timeline");
            const Json& c = doc["earth_forecast"]["couplings"];
            if (!c.is_null())
                for (const Json& run : c["runs"]) {
                    // The forecast's sensitivity runs: how much of the thunderstorms, the rain or the daytime heat a
                    // coupling carries over the first days (strength by the size of the change).
                    const std::string key = run["run"].get<std::string>();
                    const double thunder = -run["thunder_change_pct"].get<double>(), rain = -run["precip_change_pct"].get<double>();
                    const double tmax = std::abs(run["tmax_land_change_k"].get<double>());
                    auto by_pct = [](double pct) { return std::abs(pct) >= 20.0 ? "strong" : std::abs(pct) >= 5.0 ? "moderate" : "weak"; };
                    auto by_k = [](double k) { return k >= 1.0 ? "strong" : k >= 0.3 ? "moderate" : "weak"; };
                    if (key == "no_ocean_coupling") link("ocean", "weather", "sea heat and moisture feed the thunderstorms (" + std::to_string(static_cast<int>(std::lround(rain))) + " % of the rain)", thunder, "% of the thunderstorms", by_pct(thunder), "earth forecast");
                    if (key == "no_sun") link("sun", "weather", "daytime heating drives thunderstorms and the daily temperature cycle", thunder, "% of the thunderstorms", by_pct(thunder), "earth forecast");
                    if (key == "uniform_soil") link("land", "weather", "dry and wet soils shape the daytime heat", tmax, "K of land maximum temperature", by_k(tmax), "earth forecast");
                    if (key == "flat_terrain") link("land", "weather", "mountains cool and lift the air (orographic rain, " + std::to_string(static_cast<int>(std::lround(thunder))) + " % of the thunderstorms)", tmax, "K of land maximum temperature", by_k(tmax), "earth forecast");
                }
        }

        // ---- Output ----------------------------------------------------------------------------------------------
        Json scenario_rows = Json::array();
        for (const auto& sc : r.scenarios) scenario_rows.push_back({{"key", sc.key}, {"name", sc.name}, {"polar_scenario", sc.polar_name.empty() ? Json(nullptr) : Json(sc.polar_name)}});
        Json land_mask = Json::array();
        for (unsigned char v : r.land) land_mask.push_back(v != 0);
        Json drivers = Json::object();
        for (const auto& sc : r.scenarios) drivers[sc.key] = {{"today", driver_rows(runs.at(sc.key), k_today)}, {"y2050", driver_rows(runs.at(sc.key), k_of(2050))}, {"y2100", driver_rows(runs.at(sc.key), k_last)}};
        std::vector<int> year_list(years);
        for (int k = 0; k < years; ++k) year_list[k] = year_at(k);
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"earth_climate", {
                {"schema_version", "terraflux.earth.climate/v1"},
                {"method", "forcing_attribution_two_layer_energy_balance"},
                {"source_kind", "engine_output"},
                {"request", r.echo},
                {"years", year_list}, {"today", r.today}, {"reference", {r.ref0, r.ref1}}, {"central_scenario", r.central}, {"scenarios", scenario_rows},
                {"agents", [&] { Json a = Json::array(); for (const auto& k : agent_order) a.push_back({{"key", k}, {"label", r.agents.at(k).label}, {"group", r.agents.at(k).group}}); return a; }()},
                {"climate", {{"feedback_w_m2_k", tidy(r.climate.feedback_w_m2_k)}, {"ecs_k", tidy(ecs, 3)}, {"tcr_k", tidy(tcr, 3)}, {"ecs_low_k", r.ecs_low}, {"ecs_high_k", r.ecs_high},
                             {"feedbacks", {{"planck_w_m2_k", r.feedbacks.planck_w_m2_k}, {"water_vapour_lapse_rate_w_m2_k", r.feedbacks.water_vapour_lapse_rate_w_m2_k},
                                            {"surface_albedo_w_m2_k", r.feedbacks.surface_albedo_w_m2_k}, {"clouds_w_m2_k", r.feedbacks.clouds_w_m2_k}, {"other_w_m2_k", r.feedbacks.other_w_m2_k}}},
                             {"feedback_shares_today", shares_json(shares_today)}, {"feedback_shares_2100", shares_json(shares_2100)},
                             {"warming_today_k", tidy(t_today, 3)}, {"warming_2100_k", tidy(t_2100, 3)}}},
                {"drivers", drivers},
                {"series", impact_series},
                {"impacts", impact_table},
                {"map", {{"cell_degrees", {dlon, dlat}}, {"land_mask", land_mask}, {"land_mask_source", r.land_source}, {"pattern", fixed_array(pattern, 2)},
                         {"frames", map_frames}}},
                {"co2", {{"emissions_t_km2_yr", fixed_array(emission_density, 0)}, {"sinks_t_km2_yr", fixed_array(sink_density, 0)}, {"net_t_km2_yr", fixed_array(net_density, 0)},
                         {"cumulative_gt_co2", fixed_array(cumulative, 2)}, {"surface_ppm_anomaly", fixed_array(ppm_anomaly, 2)}, {"warming_per_100ppm_k", fixed_array(warming_100, 2)},
                         {"ph_drop_per_100ppm", fixed_array(ph_drop, 3)}, {"aragonite_now", fixed_array(omega_now, 2)}, {"aragonite_2100", fixed_array(omega_2100, 2)},
                         {"impact_score", fixed_array(impact_score, 2)}, {"region_index", region_of},
                         {"budget", {{"fossil_gt_co2_yr", tidy(fossil_sum)}, {"land_use_gt_co2_yr", tidy(land_use_sum)}, {"land_sink_gt_co2_yr", tidy(land_total)},
                                     {"ocean_sink_gt_co2_yr", tidy(ocean_total)}, {"atmosphere_gt_co2_yr", tidy(growth_gt)}, {"growth_ppm_yr", tidy(growth_gt / kGtCo2PerPpm, 3)},
                                     {"airborne_fraction", tidy(growth_gt / (fossil_sum + land_use_sum), 3)}, {"cumulative_total_gt_co2", r.cumulative_total},
                                     {"tcre_k_per_1000_gt", r.tcre_k_per_1000gt}, {"warming_from_cumulative_k", tidy(r.cumulative_total * r.tcre_k_per_1000gt / 1000.0, 3)},
                                     {"north_minus_south_ppm", tidy(nh / wn - sh / ws, 3)}, {"transient_warming_per_100ppm_k", tidy(transient_per_100ppm, 3)}}},
                         {"regions", region_rows}, {"sinks", sink_rows}, {"hotspots", hotspot_rows}}},
                {"couplings", couplings},
                {"links", links},
                {"limitations", Json::array({
                    "one global two-layer energy balance with a fixed feedback: no regional dynamics, no feedback that changes with the climate state, no carbon-cycle feedback (concentrations are prescribed)",
                    "forcing agents other than CO2, CH4 and N2O follow hand-entered indices scaled to rounded AR6 2019 estimates; the scenario paths approximate SSP1-2.6, SSP2-4.5 and SSP5-8.5",
                    "regional warming is a fixed pattern (land, Arctic, Southern Ocean) plus an aerosol and an overturning term, not a simulation",
                    "impacts use rounded literature relations (AR6 extreme frequencies reproduced by a shifted normal distribution, 7 %/K rain, linear sea ice)",
                    "the CO2 map spreads rounded national totals over boxes and distributes the land and ocean sinks by simple proxies; the concentration anomaly is a diffusion estimate",
                    "the space drivers (cosmic dust, cosmic rays, solar-wind energy) enter as bounds from the other runs"})},
            }},
        };
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error(std::string("cannot write output: ") + argv[2]);
        out << output.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
