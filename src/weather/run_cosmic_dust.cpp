// terraflux-run-cosmic-dust: cosmic dust, small meteoroids and extraterrestrial water entering the Earth every day.
//
// Reads a terraflux.cosmic.dust-run/v1 request:
//   influx        the total mass entering per day (central, low, high: estimates range over an order of magnitude)
//   population    the Gruen size distribution between two masses, entry speeds, entry angles, composition (water, iron...)
//   entry         the particle and heating parameters of the entry model
//   altitude      the bins of the altitude profiles
//   map           a date, a global grid with a land mask, the sporadic meteoroid sources
//   comparisons   the quantities the dust is compared with (escape of hydrogen, oceans, rain, geothermal heat ...)
//   scenarios     influx multipliers ("what would it take?")
//   solar_timeline_path   optional: a terraflux-run-solar-cycle timeline for the comparison with the Sun
// and writes a viewer timeline: hourly global maps of the influx over one day and a `cosmic_dust` block
// (terraflux.cosmic.dust/v1) with the fate of the particles, the altitude profiles of ablation, water release and
// heating, the budgets (mass, water, energy, iron, meteoric smoke, mesospheric water), the scenarios and the map.
#include "terraflux/weather/cosmic_dust.hpp"
#include "utc_time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadius = 6.371e6;
constexpr double kYear = 365.25 * 86400.0;
constexpr double kDay = 86400.0;

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

void only_keys(const Json& object, const char* section, std::initializer_list<const char*> keys) {
    if (!object.is_object()) throw std::invalid_argument(std::string("run request section must be an object: ") + section);
    const std::set<std::string> allowed(keys.begin(), keys.end());
    for (const auto& item : object.items())
        if (!allowed.count(item.key())) throw std::invalid_argument(std::string("unknown field in ") + section + ": " + item.key());
}

template <typename T>
void read_into(const Json& section, const char* key, T& target) { target = static_cast<T>(optional_number(section, key, static_cast<double>(target))); }

double tidy(double v, int digits = 4) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    const int exponent = static_cast<int>(std::floor(std::log10(std::abs(v)))) - (digits - 1);
    if (exponent < 0) { const double inverse = std::pow(10.0, -exponent); return std::round(v * inverse) / inverse; }
    const double scale = std::pow(10.0, exponent);
    return std::round(v / scale) * scale;
}

Json number(double v, int digits = 4) { return std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr); }

Json tidy_array(const std::vector<double>& values, int digits = 4) {
    Json out = Json::array();
    for (double v : values) out.push_back(number(v, digits));
    return out;
}

struct Comparisons {
    double smoke_residence_years{4.0};
    double smoke_mass_extinction_m2_kg{2000.0};
    double forcing_per_optical_depth_w_m2{25.0};
    double mesospheric_water_ppmv{3.0};
    double mesospheric_water_lifetime_days{30.0};
    double hydrogen_escape_kg_s{3.0};
    double helium_escape_kg_s{0.05};
    double ocean_mass_kg{1.4e21};
    double precipitation_kg_yr{5.0e17};
    double volcanic_water_kg_yr{1.0e12};
    double aeolian_iron_kg_yr{1.6e10};
    double geothermal_w{4.7e13};
    double feedback_w_m2_k{1.13};
    double earth_age_years{4.5e9};
    double ocean_fraction{0.71};
};

struct Request {
    Json echo;
    double influx_central{60.0}, influx_low{30.0}, influx_high{100.0};
    std::string influx_note;
    DustPopulationConfig population;
    DustEntryConfig entry;
    double bottom_km{50.0}, top_km{180.0}, step_km{1.0};
    std::string date;
    int year{2026}, month{11}, day{5};
    std::size_t nx{72}, ny{36}, hours{24};
    std::vector<unsigned char> land;
    std::string land_source;
    std::vector<MeteoroidSource> sources = default_meteoroid_sources();
    double isotropic{0.3};
    Comparisons cmp;
    std::vector<double> multipliers{1.0, 10.0, 100.0, 1000.0};
    std::string solar_path;
};

