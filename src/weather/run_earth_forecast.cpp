// terraflux-run-earth-forecast: a coupled ensemble forecast of the weather, the wind and severe weather, 1-10 days.
//
// Reads a terraflux.earth.forecast-run/v1 request:
//   grid, time, ensemble   the region (local azimuthal-equidistant grid), the lead time, members and their errors
//   atmosphere, synoptic   friction and mixing of the coupled layer; the highs, lows and new cyclones that steer it
//   air_masses             initial warm/cold and moist/dry anomalies
//   surface                land mask, terrain, sea-surface temperature, land climate, soil moisture, coupling
//   hurricanes             tropical cyclone seeds (tracked by the storm-systems kernel)
//   column, clouds, precipitation, severe, gusts    the diagnostic kernels
//   warnings               hazard thresholds and the probability that triggers a warning
//   sensitivity            optional runs of the control with one coupling switched off
//   cities                 places for point forecasts
// Every member runs the coupled wind layer hour by hour under its own evolving pressure systems, coupled to the sea,
// the land and the Sun; clouds, precipitation, severe convection and tropical cyclones are diagnosed every few hours.
// A hidden "truth" member verifies the forecast. Writes a viewer timeline (ensemble-mean air temperature and wind
// every 3 h for the first 3 days, then every 12 h) and an `earth_forecast` block (terraflux.earth.forecast/v1).
#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/cloud_formation.hpp"
#include "terraflux/weather/earth_forecast.hpp"
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/precipitation_outcomes.hpp"
#include "terraflux/weather/severe_convection.hpp"
#include "terraflux/weather/storm_systems.hpp"
#include "terraflux/weather/synthetic_terrain.hpp"
#include "utc_time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;

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

const Json* list_of(const Json& j, const char* key, std::size_t max, bool allow_empty = false) {
    if (!j.contains(key)) return nullptr;
    const Json& list = j.at(key);
    if (!list.is_array() || (!allow_empty && list.empty()) || list.size() > max)
        throw std::invalid_argument(std::string(key) + " must list " + (allow_empty ? "0" : "1") + "-" + std::to_string(max) + " entries");
    return &list;
}

double tidy(double v, int digits = 4) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*g", digits, v);
    return std::strtod(buffer, nullptr);
}

// Fixed decimals (maps): shorter than significant digits for values like temperatures.
double fixed(double v, int decimals) {
    const double s = std::pow(10.0, decimals);
    const double r = std::round(v * s) / s;
    return r == 0.0 ? 0.0 : r;
}

Json fixed_array(const std::vector<double>& values, int decimals) {
    Json out = Json::array();
    for (double v : values) out.push_back(std::isfinite(v) ? Json(fixed(v, decimals)) : Json(nullptr));
    return out;
}

Json percent_array(const std::vector<double>& probabilities) {
    Json out = Json::array();
    for (double p : probabilities) out.push_back(static_cast<int>(std::lround(100.0 * std::clamp(p, 0.0, 1.0))));
    return out;
}

// ---- The request --------------------------------------------------------------------------------------------------

struct Anomaly { double lat, lon, radius_km, amplitude; };
struct Ridge { std::string name; double lat, lon, orientation_deg, length_km, half_width_km, height_m; };
struct HurricaneSeed { std::string name; double lat, lon, start_hours, max_wind_m_s, radius_km; };
struct City { std::string name; double lat, lon; };

struct WarningConfig {
    double min_probability{0.3};
    double tornado_min_probability{0.15};
    double red_min_probability{0.5};          // red (the most severe level) needs more agreement among the members
    std::array<double, 3> gust_m_s{18.0, 25.0, 33.0};
    std::array<double, 3> rain_24h_mm{30.0, 60.0, 100.0};
    std::array<double, 3> runoff_72h_mm{25.0, 60.0, 120.0};
    std::array<double, 3> hail_cm{1.0, 2.0, 4.0};           // yellow: any thunderstorm; orange/red: hail
    std::array<double, 3> tornado_stp{1.0, 3.0, 6.0};
    std::array<double, 3> hurricane_wind_m_s{18.0, 33.0, 50.0};
    std::array<double, 3> surge_m{1.0, 2.0, 3.0};
};

struct SensitivityRun { std::string name, key, description; };

struct Request {
    Json echo;
    TransportGrid2D grid;
    std::string start_utc{"2026-09-08T00:00:00Z"};
    int days{10};
    double timestep_s{600.0};
    int detail_hours{72}, detail_every{3}, later_every{12}, diagnose_detail_every{6};
    int members{10};
    std::uint64_t seed{2026}, truth_seed{777};
    int threads{4};
    EnsembleSpread spread;
    double friction{2.0e-5}, diffusion{4.0e4};
    std::string coriolis_mode{"beta_plane"};
    double background_pa{101300.0};
    SteeringFlow steering;
    std::vector<SynopticSystem> systems;
    std::vector<GenesisRegion> genesis;
    std::vector<Anomaly> warm_anomalies, moist_anomalies;
    std::vector<unsigned char> land;
    std::string land_source;
    std::vector<std::array<double, 2>> sst_profile{{0.0, 302.0}, {20.0, 301.5}, {30.0, 299.5}, {40.0, 293.0}, {50.0, 286.5}, {60.0, 281.0}, {70.0, 276.0}};
    std::vector<Anomaly> sst_anomalies;
    std::vector<std::array<double, 2>> land_profile{{0.0, 299.5}, {20.0, 300.5}, {30.0, 298.5}, {40.0, 292.5}, {50.0, 286.0}, {60.0, 280.0}, {70.0, 274.0}};
    double lapse_k_per_km{6.5};
    double soil_background{0.5};
    std::vector<Anomaly> soil_anomalies;
    std::vector<Ridge> ridges;
    SurfaceCouplingConfig coupling;
    std::vector<HurricaneSeed> hurricanes;
    StormSystemsConfig storm_config;
    ColumnConfig column;
    CloudFormationConfig clouds;
    PrecipitationConfig precipitation;
    double moist_depth_m{2500.0};            // depth of the moist layer that convective and frontal rain draws on
    double rain_floor_rh{0.5};               // rain dries the air at most down to this relative humidity
    double hail_full_freezing_level_m{3000.0};   // large hail reaches the ground in full below this freezing level
    double hail_none_freezing_level_m{4500.0};   // and melts (or never grows) above this one
    SevereConvectionConfig severe;
    GustConfig gusts;
    CycloneHazardConfig cyclone;
    SynopticWeatherConfig weather;
    WarningConfig warnings;
    std::vector<SensitivityRun> sensitivity;
    int sensitivity_days{5};
    std::vector<City> cities;
};

std::vector<std::array<double, 2>> parse_profile(const Json& list, const char* label) {
    if (!list.is_array() || list.size() < 2 || list.size() > 40) throw std::invalid_argument(std::string(label) + " needs 2-40 [latitude, value] points");
    std::vector<std::array<double, 2>> out;
    for (const Json& p : list) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) throw std::invalid_argument(std::string(label) + " points are [latitude, value]");
        out.push_back({p[0].get<double>(), p[1].get<double>()});
        if (out.size() > 1 && !(out.back()[0] > out[out.size() - 2][0])) throw std::invalid_argument(std::string(label) + " latitudes must increase");
    }
    return out;
}

double profile_at(const std::vector<std::array<double, 2>>& p, double lat) {
    const double a = std::abs(lat);
    if (a <= p.front()[0]) return p.front()[1];
    if (a >= p.back()[0]) return p.back()[1];
    for (std::size_t k = 1; k < p.size(); ++k)
        if (a <= p[k][0]) return p[k - 1][1] + (a - p[k - 1][0]) / (p[k][0] - p[k - 1][0]) * (p[k][1] - p[k - 1][1]);
    return p.back()[1];
}

std::vector<Anomaly> parse_anomalies(const Json& j, const char* key, double max_amplitude) {
    std::vector<Anomaly> out;
    if (const Json* list = list_of(j, key, 40, true))
        for (const Json& x : *list) {
            only_keys(x, key, {"latitude_deg", "longitude_deg", "radius_km", "amplitude", "note", "name"});
            Anomaly a{required_number(x, "latitude_deg"), required_number(x, "longitude_deg"), required_number(x, "radius_km"), required_number(x, "amplitude")};
            if (!(std::abs(a.lat) <= 90.0 && std::abs(a.lon) <= 180.0 && a.radius_km > 0.0 && a.radius_km <= 5000.0 && std::abs(a.amplitude) <= max_amplitude))
                throw std::invalid_argument(std::string(key) + " need a position, a radius within 0-5000 km and |amplitude| <= " + std::to_string(max_amplitude));
            out.push_back(a);
        }
    return out;
}

Request parse_request(const Json& j) {
    Request r;
    r.echo = j;
    only_keys(j, "request", {"schema_version", "grid", "time", "ensemble", "atmosphere", "synoptic", "synoptic_weather", "air_masses", "surface", "hurricanes", "column",
                             "clouds", "precipitation", "severe", "gusts", "warnings", "sensitivity", "cities"});
    const Json& g = required(j, "grid");
    only_keys(g, "grid", {"nx", "ny", "spacing_m", "anchor_latitude_deg", "anchor_longitude_deg", "note"});
    r.grid.nx = static_cast<std::size_t>(optional_integer(g, "nx", 52, 8, 200));
    r.grid.ny = static_cast<std::size_t>(optional_integer(g, "ny", 32, 8, 200));
    const double spacing = required_number(g, "spacing_m");
    if (!(spacing >= 20000.0 && spacing <= 500000.0)) throw std::invalid_argument("grid.spacing_m must lie within 20-500 km");
    r.grid.spacing_east_m = r.grid.spacing_north_m = spacing;
    r.grid.origin_east_m = -0.5 * spacing * static_cast<double>(r.grid.nx);
    r.grid.origin_north_m = -0.5 * spacing * static_cast<double>(r.grid.ny);
    r.grid.geographic_anchor_latitude_deg = required_number(g, "anchor_latitude_deg");
    r.grid.geographic_anchor_longitude_deg = required_number(g, "anchor_longitude_deg");
    validate_transport_grid(r.grid);
    if (!(r.grid.geographic_anchor_latitude_deg >= 10.0 && r.grid.geographic_anchor_latitude_deg <= 70.0))
        throw std::invalid_argument("grid.anchor_latitude_deg must lie within 10-70 (a mid-latitude beta plane)");
    if (0.5 * spacing * static_cast<double>(std::max(r.grid.nx, r.grid.ny)) > 7.0e6) throw std::invalid_argument("the grid must lie within 7000 km of its anchor");

    if (j.contains("time")) {
        const Json& t = j.at("time");
        only_keys(t, "time", {"start_utc", "days", "timestep_seconds", "detail_hours", "detail_every_hours", "later_every_hours", "diagnose_detail_every_hours", "note"});
        r.start_utc = optional_string(t, "start_utc", r.start_utc);
        r.days = static_cast<int>(optional_integer(t, "days", r.days, 1, 15));
        r.timestep_s = optional_number(t, "timestep_seconds", r.timestep_s);
        r.detail_hours = static_cast<int>(optional_integer(t, "detail_hours", r.detail_hours, 0, 360));
        r.detail_every = static_cast<int>(optional_integer(t, "detail_every_hours", r.detail_every, 1, 24));
        r.later_every = static_cast<int>(optional_integer(t, "later_every_hours", r.later_every, 1, 24));
        r.diagnose_detail_every = static_cast<int>(optional_integer(t, "diagnose_detail_every_hours", r.diagnose_detail_every, 1, 24));
    }
    terraflux::weather::detail::utc_seconds(r.start_utc);
    if (!(r.timestep_s >= 60.0 && r.timestep_s <= 1800.0 && std::fmod(3600.0, r.timestep_s) == 0.0)) throw std::invalid_argument("time.timestep_seconds must divide an hour, within 60-1800");
    if (24 % r.later_every || 24 % r.detail_every || r.diagnose_detail_every % r.detail_every || 24 % r.diagnose_detail_every || r.detail_hours % r.later_every ||
        r.detail_hours % r.diagnose_detail_every)
        throw std::invalid_argument("time: the frame and diagnosis intervals must divide a day and each other, and the detail period must end on both");
    if (r.detail_hours > 24 * r.days) throw std::invalid_argument("time.detail_hours must lie within the forecast");

    if (j.contains("ensemble")) {
        const Json& e = j.at("ensemble");
        only_keys(e, "ensemble", {"members", "seed", "truth_seed", "threads", "spread", "note"});
        r.members = static_cast<int>(optional_integer(e, "members", r.members, 2, 50));
        r.seed = static_cast<std::uint64_t>(optional_integer(e, "seed", static_cast<long long>(r.seed), 0, 1LL << 40));
        r.truth_seed = static_cast<std::uint64_t>(optional_integer(e, "truth_seed", static_cast<long long>(r.truth_seed), 0, 1LL << 40));
        r.threads = static_cast<int>(optional_integer(e, "threads", r.threads, 1, 64));
        auto& s = r.spread;
        read_numbers(e, "spread", {{"position_km", &s.position_km}, {"speed_m_s", &s.speed_m_s}, {"deepening_pa_per_day", &s.deepening_pa_per_day},
                                   {"radius_fraction", &s.radius_fraction}, {"genesis_position_km", &s.genesis_position_km}, {"genesis_timing_hours", &s.genesis_timing_hours},
                                   {"temperature_k", &s.temperature_k}, {"hurricane_position_km", &s.hurricane_position_km}, {"hurricane_wind_m_s", &s.hurricane_wind_m_s}});
    }
    validate_ensemble_spread(r.spread);
    if (r.seed == r.truth_seed) throw std::invalid_argument("ensemble.truth_seed must differ from the seed: the truth must not be one of the members");
    read_numbers(j, "atmosphere", {{"surface_friction_coefficient_s", &r.friction}, {"diffusion_m2_s", &r.diffusion}});
    if (j.contains("atmosphere") && j.at("atmosphere").contains("coriolis_mode")) throw std::invalid_argument("atmosphere.coriolis_mode is fixed to beta_plane");
    if (!(r.friction >= 0.0 && r.friction <= 1e-3 && r.diffusion >= 0.0 && r.diffusion <= 5e5)) throw std::invalid_argument("atmosphere: friction within 0-1e-3 1/s, diffusion within 0-5e5 m2/s");

    const Json& sy = required(j, "synoptic");
    only_keys(sy, "synoptic", {"background_pressure_pa", "steering", "systems", "genesis", "note"});
    r.background_pa = optional_number(sy, "background_pressure_pa", r.background_pa);
    if (!(r.background_pa >= 95000.0 && r.background_pa <= 105000.0)) throw std::invalid_argument("synoptic.background_pressure_pa must lie within 950-1050 hPa");
    read_numbers(sy, "steering", {{"jet_latitude_deg", &r.steering.jet_latitude_deg}, {"jet_speed_m_s", &r.steering.jet_speed_m_s}, {"jet_width_deg", &r.steering.jet_width_deg},
                                  {"trade_latitude_deg", &r.steering.trade_latitude_deg}, {"trade_speed_m_s", &r.steering.trade_speed_m_s}, {"trade_width_deg", &r.steering.trade_width_deg}});
    if (!(std::abs(r.steering.jet_speed_m_s) <= 60.0 && r.steering.jet_width_deg > 0.0 && std::abs(r.steering.trade_speed_m_s) <= 30.0 && r.steering.trade_width_deg > 0.0))
        throw std::invalid_argument("synoptic.steering: speeds within 60 (jet) and 30 (trades) m/s, positive widths");
    if (const Json* list = list_of(sy, "systems", 30))
        for (const Json& x : *list) {
            only_keys(x, "synoptic.systems[]", {"name", "kind", "latitude_deg", "longitude_deg", "anomaly_hpa", "radius_km", "deepening_hpa_per_day", "mature_hours",
                                                "filling_hpa_per_day", "steering_factor", "drift_east_m_s", "drift_north_m_s", "start_hours", "note"});
            SynopticSystem s;
            s.name = optional_string(x, "name", "system");
            s.kind = optional_string(x, "kind", "low");
            s.latitude_deg = required_number(x, "latitude_deg");
            s.longitude_deg = required_number(x, "longitude_deg");
            s.anomaly_pa = 100.0 * required_number(x, "anomaly_hpa");
            s.radius_m = 1000.0 * required_number(x, "radius_km");
            s.deepening_pa_per_day = 100.0 * optional_number(x, "deepening_hpa_per_day", 0.0);
            s.mature_hours = optional_number(x, "mature_hours", 48.0);
            s.filling_pa_per_day = 100.0 * optional_number(x, "filling_hpa_per_day", 5.0);
            s.steering_factor = optional_number(x, "steering_factor", s.kind == "low" ? 0.8 : 0.3);
            s.drift_east_m_s = optional_number(x, "drift_east_m_s", 0.0);
            s.drift_north_m_s = optional_number(x, "drift_north_m_s", 0.0);
            s.start_hours = optional_number(x, "start_hours", 0.0);
            if (!(std::abs(s.latitude_deg) <= 90.0 && std::abs(s.longitude_deg) <= 180.0 && std::abs(s.anomaly_pa) <= 8000.0 && s.radius_m >= 1e5 && s.radius_m <= 4e6 &&
                  std::abs(s.deepening_pa_per_day) <= 5000.0 && s.mature_hours >= 0.0 && s.filling_pa_per_day >= 0.0 && s.steering_factor >= 0.0 && s.steering_factor <= 2.0 &&
                  s.start_hours >= 0.0))
                throw std::invalid_argument("synoptic systems: |anomaly| <= 80 hPa, radius 100-4000 km, |deepening| <= 50 hPa/day, steering_factor 0-2, non-negative times");
            r.systems.push_back(s);
        }
    if (const Json* list = list_of(sy, "genesis", 10, true))
        for (const Json& x : *list) {
            only_keys(x, "synoptic.genesis[]", {"name", "latitude_deg", "longitude_deg", "first_hours", "every_hours", "last_hours", "anomaly_hpa", "radius_km",
                                                "deepening_hpa_per_day", "mature_hours", "filling_hpa_per_day", "steering_factor", "drift_north_m_s", "note"});
            GenesisRegion gr;
            gr.name = optional_string(x, "name", "genesis");
            gr.latitude_deg = required_number(x, "latitude_deg");
            gr.longitude_deg = required_number(x, "longitude_deg");
            gr.first_hours = optional_number(x, "first_hours", gr.first_hours);
            gr.every_hours = optional_number(x, "every_hours", gr.every_hours);
            gr.last_hours = optional_number(x, "last_hours", gr.last_hours);
            gr.anomaly_pa = 100.0 * optional_number(x, "anomaly_hpa", gr.anomaly_pa / 100.0);
            gr.radius_m = 1000.0 * optional_number(x, "radius_km", gr.radius_m / 1000.0);
            gr.deepening_pa_per_day = 100.0 * optional_number(x, "deepening_hpa_per_day", gr.deepening_pa_per_day / 100.0);
            gr.mature_hours = optional_number(x, "mature_hours", gr.mature_hours);
            gr.filling_pa_per_day = 100.0 * optional_number(x, "filling_hpa_per_day", gr.filling_pa_per_day / 100.0);
            gr.steering_factor = optional_number(x, "steering_factor", gr.steering_factor);
            gr.drift_north_m_s = optional_number(x, "drift_north_m_s", gr.drift_north_m_s);
            if (!(gr.every_hours >= 6.0 && gr.first_hours >= 0.0 && gr.anomaly_pa < 0.0 && gr.anomaly_pa >= -8000.0 && gr.radius_m >= 1e5 && gr.radius_m <= 4e6))
                throw std::invalid_argument("genesis regions: every_hours >= 6, a negative anomaly down to -80 hPa, radius 100-4000 km");
            r.genesis.push_back(gr);
        }
    auto& sw = r.weather;
    read_numbers(j, "synoptic_weather", {{"low_ascent_m_s", &sw.low_ascent_m_s}, {"high_subsidence_m_s", &sw.high_subsidence_m_s}, {"warm_sector_bias", &sw.warm_sector_bias},
                                         {"warm_sector_direction_deg", &sw.warm_sector_direction_deg}, {"convergence_depth_m", &sw.convergence_depth_m}, {"cloud_rh", &sw.cloud_rh},
                                         {"full_cloud_rh", &sw.full_cloud_rh}, {"rain_rh", &sw.rain_rh}, {"efficiency", &sw.efficiency},
                                         {"orographic_lift_factor", &sw.orographic_lift_factor}});
    validate_synoptic_weather(sw);
    if (j.contains("air_masses")) {
        const Json& a = j.at("air_masses");
        only_keys(a, "air_masses", {"temperature", "humidity", "note"});
        r.warm_anomalies = parse_anomalies(a, "temperature", 20.0);
        r.moist_anomalies = parse_anomalies(a, "humidity", 10.0);   // g/kg
    }

    const Json& su = required(j, "surface");
    only_keys(su, "surface", {"land_mask", "land_mask_source", "sst_profile", "sst_anomalies", "land_profile", "lapse_rate_k_per_km", "soil_moisture", "soil_anomalies", "terrain", "coupling", "note"});
    const Json& rows = required(su, "land_mask");
    if (!rows.is_array() || rows.size() != r.grid.ny) throw std::invalid_argument("surface.land_mask needs one string per row (north first)");
    r.land.assign(r.grid.nx * r.grid.ny, 0);
    for (std::size_t row = 0; row < r.grid.ny; ++row) {
        if (!rows[row].is_string() || rows[row].get_ref<const std::string&>().size() != r.grid.nx) throw std::invalid_argument("surface.land_mask rows need one character per cell");
        const std::string& line = rows[row].get_ref<const std::string&>();
        for (std::size_t col = 0; col < r.grid.nx; ++col) {
            if (line[col] != '#' && line[col] != '.') throw std::invalid_argument("surface.land_mask characters must be '#' (land) or '.' (sea)");
            r.land[(r.grid.ny - 1 - row) * r.grid.nx + col] = line[col] == '#';
        }
    }
    r.land_source = optional_string(su, "land_mask_source", "unlabelled");
    if (su.contains("sst_profile")) r.sst_profile = parse_profile(su.at("sst_profile"), "surface.sst_profile");
    if (su.contains("land_profile")) r.land_profile = parse_profile(su.at("land_profile"), "surface.land_profile");
    for (const auto& p : r.sst_profile) if (!(p[1] >= 268.0 && p[1] <= 308.0)) throw std::invalid_argument("surface.sst_profile values must lie within 268-308 K");
    for (const auto& p : r.land_profile) if (!(p[1] >= 230.0 && p[1] <= 320.0)) throw std::invalid_argument("surface.land_profile values must lie within 230-320 K");
    r.sst_anomalies = parse_anomalies(su, "sst_anomalies", 10.0);
    r.lapse_k_per_km = optional_number(su, "lapse_rate_k_per_km", r.lapse_k_per_km);
    r.soil_background = optional_number(su, "soil_moisture", r.soil_background);
    r.soil_anomalies = parse_anomalies(su, "soil_anomalies", 1.0);
    if (!(r.lapse_k_per_km >= 0.0 && r.lapse_k_per_km <= 10.0 && r.soil_background >= 0.0 && r.soil_background <= 1.0))
        throw std::invalid_argument("surface: lapse_rate_k_per_km within 0-10, soil_moisture within 0-1");
    if (su.contains("terrain")) {
        const Json& t = su.at("terrain");
        only_keys(t, "surface.terrain", {"ridges", "note"});
        if (const Json* list = list_of(t, "ridges", 40, true))
            for (const Json& x : *list) {
                only_keys(x, "surface.terrain.ridges[]", {"name", "latitude_deg", "longitude_deg", "orientation_deg", "length_km", "half_width_km", "height_m", "note"});
                Ridge rg{optional_string(x, "name", "ridge"), required_number(x, "latitude_deg"), required_number(x, "longitude_deg"), optional_number(x, "orientation_deg", 0.0),
                         required_number(x, "length_km"), required_number(x, "half_width_km"), required_number(x, "height_m")};
                if (!(rg.length_km > 0.0 && rg.half_width_km > 0.0 && rg.height_m >= 0.0 && rg.height_m <= 5000.0))
                    throw std::invalid_argument("terrain ridges need a positive length and width and a height within 0-5000 m");
                r.ridges.push_back(rg);
            }
    }
    auto& c = r.coupling;
    read_numbers(su, "coupling", {{"ocean_temperature_time_h", &c.ocean_temperature_time_h}, {"sea_air_offset_k", &c.sea_air_offset_k}, {"ocean_moisture_time_h", &c.ocean_moisture_time_h},
                                  {"sea_relative_humidity", &c.sea_relative_humidity}, {"land_heating_k_per_h", &c.land_heating_k_per_h}, {"dry_soil_warming_k", &c.dry_soil_warming_k},
                                  {"cloud_shading", &c.cloud_shading}, {"wet_soil_heating_reduction", &c.wet_soil_heating_reduction},
                                  {"evapotranspiration_g_kg_per_h", &c.evapotranspiration_g_kg_per_h}, {"land_climate_time_h", &c.land_climate_time_h},
                                  {"soil_capacity_mm", &c.soil_capacity_mm}, {"drainage_mm_per_day", &c.drainage_mm_per_day}, {"boundary_layer_depth_m", &c.boundary_layer_depth_m}});
    validate_surface_coupling(c);

    r.storm_config.genesis_gpi_threshold = 1.0e6;   // tropical cyclones only from the seeds unless configured
    if (j.contains("hurricanes")) {
        const Json& h = j.at("hurricanes");
        only_keys(h, "hurricanes", {"seeds", "genesis_gpi_threshold", "sst_threshold_k", "intensification_time_scale_h", "steering_factor", "wind_decay_exponent",
                                    "rain_scale_km", "surge_reach_km", "note"});
        r.cyclone.decay_exponent = optional_number(h, "wind_decay_exponent", r.cyclone.decay_exponent);
        r.cyclone.rain_scale_m = 1000.0 * optional_number(h, "rain_scale_km", r.cyclone.rain_scale_m / 1000.0);
        r.cyclone.surge_reach_m = 1000.0 * optional_number(h, "surge_reach_km", r.cyclone.surge_reach_m / 1000.0);
        if (!(r.cyclone.decay_exponent >= 0.3 && r.cyclone.decay_exponent <= 1.0 && r.cyclone.rain_scale_m >= 1e4 && r.cyclone.rain_scale_m <= 1e6 &&
              r.cyclone.surge_reach_m >= 1e4 && r.cyclone.surge_reach_m <= 1e6))
            throw std::invalid_argument("hurricanes: wind_decay_exponent within 0.3-1, rain_scale_km and surge_reach_km within 10-1000");
        r.storm_config.genesis_gpi_threshold = optional_number(h, "genesis_gpi_threshold", r.storm_config.genesis_gpi_threshold);
        r.storm_config.sst_threshold_k = optional_number(h, "sst_threshold_k", r.storm_config.sst_threshold_k);
        r.storm_config.intensification_time_scale_s = 3600.0 * optional_number(h, "intensification_time_scale_h", r.storm_config.intensification_time_scale_s / 3600.0);
        r.storm_config.steering_factor = optional_number(h, "steering_factor", r.storm_config.steering_factor);
        if (const Json* list = list_of(h, "seeds", 8, true))
            for (const Json& x : *list) {
                only_keys(x, "hurricanes.seeds[]", {"name", "latitude_deg", "longitude_deg", "start_hours", "max_wind_m_s", "radius_max_wind_km", "note"});
                HurricaneSeed s{optional_string(x, "name", "storm"), required_number(x, "latitude_deg"), required_number(x, "longitude_deg"), optional_number(x, "start_hours", 0.0),
                                required_number(x, "max_wind_m_s"), optional_number(x, "radius_max_wind_km", 40.0)};
                if (!(s.max_wind_m_s >= 10.0 && s.max_wind_m_s <= 90.0 && s.radius_km >= 10.0 && s.radius_km <= 200.0 && s.start_hours >= 0.0))
                    throw std::invalid_argument("hurricane seeds need a wind within 10-90 m/s, a radius of maximum wind within 10-200 km and a non-negative start");
                r.hurricanes.push_back(s);
            }
    }
    r.storm_config.substep_seconds = 900.0;

    auto& col = r.column;
    col.layer_count = 16;
    col.layer_thickness_m = 800.0;
    col.parcel_step_m = 100.0;
    col.tropopause_height_m = 12000.0;
    if (j.contains("column")) {
        const Json& x = j.at("column");
        only_keys(x, "column", {"layer_count", "layer_thickness_m", "environment_lapse_rate_k_m", "tropopause_height_m", "humidity_scale_height_m", "parcel_step_m",
                                "inversion_base_m", "inversion_depth_m", "inversion_strength_k", "lapse_rate_above_inversion_k_m", "note"});
        col.layer_count = static_cast<std::size_t>(optional_integer(x, "layer_count", static_cast<long long>(col.layer_count), 4, 64));
        for (auto [key, target] : std::initializer_list<std::pair<const char*, double*>>{
                 {"layer_thickness_m", &col.layer_thickness_m}, {"environment_lapse_rate_k_m", &col.environment_lapse_rate_k_m}, {"tropopause_height_m", &col.tropopause_height_m},
                 {"humidity_scale_height_m", &col.humidity_scale_height_m}, {"parcel_step_m", &col.parcel_step_m}, {"inversion_base_m", &col.inversion_base_m},
                 {"inversion_depth_m", &col.inversion_depth_m}, {"inversion_strength_k", &col.inversion_strength_k}, {"lapse_rate_above_inversion_k_m", &col.lapse_rate_above_inversion_k_m}})
            *target = optional_number(x, key, *target);
    }
    validate_column_config(col);
    r.clouds.column = col;
    read_numbers(j, "clouds", {{"frontal_gradient_threshold_k_m", &r.clouds.frontal_gradient_threshold_k_m}, {"boundary_layer_depth_m", &r.clouds.boundary_layer_depth_m},
                               {"lifting_time_scale_s", &r.clouds.lifting_time_scale_s}, {"cin_trigger_j_kg", &r.clouds.cin_trigger_j_kg},
                               {"cape_threshold_j_kg", &r.clouds.cape_threshold_j_kg}, {"max_cloud_top_m", &r.clouds.max_cloud_top_m}});
    validate_cloud_formation_config(r.clouds);
    read_numbers(j, "precipitation", {{"stratiform_efficiency", &r.precipitation.stratiform_efficiency}, {"convective_efficiency", &r.precipitation.convective_efficiency},
                                      {"convective_updraft_fraction", &r.precipitation.convective_updraft_fraction}, {"hail_melting_cm_per_km", &r.precipitation.hail_melting_cm_per_km},
                                      {"hail_full_freezing_level_m", &r.hail_full_freezing_level_m}, {"hail_none_freezing_level_m", &r.hail_none_freezing_level_m},
                                      {"moist_depth_m", &r.moist_depth_m}, {"rain_floor_relative_humidity", &r.rain_floor_rh}});
    if (!(r.hail_full_freezing_level_m >= 0.0 && r.hail_none_freezing_level_m > r.hail_full_freezing_level_m && r.hail_none_freezing_level_m <= 10000.0))
        throw std::invalid_argument("precipitation: 0 <= hail_full_freezing_level_m < hail_none_freezing_level_m <= 10000");
    if (!(r.moist_depth_m >= 500.0 && r.moist_depth_m <= 10000.0 && r.rain_floor_rh >= 0.0 && r.rain_floor_rh < 1.0))
        throw std::invalid_argument("precipitation: moist_depth_m within 500-10000, rain_floor_relative_humidity within 0-1");
    validate_precipitation_config(r.precipitation);
    read_numbers(j, "severe", {{"stp_low", &r.severe.stp_low}, {"stp_moderate", &r.severe.stp_moderate}, {"stp_significant", &r.severe.stp_significant}});
    validate_severe_convection_config(r.severe);
    r.storm_config.column = col;
    validate_storm_systems_config(r.storm_config);
    read_numbers(j, "gusts", {{"gust_factor", &r.gusts.gust_factor}, {"convective_gust_m_s", &r.gusts.convective_gust_m_s}, {"terrain_reduction_per_km", &r.gusts.terrain_reduction_per_km},
                              {"surface_factor_land", &r.gusts.surface_factor_land}, {"surface_factor_sea", &r.gusts.surface_factor_sea}});
    if (!(r.gusts.gust_factor >= 1.0 && r.gusts.gust_factor <= 3.0 && r.gusts.convective_gust_m_s >= 0.0 && r.gusts.convective_gust_m_s <= 40.0 &&
          r.gusts.terrain_reduction_per_km >= 0.0 && r.gusts.terrain_reduction_per_km <= 0.5 && r.gusts.surface_factor_land >= 0.2 && r.gusts.surface_factor_land <= 1.0 &&
          r.gusts.surface_factor_sea >= 0.2 && r.gusts.surface_factor_sea <= 1.0))
        throw std::invalid_argument("gusts: gust_factor within 1-3, convective_gust_m_s within 0-40, terrain_reduction_per_km within 0-0.5, surface factors within 0.2-1");

    if (j.contains("warnings")) {
        const Json& w = j.at("warnings");
        only_keys(w, "warnings", {"min_probability", "tornado_min_probability", "red_min_probability", "gust_m_s", "rain_24h_mm", "runoff_72h_mm", "hail_cm", "tornado_stp", "hurricane_wind_m_s", "surge_m", "note"});
        r.warnings.min_probability = optional_number(w, "min_probability", r.warnings.min_probability);
        r.warnings.tornado_min_probability = optional_number(w, "tornado_min_probability", r.warnings.tornado_min_probability);
        r.warnings.red_min_probability = optional_number(w, "red_min_probability", r.warnings.red_min_probability);
        auto triple = [&](const char* key, std::array<double, 3>& target) {
            if (!w.contains(key)) return;
            const Json& x = w.at(key);
            if (!x.is_array() || x.size() != 3 || !std::all_of(x.begin(), x.end(), [](const Json& v) { return v.is_number(); }))
                throw std::invalid_argument(std::string("warnings.") + key + " needs three thresholds (yellow, orange, red)");
            for (int k = 0; k < 3; ++k) target[static_cast<std::size_t>(k)] = x[static_cast<std::size_t>(k)].get<double>();
            if (!(target[0] > 0.0 && target[0] < target[1] && target[1] < target[2])) throw std::invalid_argument(std::string("warnings.") + key + " thresholds must be positive and increasing");
        };
        triple("gust_m_s", r.warnings.gust_m_s);
        triple("rain_24h_mm", r.warnings.rain_24h_mm);
        triple("runoff_72h_mm", r.warnings.runoff_72h_mm);
        triple("hail_cm", r.warnings.hail_cm);
        triple("tornado_stp", r.warnings.tornado_stp);
        triple("hurricane_wind_m_s", r.warnings.hurricane_wind_m_s);
        triple("surge_m", r.warnings.surge_m);
        if (!(r.warnings.min_probability > 0.0 && r.warnings.min_probability <= 1.0 && r.warnings.tornado_min_probability > 0.0 && r.warnings.tornado_min_probability <= 1.0 &&
              r.warnings.red_min_probability > 0.0 && r.warnings.red_min_probability <= 1.0))
            throw std::invalid_argument("warnings: probabilities within 0-1");
    }
    if (j.contains("sensitivity")) {
        const Json& s = j.at("sensitivity");
        only_keys(s, "sensitivity", {"runs", "days", "note"});
        r.sensitivity_days = static_cast<int>(optional_integer(s, "days", r.sensitivity_days, 1, 15));
        const std::map<std::string, std::string> known{{"no_ocean_coupling", "the air over the sea keeps its initial heat and moisture (no fluxes from the ocean)"},
                                                       {"uniform_soil", "uniform soil moisture everywhere (no wet or dry regions)"},
                                                       {"flat_terrain", "no mountains (no blocking, no orographic rain)"},
                                                       {"no_sun", "no daytime heating and no nocturnal cooling over land"}};
        if (const Json* list = list_of(s, "runs", 4, true))
            for (const Json& x : *list) {
                if (!x.is_string() || !known.count(x.get<std::string>())) throw std::invalid_argument("sensitivity.runs are no_ocean_coupling, uniform_soil, flat_terrain or no_sun");
                r.sensitivity.push_back({x.get<std::string>(), x.get<std::string>(), known.at(x.get<std::string>())});
            }
        if (r.sensitivity_days > r.days) throw std::invalid_argument("sensitivity.days must lie within the forecast");
    }
    if (const Json* list = list_of(j, "cities", 40, true))
        for (const Json& x : *list) {
            only_keys(x, "cities[]", {"name", "latitude_deg", "longitude_deg"});
            City city{optional_string(x, "name", "city"), required_number(x, "latitude_deg"), required_number(x, "longitude_deg")};
            if (!(std::abs(city.lat) <= 90.0 && std::abs(city.lon) <= 180.0)) throw std::invalid_argument("cities need a valid latitude and longitude");
            r.cities.push_back(city);
        }
    return r;
}