Request parse_request(const Json& j) {
    Request r;
    r.echo = j;
    only_keys(j, "request", {"schema_version", "influx", "population", "entry", "altitude", "map", "comparisons", "scenarios", "solar_timeline_path"});
    const Json& f = required(j, "influx");
    only_keys(f, "influx", {"central_t_per_day", "low_t_per_day", "high_t_per_day", "note"});
    r.influx_central = required_number(f, "central_t_per_day");
    r.influx_low = optional_number(f, "low_t_per_day", r.influx_central);
    r.influx_high = optional_number(f, "high_t_per_day", r.influx_central);
    r.influx_note = f.contains("note") ? optional_string(f, "note", "") : "";
    if (!(r.influx_low > 0.0 && r.influx_low <= r.influx_central && r.influx_central <= r.influx_high && r.influx_high <= 1.0e6))
        throw std::invalid_argument("influx needs 0 < low_t_per_day <= central_t_per_day <= high_t_per_day <= 1e6");
    auto& p = r.population;
    p.influx_t_per_day = r.influx_central;
    p.speeds = default_dust_speeds();
    if (j.contains("population")) {
        const Json& s = required(j, "population");
        only_keys(s, "population", {"min_mass_g", "max_mass_g", "mass_bins", "speeds", "zenith_deg", "composition"});
        read_into(s, "min_mass_g", p.min_mass_g);
        read_into(s, "max_mass_g", p.max_mass_g);
        p.mass_bins = static_cast<std::size_t>(optional_integer(s, "mass_bins", static_cast<long long>(p.mass_bins), 4, 200));
        if (s.contains("speeds")) {
            const Json& list = required(s, "speeds");
            if (!list.is_array()) throw std::invalid_argument("population.speeds must be [km/s, weight] pairs");
            p.speeds.clear();
            for (const Json& x : list) {
                if (!x.is_array() || x.size() != 2 || !x[0].is_number() || !x[1].is_number()) throw std::invalid_argument("population.speeds must be [km/s, weight] pairs");
                p.speeds.push_back({x[0].get<double>(), x[1].get<double>()});
            }
        }
        if (s.contains("zenith_deg")) {
            const Json& list = required(s, "zenith_deg");
            if (!list.is_array()) throw std::invalid_argument("population.zenith_deg must be an array");
            p.zenith_deg.clear();
            for (const Json& x : list) {
                if (!x.is_number()) throw std::invalid_argument("population.zenith_deg must hold numbers");
                p.zenith_deg.push_back(x.get<double>());
            }
        }
        if (s.contains("composition")) {
            const Json& c = required(s, "composition");
            only_keys(c, "population.composition", {"water", "iron", "sodium", "magnesium", "silicon", "nickel", "note"});
            read_into(c, "water", p.composition.water);
            read_into(c, "iron", p.composition.iron);
            read_into(c, "sodium", p.composition.sodium);
            read_into(c, "magnesium", p.composition.magnesium);
            read_into(c, "silicon", p.composition.silicon);
            read_into(c, "nickel", p.composition.nickel);
        }
    }
    validate_dust_population(p);
    if (j.contains("entry")) {
        const Json& e = required(j, "entry");
        only_keys(e, "entry", {"particle_density_kg_m3", "specific_heat_j_kg_k", "heat_transfer", "drag", "emissivity", "latent_heat_j_kg", "vapour_a", "vapour_b",
                               "vapour_molar_mass_kg", "melt_start_k", "melt_full_k", "dehydration_start_k", "dehydration_end_k", "start_altitude_m", "air_temperature_k", "max_step_m"});
        auto& c = r.entry;
        for (auto [key, target] : std::initializer_list<std::pair<const char*, double*>>{
                 {"particle_density_kg_m3", &c.particle_density_kg_m3}, {"specific_heat_j_kg_k", &c.specific_heat_j_kg_k}, {"heat_transfer", &c.heat_transfer}, {"drag", &c.drag},
                 {"emissivity", &c.emissivity}, {"latent_heat_j_kg", &c.latent_heat_j_kg}, {"vapour_a", &c.vapour_a}, {"vapour_b", &c.vapour_b},
                 {"vapour_molar_mass_kg", &c.vapour_molar_mass_kg}, {"melt_start_k", &c.melt_start_k}, {"melt_full_k", &c.melt_full_k},
                 {"dehydration_start_k", &c.dehydration_start_k}, {"dehydration_end_k", &c.dehydration_end_k}, {"start_altitude_m", &c.start_altitude_m},
                 {"air_temperature_k", &c.air_temperature_k}, {"max_step_m", &c.max_step_m}})
            read_into(e, key, *target);
    }
    validate_dust_entry(r.entry);
    if (j.contains("altitude")) {
        const Json& a = required(j, "altitude");
        only_keys(a, "altitude", {"bottom_km", "top_km", "step_km"});
        read_into(a, "bottom_km", r.bottom_km);
        read_into(a, "top_km", r.top_km);
        read_into(a, "step_km", r.step_km);
    }
    if (!(r.bottom_km >= 0.0 && r.top_km <= 200.0 && r.top_km > r.bottom_km && r.step_km >= 0.25 && (r.top_km - r.bottom_km) / r.step_km <= 400.0))
        throw std::invalid_argument("altitude needs 0 <= bottom_km < top_km <= 200 and at most 400 bins of at least 0.25 km");
    if (r.top_km * 1000.0 < r.entry.start_altitude_m - 1e-6) throw std::invalid_argument("altitude.top_km must reach the entry start altitude");

    const Json& m = required(j, "map");
    only_keys(m, "map", {"date_utc", "cells_east", "cells_north", "hours", "land_mask", "land_mask_source", "sources", "isotropic_share"});
    r.date = optional_string(m, "date_utc", "2026-11-05");
    if (r.date.size() != 10 || r.date[4] != '-' || r.date[7] != '-') throw std::invalid_argument("map.date_utc must be YYYY-MM-DD");
    r.year = std::stoi(r.date.substr(0, 4));
    r.month = std::stoi(r.date.substr(5, 2));
    r.day = std::stoi(r.date.substr(8, 2));
    if (r.year < 1900 || r.year > 2200 || r.month < 1 || r.month > 12 || r.day < 1 || r.day > 31) throw std::invalid_argument("map.date_utc must be a date within 1900-2200");
    r.nx = static_cast<std::size_t>(optional_integer(m, "cells_east", 72, 8, 720));
    r.ny = static_cast<std::size_t>(optional_integer(m, "cells_north", 36, 4, 360));
    r.hours = static_cast<std::size_t>(optional_integer(m, "hours", 24, 2, 48));
    if (m.contains("land_mask")) {
        const Json& rows = required(m, "land_mask");
        if (!rows.is_array() || rows.size() != r.ny) throw std::invalid_argument("map.land_mask needs one string per row (north first)");
        r.land.assign(r.nx * r.ny, 0);
        for (std::size_t row = 0; row < r.ny; ++row) {
            if (!rows[row].is_string() || rows[row].get_ref<const std::string&>().size() != r.nx) throw std::invalid_argument("map.land_mask rows need one character per cell ('#' land, '.' sea)");
            const std::string& s = rows[row].get_ref<const std::string&>();
            for (std::size_t col = 0; col < r.nx; ++col) {
                if (s[col] != '#' && s[col] != '.') throw std::invalid_argument("map.land_mask characters must be '#' (land) or '.' (sea)");
                r.land[(r.ny - 1 - row) * r.nx + col] = s[col] == '#';   // rows are given north first; the grid runs south to north
            }
        }
        r.land_source = optional_string(m, "land_mask_source", "unlabelled");
    }
    if (m.contains("sources")) {
        const Json& list = required(m, "sources");
        if (!list.is_array() || list.empty() || list.size() > 12) throw std::invalid_argument("map.sources must list 1-12 sources");
        r.sources.clear();
        for (const Json& x : list) {
            only_keys(x, "map.sources[]", {"name", "longitude_from_apex_deg", "ecliptic_latitude_deg", "weight"});
            MeteoroidSource s{optional_string(x, "name", "source"), required_number(x, "longitude_from_apex_deg"), required_number(x, "ecliptic_latitude_deg"), required_number(x, "weight")};
            if (!(std::abs(s.longitude_from_apex_deg) <= 180.0 && std::abs(s.ecliptic_latitude_deg) <= 90.0 && s.weight >= 0.0)) throw std::invalid_argument("map sources need |longitude| <= 180, |latitude| <= 90 and a weight >= 0");
            r.sources.push_back(s);
        }
    }
    read_into(m, "isotropic_share", r.isotropic);
    if (!(r.isotropic >= 0.0 && r.isotropic <= 1.0)) throw std::invalid_argument("map.isotropic_share must lie within 0-1");

    if (j.contains("comparisons")) {
        const Json& c = required(j, "comparisons");
        only_keys(c, "comparisons", {"smoke_residence_years", "smoke_mass_extinction_m2_kg", "forcing_per_optical_depth_w_m2", "mesospheric_water_ppmv", "mesospheric_water_lifetime_days",
                                     "hydrogen_escape_kg_s", "helium_escape_kg_s", "ocean_mass_kg", "precipitation_kg_yr", "volcanic_water_kg_yr", "aeolian_iron_kg_yr", "geothermal_w",
                                     "feedback_w_m2_k", "earth_age_years", "ocean_fraction", "note"});
        auto& k = r.cmp;
        for (auto [key, target] : std::initializer_list<std::pair<const char*, double*>>{
                 {"smoke_residence_years", &k.smoke_residence_years}, {"smoke_mass_extinction_m2_kg", &k.smoke_mass_extinction_m2_kg},
                 {"forcing_per_optical_depth_w_m2", &k.forcing_per_optical_depth_w_m2}, {"mesospheric_water_ppmv", &k.mesospheric_water_ppmv},
                 {"mesospheric_water_lifetime_days", &k.mesospheric_water_lifetime_days}, {"hydrogen_escape_kg_s", &k.hydrogen_escape_kg_s}, {"helium_escape_kg_s", &k.helium_escape_kg_s},
                 {"ocean_mass_kg", &k.ocean_mass_kg}, {"precipitation_kg_yr", &k.precipitation_kg_yr}, {"volcanic_water_kg_yr", &k.volcanic_water_kg_yr},
                 {"aeolian_iron_kg_yr", &k.aeolian_iron_kg_yr}, {"geothermal_w", &k.geothermal_w}, {"feedback_w_m2_k", &k.feedback_w_m2_k}, {"earth_age_years", &k.earth_age_years},
                 {"ocean_fraction", &k.ocean_fraction}})
            read_into(c, key, *target);
        for (double v : {k.smoke_residence_years, k.smoke_mass_extinction_m2_kg, k.forcing_per_optical_depth_w_m2, k.mesospheric_water_ppmv, k.mesospheric_water_lifetime_days, k.ocean_mass_kg,
                         k.precipitation_kg_yr, k.feedback_w_m2_k, k.earth_age_years})
            if (!(v > 0.0)) throw std::invalid_argument("comparison quantities must be positive");
        for (double v : {k.hydrogen_escape_kg_s, k.helium_escape_kg_s, k.volcanic_water_kg_yr, k.aeolian_iron_kg_yr, k.geothermal_w})
            if (!(v >= 0.0)) throw std::invalid_argument("comparison fluxes must not be negative");
        if (!(k.ocean_fraction > 0.0 && k.ocean_fraction < 1.0)) throw std::invalid_argument("comparisons.ocean_fraction must lie within 0-1");
    }
    if (j.contains("scenarios")) {
        const Json& s = required(j, "scenarios");
        only_keys(s, "scenarios", {"multipliers"});
        const Json& list = required(s, "multipliers");
        if (!list.is_array() || list.empty() || list.size() > 12) throw std::invalid_argument("scenarios.multipliers must list 1-12 factors");
        r.multipliers.clear();
        for (const Json& x : list) {
            if (!x.is_number() || !(x.get<double>() > 0.0 && x.get<double>() <= 1.0e6)) throw std::invalid_argument("scenario multipliers must lie within 0-1e6");
            r.multipliers.push_back(x.get<double>());
        }
    }
    if (j.contains("solar_timeline_path")) r.solar_path = optional_string(j, "solar_timeline_path", "");
    return r;
}