// ---- The static world ---------------------------------------------------------------------------------------------

struct World {
    std::size_t n{0};
    std::vector<GeoPoint> geo;
    std::vector<double> elevation, slope, sst, land_climate, soil;
    std::vector<unsigned char> land;
    std::vector<unsigned char> warn;         // warnings are issued for land and the sea next to it (the coast)
    std::vector<std::size_t> city_cells;
    std::vector<bool> city_inside;
};

double anomaly_sum(const std::vector<Anomaly>& list, double lat, double lon) {
    double s = 0.0;
    for (const auto& a : list) {
        const double d = great_circle_km(lat, lon, a.lat, a.lon) / a.radius_km;
        s += a.amplitude * std::exp(-d * d);
    }
    return s;
}

World make_world(const Request& r, bool flat_terrain, bool uniform_soil) {
    World w;
    const auto& g = r.grid;
    w.n = g.nx * g.ny;
    w.land = r.land;
    for (std::size_t i = 0; i < w.n; ++i) {
        const double x = g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m;
        const double y = g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m;
        w.geo.push_back(local_to_geographic(x, y, g.geographic_anchor_latitude_deg, g.geographic_anchor_longitude_deg));
    }
    SyntheticTerrainSpec spec;
    for (const auto& rg : r.ridges) {
        const auto p = geographic_to_local(rg.lat, rg.lon, g.geographic_anchor_latitude_deg, g.geographic_anchor_longitude_deg);
        spec.ridges.push_back({rg.orientation_deg, p[0], p[1], rg.height_m, rg.half_width_km * 1000.0, rg.length_km * 1000.0});
    }
    w.elevation = spec.ridges.empty() || flat_terrain ? std::vector<double>(w.n, 0.0) : build_synthetic_terrain(spec, g);
    for (std::size_t i = 0; i < w.n; ++i) if (!w.land[i]) w.elevation[i] = 0.0;
    w.slope.assign(w.n, 0.0);
    for (std::size_t j = 0; j < g.ny; ++j)
        for (std::size_t i = 0; i < g.nx; ++i) {
            const std::size_t k = j * g.nx + i;
            const double dx = (w.elevation[j * g.nx + std::min(i + 1, g.nx - 1)] - w.elevation[j * g.nx + (i > 0 ? i - 1 : 0)]) / (2.0 * g.spacing_east_m);
            const double dy = (w.elevation[std::min(j + 1, g.ny - 1) * g.nx + i] - w.elevation[(j > 0 ? j - 1 : 0) * g.nx + i]) / (2.0 * g.spacing_north_m);
            w.slope[k] = std::hypot(dx, dy);
        }
    for (std::size_t i = 0; i < w.n; ++i) {
        const double lat = w.geo[i].latitude_deg, lon = w.geo[i].longitude_deg;
        w.sst.push_back(profile_at(r.sst_profile, lat) + anomaly_sum(r.sst_anomalies, lat, lon));
        w.land_climate.push_back(profile_at(r.land_profile, lat) - r.lapse_k_per_km * w.elevation[i] / 1000.0);
        w.soil.push_back(w.land[i] ? std::clamp(uniform_soil ? r.soil_background : r.soil_background + anomaly_sum(r.soil_anomalies, lat, lon), 0.02, 1.0) : 1.0);
    }
    w.warn.assign(w.n, 0);
    for (std::size_t j = 0; j < g.ny; ++j)
        for (std::size_t i = 0; i < g.nx; ++i) {
            const std::size_t k = j * g.nx + i;
            bool coast = w.land[k] != 0;
            for (int dj = -1; dj <= 1 && !coast; ++dj)
                for (int di = -1; di <= 1 && !coast; ++di) {
                    const long long x = static_cast<long long>(i) + di, y = static_cast<long long>(j) + dj;
                    if (x >= 0 && y >= 0 && x < static_cast<long long>(g.nx) && y < static_cast<long long>(g.ny) && w.land[static_cast<std::size_t>(y) * g.nx + static_cast<std::size_t>(x)]) coast = true;
                }
            w.warn[k] = coast;
        }
    for (const auto& c : r.cities) {
        const auto p = geographic_to_local(c.lat, c.lon, g.geographic_anchor_latitude_deg, g.geographic_anchor_longitude_deg);
        const double fx = (p[0] - g.origin_east_m) / g.spacing_east_m, fy = (p[1] - g.origin_north_m) / g.spacing_north_m;
        const bool inside = fx >= 0.0 && fy >= 0.0 && fx < static_cast<double>(g.nx) && fy < static_cast<double>(g.ny);
        const std::size_t ix = static_cast<std::size_t>(std::clamp(fx, 0.0, static_cast<double>(g.nx) - 1.0));
        const std::size_t iy = static_cast<std::size_t>(std::clamp(fy, 0.0, static_cast<double>(g.ny) - 1.0));
        w.city_cells.push_back(iy * g.nx + ix);
        w.city_inside.push_back(inside);
    }
    return w;
}

// ---- One member ---------------------------------------------------------------------------------------------------

struct Schedule {
    std::vector<int> frame_hours;        // output frames
    std::vector<int> diagnosis_hours;    // full physics
    int hours{0};
};

Schedule make_schedule(const Request& r, int days) {
    Schedule s;
    s.hours = 24 * days;
    for (int h = 0; h <= s.hours; h += (h < r.detail_hours ? r.detail_every : r.later_every)) s.frame_hours.push_back(h);
    if (s.frame_hours.back() != s.hours) s.frame_hours.push_back(s.hours);
    for (int h = 0; h <= s.hours; h += (h < r.detail_hours ? r.diagnose_detail_every : r.later_every)) s.diagnosis_hours.push_back(h);
    if (s.diagnosis_hours.back() != s.hours) s.diagnosis_hours.push_back(s.hours);
    return s;
}

struct FrameFields {
    std::vector<double> t, u, v, p, p_synoptic, precip, cloud, gust, storm_wind, hail, stp, surge;
    std::vector<unsigned char> thunder, tornado, convective;
};
struct DailyFields {
    std::vector<double> tmax, tmin, precip, gust, hail, stp, storm_wind, surge, runoff, cloud;
    std::vector<unsigned char> thunder, tornado, tornado_significant, tornado_violent;
};
struct TrackPoint { double hour, lat, lon, wind; std::string category; };
struct StormTrack { std::size_t id; std::string name; std::vector<TrackPoint> points; };
struct MemberResult {
    std::vector<FrameFields> frames;
    std::vector<DailyFields> days;
    std::vector<std::vector<std::array<double, 5>>> city_hours;   // per city, per hour: T, wind, gust, direction (deg from), precip
    std::vector<StormTrack> tracks;
    double land_heating_sum{0.0}, layered_sum{0.0}, condensation_sum{0.0}, orographic_sum{0.0}, convective_sum{0.0}, storm_rain_sum{0.0};
    double max_storm_wind{0.0};
    std::vector<std::pair<double, std::vector<SynopticEvolution::Active>>> systems;   // per frame
};

struct RunOptions {
    std::uint64_t seed{0};
    bool perturbed{true};
    bool ocean_coupling{true}, sun{true};
    int days{10};
};