// The solar quantities the dust is compared with, from a solar-cycle timeline.
Json solar_comparison(const std::string& path, double dust_power_w_m2) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("cannot open the solar-cycle timeline: " + path);
    Json doc;
    try { doc = Json::parse(file); } catch (const Json::exception& e) { throw std::invalid_argument("invalid solar-cycle JSON " + path + ": " + e.what()); }
    if (!doc.contains("solar") || doc["solar"].value("schema_version", "") != "terraflux.solar.cycle/v1") throw std::invalid_argument(path + " is not a terraflux.solar.cycle/v1 timeline");
    const Json& s = doc["solar"];
    const double tsi_change = s.at("irradiance").at("reference").at("tsi_change_w_m2").get<double>();
    const double flare = s.at("flares").at("energy").at("mean_irradiance_at_reference_w_m2").get<double>();
    const double forcing = s.at("climate").at("forcing_at_reference_w_m2").get<double>();
    // Per square metre of the Earth's surface: the TSI is per square metre facing the Sun (a quarter over the sphere).
    return {{"path", path}, {"tsi_cycle_change_w_m2", tsi_change}, {"tsi_cycle_change_global_mean_w_m2", tidy(tsi_change / 4.0)}, {"solar_cycle_forcing_w_m2", forcing},
            {"flare_mean_irradiance_w_m2", flare}, {"flare_global_mean_w_m2", tidy(flare / 4.0)}, {"dust_kinetic_power_w_m2", tidy(dust_power_w_m2)},
            {"dust_vs_solar_cycle", tidy(dust_power_w_m2 / (tsi_change / 4.0))}, {"dust_vs_flares", tidy(dust_power_w_m2 / (flare / 4.0))}};
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
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.cosmic.dust-run/v1") throw std::invalid_argument("schema_version must be terraflux.cosmic.dust-run/v1");
        const Request r = parse_request(request);
        const double area = 4.0 * kPi * kEarthRadius * kEarthRadius;

        // ---- Entry of the population (central influx; everything scales with the influx) ------------------------
        std::vector<double> edges;
        for (double km = r.bottom_km; km <= r.top_km + 1e-9; km += r.step_km) edges.push_back(km * 1000.0);
        const DustPopulationResult res = simulate_dust_population(r.population, r.entry, edges);
        const double to_t_day = kDay / 1000.0;
        const auto& k = r.cmp;
        Json bins = Json::array();
        for (const auto& b : res.bins) {
            const double diameter_um = 2.0e6 * std::cbrt(3.0 * b.bin.mass_g * 1.0e-3 / (4.0 * kPi * r.entry.particle_density_kg_m3));
            bins.push_back({{"mass_g", number(b.bin.mass_g, 3)}, {"diameter_um", number(diameter_um, 3)}, {"mass_fraction", number(b.bin.mass_fraction, 4)},
                            {"number_fraction", number(b.bin.number_fraction, 4)}, {"surviving_fraction", number(b.surviving_fraction, 4)},
                            {"fate", {{"unmelted", number(b.fate_fraction[0], 4)}, {"partially_melted", number(b.fate_fraction[1], 4)}, {"melted", number(b.fate_fraction[2], 4)},
                                      {"vaporised", number(b.fate_fraction[3], 4)}}},
                            {"water_released_fraction", number(b.water_released_fraction, 4)}, {"mean_peak_temperature_k", number(b.mean_peak_temperature_k, 4)}});
        }
        std::vector<double> centres, ablation, water, energy;
        for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
            centres.push_back(0.5 * (edges[i] + edges[i + 1]) / 1000.0);
            ablation.push_back(res.ablation_kg_s_by_bin[i] * to_t_day);
            water.push_back(res.water_kg_s_by_bin[i] * kDay);
            energy.push_back(res.energy_w_by_bin[i]);
        }

        // ---- Scenarios: the linear budgets for each influx ---------------------------------------------------------
        const double central = r.influx_central;
        auto per_influx = [&](double t_day) {
            const double f = t_day / central;
            const double ablated = res.ablated_kg_s * f, surviving = res.surviving_kg_s * f;
            const double water_in = res.water_in_kg_s * f, water_released = res.water_released_kg_s * f, water_surface = res.water_to_surface_kg_s * f;
            const double iron = res.influx_kg_s * f * r.population.composition.iron;
            const double power = res.kinetic_power_w * f;
            // Meteoric smoke: the evaporated mass recondenses into nanometre particles that spend years in the middle
            // atmosphere before they reach the ground (mesosphere, polar vortex, stratosphere).
            const double burden = ablated * k.smoke_residence_years * kYear;
            const double tau = burden * k.smoke_mass_extinction_m2_kg / area;
            const double forcing = k.forcing_per_optical_depth_w_m2 * tau;
            // Water above 80 km against the mesospheric water there (mixing ratio x the air above 80 km).
            const double air_above_80 = standard_air_pressure_pa(80.0e3) / 9.80665 * area;
            const double meso_water = air_above_80 * k.mesospheric_water_ppmv * 1.0e-6 * 18.015 / 28.96;
            const double meteoric_share = res.water_released_above_80km_kg_s * f * k.mesospheric_water_lifetime_days * kDay / meso_water;
            const double escape_water_kg_s = k.hydrogen_escape_kg_s * 18.015 / 2.016;   // the hydrogen escaping as water equivalent
            return Json{
                {"influx_t_per_day", t_day}, {"influx_t_per_year", tidy(t_day * 365.25)},
                {"ablated_t_per_day", tidy(ablated * to_t_day)}, {"surviving_t_per_day", tidy(surviving * to_t_day)},
                {"surviving_t_per_year", tidy(surviving * kYear / 1000.0)},
                {"surface_deposition_ug_m2_yr", tidy(surviving * kYear / area * 1.0e9)},
                {"spherules_over_100um_per_m2_yr", tidy(res.survivors_over_100um_per_s * f * kYear / area)},
                {"particles_over_50um_per_m2_yr", tidy(res.survivors_over_50um_per_s * f * kYear / area)},
                {"water", {{"in_t_per_day", tidy(water_in * to_t_day)}, {"in_t_per_year", tidy(water_in * kYear / 1000.0)}, {"released_t_per_day", tidy(water_released * to_t_day)},
                           {"released_above_80km_t_per_day", tidy(res.water_released_above_80km_kg_s * f * to_t_day)}, {"to_surface_kg_per_day", tidy(water_surface * kDay)},
                           {"to_oceans_t_per_year", tidy(water_in * kYear / 1000.0 * k.ocean_fraction)},
                           {"share_of_ocean_per_year", tidy(water_in * kYear / k.ocean_mass_kg)},
                           {"share_of_ocean_over_earth_history", tidy(water_in * kYear * k.earth_age_years / k.ocean_mass_kg)},
                           {"share_of_precipitation", tidy(water_in * kYear / k.precipitation_kg_yr)},
                           {"vs_hydrogen_escape_water_equivalent", tidy(water_in / escape_water_kg_s)},
                           {"vs_volcanic_degassing", k.volcanic_water_kg_yr > 0.0 ? Json(tidy(water_in * kYear / k.volcanic_water_kg_yr)) : Json(nullptr)},
                           {"share_of_mesospheric_water", tidy(meteoric_share)}}},
                {"mass", {{"gain_t_per_year", tidy(res.influx_kg_s * f * kYear / 1000.0)}, {"hydrogen_loss_t_per_year", tidy(k.hydrogen_escape_kg_s * kYear / 1000.0)},
                          {"helium_loss_t_per_year", tidy(k.helium_escape_kg_s * kYear / 1000.0)},
                          {"net_t_per_year", tidy((res.influx_kg_s * f - k.hydrogen_escape_kg_s - k.helium_escape_kg_s) * kYear / 1000.0)}}},
                {"energy", {{"kinetic_power_w", tidy(power)}, {"per_m2_w", tidy(power / area)}, {"share_of_sunlight", tidy(power / (1361.0 / 4.0 * area))},
                            {"vs_geothermal", k.geothermal_w > 0.0 ? Json(tidy(power / k.geothermal_w)) : Json(nullptr)}}},
                {"iron", {{"in_t_per_year", tidy(iron * kYear / 1000.0)}, {"to_oceans_t_per_year", tidy(iron * kYear / 1000.0 * k.ocean_fraction)},
                          {"vs_aeolian_dust_iron", k.aeolian_iron_kg_yr > 0.0 ? Json(tidy(iron * kYear / k.aeolian_iron_kg_yr)) : Json(nullptr)}}},
                {"smoke", {{"burden_t", tidy(burden / 1000.0)}, {"optical_depth", tidy(tau)}, {"surface_dimming_w_m2", tidy(tau * 1361.0 / 4.0)},
                           {"forcing_bound_w_m2", tidy(forcing)}, {"equilibrium_temperature_bound_k", tidy(forcing / k.feedback_w_m2_k)}}},
            };
        };
        Json scenarios = {{"low", per_influx(r.influx_low)}, {"central", per_influx(r.influx_central)}, {"high", per_influx(r.influx_high)}};
        Json what_if = Json::array();
        for (double mult : r.multipliers) {
            const Json s = per_influx(r.influx_central * mult);
            what_if.push_back({{"multiplier", mult}, {"influx_t_per_day", s["influx_t_per_day"]}, {"smoke_optical_depth", s["smoke"]["optical_depth"]},
                               {"forcing_bound_w_m2", s["smoke"]["forcing_bound_w_m2"]}, {"temperature_bound_k", s["smoke"]["equilibrium_temperature_bound_k"]},
                               {"water_t_per_year", s["water"]["in_t_per_year"]}, {"energy_share_of_sunlight", s["energy"]["share_of_sunlight"]}});
        }
        // The influx needed for the smoke forcing to reach 0.1 and 1 W/m2.
        const double forcing_per_t_day = per_influx(r.influx_central)["smoke"]["forcing_bound_w_m2"].get<double>() / r.influx_central;
        Json thresholds = {{"forcing_0_1_w_m2_t_per_day", tidy(0.1 / forcing_per_t_day)}, {"forcing_1_w_m2_t_per_day", tidy(1.0 / forcing_per_t_day)},
                           {"multiplier_for_0_1_w_m2", tidy(0.1 / forcing_per_t_day / r.influx_central)}, {"multiplier_for_1_w_m2", tidy(1.0 / forcing_per_t_day / r.influx_central)}};

        // ---- The map: hourly influx over one day --------------------------------------------------------------------
        const double cell_deg_x = 360.0 / static_cast<double>(r.nx), cell_deg_y = 180.0 / static_cast<double>(r.ny);
        const double metres_per_degree = 2.0 * kPi * kEarthRadius / 360.0;
        const Json grid = {{"nx", r.nx}, {"ny", r.ny}, {"origin_east_m", -180.0 * metres_per_degree}, {"origin_north_m", -90.0 * metres_per_degree},
                           {"spacing_east_m", cell_deg_x * metres_per_degree}, {"spacing_north_m", cell_deg_y * metres_per_degree},
                           {"geographic_anchor_longitude_deg", 0.0}, {"geographic_anchor_latitude_deg", 0.0},
                           {"coordinate_system", "local_azimuthal_equidistant_m"}, {"array_order", "row_major_y_then_x"}};
        const std::size_t count = r.nx * r.ny;
        using terraflux::weather::detail::days_from_civil;
        const double day0 = static_cast<double>(days_from_civil(r.year, static_cast<unsigned>(r.month), static_cast<unsigned>(r.day)) - days_from_civil(2000, 1, 1)) - 0.5;
        const double mean_ug_m2_day = res.influx_kg_s * kDay / area * 1.0e9;
        auto lat_of = [&](std::size_t i) { return -90.0 + (static_cast<double>(i / r.nx) + 0.5) * cell_deg_y; };
        auto lon_of = [&](std::size_t i) { return -180.0 + (static_cast<double>(i % r.nx) + 0.5) * cell_deg_x; };
        // Cell areas (cos latitude) for area-weighted means.
        std::vector<double> weight(count);
        double weight_total = 0.0;
        for (std::size_t i = 0; i < count; ++i) { weight[i] = std::cos(lat_of(i) * kPi / 180.0); weight_total += weight[i]; }
        Json frames = Json::array(), map_frames = Json::array();
        std::vector<double> daily(count, 0.0);
        const std::vector<int> placeholder_air(count, 288), zeros(count, 0);
        const std::string start_time = r.date + "T00:00:00Z";
        for (std::size_t h = 0; h < r.hours; ++h) {
            const double hour = 24.0 * static_cast<double>(h) / static_cast<double>(r.hours);
            const double d = day0 + hour / 24.0;
            std::vector<double> influx(count);
            double area_mean = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                influx[i] = relative_dust_influx(lat_of(i), lon_of(i), d, r.sources, r.isotropic);
                area_mean += influx[i] * weight[i] / weight_total;
            }
            // The point-source geometry averages to 1 only over the sphere; rescale the discrete grid exactly.
            std::vector<double> rate(count);
            Json daylight = Json::array();
            const double sun_lambda = solar_ecliptic_longitude_deg(d) * kPi / 180.0, eps = 23.439 * kPi / 180.0;
            const double sun_ra = std::atan2(std::cos(eps) * std::sin(sun_lambda), std::cos(sun_lambda)), sun_dec = std::asin(std::sin(eps) * std::sin(sun_lambda));
            const double gmst = greenwich_sidereal_deg(d) * kPi / 180.0;
            for (std::size_t i = 0; i < count; ++i) {
                rate[i] = mean_ug_m2_day * influx[i] / area_mean;
                daily[i] += rate[i] / static_cast<double>(r.hours);
                const double phi = lat_of(i) * kPi / 180.0, lst = gmst + lon_of(i) * kPi / 180.0;
                daylight.push_back(std::sin(phi) * std::sin(sun_dec) + std::cos(phi) * std::cos(sun_dec) * std::cos(lst - sun_ra) > 0.0);
            }
            Json points = Json::array();
            for (const auto& s : r.sources) {
                const auto eq = meteoroid_source_equatorial(s, d);
                double lon = eq[0] - greenwich_sidereal_deg(d);
                lon = std::fmod(std::fmod(lon + 180.0, 360.0) + 360.0, 360.0) - 180.0;
                points.push_back({{"name", s.name}, {"latitude_deg", tidy(eq[1], 4)}, {"longitude_deg", tidy(lon, 4)}});
            }
            const double sun_lon = std::fmod(std::fmod(sun_ra * 180.0 / kPi - greenwich_sidereal_deg(d) + 180.0, 360.0) + 360.0, 360.0) - 180.0;
            const double elapsed = hour * 3600.0;
            frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", start_time}, {"elapsed_seconds", elapsed}, {"grid", grid},
                              {"observation_updates", Json::array()},
                              {"fields", {{"air_temperature", {{"units", "K"}, {"values", placeholder_air}}}, {"wind_east", {{"units", "m/s"}, {"values", zeros}}},
                                          {"wind_north", {{"units", "m/s"}, {"values", zeros}}}}}});
            map_frames.push_back({{"hour_utc", tidy(hour, 4)}, {"elapsed_seconds", elapsed}, {"influx_ug_m2_day", tidy_array(rate, 3)}, {"daylight", daylight},
                                  {"source_zenith_points", points}, {"subsolar", {{"latitude_deg", tidy(sun_dec * 180.0 / kPi, 4)}, {"longitude_deg", tidy(sun_lon, 4)}}}});
        }
        // Daily means by latitude and the morning/evening contrast.
        Json by_latitude = Json::array();
        for (std::size_t row = 0; row < r.ny; ++row) {
            double s = 0.0;
            for (std::size_t col = 0; col < r.nx; ++col) s += daily[row * r.nx + col];
            by_latitude.push_back({{"latitude_deg", tidy(-90.0 + (static_cast<double>(row) + 0.5) * cell_deg_y, 4)}, {"influx_ug_m2_day", tidy(s / static_cast<double>(r.nx), 4)}});
        }
        Json land_mask = Json::array();
        for (unsigned char v : r.land) land_mask.push_back(v != 0);
        double land_share = 0.0, land_weight = 0.0;
        for (std::size_t i = 0; i < r.land.size(); ++i) { land_weight += weight[i]; if (r.land[i]) land_share += weight[i] * daily[i]; }
        double total_daily = 0.0;
        for (std::size_t i = 0; i < count; ++i) total_daily += weight[i] * daily[i];

        Json sources = Json::array();
        for (const auto& s : r.sources) sources.push_back({{"name", s.name}, {"longitude_from_apex_deg", s.longitude_from_apex_deg}, {"ecliptic_latitude_deg", s.ecliptic_latitude_deg}, {"weight", s.weight}});
        const auto& c = r.population.composition;
        Json speeds = Json::array();
        for (const auto& s : r.population.speeds) speeds.push_back({s.speed_km_s, s.weight});
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"cosmic_dust", {
                {"schema_version", "terraflux.cosmic.dust/v1"},
                {"method", "gruen_distribution_single_body_entry_langmuir_evaporation"},
                {"source_kind", "engine_output"},
                {"request", r.echo},
                {"influx", {{"central_t_per_day", r.influx_central}, {"low_t_per_day", r.influx_low}, {"high_t_per_day", r.influx_high}, {"note", r.influx_note},
                            {"gruen_t_per_day", tidy(gruen_mass_flux_t_per_day(r.population.min_mass_g, r.population.max_mass_g, kEarthRadius))}}},
                {"population", {{"min_mass_g", r.population.min_mass_g}, {"max_mass_g", r.population.max_mass_g}, {"speeds_km_s", speeds}, {"zenith_deg", r.population.zenith_deg},
                                {"composition", {{"water", c.water}, {"iron", c.iron}, {"sodium", c.sodium}, {"magnesium", c.magnesium}, {"silicon", c.silicon}, {"nickel", c.nickel}}},
                                {"bins", bins}}},
                {"fate", {{"influx_t_per_day", tidy(res.influx_kg_s * to_t_day)}, {"ablated_t_per_day", tidy(res.ablated_kg_s * to_t_day)},
                          {"surviving_t_per_day", tidy(res.surviving_kg_s * to_t_day)},
                          {"surviving_by_state_t_per_day", {{"unmelted", tidy(res.surviving_by_fate_kg_s[0] * to_t_day)}, {"partially_melted", tidy(res.surviving_by_fate_kg_s[1] * to_t_day)},
                                                            {"melted", tidy(res.surviving_by_fate_kg_s[2] * to_t_day)}}},
                          {"water_in_t_per_day", tidy(res.water_in_kg_s * to_t_day)}, {"water_released_t_per_day", tidy(res.water_released_kg_s * to_t_day)},
                          {"water_to_surface_kg_per_day", tidy(res.water_to_surface_kg_s * kDay)},
                          {"kinetic_power_w", tidy(res.kinetic_power_w)}, {"median_ablation_altitude_km", tidy(res.median_ablation_altitude_m / 1000.0)},
                          {"median_water_altitude_km", tidy(res.median_water_altitude_m / 1000.0)}}},
                {"profiles", {{"altitude_km", tidy_array(centres, 5)}, {"step_km", r.step_km}, {"ablation_t_per_day_per_bin", tidy_array(ablation)},
                              {"water_kg_per_day_per_bin", tidy_array(water)}, {"energy_w_per_bin", tidy_array(energy)},
                              {"air_density_kg_m3", [&] { std::vector<double> v; for (double km : centres) v.push_back(standard_air_density_kg_m3(km * 1000.0)); return tidy_array(v); }()}}},
                {"entry", {{"particle_density_kg_m3", r.entry.particle_density_kg_m3}, {"heat_transfer", r.entry.heat_transfer}, {"latent_heat_j_kg", r.entry.latent_heat_j_kg},
                           {"vapour_a", r.entry.vapour_a}, {"vapour_b", r.entry.vapour_b}, {"melt_start_k", r.entry.melt_start_k}, {"melt_full_k", r.entry.melt_full_k},
                           {"dehydration_start_k", r.entry.dehydration_start_k}, {"dehydration_end_k", r.entry.dehydration_end_k}}},
                {"scenarios", scenarios},
                {"what_if", what_if},
                {"thresholds", thresholds},
                {"comparisons", {{"smoke_residence_years", k.smoke_residence_years}, {"smoke_mass_extinction_m2_kg", k.smoke_mass_extinction_m2_kg},
                                 {"forcing_per_optical_depth_w_m2", k.forcing_per_optical_depth_w_m2}, {"mesospheric_water_ppmv", k.mesospheric_water_ppmv},
                                 {"mesospheric_water_lifetime_days", k.mesospheric_water_lifetime_days}, {"hydrogen_escape_kg_s", k.hydrogen_escape_kg_s},
                                 {"helium_escape_kg_s", k.helium_escape_kg_s}, {"ocean_mass_kg", k.ocean_mass_kg}, {"precipitation_kg_yr", k.precipitation_kg_yr},
                                 {"volcanic_water_kg_yr", k.volcanic_water_kg_yr}, {"aeolian_iron_kg_yr", k.aeolian_iron_kg_yr}, {"geothermal_w", k.geothermal_w},
                                 {"feedback_w_m2_k", k.feedback_w_m2_k}, {"earth_age_years", k.earth_age_years}, {"ocean_fraction", k.ocean_fraction}}},
                {"deposition_export", {{"schema_version", "terraflux.cosmic.deposition/v1"},
                                       {"dust_kg_m2_s", tidy(res.surviving_kg_s / area)}, {"smoke_kg_m2_s", tidy(res.ablated_kg_s / area)},
                                       {"water_kg_m2_s", tidy(res.water_in_kg_s / area)}, {"iron_kg_m2_s", tidy(res.influx_kg_s * c.iron / area)},
                                       {"note", "global means of the central influx (the smoke arrives after years, mostly at high latitudes; the map's diurnal pattern averages out)"}}},
                {"map", {{"projection", "equirectangular"}, {"cell_degrees", {cell_deg_x, cell_deg_y}}, {"date_utc", r.date}, {"hours", r.hours},
                         {"mean_influx_ug_m2_day", tidy(mean_ug_m2_day)}, {"isotropic_share", r.isotropic}, {"sources", sources},
                         {"land_mask", land_mask}, {"land_mask_source", r.land.empty() ? Json(nullptr) : Json(r.land_source)},
                         {"land_share_of_influx", r.land.empty() ? Json(nullptr) : Json(tidy(land_share / total_daily, 4))},
                         {"daily_mean_ug_m2_day", tidy_array(daily, 3)}, {"by_latitude", by_latitude}, {"frames", map_frames}}},
                {"limitations", Json::array({
                    "the total influx is a scenario input: published estimates range from about 5 to 300 t/day; the Gruen distribution sets only the shape",
                    "single-body entry of homogeneous spheres on straight paths: no fragmentation, no differential evaporation of elements, no porous or icy particles; the vapour pressure is calibrated to the micrometeorite collections",
                    "the water content of the dust (hydrated minerals, solar-wind rims) is uncertain by a factor of a few",
                    "meteoric smoke is a burden estimate (residence time x ablation), not a transport model; its optical properties and the sign of its forcing are uncertain",
                    "point sources on the sky stand for the sporadic meteoroid environment; meteor showers, the velocity of each source and the gravitational focusing by latitude are left out",
                    "the comparison quantities (hydrogen escape, volcanic water, aeolian iron, ...) are rounded literature orders of magnitude"})},
            }},
        };
        if (!r.solar_path.empty()) output["cosmic_dust"]["solar_comparison"] = solar_comparison(r.solar_path, res.kinetic_power_w / area);
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error(std::string("cannot write output: ") + argv[2]);
        out << output.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