// Seeds of the members and the truth pass through splitmix64: neighbouring or small seeds then give independent streams.
std::uint64_t mix_seed(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

std::vector<double> smooth_noise(NormalSequence& random, const World& w, const TransportGrid2D& g, double amplitude, int blobs = 8) {
    std::vector<double> out(w.n, 0.0);
    if (amplitude <= 0.0) return out;
    const double width = 0.5 * static_cast<double>(g.nx) * g.spacing_east_m, height = 0.5 * static_cast<double>(g.ny) * g.spacing_north_m;
    for (int b = 0; b < blobs; ++b) {
        const double cx = width * (2.0 * random.uniform() - 1.0), cy = height * (2.0 * random.uniform() - 1.0), a = amplitude * random.next(), radius = 900000.0;
        for (std::size_t i = 0; i < w.n; ++i) {
            const double x = g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m - cx;
            const double y = g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m - cy;
            out[i] += a * std::exp(-(x * x + y * y) / (radius * radius));
        }
    }
    return out;
}

std::string storm_name_for(const Request& r, const StormSystemsState& state, std::size_t id) {
    // Seeds are inserted in the order of the request: the k-th seeded storm has id k+1 unless genesis came first.
    (void)state;
    if (id >= 1 && id <= r.hurricanes.size()) return r.hurricanes[id - 1].name;
    return "storm " + std::to_string(id);
}

MemberResult run_member(const Request& r, const World& world0, const Schedule& schedule, const RunOptions& options) {
    const auto& g = r.grid;
    World world = world0;
    NormalSequence random(mix_seed(options.seed));
    const std::size_t n = world.n;
    const double start = terraflux::weather::detail::utc_seconds(r.start_utc);
    static const double j2000 = terraflux::weather::detail::utc_seconds("2000-01-01T12:00:00Z");
    SynopticEvolution synoptic(r.systems, r.genesis, r.steering, g, static_cast<double>(schedule.hours), options.perturbed ? &random : nullptr, r.spread);

    // The coupled layer.
    CoupledWindConfig config;
    config.grid = g;
    config.time.start_time_utc = r.start_utc;
    config.time.timestep_seconds = r.timestep_s;
    config.time.steps = static_cast<std::size_t>(std::lround(3600.0 / r.timestep_s));
    config.atmosphere.air_density_kg_m3 = 1.2;
    config.atmosphere.surface_friction_coefficient_s = r.friction;
    config.atmosphere.coriolis_mode = "beta_plane";
    config.diffusion = {r.diffusion, r.diffusion, r.diffusion};
    config.orography.enabled = std::any_of(world.elevation.begin(), world.elevation.end(), [](double h) { return h > 0.0; });
    config.orography.elevation_m = world.elevation;
    validate_coupled_wind_config(config);
    PressureSystemsConfig systems{r.background_pa, synoptic.centers()};
    CoupledWindState state;
    state.pressure_pa = build_pressure_field(systems, g);
    PressureWindConfig pw;
    pw.grid = g;
    pw.atmosphere = config.atmosphere;
    pw.time = config.time;
    const auto balanced = balanced_wind(state.pressure_pa, pw);
    state.wind_east_m_s = balanced.east_m_s;
    state.wind_north_m_s = balanced.north_m_s;
    const auto noise = smooth_noise(random, world, g, options.perturbed ? r.spread.temperature_k : 0.0);
    state.air_temperature_k.resize(n);
    state.specific_humidity_kg_kg.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double lat = world.geo[i].latitude_deg, lon = world.geo[i].longitude_deg;
        const double base = world.land[i] ? world.land_climate[i] : world.sst[i] - r.coupling.sea_air_offset_k;
        state.air_temperature_k[i] = base + anomaly_sum(r.warm_anomalies, lat, lon) + noise[i];
        const double rh = world.land[i] ? 0.4 + 0.35 * world.soil[i] : 0.78;
        state.specific_humidity_kg_kg[i] = std::max(1e-5, rh * saturation_specific_humidity_kg_kg(state.air_temperature_k[i], state.pressure_pa[i]) + anomaly_sum(r.moist_anomalies, lat, lon) * 1e-3);
    }
    if (config.orography.enabled) state.orographic_precipitation_mm.assign(n, 0.0);
    SurfaceState surface{world.land, world.sst, world.land_climate, world.soil, world.soil, world.elevation, std::vector<double>(n, 0.3)};
    SurfaceCouplingConfig coupling = r.coupling;
    if (!options.sun) coupling.land_heating_k_per_h = 0.0;
    coupling.ocean_fluxes = options.ocean_coupling;
    validate_surface_coupling(coupling);

    // Tropical cyclones.
    SurfaceConfig storm_surface{world.land, world.sst};
    StormSystemsState storms;
    std::vector<std::pair<double, StormSeed>> seeds;
    for (const auto& s : r.hurricanes) {
        auto p = geographic_to_local(s.lat, s.lon, g.geographic_anchor_latitude_deg, g.geographic_anchor_longitude_deg);
        double wind = s.max_wind_m_s;
        if (options.perturbed) {
            p[0] += r.spread.hurricane_position_km * 1000.0 * random.next();
            p[1] += r.spread.hurricane_position_km * 1000.0 * random.next();
            wind = std::clamp(wind + r.spread.hurricane_wind_m_s * random.next(), 12.0, 90.0);
        }
        seeds.push_back({s.start_hours, {p[0], p[1], wind, s.radius_km * 1000.0}});
    }
    std::size_t next_seed = 0;
    StormEnvironment environment;
    bool have_environment = false;

    MemberResult out;
    const std::size_t cities = r.cities.size();
    out.city_hours.assign(cities, {});
    std::vector<double> precip_since_diag(n, 0.0), precip_hour(n, 0.0);
    std::vector<double> convective_rate(n, 0.0), storm_rain_rate(n, 0.0);
    std::vector<unsigned char> cumulonimbus(n, 0);
    CloudField last_clouds;
    PrecipitationField last_precip;
    SevereConvectionField last_severe;
    StormFields last_storm_fields{std::vector<double>(n, 0.0), std::vector<double>(n, 0.0), std::vector<double>(n, 0.0), std::vector<double>(n, 0.0)};
    bool have_diagnosis = false;
    std::vector<double> runoff_day(n, 0.0);
    std::vector<std::vector<double>> runoff_by_day;
    DailyFields day;
    auto reset_day = [&]() {
        day = DailyFields{};
        day.tmax.assign(n, -1e9); day.tmin.assign(n, 1e9); day.precip.assign(n, 0.0); day.gust.assign(n, 0.0); day.hail.assign(n, 0.0); day.stp.assign(n, 0.0);
        day.storm_wind.assign(n, 0.0); day.surge.assign(n, 0.0); day.runoff.assign(n, 0.0); day.cloud.assign(n, 0.0);
        day.thunder.assign(n, 0); day.tornado.assign(n, 0); day.tornado_significant.assign(n, 0); day.tornado_violent.assign(n, 0);
    };
    reset_day();
    int cloud_samples = 0;
    std::vector<std::size_t> track_index;   // storm id -> index into out.tracks

    // The wind at 10 m: the layer wind slowed by surface friction, plus the tropical cyclones' vortices.
    auto total_wind = [&](std::vector<double>& u, std::vector<double>& v) {
        std::vector<double> vu(n, 0.0), vv(n, 0.0);
        if (!storms.storms.empty()) add_storm_vortices(vu, vv, storms, g, r.cyclone.decay_exponent);
        u.resize(n);
        v.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto w10 = surface_wind_m_s(state.wind_east_m_s[i], state.wind_north_m_s[i], vu[i], vv[i], world.land[i] != 0, world.elevation[i], r.gusts);
            u[i] = w10[0];
            v[i] = w10[1];
        }
    };
    auto diagnose = [&](double interval_h) {
        const auto fronts = analyse_fronts(state, g, r.clouds);
        last_clouds = diagnose_cloud_formation(state, g, r.clouds, 0.0, fronts);
        PrecipitationConfig pc = r.precipitation;
        pc.interval_seconds = std::max(3600.0, interval_h * 3600.0);
        last_precip = diagnose_precipitation(state, g, r.clouds, last_clouds, pc);
        for (std::size_t i = 0; i < n; ++i)
            last_precip.hail_max_diameter_cm[i] = ground_hail_cm(last_precip.hail_max_diameter_cm[i], last_precip.freezing_level_m[i], r.hail_full_freezing_level_m,
                                                                 r.hail_none_freezing_level_m);
        last_severe = diagnose_severe_convection(state, g, r.clouds, last_clouds, r.severe);
        if (!storms.storms.empty() || next_seed < seeds.size() || r.storm_config.genesis_gpi_threshold < 1.0e5) {
            environment = diagnose_storm_environment(state, g, storm_surface, r.storm_config);
            have_environment = true;
        }
        for (std::size_t i = 0; i < n; ++i) {
            cumulonimbus[i] = last_clouds.thunderstorm[i];
            convective_rate[i] = last_precip.duration_fraction[i] * last_precip.rate_mm_h[i];
        }
        have_diagnosis = true;
    };
    // The storm hazards on this grid: wind, rain and surge from the scale-aware cyclone fields, the pressure anomaly
    // (for the maps) from the storm kernel.
    auto refresh_storm_fields = [&]() {
        if (storms.storms.empty()) {
            last_storm_fields = StormFields{std::vector<double>(n, 0.0), std::vector<double>(n, 0.0), std::vector<double>(n, 0.0), std::vector<double>(n, 0.0)};
            return;
        }
        last_storm_fields = storm_fields(storms, g, storm_surface, r.storm_config);
        const auto hz = cyclone_hazards(storms, g, world.land, r.cyclone);
        last_storm_fields.wind_speed_m_s = hz.wind_m_s;
        last_storm_fields.rain_rate_mm_h = hz.rain_mm_h;
        last_storm_fields.surge_m = hz.surge_m;
    };
    auto record_track = [&](double hour) {
        for (const auto& s : storms.storms) {
            std::size_t k = 0;
            while (k < track_index.size() && out.tracks[track_index[k]].id != s.id) ++k;
            if (k == track_index.size()) {
                out.tracks.push_back({s.id, storm_name_for(r, storms, s.id), {}});
                track_index.push_back(out.tracks.size() - 1);
            }
            const auto geo = local_to_geographic(s.east_m, s.north_m, g.geographic_anchor_latitude_deg, g.geographic_anchor_longitude_deg);
            out.tracks[track_index[k]].points.push_back({hour, geo.latitude_deg, geo.longitude_deg, s.max_wind_m_s, s.category});
            out.max_storm_wind = std::max(out.max_storm_wind, s.max_wind_m_s);
        }
    };
    auto record_frame = [&](int hour) {
        FrameFields f;
        f.t = state.air_temperature_k;
        total_wind(f.u, f.v);
        f.p.resize(n);
        for (std::size_t i = 0; i < n; ++i) f.p[i] = state.pressure_pa[i] + last_storm_fields.pressure_anomaly_pa[i];
        f.p_synoptic = state.pressure_pa;
        f.precip = precip_since_diag;   // since the previous frame
        f.cloud = surface.cloud_fraction;
        f.gust.resize(n);
        f.storm_wind = last_storm_fields.wind_speed_m_s;
        f.surge = last_storm_fields.surge_m;
        f.hail.assign(n, 0.0);
        f.stp.assign(n, 0.0);
        f.thunder.assign(n, 0);
        f.tornado.assign(n, 0);
        f.convective = cumulonimbus;
        for (std::size_t i = 0; i < n; ++i) {
            f.gust[i] = gust_m_s(std::hypot(f.u[i], f.v[i]), cumulonimbus[i], r.gusts);
            if (have_diagnosis) {
                f.hail[i] = last_precip.hail_max_diameter_cm[i];
                f.stp[i] = last_severe.significant_tornado_parameter[i];
                f.thunder[i] = last_precip.lightning[i];
                f.tornado[i] = last_severe.tornado_possible[i];
            }
        }
        out.frames.push_back(std::move(f));
        out.systems.push_back({static_cast<double>(hour), synoptic.active()});
    };

    // Hour 0: diagnose the initial state, seed the storms due now.
    while (next_seed < seeds.size() && seeds[next_seed].first <= 0.0) {
        const auto env = diagnose_storm_environment(state, g, storm_surface, r.storm_config);
        advance_storm_systems(storms, {seeds[next_seed].second}, state, g, storm_surface, env, r.storm_config, 0.0, 0.0);
        ++next_seed;
    }
    diagnose(static_cast<double>(schedule.diagnosis_hours.size() > 1 ? schedule.diagnosis_hours[1] : 6));
    refresh_storm_fields();
    std::fill(precip_since_diag.begin(), precip_since_diag.end(), 0.0);
    record_frame(0);
    record_track(0.0);
    std::size_t next_frame = 1, next_diag = 1;
    for (int hour = 0; hour < schedule.hours; ++hour) {
        synoptic.advance(1.0);
        state.pressure_pa = build_pressure_field({r.background_pa, synoptic.centers()}, g);
        if (config.orography.enabled) std::fill(state.orographic_precipitation_mm.begin(), state.orographic_precipitation_mm.end(), 0.0);
        state = advance_coupled_wind(state, config);
        const double t_hours = static_cast<double>(hour + 1);
        const double d = (start + t_hours * 3600.0 - j2000) / 86400.0;
        std::vector<double> cos_z(n), mean_cos_z(n);
        const double declination = solar_declination_deg(d);
        for (std::size_t i = 0; i < n; ++i) {
            cos_z[i] = solar_cos_zenith(world.geo[i].latitude_deg, world.geo[i].longitude_deg, d - 0.5 / 24.0);
            mean_cos_z[i] = daily_mean_cos_zenith(world.geo[i].latitude_deg, declination);
        }
        // Layered cloud from the humidity and the synoptic ascent (the convective cloud of the latest diagnosis stays).
        std::vector<std::array<double, 4>> ascending;
        for (const auto& a : synoptic.active()) ascending.push_back({a.east_m, a.north_m, a.anomaly_pa, a.radius_m});
        auto ascent = synoptic_ascent_m_s(ascending, state.wind_east_m_s, state.wind_north_m_s, g, r.weather);
        // Forced ascent where the wind blows up the terrain: orographic cloud and rain on the windward slopes.
        const auto upslope = upslope_ascent_m_s(state.wind_east_m_s, state.wind_north_m_s, world.elevation, g, r.weather.orographic_lift_factor);
        for (std::size_t i = 0; i < n; ++i) ascent[i] += upslope[i];
        std::vector<double> layered_rain(n, 0.0), upslope_share(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = state.air_temperature_k[i], p = state.pressure_pa[i], q = state.specific_humidity_kg_kg[i];
            const double rh = q / saturation_specific_humidity_kg_kg(t, p);
            const double layered = layered_cloud_fraction(rh, ascent[i], r.weather);
            surface.cloud_fraction[i] = std::max(have_diagnosis ? last_clouds.cloud_fraction[i] : 0.0, layered);
            layered_rain[i] = layered_rain_mm_h(rh, ascent[i], p / (287.05 * t), q, r.weather);
            if (ascent[i] > 0.0 && upslope[i] > 0.0) upslope_share[i] = std::min(1.0, upslope[i] / ascent[i]);
        }
        const auto report = couple_surface(state, surface, cos_z, mean_cos_z, coupling, 1.0);

        out.land_heating_sum += report.mean_land_heating_k;
        // Storms: new seeds, then one hour against the latest environment.
        while (next_seed < seeds.size() && seeds[next_seed].first <= t_hours) {
            const auto env = diagnose_storm_environment(state, g, storm_surface, r.storm_config);
            advance_storm_systems(storms, {seeds[next_seed].second}, state, g, storm_surface, env, r.storm_config, t_hours * 3600.0, 0.0);
            ++next_seed;
        }
        if (have_environment && (!storms.storms.empty() || r.storm_config.genesis_gpi_threshold < 1.0e5))
            advance_storm_systems(storms, {}, state, g, storm_surface, environment, r.storm_config, hour * 3600.0, 3600.0);
        refresh_storm_fields();
        // Precipitation of the hour: large-scale condensation, orographic, convective/frontal (at the rate of the latest
        // diagnosis) and tropical cyclone rain.
        for (std::size_t i = 0; i < n; ++i) {
            const double oro = config.orography.enabled ? state.orographic_precipitation_mm[i] : 0.0;
            const double storm = last_storm_fields.rain_rate_mm_h[i];
            // Convective and frontal rain draws on the moisture of the air: it cannot rain out more than the water above
            // the floor humidity, and the rain dries the air (the ocean and the land then moisten it again).
            const double t = state.air_temperature_k[i], p = state.pressure_pa[i];
            const double column_kg_m2 = p / (287.05 * t) * r.moist_depth_m;
            const double available = std::max(0.0, state.specific_humidity_kg_kg[i] - r.rain_floor_rh * saturation_specific_humidity_kg_kg(t, p)) * column_kg_m2;
            const double demand = convective_rate[i] + layered_rain[i];
            const double scale = demand > available && demand > 0.0 ? available / demand : 1.0;
            const double convective = convective_rate[i] * scale, layered = layered_rain[i] * scale;
            state.specific_humidity_kg_kg[i] = std::max(1.0e-6, state.specific_humidity_kg_kg[i] - (convective + layered) / column_kg_m2);
            precip_hour[i] = report.condensation_mm[i] + oro + convective + layered + storm;
            out.layered_sum += layered * (1.0 - upslope_share[i]);
            out.orographic_sum += layered * upslope_share[i];
            precip_since_diag[i] += precip_hour[i];
            out.condensation_sum += report.condensation_mm[i];
            out.orographic_sum += oro;
            out.convective_sum += convective;
            out.storm_rain_sum += storm;
            day.precip[i] += precip_hour[i];
            if (world.land[i]) {
                double soil = surface.soil_moisture[i];
                const double runoff = add_rain_to_soil(soil, precip_hour[i], world.slope[i], coupling);
                surface.soil_moisture[i] = soil;
                day.runoff[i] += runoff;
            }
            day.tmax[i] = std::max(day.tmax[i], state.air_temperature_k[i]);
            day.tmin[i] = std::min(day.tmin[i], state.air_temperature_k[i]);
            day.surge[i] = std::max(day.surge[i], last_storm_fields.surge_m[i]);
            day.storm_wind[i] = std::max(day.storm_wind[i], last_storm_fields.wind_speed_m_s[i]);
        }
        for (std::size_t i = 0; i < n; ++i) day.cloud[i] += surface.cloud_fraction[i];
        ++cloud_samples;
        std::vector<double> u, v;
        total_wind(u, v);
        for (std::size_t i = 0; i < n; ++i) day.gust[i] = std::max(day.gust[i], gust_m_s(std::hypot(u[i], v[i]), cumulonimbus[i], r.gusts));
        for (std::size_t c = 0; c < cities; ++c) {
            const std::size_t i = world.city_cells[c];
            const double speed = std::hypot(u[i], v[i]);
            const double from = std::fmod(std::atan2(-u[i], -v[i]) * 180.0 / kPi + 360.0, 360.0);
            out.city_hours[c].push_back({state.air_temperature_k[i], speed, gust_m_s(speed, cumulonimbus[i], r.gusts), from, precip_hour[i]});
        }
        const int h1 = hour + 1;
        const bool diag_now = next_diag < schedule.diagnosis_hours.size() && schedule.diagnosis_hours[next_diag] == h1;
        if (diag_now) {
            const double interval = next_diag + 1 < schedule.diagnosis_hours.size() ? schedule.diagnosis_hours[next_diag + 1] - h1 : 6.0;
            diagnose(interval);
            ++next_diag;
            for (std::size_t i = 0; i < n; ++i) {
                day.thunder[i] |= last_precip.lightning[i];
                day.hail[i] = std::max(day.hail[i], last_precip.hail_max_diameter_cm[i]);
                day.stp[i] = std::max(day.stp[i], last_severe.significant_tornado_parameter[i]);
                if (last_severe.tornado_possible[i]) {
                    day.tornado[i] = 1;
                    if (last_severe.significant_tornado_parameter[i] >= r.warnings.tornado_stp[1]) day.tornado_significant[i] = 1;
                    if (last_severe.significant_tornado_parameter[i] >= r.warnings.tornado_stp[2]) day.tornado_violent[i] = 1;
                }
            }
        }
        if (h1 % 3 == 0) record_track(t_hours);
        if (next_frame < schedule.frame_hours.size() && schedule.frame_hours[next_frame] == h1) {
            record_frame(h1);
            ++next_frame;
            std::fill(precip_since_diag.begin(), precip_since_diag.end(), 0.0);
        }
        if (h1 % 24 == 0) {
            for (std::size_t i = 0; i < n; ++i) day.cloud[i] = cloud_samples > 0 ? day.cloud[i] / cloud_samples : surface.cloud_fraction[i];
            runoff_by_day.push_back(day.runoff);
            // Flood potential: runoff over the last three days.
            DailyFields finished = day;
            for (std::size_t i = 0; i < n; ++i) {
                double sum = 0.0;
                for (std::size_t k = runoff_by_day.size() >= 3 ? runoff_by_day.size() - 3 : 0; k < runoff_by_day.size(); ++k) sum += runoff_by_day[k][i];
                finished.runoff[i] = sum;
            }
            out.days.push_back(std::move(finished));
            reset_day();
            cloud_samples = 0;
        }
    }
    return out;
}

// ---- Aggregation --------------------------------------------------------------------------------------------------

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double x = q * static_cast<double>(v.size() - 1);
    const std::size_t k = static_cast<std::size_t>(std::floor(x));
    const double w = x - static_cast<double>(k);
    return k + 1 < v.size() ? v[k] * (1.0 - w) + v[k + 1] * w : v[k];
}

struct Stats { std::vector<double> mean, spread; };
Stats stats(const std::vector<const std::vector<double>*>& fields) {
    const std::size_t n = fields.front()->size();
    Stats s{std::vector<double>(n, 0.0), std::vector<double>(n, 0.0)};
    const double m = static_cast<double>(fields.size());
    for (const auto* f : fields) for (std::size_t i = 0; i < n; ++i) s.mean[i] += (*f)[i] / m;
    for (const auto* f : fields) for (std::size_t i = 0; i < n; ++i) s.spread[i] += ((*f)[i] - s.mean[i]) * ((*f)[i] - s.mean[i]) / std::max(1.0, m - 1.0);
    for (double& x : s.spread) x = std::sqrt(x);
    return s;
}

template <typename Predicate>
std::vector<double> probability(std::size_t n, std::size_t members, Predicate&& hit) {
    std::vector<double> p(n, 0.0);
    for (std::size_t m = 0; m < members; ++m)
        for (std::size_t i = 0; i < n; ++i) if (hit(m, i)) p[i] += 1.0 / static_cast<double>(members);
    return p;
}

// Weather symbol of a day (ensemble): 0 clear, 1 partly cloudy, 2 cloudy, 3 rain, 4 snow, 5 thunderstorms, 6 gale, 7 hurricane.
int weather_symbol(double cloud, double p_rain, double p_thunder, double p_gale, double p_hurricane, double tmax_k) {
    if (p_hurricane >= 0.3) return 7;
    if (p_thunder >= 0.4) return 5;
    if (p_gale >= 0.5) return 6;
    if (p_rain >= 0.5) return tmax_k < 274.15 ? 4 : 3;
    if (cloud < 0.25) return 0;
    if (cloud < 0.6) return 1;
    return 2;
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
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.earth.forecast-run/v1") throw std::invalid_argument("schema_version must be terraflux.earth.forecast-run/v1");
        const Request r = parse_request(request);
        const auto& g = r.grid;
        const World world = make_world(r, false, false);
        const std::size_t n = world.n;
        const Schedule schedule = make_schedule(r, r.days);

        // ---- The runs: members, truth, control and sensitivity runs in parallel ---------------------------------------
        struct Job { std::string kind; RunOptions options; bool flat{false}, uniform_soil{false}; };
        std::vector<Job> jobs;
        for (int m = 0; m < r.members; ++m) jobs.push_back({"member", {r.seed * 1000003ULL + static_cast<std::uint64_t>(m) + 1, true, true, true, r.days}});
        jobs.push_back({"truth", {r.truth_seed, true, true, true, r.days}});
        if (!r.sensitivity.empty()) {
            jobs.push_back({"control", {r.seed, false, true, true, r.sensitivity_days}});
            for (const auto& s : r.sensitivity)
                jobs.push_back({s.key, {r.seed, false, s.key != "no_ocean_coupling", s.key != "no_sun", r.sensitivity_days}, s.key == "flat_terrain", s.key == "uniform_soil"});
        }
        std::vector<MemberResult> results(jobs.size());
        std::atomic<std::size_t> next{0};
        std::exception_ptr failure;
        std::mutex failure_mutex;
        auto worker = [&]() {
            for (std::size_t k = next++; k < jobs.size(); k = next++) {
                try {
                    const Job& job = jobs[k];
                    const World w = job.flat || job.uniform_soil ? make_world(r, job.flat, job.uniform_soil) : world;
                    results[k] = run_member(r, w, job.options.days == r.days ? schedule : make_schedule(r, job.options.days), job.options);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(failure_mutex);
                    if (!failure) failure = std::current_exception();
                }
            }
        };
        std::vector<std::thread> pool;
        const int threads = std::max(1, std::min<int>(r.threads, static_cast<int>(jobs.size())));
        for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
        if (failure) std::rethrow_exception(failure);
        const std::size_t members = static_cast<std::size_t>(r.members);
        const MemberResult& truth = results[members];

        // ---- Frames: ensemble statistics --------------------------------------------------------------------------
        const auto& w = r.warnings;
        const Json grid = {{"nx", g.nx}, {"ny", g.ny}, {"origin_east_m", g.origin_east_m}, {"origin_north_m", g.origin_north_m}, {"spacing_east_m", g.spacing_east_m},
                           {"spacing_north_m", g.spacing_north_m}, {"geographic_anchor_longitude_deg", g.geographic_anchor_longitude_deg},
                           {"geographic_anchor_latitude_deg", g.geographic_anchor_latitude_deg}, {"coordinate_system", "local_azimuthal_equidistant_m"},
                           {"array_order", "row_major_y_then_x"}};
        Json frames = Json::array(), frame_products = Json::array(), verification_frames = Json::array();
        // Climatology of the verification: the time mean of the truth. The pressure is verified without the tropical
        // cyclone cores (their small, deep centres would dominate the scores).
        std::vector<double> truth_clim_t(n, 0.0), truth_clim_p(n, 0.0);
        for (const auto& f : truth.frames)
            for (std::size_t i = 0; i < n; ++i) { truth_clim_t[i] += f.t[i] / truth.frames.size(); truth_clim_p[i] += f.p_synoptic[i] / truth.frames.size(); }
        for (std::size_t k = 0; k < schedule.frame_hours.size(); ++k) {
            const int hour = schedule.frame_hours[k];
            std::vector<const std::vector<double>*> t, u, v, p, ps, pr, cl, gu;
            for (std::size_t m = 0; m < members; ++m) {
                const auto& f = results[m].frames[k];
                t.push_back(&f.t); u.push_back(&f.u); v.push_back(&f.v); p.push_back(&f.p); ps.push_back(&f.p_synoptic); pr.push_back(&f.precip); cl.push_back(&f.cloud); gu.push_back(&f.gust);
            }
            const Stats st = stats(t), su = stats(u), sv = stats(v), sp = stats(p), sps = stats(ps), spr = stats(pr), scl = stats(cl), sg = stats(gu);
            const auto p_rain = probability(n, members, [&](std::size_t m, std::size_t i) { return results[m].frames[k].precip[i] >= 1.0; });
            const auto p_gust = probability(n, members, [&](std::size_t m, std::size_t i) { return results[m].frames[k].gust[i] >= w.gust_m_s[1]; });
            const auto p_thunder = probability(n, members, [&](std::size_t m, std::size_t i) { return results[m].frames[k].thunder[i] != 0; });
            const auto p_tornado = probability(n, members, [&](std::size_t m, std::size_t i) { return results[m].frames[k].tornado[i] != 0; });
            frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", r.start_utc}, {"elapsed_seconds", hour * 3600.0}, {"grid", grid},
                              {"observation_updates", Json::array()},
                              {"fields", {{"air_temperature", {{"units", "K"}, {"values", fixed_array(st.mean, 1)}}}, {"wind_east", {{"units", "m/s"}, {"values", fixed_array(su.mean, 1)}}},
                                          {"wind_north", {{"units", "m/s"}, {"values", fixed_array(sv.mean, 1)}}}}}});
            std::vector<double> p_hpa(n);
            for (std::size_t i = 0; i < n; ++i) p_hpa[i] = sp.mean[i] / 100.0;
            Json systems = Json::array();
            for (const auto& a : results[0].systems[k].second) {
                const auto geo = local_to_geographic(a.east_m, a.north_m, g.geographic_anchor_latitude_deg, g.geographic_anchor_longitude_deg);
                systems.push_back({{"name", a.name}, {"kind", a.kind}, {"latitude_deg", tidy(geo.latitude_deg)}, {"longitude_deg", tidy(geo.longitude_deg)}, {"anomaly_hpa", tidy(a.anomaly_pa / 100.0, 3)}});
            }
            frame_products.push_back({{"hour", hour}, {"elapsed_seconds", hour * 3600.0}, {"pressure_hpa", fixed_array(p_hpa, 1)}, {"precip_mm", fixed_array(spr.mean, 1)},
                                      {"p_precip_1mm", percent_array(p_rain)}, {"cloud_pct", percent_array(scl.mean)}, {"gust_m_s", fixed_array(sg.mean, 0)},
                                      {"p_gust_orange", percent_array(p_gust)}, {"p_thunder", percent_array(p_thunder)}, {"p_tornado", percent_array(p_tornado)},
                                      {"systems_member_1", systems}});
            // Verification against the truth.
            std::vector<double> truth_p(n), mean_p(n), spread_p(n), pers_p(n), clim_p(n);
            for (std::size_t i = 0; i < n; ++i) {
                truth_p[i] = truth.frames[k].p_synoptic[i] / 100.0; mean_p[i] = sps.mean[i] / 100.0; spread_p[i] = sps.spread[i] / 100.0;
                pers_p[i] = truth.frames[0].p_synoptic[i] / 100.0; clim_p[i] = truth_clim_p[i] / 100.0;
            }
            std::vector<double> pers_t(n), clim_t(n);
            for (std::size_t i = 0; i < n; ++i) { pers_t[i] = truth.frames[0].t[i]; clim_t[i] = truth_clim_t[i]; }
            const auto score_p = score_forecast(mean_p, spread_p, truth_p, clim_p, pers_p);
            const auto score_t = score_forecast(st.mean, st.spread, truth.frames[k].t, clim_t, pers_t);
            Json v_rain = nullptr;
            if (k > 0) {
                std::vector<unsigned char> outcome(n);
                for (std::size_t i = 0; i < n; ++i) outcome[i] = truth.frames[k].precip[i] >= 1.0;
                const auto bs = brier_score(p_rain, outcome);
                v_rain = {{"brier", tidy(bs[0])}, {"skill", tidy(bs[1])}};
            }
            auto score_json = [](const SkillScores& s) {
                return Json{{"rmse", tidy(s.rmse)}, {"spread", tidy(s.spread)}, {"climatology_rmse", tidy(s.climatology_rmse)}, {"persistence_rmse", tidy(s.persistence_rmse)},
                            {"skill_vs_climatology", tidy(s.skill_vs_climatology)}, {"anomaly_correlation", tidy(s.anomaly_correlation)}};
            };
            verification_frames.push_back({{"hour", hour}, {"pressure_hpa", score_json(score_p)}, {"temperature_k", score_json(score_t)}, {"rain_1mm", v_rain}});
        }

        // ---- Daily products, warnings ------------------------------------------------------------------------------
        const char* hazards[] = {"wind", "rain", "flood", "thunderstorm", "tornado", "hurricane", "surge"};
        Json days = Json::array(), warning_list = Json::array();
        std::vector<std::vector<std::array<int, 7>>> city_levels(r.cities.size());
        std::vector<std::vector<Json>> city_days(r.cities.size());
        const std::size_t day_count = results[0].days.size();
        for (std::size_t d = 0; d < day_count; ++d) {
            std::vector<const std::vector<double>*> tmax, tmin, precip, gust, cloud, runoff, surge;
            for (std::size_t m = 0; m < members; ++m) {
                const auto& x = results[m].days[d];
                tmax.push_back(&x.tmax); tmin.push_back(&x.tmin); precip.push_back(&x.precip); gust.push_back(&x.gust); cloud.push_back(&x.cloud); runoff.push_back(&x.runoff); surge.push_back(&x.surge);
            }
            const Stats s_tmax = stats(tmax), s_tmin = stats(tmin), s_precip = stats(precip), s_gust = stats(gust), s_cloud = stats(cloud), s_runoff = stats(runoff), s_surge = stats(surge);
            auto prob = [&](auto&& hit) { return probability(n, members, [&](std::size_t m, std::size_t i) { return hit(results[m].days[d], i); }); };
            const auto p1 = prob([](const DailyFields& x, std::size_t i) { return x.precip[i] >= 1.0; });
            const auto p10 = prob([](const DailyFields& x, std::size_t i) { return x.precip[i] >= 10.0; });
            std::array<std::vector<double>, 3> p_gust, p_rain, p_runoff, p_thunder, p_tornado, p_storm, p_surge;
            for (int l = 0; l < 3; ++l) {
                const std::size_t L = static_cast<std::size_t>(l);
                p_gust[L] = prob([&](const DailyFields& x, std::size_t i) { return x.gust[i] >= w.gust_m_s[L]; });
                p_rain[L] = prob([&](const DailyFields& x, std::size_t i) { return x.precip[i] >= w.rain_24h_mm[L]; });
                p_runoff[L] = prob([&](const DailyFields& x, std::size_t i) { return x.runoff[i] >= w.runoff_72h_mm[L]; });
                p_thunder[L] = prob([&](const DailyFields& x, std::size_t i) { return l == 0 ? x.thunder[i] != 0 : x.thunder[i] && x.hail[i] >= w.hail_cm[L]; });
                p_tornado[L] = prob([&](const DailyFields& x, std::size_t i) { return l == 0 ? x.tornado[i] != 0 : l == 1 ? x.tornado_significant[i] != 0 : x.tornado_violent[i] != 0; });
                p_storm[L] = prob([&](const DailyFields& x, std::size_t i) { return x.storm_wind[i] >= w.hurricane_wind_m_s[L]; });
                p_surge[L] = prob([&](const DailyFields& x, std::size_t i) { return x.surge[i] >= w.surge_m[L]; });
            }
            const std::array<std::array<std::vector<double>, 3>*, 7> sets{&p_gust, &p_rain, &p_runoff, &p_thunder, &p_tornado, &p_storm, &p_surge};
            const std::array<const std::vector<double>*, 7> values{&s_gust.mean, &s_precip.mean, &s_runoff.mean, &p_thunder[0], &p_tornado[0], &p_storm[0], &s_surge.mean};
            Json levels = Json::object();
            std::vector<std::vector<int>> level_arrays(7, std::vector<int>(n, 0));
            for (std::size_t h = 0; h < 7; ++h) {
                const double pmin = h == 4 ? w.tornado_min_probability : w.min_probability;
                for (std::size_t i = 0; i < n; ++i) {
                    if (!world.warn[i]) continue;
                    int level = warning_level({(*sets[h])[0][i], (*sets[h])[1][i], (*sets[h])[2][i]}, pmin);
                    if (level == 3 && (*sets[h])[2][i] < std::max(pmin, w.red_min_probability)) level = 2;
                    level_arrays[h][i] = level;
                }
                levels[hazards[h]] = level_arrays[h];
                for (const auto& region : warning_regions(level_arrays[h], *values[h], g)) {
                    const auto& geo = world.geo[region.peak_cell];
                    Json inside = Json::array();
                    for (std::size_t c = 0; c < r.cities.size(); ++c)
                        if (world.city_inside[c] && std::binary_search(region.cells.begin(), region.cells.end(), world.city_cells[c])) inside.push_back(r.cities[c].name);
                    std::size_t nearest = 0;
                    double best = 1e18;
                    for (std::size_t c = 0; c < r.cities.size(); ++c) {
                        const double km = great_circle_km(geo.latitude_deg, geo.longitude_deg, r.cities[c].lat, r.cities[c].lon);
                        if (km < best) { best = km; nearest = c; }
                    }
                    const std::size_t lvl = static_cast<std::size_t>(region.level - 1);
                    warning_list.push_back({{"day", d + 1}, {"hazard", hazards[h]}, {"level", region.level}, {"cells", region.cells.size()}, {"area_km2", tidy(region.area_km2, 3)},
                                            {"peak_cell", region.peak_cell}, {"latitude_deg", tidy(geo.latitude_deg)}, {"longitude_deg", tidy(geo.longitude_deg)},
                                            {"peak_value", tidy(region.peak_value, 3)}, {"peak_probability", tidy((*sets[h])[lvl][region.peak_cell], 3)},
                                            {"cities", inside}, {"nearest_city", r.cities.empty() ? Json(nullptr) : Json(r.cities[nearest].name)},
                                            {"nearest_city_km", r.cities.empty() ? Json(nullptr) : Json(tidy(best, 3))}});
                }
            }
            std::vector<int> symbol(n);
            std::vector<double> tmax_c(n), tmin_c(n), truth_tmax_c(n);
            for (std::size_t i = 0; i < n; ++i) {
                symbol[i] = weather_symbol(s_cloud.mean[i], p1[i], p_thunder[0][i], p_gust[1][i], p_storm[1][i], s_tmax.mean[i]);
                tmax_c[i] = s_tmax.mean[i] - 273.15;
                tmin_c[i] = s_tmin.mean[i] - 273.15;
                truth_tmax_c[i] = truth.days[d].tmax[i] - 273.15;
            }
            // Verification of the day: maximum temperature and rain >= 1 mm.
            std::vector<unsigned char> rained(n);
            for (std::size_t i = 0; i < n; ++i) rained[i] = truth.days[d].precip[i] >= 1.0;
            const auto bs = brier_score(p1, rained);
            double se = 0.0;
            for (std::size_t i = 0; i < n; ++i) se += (s_tmax.mean[i] - truth.days[d].tmax[i]) * (s_tmax.mean[i] - truth.days[d].tmax[i]);
            days.push_back({{"day", d + 1}, {"tmax_c", fixed_array(tmax_c, 1)}, {"tmin_c", fixed_array(tmin_c, 1)}, {"tmax_spread_k", fixed_array(s_tmax.spread, 1)},
                            {"precip_mm", fixed_array(s_precip.mean, 1)}, {"precip_spread_mm", fixed_array(s_precip.spread, 1)}, {"p_precip_1mm", percent_array(p1)},
                            {"p_precip_10mm", percent_array(p10)}, {"gust_m_s", fixed_array(s_gust.mean, 0)}, {"cloud_pct", percent_array(s_cloud.mean)},
                            {"runoff_72h_mm", fixed_array(s_runoff.mean, 1)}, {"surge_m", fixed_array(s_surge.mean, 2)}, {"symbol", symbol},
                            {"p_gust", {percent_array(p_gust[0]), percent_array(p_gust[1]), percent_array(p_gust[2])}},
                            {"p_rain", {percent_array(p_rain[0]), percent_array(p_rain[1]), percent_array(p_rain[2])}},
                            {"p_thunder", percent_array(p_thunder[0])}, {"p_hail", percent_array(p_thunder[1])}, {"p_tornado", percent_array(p_tornado[0])},
                            {"p_tropical_storm", percent_array(p_storm[0])}, {"p_hurricane", percent_array(p_storm[1])},
                            {"warnings", levels}, {"truth_tmax_c", fixed_array(truth_tmax_c, 1)},
                            {"verification", {{"tmax_rmse_k", tidy(std::sqrt(se / n))}, {"rain_brier", tidy(bs[0])}, {"rain_brier_skill", tidy(bs[1])}}}});
            // Cities of the day.
            for (std::size_t c = 0; c < r.cities.size(); ++c) {
                const std::size_t i = world.city_cells[c];
                std::vector<double> tx, tn, pr;
                for (std::size_t m = 0; m < members; ++m) { tx.push_back(results[m].days[d].tmax[i] - 273.15); tn.push_back(results[m].days[d].tmin[i] - 273.15); pr.push_back(results[m].days[d].precip[i]); }
                std::array<int, 7> lv{};
                for (std::size_t h = 0; h < 7; ++h) lv[h] = level_arrays[h][i];
                city_levels[c].push_back(lv);
                city_days[c].push_back({{"day", d + 1}, {"tmax_c", tidy(s_tmax.mean[i] - 273.15, 3)}, {"tmax_p10_c", tidy(quantile(tx, 0.1), 3)}, {"tmax_p90_c", tidy(quantile(tx, 0.9), 3)},
                                        {"tmin_c", tidy(s_tmin.mean[i] - 273.15, 3)}, {"tmin_p10_c", tidy(quantile(tn, 0.1), 3)}, {"tmin_p90_c", tidy(quantile(tn, 0.9), 3)},
                                        {"precip_mm", tidy(s_precip.mean[i], 3)}, {"precip_p90_mm", tidy(quantile(pr, 0.9), 3)}, {"p_precip_1mm", tidy(p1[i], 3)},
                                        {"gust_m_s", tidy(s_gust.mean[i], 3)}, {"cloud", tidy(s_cloud.mean[i], 3)}, {"symbol", symbol[i]},
                                        {"warnings", {{"wind", lv[0]}, {"rain", lv[1]}, {"flood", lv[2]}, {"thunderstorm", lv[3]}, {"tornado", lv[4]}, {"hurricane", lv[5]}, {"surge", lv[6]}}},
                                        {"truth_tmax_c", tidy(truth.days[d].tmax[i] - 273.15, 3)}, {"truth_precip_mm", tidy(truth.days[d].precip[i], 3)}});
            }
        }

        // ---- Cities: hourly wind for the first days, daily tables ---------------------------------------------------
        Json cities = Json::array();
        const int wind_hours = std::min(72, schedule.hours);
        for (std::size_t c = 0; c < r.cities.size(); ++c) {
            Json hourly = {{"hour", Json::array()}, {"wind_m_s", Json::array()}, {"wind_p10_m_s", Json::array()}, {"wind_p90_m_s", Json::array()}, {"gust_m_s", Json::array()},
                           {"gust_p90_m_s", Json::array()}, {"direction_deg", Json::array()}, {"temperature_c", Json::array()}, {"precip_mm", Json::array()}, {"truth_wind_m_s", Json::array()}};
            for (int h = 0; h < wind_hours; ++h) {
                std::vector<double> sp, gu;
                double su = 0.0, sv = 0.0, st = 0.0, spr = 0.0;
                for (std::size_t m = 0; m < members; ++m) {
                    const auto& x = results[m].city_hours[c][static_cast<std::size_t>(h)];
                    sp.push_back(x[1]); gu.push_back(x[2]); st += x[0] / members; spr += x[4] / members;
                    su += -x[1] * std::sin(x[3] * kPi / 180.0); sv += -x[1] * std::cos(x[3] * kPi / 180.0);
                }
                hourly["hour"].push_back(h + 1);
                hourly["wind_m_s"].push_back(tidy(std::accumulate(sp.begin(), sp.end(), 0.0) / members, 3));
                hourly["wind_p10_m_s"].push_back(tidy(quantile(sp, 0.1), 3));
                hourly["wind_p90_m_s"].push_back(tidy(quantile(sp, 0.9), 3));
                hourly["gust_m_s"].push_back(tidy(std::accumulate(gu.begin(), gu.end(), 0.0) / members, 3));
                hourly["gust_p90_m_s"].push_back(tidy(quantile(gu, 0.9), 3));
                const double direction = tidy(std::fmod(std::atan2(-su, -sv) * 180.0 / kPi + 360.0, 360.0), 3);
                hourly["direction_deg"].push_back(direction >= 360.0 ? direction - 360.0 : direction);   // 359.96 prints as 360
                hourly["temperature_c"].push_back(tidy(st - 273.15, 3));
                hourly["precip_mm"].push_back(tidy(spr, 2));
                hourly["truth_wind_m_s"].push_back(tidy(truth.city_hours[c][static_cast<std::size_t>(h)][1], 3));
            }
            const auto& geo = world.geo[world.city_cells[c]];
            cities.push_back({{"name", r.cities[c].name}, {"latitude_deg", r.cities[c].lat}, {"longitude_deg", r.cities[c].lon}, {"cell", world.city_cells[c]},
                              {"inside", static_cast<bool>(world.city_inside[c])}, {"cell_latitude_deg", tidy(geo.latitude_deg)}, {"cell_longitude_deg", tidy(geo.longitude_deg)},
                              {"land", world.land[world.city_cells[c]] != 0}, {"elevation_m", tidy(world.elevation[world.city_cells[c]], 3)},
                              {"days", city_days[c]}, {"hourly", hourly}});
        }

        // ---- Tropical cyclone tracks of every member, and the truth -------------------------------------------------
        auto tracks_json = [&](const MemberResult& m) {
            Json out = Json::array();
            for (const auto& t : m.tracks) {
                Json pts = Json::array();
                for (const auto& p : t.points) pts.push_back({tidy(p.hour), tidy(p.lat), tidy(p.lon), tidy(p.wind, 3), p.category});
                out.push_back({{"id", t.id}, {"name", t.name}, {"points", pts}});
            }
            return out;
        };
        Json member_tracks = Json::array();
        for (std::size_t m = 0; m < members; ++m) member_tracks.push_back(tracks_json(results[m]));

        // ---- Skill by lead day ------------------------------------------------------------------------------------
        Json skill_days = Json::array();
        double useful_hours = 0.0;
        for (std::size_t k = 0; k < verification_frames.size(); ++k) {
            const double acc = verification_frames[k]["pressure_hpa"]["anomaly_correlation"].get<double>();
            if (acc >= 0.6) useful_hours = verification_frames[k]["hour"].get<double>();
            else if (k > 0) break;
        }
        for (int lead : {1, 3, 5, 7, 10}) {
            if (lead * 24 > schedule.hours) continue;
            std::size_t k = 0;
            while (k + 1 < schedule.frame_hours.size() && schedule.frame_hours[k] < lead * 24) ++k;
            const auto& v = verification_frames[k];
            skill_days.push_back({{"day", lead}, {"pressure_anomaly_correlation", v["pressure_hpa"]["anomaly_correlation"]}, {"pressure_skill", v["pressure_hpa"]["skill_vs_climatology"]},
                                  {"pressure_rmse_hpa", v["pressure_hpa"]["rmse"]}, {"pressure_spread_hpa", v["pressure_hpa"]["spread"]},
                                  {"temperature_rmse_k", v["temperature_k"]["rmse"]}, {"temperature_skill", v["temperature_k"]["skill_vs_climatology"]},
                                  {"rain_brier_skill", days.size() >= static_cast<std::size_t>(lead) ? days[static_cast<std::size_t>(lead - 1)]["verification"]["rain_brier_skill"] : Json(nullptr)},
                                  {"tmax_rmse_k", days.size() >= static_cast<std::size_t>(lead) ? days[static_cast<std::size_t>(lead - 1)]["verification"]["tmax_rmse_k"] : Json(nullptr)}});
        }

        // ---- Couplings: what each link contributes (sensitivity runs against the control) ---------------------------
        Json couplings = Json::array();
        if (!r.sensitivity.empty()) {
            const std::size_t control_index = members + 1;
            const auto& control = results[control_index];
            const std::size_t cd = control.days.size();
            auto domain_stats = [&](const MemberResult& m) {
                double precip = 0.0, tmax_land = 0.0, tmin_land = 0.0, land = 0.0, thunder = 0.0, gust = 0.0;
                for (const auto& dd : m.days)
                    for (std::size_t i = 0; i < n; ++i) {
                        precip += dd.precip[i] / (n * m.days.size());
                        gust = std::max(gust, dd.gust[i]);
                        thunder += dd.thunder[i] ? 1.0 / (n * m.days.size()) : 0.0;
                        if (world.land[i]) { tmax_land += dd.tmax[i]; tmin_land += dd.tmin[i]; land += 1.0; }
                    }
                return std::array<double, 6>{precip, tmax_land / land, tmin_land / land, thunder, gust, m.max_storm_wind};
            };
            const auto base = domain_stats(control);
            const double orographic_share = (control.orographic_sum) / std::max(1e-9, control.condensation_sum + control.orographic_sum + control.convective_sum + control.layered_sum + control.storm_rain_sum);
            for (std::size_t s = 0; s < r.sensitivity.size(); ++s) {
                const auto x = domain_stats(results[control_index + 1 + s]);
                couplings.push_back({{"run", r.sensitivity[s].key}, {"description", r.sensitivity[s].description}, {"days", cd},
                                     {"precip_change_pct", tidy((x[0] / std::max(1e-9, base[0]) - 1.0) * 100.0, 3)}, {"tmax_land_change_k", tidy(x[1] - base[1], 3)},
                                     {"tmin_land_change_k", tidy(x[2] - base[2], 3)}, {"thunder_change_pct", tidy(base[3] > 0 ? (x[3] / base[3] - 1.0) * 100.0 : 0.0, 3)},
                                     {"max_gust_change_m_s", tidy(x[4] - base[4], 3)}, {"max_hurricane_wind_change_m_s", tidy(x[5] - base[5], 3)}});
            }
            couplings = {{"runs", couplings}, {"control", {{"precip_mm_per_day", tidy(base[0], 3)}, {"tmax_land_c", tidy(base[1] - 273.15, 3)}, {"tmin_land_c", tidy(base[2] - 273.15, 3)},
                                                           {"thunder_share", tidy(base[3], 3)}, {"max_gust_m_s", tidy(base[4], 3)}, {"max_hurricane_wind_m_s", tidy(base[5], 3)},
                                                           {"precip_sources", {{"saturation", tidy(control.condensation_sum)}, {"layered", tidy(control.layered_sum)}, {"orographic", tidy(control.orographic_sum)},
                                                                               {"convective_frontal", tidy(control.convective_sum)}, {"tropical_cyclone", tidy(control.storm_rain_sum)}}},
                                                           {"orographic_share", tidy(orographic_share, 3)}}}};
        }

        // ---- Static maps --------------------------------------------------------------------------------------------
        Json land = Json::array();
        std::vector<double> lat(n), lon(n);
        for (std::size_t i = 0; i < n; ++i) { land.push_back(world.land[i] != 0); lat[i] = world.geo[i].latitude_deg; lon[i] = world.geo[i].longitude_deg; }
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"earth_forecast", {
                {"schema_version", "terraflux.earth.forecast/v1"},
                {"method", "coupled_ensemble_forecast"},
                {"source_kind", "engine_output"},
                {"request", r.echo},
                {"start_utc", r.start_utc}, {"forecast_days", r.days}, {"members", r.members},
                {"frame_hours", schedule.frame_hours}, {"diagnosis_hours", schedule.diagnosis_hours},
                {"static", {{"land_mask", land}, {"land_mask_source", r.land_source}, {"latitude_deg", fixed_array(lat, 2)}, {"longitude_deg", fixed_array(lon, 2)},
                            {"elevation_m", fixed_array(world.elevation, 0)}, {"sea_surface_temperature_c", [&] { std::vector<double> v(n); for (std::size_t i = 0; i < n; ++i) v[i] = world.land[i] ? NAN : world.sst[i] - 273.15; return fixed_array(v, 1); }()},
                            {"soil_moisture_initial", fixed_array(world.soil, 2)}}},
                {"frames", frame_products},
                {"days", days},
                {"warnings", {{"hazards", {"wind", "rain", "flood", "thunderstorm", "tornado", "hurricane", "surge"}},
                              {"levels", {"none", "yellow", "orange", "red"}},
                              {"thresholds", {{"min_probability", w.min_probability}, {"tornado_min_probability", w.tornado_min_probability}, {"red_min_probability", w.red_min_probability}, {"gust_m_s", w.gust_m_s},
                                              {"rain_24h_mm", w.rain_24h_mm}, {"runoff_72h_mm", w.runoff_72h_mm}, {"hail_cm", w.hail_cm}, {"tornado_stp", w.tornado_stp},
                                              {"hurricane_wind_m_s", w.hurricane_wind_m_s}, {"surge_m", w.surge_m}}},
                              {"regions", warning_list}}},
                {"cities", cities},
                {"tracks", {{"members", member_tracks}, {"truth", tracks_json(truth)}}},
                {"verification", {{"frames", verification_frames}, {"lead_days", skill_days}, {"useful_hours", useful_hours},
                                  {"note", "verified against a hidden member drawn like the others (the truth of the model world)"}}},
                {"couplings", couplings.empty() ? Json(nullptr) : couplings},
                {"limitations", Json::array({
                    "a single coupled boundary layer with prescribed, moving Gaussian pressure systems: no dynamics of the pressure field, no jet stream or upper-level waves",
                    "the ensemble perturbs positions, speeds, deepening, genesis and the initial temperature; its errors grow roughly linearly, not chaotically",
                    "clouds, precipitation and severe convection are rule-of-thumb diagnostics every few hours; precipitation between diagnoses continues at the diagnosed rate",
                    "the land mask is real (global-land-mask), the terrain, sea-surface temperatures, soil moisture and the weather situation are synthetic",
                    "verification is against a member of the same model world, so it measures the predictability of this model, not the skill of a real forecast",
                    "floods are a runoff index (72-hour runoff from saturated soil and slopes), not a river routing; storm surge comes from the tropical-cyclone kernel"})},
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
