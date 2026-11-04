// terraflux-run-polar-melt: melting polar ice and the Atlantic overturning over centuries.
//
// Reads a terraflux.polar.melt-run/v1 request (calendar span and step, warming scenarios as global-mean
// warming curves, a Greenland-like ice sheet, an Arctic sea-ice column, the overturning box model calibrated
// to the present circulation, and an uncertainty ensemble) and writes a viewer timeline: Greenland maps of
// one scenario as frames, and a `polar` block with every scenario's series, the events (first ice-free
// September, negative surface mass balance, threshold crossing, weakening, collapse), the ensemble, the
// equilibrium diagram of the overturning and ecosystem-relevant indicators.
//
// Coupling per step: warming -> Greenland air (amplified, seasonal, lapse rate to the ice surface), snowfall
// scaled with temperature, ocean thermal forcing at the fronts; warming -> Arctic air and sun over the
// sea-ice column, whose Atlantic-water heat from below scales with the overturning; freshwater anomalies
// (Greenland runoff, calving and frontal melt, Arctic sea-ice loss, a stronger water cycle) -> the north box
// of the overturning, whose targets warm with polar amplification; the overturning's north-box temperature
// feeds back on the Greenland fronts.
#include "terraflux/weather/ice_sheet.hpp"
#include "terraflux/weather/marine_indicators.hpp"
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/ocean_circulation.hpp"
#include "terraflux/weather/overturning_box.hpp"
#include "terraflux/weather/sea_ice.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;
constexpr double kYear = 365.25 * 86400.0;
constexpr double kIceGt = 917.0 / 1.0e12;   // m3 of ice -> Gt

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

std::size_t required_positive_size(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number_integer() || value.get<long long>() <= 0 || value.get<long long>() > 100000) throw std::invalid_argument(std::string("run request field must be a positive integer: ") + key);
    return static_cast<std::size_t>(value.get<long long>());
}

std::vector<double> number_list(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_array() || value.empty()) throw std::invalid_argument(std::string("run request field must be a non-empty array: ") + key);
    std::vector<double> out;
    for (const Json& v : value) {
        if (!v.is_number() || !std::isfinite(v.get<double>())) throw std::invalid_argument(std::string("run request array must contain finite numbers: ") + key);
        out.push_back(v.get<double>());
    }
    return out;
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

Json tidy_array(const std::vector<double>& values, int digits = 5) {
    Json out = Json::array();
    for (double v : values) out.push_back(std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr));
    return out;
}

Json maybe(double v, int digits = 5) { return std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr); }

// Piecewise-linear curve in calendar years, held constant beyond its ends.
struct Curve {
    std::vector<double> years, values;
    double at(double year) const {
        if (year <= years.front()) return values.front();
        if (year >= years.back()) return values.back();
        std::size_t k = 1;
        while (years[k] < year) ++k;
        const double w = (year - years[k - 1]) / (years[k] - years[k - 1]);
        return values[k - 1] + (values[k] - values[k - 1]) * w;
    }
};

struct Scenario {
    std::string name;
    Curve warming;
};

struct GreenlandSetup {
    bool enabled{false};
    IceSheetConfig config;
    std::vector<double> initial_thickness;
    double south_sea_level_temperature_k{271.15};      // annual mean at sea level at the southern edge of the grid
    double temperature_gradient_k_per_km{-0.0065};      // northward change of the sea-level temperature
    double seasonal_amplitude_k{10.0};
    double warming_amplification{1.6};                   // Greenland warming per kelvin of global warming
    double precipitation_m_we_yr{0.5};                   // at the reference surface temperature
    double precipitation_reference_k{263.15};
    double precipitation_per_k{0.07};                    // Clausius-Clapeyron scaling with the surface temperature
    double thermal_forcing_k{2.0};                       // ocean at the fronts above freezing, start climate
    double thermal_forcing_per_k{0.4};                   // per kelvin of global warming
    double thermal_forcing_amoc_factor{1.0};             // per kelvin of north-box temperature change
    double spinup_years{3000.0};
    double spinup_step_days{365.25 / 12.0};
    std::size_t substeps{3};                            // the ice sheet steps every n model steps (about monthly for 10-day steps)
};

struct ArcticSetup {
    bool enabled{false};
    ArcticColumnConfig config;
    double mean_temperature_k{260.15};                   // annual-mean air, start climate
    double seasonal_amplitude_k{17.0};
    double warming_amplification{3.0};                   // Arctic amplification
    double latitude_deg{80.0};
    double relative_humidity{0.85};
    double wind_speed_m_s{5.0};
    double cloud_winter{0.5}, cloud_summer{0.85};
    double deep_heat_per_k{0.3};                         // extra Atlantic-water heat per kelvin of global warming
    bool heat_follows_overturning{true};                 // the Atlantic-water heat scales with the overturning
    double freshwater_residence_years{10.0};             // the Arctic stores liquid fresh water (Beaufort Gyre) before releasing it;
                                                         // exported ice melts in the North Atlantic within the year
    double spinup_years{60.0};
};

struct OverturningSetup {
    OverturningCalibration calibration;
    OverturningBoxConfig box;
    double north_amplification{1.5};
    double south_amplification{1.0};
    double hydrological_sv_per_k{0.01};
    double greenland_fraction{1.0};
    double noise_sv{0.0};
    double seed{1.0};
    double collapse_fraction{0.2};
    double early_warning_window_years{0.0};
};

struct Request {
    int start_year{1850}, end_year{2300};
    double step_days{10.0};
    int output_every_years{10};
    std::vector<Scenario> scenarios;
    bool control{true};
    GreenlandSetup greenland;
    ArcticSetup arctic;
    OverturningSetup overturning;
    std::vector<double> ensemble_salinity, ensemble_greenland_amplification;
    std::string map_scenario;
    Json echo;
};

Request parse_request(const Json& j) {
    Request r;
    r.echo = j;
    const Json& t = required(j, "time");
    r.start_year = static_cast<int>(required_number(t, "start_year"));
    r.end_year = static_cast<int>(required_number(t, "end_year"));
    r.step_days = optional_number(t, "timestep_days", r.step_days);
    r.output_every_years = static_cast<int>(optional_number(t, "output_every_years", r.output_every_years));
    if (r.start_year < 1000 || r.end_year > 5000 || r.end_year <= r.start_year || r.end_year - r.start_year > 2000) throw std::invalid_argument("time needs 1000 <= start_year < end_year <= 5000 and at most 2000 years");
    if (!(r.step_days >= 1.0 && r.step_days <= 30.5)) throw std::invalid_argument("time.timestep_days must lie within 1-30.5");
    if (r.output_every_years < 1 || (r.end_year - r.start_year) / r.output_every_years + 1 > 240) throw std::invalid_argument("output_every_years must keep the timeline within 240 frames");
    const Json& list = required(j, "scenarios");
    if (!list.is_array() || list.empty() || list.size() > 8) throw std::invalid_argument("scenarios must be an array of 1-8 scenarios");
    for (const Json& s : list) {
        Scenario sc;
        sc.name = optional_string(s, "name", "scenario");
        const Json& pts = required(s, "global_warming_k");
        if (!pts.is_array() || pts.size() < 1) throw std::invalid_argument("scenario global_warming_k needs [year, kelvin] points");
        for (const Json& pt : pts) {
            if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number()) throw std::invalid_argument("scenario points are [year, kelvin]");
            const double y = pt[0].get<double>(), v = pt[1].get<double>();
            if (!sc.warming.years.empty() && y <= sc.warming.years.back()) throw std::invalid_argument("scenario years must increase");
            if (!(v > -5.0 && v < 15.0)) throw std::invalid_argument("scenario warming must lie within -5..15 K");
            sc.warming.years.push_back(y);
            sc.warming.values.push_back(v);
        }
        for (const auto& other : r.scenarios) if (other.name == sc.name) throw std::invalid_argument("scenario names must be unique");
        r.scenarios.push_back(sc);
    }
    r.control = optional_bool(j, "control", r.control);
    r.map_scenario = optional_string(j, "map_scenario", r.scenarios.back().name);
    if (std::none_of(r.scenarios.begin(), r.scenarios.end(), [&](const Scenario& s) { return s.name == r.map_scenario; })) throw std::invalid_argument("map_scenario must name a scenario");

    if (j.contains("greenland")) {
        const Json& g = required(j, "greenland");
        auto& gl = r.greenland;
        gl.enabled = true;
        const Json& grid = required(g, "grid");
        auto& c = gl.config;
        c.grid.nx = required_positive_size(grid, "nx");
        c.grid.ny = required_positive_size(grid, "ny");
        c.grid.spacing_east_m = required_number(grid, "spacing_m");
        c.grid.spacing_north_m = c.grid.spacing_east_m;
        c.grid.origin_east_m = optional_number(grid, "origin_east_m", 0.0);
        c.grid.origin_north_m = optional_number(grid, "origin_north_m", 0.0);
        c.grid.geographic_anchor_longitude_deg = optional_number(grid, "anchor_longitude_deg", -42.0);
        c.grid.geographic_anchor_latitude_deg = optional_number(grid, "anchor_latitude_deg", 60.0);
        const std::size_t count = c.grid.nx * c.grid.ny;
        if (count > 20000) throw std::invalid_argument("greenland grid is limited to 20000 cells");
        // Land: an ellipse (the island) with a bed that rises toward the centre; sea elsewhere.
        const Json& land = required(g, "land");
        const double ce = required_number(land, "center_east_m"), cn = required_number(land, "center_north_m");
        const double ae = required_number(land, "radius_east_m"), an = required_number(land, "radius_north_m");
        const double coast_bed = optional_number(land, "coast_bed_m", 0.0), centre_bed = optional_number(land, "centre_bed_m", 300.0), sea_bed = optional_number(land, "sea_bed_m", -500.0);
        if (!(ae > 0.0 && an > 0.0)) throw std::invalid_argument("greenland land radii must be positive");
        c.ocean.assign(count, 1);
        c.bed_elevation_m.assign(count, sea_bed);
        for (std::size_t i = 0; i < count; ++i) {
            const double e = c.grid.origin_east_m + (static_cast<double>(i % c.grid.nx) + 0.5) * c.grid.spacing_east_m - ce;
            const double n = c.grid.origin_north_m + (static_cast<double>(i / c.grid.nx) + 0.5) * c.grid.spacing_north_m - cn;
            const double rr = std::hypot(e / ae, n / an);
            if (rr < 1.0) { c.ocean[i] = 0; c.bed_elevation_m[i] = coast_bed + (centre_bed - coast_bed) * (1.0 - rr * rr); }
        }
        if (g.contains("physics")) {
            const Json& p = required(g, "physics");
            auto& ph = c.physics;
            read_into(p, "rate_factor_pa3_s", ph.rate_factor_pa3_s);
            if (p.contains("degree_day_factor_snow_mm_k_day")) ph.degree_day_factor_snow_m_k_s = required_number(p, "degree_day_factor_snow_mm_k_day") / 1000.0 / 86400.0;
            if (p.contains("degree_day_factor_ice_mm_k_day")) ph.degree_day_factor_ice_m_k_s = required_number(p, "degree_day_factor_ice_mm_k_day") / 1000.0 / 86400.0;
            read_into(p, "pdd_standard_deviation_k", ph.pdd_standard_deviation_k);
            read_into(p, "refreezing_fraction", ph.refreezing_fraction);
            read_into(p, "snow_threshold_k", ph.snow_threshold_k);
            read_into(p, "lapse_rate_k_m", ph.lapse_rate_k_m);
            read_into(p, "front_water_depth_m", ph.front_water_depth_m);
            read_into(p, "marine_front_fraction", ph.marine_front_fraction);
        }
        validate_ice_sheet_config(c);
        const Json& dome = required(g, "initial_dome");
        gl.initial_thickness = elliptical_dome(c, optional_number(dome, "center_east_m", ce), optional_number(dome, "center_north_m", cn),
                                               optional_number(dome, "radius_east_m", ae), optional_number(dome, "radius_north_m", an), required_number(dome, "center_thickness_m"));
        const Json& cl = required(g, "climate");
        read_into(cl, "south_sea_level_temperature_k", gl.south_sea_level_temperature_k);
        read_into(cl, "temperature_gradient_k_per_km", gl.temperature_gradient_k_per_km);
        read_into(cl, "seasonal_amplitude_k", gl.seasonal_amplitude_k);
        read_into(cl, "warming_amplification", gl.warming_amplification);
        read_into(cl, "precipitation_m_we_yr", gl.precipitation_m_we_yr);
        read_into(cl, "precipitation_reference_k", gl.precipitation_reference_k);
        read_into(cl, "precipitation_per_k", gl.precipitation_per_k);
        read_into(cl, "thermal_forcing_k", gl.thermal_forcing_k);
        read_into(cl, "thermal_forcing_per_k", gl.thermal_forcing_per_k);
        read_into(cl, "thermal_forcing_amoc_factor", gl.thermal_forcing_amoc_factor);
        read_into(g, "spinup_years", gl.spinup_years);
        if (g.contains("step_every")) gl.substeps = required_positive_size(g, "step_every");
        if (gl.substeps > 12) throw std::invalid_argument("greenland.step_every must lie within 1-12");
        if (gl.south_sea_level_temperature_k < 230.0 || gl.south_sea_level_temperature_k > 300.0 || std::abs(gl.temperature_gradient_k_per_km) > 0.05 || gl.seasonal_amplitude_k < 0.0 || gl.seasonal_amplitude_k > 30.0 ||
            gl.warming_amplification < 0.0 || gl.warming_amplification > 5.0 || gl.precipitation_m_we_yr < 0.0 || gl.precipitation_m_we_yr > 5.0 || gl.precipitation_per_k < 0.0 || gl.precipitation_per_k > 0.2 ||
            gl.thermal_forcing_k < -2.0 || gl.thermal_forcing_k > 10.0 || gl.thermal_forcing_per_k < 0.0 || gl.thermal_forcing_per_k > 3.0 || gl.thermal_forcing_amoc_factor < 0.0 || gl.thermal_forcing_amoc_factor > 3.0 ||
            gl.spinup_years < 0.0 || gl.spinup_years > 20000.0) {
            throw std::invalid_argument("greenland climate parameters are outside their supported ranges");
        }
    }
    if (j.contains("arctic_sea_ice")) {
        const Json& a = required(j, "arctic_sea_ice");
        auto& ar = r.arctic;
        ar.enabled = true;
        read_into(a, "mean_temperature_k", ar.mean_temperature_k);
        read_into(a, "seasonal_amplitude_k", ar.seasonal_amplitude_k);
        read_into(a, "warming_amplification", ar.warming_amplification);
        read_into(a, "latitude_deg", ar.latitude_deg);
        read_into(a, "relative_humidity", ar.relative_humidity);
        read_into(a, "wind_speed_m_s", ar.wind_speed_m_s);
        read_into(a, "cloud_winter", ar.cloud_winter);
        read_into(a, "cloud_summer", ar.cloud_summer);
        read_into(a, "deep_ocean_heat_flux_w_m2", ar.config.deep_ocean_heat_flux_w_m2);
        read_into(a, "deep_heat_per_k", ar.deep_heat_per_k);
        ar.heat_follows_overturning = optional_bool(a, "heat_follows_overturning", ar.heat_follows_overturning);
        read_into(a, "freshwater_residence_years", ar.freshwater_residence_years);
        read_into(a, "mixed_layer_depth_m", ar.config.mixed_layer_depth_m);
        read_into(a, "arctic_ocean_area_m2", ar.config.arctic_ocean_area_m2);
        if (a.contains("ice_export_per_year")) ar.config.ice_export_per_s = required_number(a, "ice_export_per_year") / kYear;
        read_into(a, "spinup_years", ar.spinup_years);
        validate_arctic_column(ar.config);
        if (ar.mean_temperature_k < 220.0 || ar.mean_temperature_k > 290.0 || ar.seasonal_amplitude_k < 0.0 || ar.seasonal_amplitude_k > 40.0 || ar.warming_amplification < 0.0 || ar.warming_amplification > 8.0 ||
            ar.latitude_deg < 50.0 || ar.latitude_deg > 90.0 || ar.relative_humidity < 0.05 || ar.relative_humidity > 1.0 || ar.wind_speed_m_s < 0.0 || ar.wind_speed_m_s > 30.0 ||
            ar.cloud_winter < 0.0 || ar.cloud_winter > 1.0 || ar.cloud_summer < 0.0 || ar.cloud_summer > 1.0 || std::abs(ar.deep_heat_per_k) > 20.0 || ar.freshwater_residence_years < 0.1 || ar.freshwater_residence_years > 200.0 ||
            ar.spinup_years < 0.0 || ar.spinup_years > 1000.0) {
            throw std::invalid_argument("arctic_sea_ice parameters are outside their supported ranges");
        }
    }
    const Json& o = required(j, "overturning");
    auto& ov = r.overturning;
    if (o.contains("present")) {
        const Json& p = required(o, "present");
        read_into(p, "overturning_sv", ov.calibration.overturning_sv);
        read_into(p, "temperature_difference_k", ov.calibration.temperature_difference_k);
        read_into(p, "salinity_difference_psu", ov.calibration.salinity_difference_psu);
        read_into(p, "north_temperature_k", ov.calibration.north_temperature_k);
        read_into(p, "north_salinity_psu", ov.calibration.north_salinity_psu);
    }
    if (o.contains("box")) {
        const Json& b = required(o, "box");
        read_into(b, "north_volume_m3", ov.box.north_volume_m3);
        read_into(b, "south_volume_m3", ov.box.south_volume_m3);
        read_into(b, "temperature_relaxation_s", ov.box.temperature_relaxation_s);
        if (b.contains("temperature_relaxation_years")) ov.box.temperature_relaxation_s = required_number(b, "temperature_relaxation_years") * kYear;
        read_into(b, "thermal_expansion_per_k", ov.box.thermal_expansion_per_k);
        read_into(b, "haline_contraction_per_psu", ov.box.haline_contraction_per_psu);
    }
    read_into(o, "north_amplification", ov.north_amplification);
    read_into(o, "south_amplification", ov.south_amplification);
    read_into(o, "hydrological_sv_per_k", ov.hydrological_sv_per_k);
    read_into(o, "greenland_fraction", ov.greenland_fraction);
    read_into(o, "noise_sv", ov.noise_sv);
    read_into(o, "seed", ov.seed);
    read_into(o, "collapse_fraction", ov.collapse_fraction);
    read_into(o, "early_warning_window_years", ov.early_warning_window_years);
    if (ov.north_amplification < 0.0 || ov.north_amplification > 5.0 || ov.south_amplification < 0.0 || ov.south_amplification > 5.0 || std::abs(ov.hydrological_sv_per_k) > 0.2 ||
        ov.greenland_fraction < 0.0 || ov.greenland_fraction > 1.0 || ov.noise_sv < 0.0 || ov.noise_sv > 0.5 || ov.collapse_fraction <= 0.0 || ov.collapse_fraction >= 1.0 ||
        ov.early_warning_window_years < 0.0 || ov.early_warning_window_years > 1000.0) {
        throw std::invalid_argument("overturning parameters are outside their supported ranges");
    }
    calibrate_overturning(ov.calibration, ov.box);   // throws for an unusable present state
    if (j.contains("ensemble")) {
        const Json& e = required(j, "ensemble");
        if (e.contains("salinity_difference_psu")) r.ensemble_salinity = number_list(e, "salinity_difference_psu");
        if (e.contains("greenland_warming_amplification")) r.ensemble_greenland_amplification = number_list(e, "greenland_warming_amplification");
        for (double v : r.ensemble_salinity) { auto cal = ov.calibration; cal.salinity_difference_psu = v; calibrate_overturning(cal, ov.box); }
        for (double v : r.ensemble_greenland_amplification) if (v < 0.0 || v > 5.0) throw std::invalid_argument("ensemble greenland_warming_amplification must lie within 0-5");
        if (r.ensemble_salinity.size() * std::max<std::size_t>(1, r.ensemble_greenland_amplification.size()) > 40 || r.ensemble_greenland_amplification.size() > 10) throw std::invalid_argument("the ensemble is limited to 40 members");
    }
    return r;
}

// Daily-mean surface shortwave at a latitude on a day of the year (24 hourly samples of the sun).
double daily_shortwave(double latitude_deg, double day_of_year, double vapour_pressure_pa, double cloud) {
    double sum = 0.0;
    for (int h = 0; h < 24; ++h) sum += surface_shortwave_w_m2(solar_cosine_zenith(latitude_deg, 0.0, day_of_year, h + 0.5), vapour_pressure_pa, cloud);
    return sum / 24.0;
}

double seasonal(double day_of_year, double amplitude) { return amplitude * std::cos(2.0 * kPi * (day_of_year - 196.0) / 365.25); }

AirState arctic_air(const ArcticSetup& a, double warming, double day) {
    AirState air;
    air.temperature_k = a.mean_temperature_k + a.warming_amplification * warming + seasonal(day, a.seasonal_amplitude_k);
    air.pressure_pa = 101325.0;
    air.specific_humidity_kg_kg = a.relative_humidity * saturation_specific_humidity_kg_kg(air.temperature_k, air.pressure_pa);
    air.wind_speed_m_s = a.wind_speed_m_s;
    const double summer = 0.5 * (1.0 - std::cos(2.0 * kPi * (day - 15.0) / 365.25));   // 0 mid-January, 1 mid-July
    air.cloud_fraction = a.cloud_winter + (a.cloud_summer - a.cloud_winter) * summer;
    air.shortwave_down_w_m2 = daily_shortwave(a.latitude_deg, day, vapour_pressure_pa(air.specific_humidity_kg_kg, air.pressure_pa), air.cloud_fraction);
    return air;
}

IceSheetForcing greenland_forcing(const GreenlandSetup& g, const IceSheetState& s, double warming, double day, double thermal_forcing) {
    const auto& grid = g.config.grid;
    const std::size_t count = grid.nx * grid.ny;
    IceSheetForcing f;
    f.air_temperature_k.resize(count);
    f.precipitation_m_s.resize(count);
    f.ocean_thermal_forcing_k.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double north_km = (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m / 1000.0;
        const double annual = g.south_sea_level_temperature_k + g.temperature_gradient_k_per_km * north_km + g.warming_amplification * warming;
        f.air_temperature_k[i] = annual + seasonal(day, g.seasonal_amplitude_k);
        const double surface_annual = annual - g.config.physics.lapse_rate_k_m * std::max(0.0, s.surface_elevation_m.empty() ? 0.0 : s.surface_elevation_m[i]);
        f.precipitation_m_s[i] = std::min(1.0e-5, g.precipitation_m_we_yr * std::exp(g.precipitation_per_k * (surface_annual - g.precipitation_reference_k)) / kYear);
        if (g.config.ocean[i]) f.ocean_thermal_forcing_k[i] = std::min(30.0, std::max(-5.0, thermal_forcing));
    }
    return f;
}

double greenland_freshwater(const IceSheetState& s) { return s.runoff_m3_s + s.calving_m3_s + s.frontal_ablation_m3_s; }

// Exported sea ice melts in the North Atlantic: fresh water relative to sea water of 35 psu.
double exported_freshwater(const ArcticColumnState& s, const ArcticColumnConfig& c) {
    return s.export_m_s * c.arctic_ocean_area_m2 * c.ice.ice_density_kg_m3 / c.reference_density_kg_m3 * (1.0 - c.ice.ice_salinity_psu / 35.0);
}

// Running mean over the last year of steps.
struct AnnualMean {
    std::size_t length;
    std::deque<double> values;
    double sum{0.0};
    explicit AnnualMean(std::size_t n) : length(n) {}
    double push(double v) {
        values.push_back(v);
        sum += v;
        if (values.size() > length) { sum -= values.front(); values.pop_front(); }
        return sum / static_cast<double>(values.size());
    }
};

struct Baseline {
    IceSheetState greenland;
    ArcticColumnState arctic;
    double greenland_flux_m3_s{0.0};     // mean freshwater of the spun-up ice sheet
    double arctic_flux_m3_s{0.0};        // mean liquid sea-ice meltwater of the spun-up column
    double arctic_export_m3_s{0.0};      // mean freshwater of the exported ice
    Json spinup;
};

Baseline spin_up(const Request& r) {
    Baseline b;
    Json info = Json::object();
    if (r.greenland.enabled) {
        const auto& g = r.greenland;
        IceSheetState s = initialize_ice_sheet(g.config, g.initial_thickness);
        const double dt = g.spinup_step_days * 86400.0;
        const auto steps = static_cast<long long>(std::llround(g.spinup_years * 365.25 / g.spinup_step_days));
        const double initial_volume = s.volume_m3;
        long double flux = 0.0L;
        long long averaged = 0;
        for (long long k = 0; k < steps; ++k) {
            const double day = std::fmod((static_cast<double>(k) + 0.5) * g.spinup_step_days, 365.25);
            s = advance_ice_sheet(s, greenland_forcing(g, s, 0.0, day, g.thermal_forcing_k), g.config, dt);
            if (k >= steps - static_cast<long long>(std::llround(50.0 * 365.25 / g.spinup_step_days))) { flux += greenland_freshwater(s); ++averaged; }
        }
        // Drift over the last 50 spin-up years as a check on the equilibrium.
        b.greenland_flux_m3_s = averaged ? static_cast<double>(flux / averaged) : 0.0;
        s.elapsed_seconds = 0.0;
        s.cumulative_freshwater_m3 = 0.0;
        s.cumulative_sea_level_rise_m = 0.0;
        b.greenland = s;
        info["greenland"] = {{"years", g.spinup_years}, {"initial_volume_m3", initial_volume}, {"final_volume_m3", s.volume_m3}, {"sea_level_equivalent_m", tidy(s.sea_level_equivalent_m)},
                             {"freshwater_m3_s", tidy(b.greenland_flux_m3_s)}, {"climate", "start-year climate (no warming) with the seasonal cycle"}};
    }
    if (r.arctic.enabled) {
        ArcticColumnState s;
        s.mixed_layer_salinity_psu = r.arctic.config.reference_salinity_psu;
        s.mixed_layer_temperature_k = seawater_freezing_point_k(s.mixed_layer_salinity_psu);
        const double dt = r.step_days * 86400.0;
        const auto steps = static_cast<long long>(std::llround(r.arctic.spinup_years * 365.25 / r.step_days));
        ArcticColumnConfig cfg = r.arctic.config;
        long double flux = 0.0L, exported = 0.0L;
        long long averaged = 0;
        for (long long k = 0; k < steps; ++k) {
            const double day = std::fmod((static_cast<double>(k) + 0.5) * r.step_days, 365.25);
            s = advance_arctic_column(s, arctic_air(r.arctic, 0.0, day), cfg, dt);
            if (k >= steps - static_cast<long long>(std::llround(10.0 * 365.25 / r.step_days))) { flux += s.freshwater_m_s * cfg.arctic_ocean_area_m2; exported += exported_freshwater(s, cfg); ++averaged; }
        }
        b.arctic_flux_m3_s = averaged ? static_cast<double>(flux / averaged) : 0.0;
        b.arctic_export_m3_s = averaged ? static_cast<double>(exported / averaged) : 0.0;
        s.elapsed_seconds = 0.0;
        b.arctic = s;
        info["arctic_sea_ice"] = {{"years", r.arctic.spinup_years}, {"meltwater_m3_s", tidy(b.arctic_flux_m3_s)}, {"ice_export_freshwater_m3_s", tidy(b.arctic_export_m3_s)},
                                  {"march_thickness_m", tidy(s.ice_thickness_m)}, {"concentration", tidy(s.ice_concentration)}};
    }
    b.spinup = info;
    return b;
}

struct AnnualRecord {
    int year{0};
    double warming{0.0}, overturning_sv{0.0}, heat_transport_pw{0.0}, north_temperature_k{0.0}, south_temperature_k{0.0}, north_salinity_psu{0.0}, south_salinity_psu{0.0};
    double freshwater_sv{0.0}, threshold_sv{0.0}, recovery_years{0.0};
    double fw_greenland_sv{0.0}, fw_arctic_sv{0.0}, fw_hydrological_sv{0.0};
    double greenland_volume_m3{0.0}, greenland_sle_m{0.0}, sea_level_rise_m{0.0}, greenland_area_km2{0.0};
    double smb_gt{0.0}, melt_gt{0.0}, accumulation_gt{0.0}, runoff_gt{0.0}, calving_gt{0.0}, frontal_gt{0.0};
    double arctic_march_m{0.0}, arctic_september_m{0.0}, arctic_min_concentration{1.0}, arctic_ice_free_days{0.0}, arctic_max_temperature_k{0.0}, arctic_deep_heat_w_m2{0.0};
    double oxygen_north{0.0}, oxygen_south{0.0};
    double thermal_forcing_k{0.0};
    // Greenland maps (annual).
    std::vector<double> thickness, surface, smb_m_yr, melt_m_yr, air_k;
};

struct MemberResult {
    std::vector<AnnualRecord> years;
    std::vector<double> overturning_annual;
    int ice_free_year{0}, negative_smb_year{0}, crossing_year{0}, weakening25_year{0}, weakening50_year{0}, collapse_year{0};
    double initial_overturning_sv{0.0};
    double north_reference_k{0.0}, south_reference_k{0.0}, oxygen_north_reference{0.0}, oxygen_south_reference{0.0};
    std::vector<double> forcing_path_sv, overturning_path_sv;   // annual, for the equilibrium diagram
};

MemberResult run_member(const Request& r, const Scenario& sc, const Baseline& base, double salinity_difference, double greenland_amplification, bool keep_maps) {
    MemberResult out;
    auto cal = r.overturning.calibration;
    cal.salinity_difference_psu = salinity_difference;
    const CalibratedOverturning amoc = calibrate_overturning(cal, r.overturning.box);
    OverturningBoxState ob;
    ob.north_temperature_k = cal.north_temperature_k;
    ob.south_temperature_k = cal.north_temperature_k + cal.temperature_difference_k;
    ob.north_salinity_psu = cal.north_salinity_psu;
    ob.south_salinity_psu = cal.north_salinity_psu + cal.salinity_difference_psu;
    const double q0 = overturning_m3_s(ob, amoc.config);
    out.initial_overturning_sv = q0 / 1.0e6;
    out.north_reference_k = ob.north_temperature_k;
    out.south_reference_k = ob.south_temperature_k;
    out.oxygen_north_reference = oxygen_solubility_umol_kg(ob.north_temperature_k, ob.north_salinity_psu);
    out.oxygen_south_reference = oxygen_solubility_umol_kg(std::min(ob.south_temperature_k, 313.0), ob.south_salinity_psu);
    GreenlandSetup gl = r.greenland;
    gl.warming_amplification = greenland_amplification;
    IceSheetState ice = base.greenland;
    ArcticColumnState col = base.arctic;
    ArcticColumnConfig col_config = r.arctic.config;
    const double dt = r.step_days * 86400.0;
    const std::size_t per_year = static_cast<std::size_t>(std::llround(365.25 / r.step_days));
    AnnualMean greenland_mean(per_year), export_mean(per_year);
    // Liquid Arctic fresh water leaves through a reservoir with a residence time; it starts in steady state.
    const double residence = r.arctic.freshwater_residence_years * kYear;
    double arctic_store = base.arctic_flux_m3_s * residence;
    NormalSequence noise(static_cast<std::uint64_t>(r.overturning.seed));
    const double start_warming = sc.warming.at(r.start_year);
    const auto steps = static_cast<long long>(std::llround((r.end_year - r.start_year) * 365.25 / r.step_days));
    const std::size_t count = gl.enabled ? gl.config.grid.nx * gl.config.grid.ny : 0;
    // Annual accumulators.
    AnnualRecord acc;
    double q_sum = 0.0, ht_sum = 0.0, f_sum = 0.0, tn_sum = 0.0, ts_sum = 0.0, sn_sum = 0.0, ss_sum = 0.0, fg_sum = 0.0, fa_sum = 0.0, fh_sum = 0.0, w_sum = 0.0, tf_sum = 0.0, deep_sum = 0.0, d_star_sum = 0.0;
    double march_sum = 0.0, sep_sum = 0.0;
    int n_steps = 0, n_march = 0, n_sep = 0;
    std::vector<double> smb_map(count, 0.0), melt_map(count, 0.0), air_map(count, 0.0), air_sample(count, 0.0);
    int current_year = r.start_year;
    for (long long k = 0; k < steps; ++k) {
        const double t_years = (static_cast<double>(k) + 0.5) * r.step_days / 365.25;
        const double year = r.start_year + t_years;
        const double day = std::fmod(t_years, 1.0) * 365.25;
        const double warming = sc.warming.at(year) - start_warming;
        // Greenland: every `substeps` model steps, over their combined interval (forced at its middle).
        double g_flux_anomaly = 0.0, thermal_forcing = 0.0;
        if (gl.enabled) {
            thermal_forcing = gl.thermal_forcing_k + gl.thermal_forcing_per_k * warming + gl.thermal_forcing_amoc_factor * (ob.north_temperature_k - out.north_reference_k);
            if (k % static_cast<long long>(gl.substeps) == 0) {
                const long long n = std::min<long long>(static_cast<long long>(gl.substeps), steps - k);
                const double span = dt * static_cast<double>(n);
                const double mid_years = (static_cast<double>(k) + 0.5 * static_cast<double>(n)) * r.step_days / 365.25;
                const double mid_warming = sc.warming.at(r.start_year + mid_years) - start_warming;
                const IceSheetForcing f = greenland_forcing(gl, ice, mid_warming, std::fmod(mid_years, 1.0) * 365.25, thermal_forcing);
                ice = advance_ice_sheet(ice, f, gl.config, span);
                for (std::size_t i = 0; i < count; ++i) air_sample[i] = gl.config.ocean[i] ? f.air_temperature_k[i] : ice.surface_air_temperature_k[i];
            }
            g_flux_anomaly = greenland_mean.push(greenland_freshwater(ice)) - base.greenland_flux_m3_s;
            for (std::size_t i = 0; i < count; ++i) {
                smb_map[i] += ice.smb_m_s[i] * dt;
                melt_map[i] += ice.melt_m_s[i] * dt;
                air_map[i] += air_sample[i];
            }
        }
        // Arctic sea ice.
        double a_flux_anomaly = 0.0, deep = 0.0;
        if (r.arctic.enabled) {
            const double q_now = overturning_m3_s(ob, amoc.config);
            deep = r.arctic.config.deep_ocean_heat_flux_w_m2 * (r.arctic.heat_follows_overturning ? std::max(0.0, q_now / q0) : 1.0) + r.arctic.deep_heat_per_k * warming;
            col_config.deep_ocean_heat_flux_w_m2 = std::min(200.0, std::max(-50.0, deep));
            col = advance_arctic_column(col, arctic_air(r.arctic, warming, day), col_config, dt);
            const double decay = std::exp(-dt / residence);
            const double liquid = col.freshwater_m_s * col_config.arctic_ocean_area_m2;
            const double next_store = arctic_store * decay + liquid * residence * (1.0 - decay);
            const double released = (arctic_store - next_store) / dt + liquid;   // exact outflow of the linear reservoir over the step
            arctic_store = next_store;
            a_flux_anomaly = (released - base.arctic_flux_m3_s) + (export_mean.push(exported_freshwater(col, col_config)) - base.arctic_export_m3_s);
            const double cover = col.ice_concentration * col.ice_thickness_m;
            if (day >= 59.0 && day < 90.0) { march_sum += cover; ++n_march; }
            if (day >= 243.0 && day < 273.0) { sep_sum += cover; ++n_sep; acc.arctic_min_concentration = std::min(acc.arctic_min_concentration, col.ice_concentration); }
            if (col.ice_concentration < 0.15) acc.arctic_ice_free_days += r.step_days;
            acc.arctic_max_temperature_k = std::max(acc.arctic_max_temperature_k, col.mixed_layer_temperature_k);
        }
        // Overturning.
        const double hydrological = r.overturning.hydrological_sv_per_k * 1.0e6 * warming;
        const double greenland_part = r.overturning.greenland_fraction * g_flux_anomaly;
        const double arctic_part = a_flux_anomaly;
        double f_total = amoc.freshwater_sv * 1.0e6 + hydrological + greenland_part + arctic_part;
        const double f_deterministic = f_total;
        if (r.overturning.noise_sv > 0.0) f_total += r.overturning.noise_sv * 1.0e6 * std::sqrt(kYear / dt) * noise.next();
        const double tn_star = amoc.north_target_k + r.overturning.north_amplification * warming;
        const double ts_star = amoc.south_target_k + r.overturning.south_amplification * warming;
        ob = advance_overturning(ob, amoc.config, f_total, tn_star, ts_star, dt);
        const double q = overturning_m3_s(ob, amoc.config);
        q_sum += q; ht_sum += heat_transport_w(ob, amoc.config); f_sum += f_deterministic; tn_sum += ob.north_temperature_k; ts_sum += ob.south_temperature_k;
        sn_sum += ob.north_salinity_psu; ss_sum += ob.south_salinity_psu; fg_sum += greenland_part; fa_sum += arctic_part; fh_sum += hydrological; w_sum += warming; tf_sum += thermal_forcing; deep_sum += deep;
        d_star_sum += ts_star - tn_star;
        ++n_steps;
        // Close the calendar year.
        const int step_end_year = r.start_year + static_cast<int>(std::floor((static_cast<double>(k) + 1.0) * r.step_days / 365.25 + 1.0e-9));
        if (step_end_year > current_year || k + 1 == steps) {
            const double n = static_cast<double>(n_steps);
            AnnualRecord rec = acc;
            rec.year = current_year;
            rec.warming = w_sum / n;
            rec.overturning_sv = q_sum / n / 1.0e6;
            rec.heat_transport_pw = ht_sum / n / 1.0e15;
            rec.north_temperature_k = tn_sum / n; rec.south_temperature_k = ts_sum / n; rec.north_salinity_psu = sn_sum / n; rec.south_salinity_psu = ss_sum / n;
            rec.freshwater_sv = f_sum / n / 1.0e6;
            rec.fw_greenland_sv = fg_sum / n / 1.0e6; rec.fw_arctic_sv = fa_sum / n / 1.0e6; rec.fw_hydrological_sv = fh_sum / n / 1.0e6;
            rec.thermal_forcing_k = tf_sum / n;
            rec.arctic_deep_heat_w_m2 = deep_sum / n;
            const double d_star = d_star_sum / n;
            rec.threshold_sv = critical_freshwater_m3_s(amoc.config, d_star) / 1.0e6;
            rec.recovery_years = equilibrium_recovery_time_s(amoc.config, rec.freshwater_sv * 1.0e6, d_star) / kYear;
            rec.oxygen_north = oxygen_solubility_umol_kg(std::min(313.0, std::max(271.0, rec.north_temperature_k)), rec.north_salinity_psu);
            rec.oxygen_south = oxygen_solubility_umol_kg(std::min(313.0, std::max(271.0, rec.south_temperature_k)), rec.south_salinity_psu);
            if (r.arctic.enabled) { rec.arctic_march_m = n_march ? march_sum / n_march : 0.0; rec.arctic_september_m = n_sep ? sep_sum / n_sep : 0.0; }
            if (gl.enabled) {
                rec.greenland_volume_m3 = ice.volume_m3;
                rec.greenland_sle_m = ice.sea_level_equivalent_m;
                rec.sea_level_rise_m = ice.cumulative_sea_level_rise_m;
                rec.greenland_area_km2 = ice.area_m2 / 1.0e6;
                double smb = 0.0, melt = 0.0;
                const double area = gl.config.grid.spacing_east_m * gl.config.grid.spacing_north_m;
                const double to_year = kYear / (n * dt);   // calendar years hold 36 or 37 steps
                for (std::size_t i = 0; i < count; ++i) { smb_map[i] *= to_year; melt_map[i] *= to_year; smb += smb_map[i] * area; melt += melt_map[i] * area; }
                rec.smb_gt = smb * kIceGt;
                rec.melt_gt = melt * kIceGt;
                if (keep_maps) {
                    rec.thickness = ice.thickness_m;
                    rec.surface = ice.surface_elevation_m;
                    rec.smb_m_yr = smb_map;
                    rec.melt_m_yr = melt_map;
                    rec.air_k.resize(count);
                    for (std::size_t i = 0; i < count; ++i) rec.air_k[i] = air_map[i] / n;
                }
            }
            // Events.
            if (!out.ice_free_year && r.arctic.enabled && rec.arctic_min_concentration < 0.15) out.ice_free_year = rec.year;
            if (!out.negative_smb_year && gl.enabled && rec.smb_gt < 0.0) out.negative_smb_year = rec.year;
            if (!out.crossing_year && rec.freshwater_sv > rec.threshold_sv) out.crossing_year = rec.year;
            if (!out.weakening25_year && rec.overturning_sv < 0.75 * out.initial_overturning_sv) out.weakening25_year = rec.year;
            if (!out.weakening50_year && rec.overturning_sv < 0.5 * out.initial_overturning_sv) out.weakening50_year = rec.year;
            if (!out.collapse_year && rec.overturning_sv < r.overturning.collapse_fraction * out.initial_overturning_sv) out.collapse_year = rec.year;
            out.overturning_annual.push_back(rec.overturning_sv);
            out.forcing_path_sv.push_back(rec.freshwater_sv);
            out.overturning_path_sv.push_back(rec.overturning_sv);
            out.years.push_back(std::move(rec));
            // Reset.
            acc = AnnualRecord{};
            q_sum = ht_sum = f_sum = tn_sum = ts_sum = sn_sum = ss_sum = fg_sum = fa_sum = fh_sum = w_sum = tf_sum = deep_sum = d_star_sum = 0.0;
            march_sum = sep_sum = 0.0;
            n_steps = n_march = n_sep = 0;
            std::fill(smb_map.begin(), smb_map.end(), 0.0);
            std::fill(melt_map.begin(), melt_map.end(), 0.0);
            std::fill(air_map.begin(), air_map.end(), 0.0);
            current_year = step_end_year;
        }
    }
    return out;
}

Json series_json(const MemberResult& m, const std::vector<int>& output_years) {
    Json s = Json::object();
    auto pick = [&](auto getter, int digits = 5) {
        Json a = Json::array();
        for (int y : output_years) {
            const AnnualRecord* rec = nullptr;
            for (const auto& r : m.years) if (r.year == y) { rec = &r; break; }
            a.push_back(rec ? maybe(getter(*rec), digits) : Json(nullptr));
        }
        return a;
    };
    s["global_warming_k"] = pick([](const AnnualRecord& r) { return r.warming; }, 4);
    s["overturning_sv"] = pick([](const AnnualRecord& r) { return r.overturning_sv; });
    s["heat_transport_pw"] = pick([](const AnnualRecord& r) { return r.heat_transport_pw; });
    s["threshold_sv"] = pick([](const AnnualRecord& r) { return r.threshold_sv; });
    s["freshwater_sv"] = pick([](const AnnualRecord& r) { return r.freshwater_sv; });
    s["freshwater_greenland_sv"] = pick([](const AnnualRecord& r) { return r.fw_greenland_sv; });
    s["freshwater_arctic_sv"] = pick([](const AnnualRecord& r) { return r.fw_arctic_sv; });
    s["freshwater_hydrological_sv"] = pick([](const AnnualRecord& r) { return r.fw_hydrological_sv; });
    s["recovery_time_years"] = pick([](const AnnualRecord& r) { return r.recovery_years; });
    s["north_temperature_k"] = pick([](const AnnualRecord& r) { return r.north_temperature_k; });
    s["south_temperature_k"] = pick([](const AnnualRecord& r) { return r.south_temperature_k; });
    s["north_salinity_psu"] = pick([](const AnnualRecord& r) { return r.north_salinity_psu; });
    s["south_salinity_psu"] = pick([](const AnnualRecord& r) { return r.south_salinity_psu; });
    s["greenland_volume_m3"] = pick([](const AnnualRecord& r) { return r.greenland_volume_m3; });
    s["greenland_sea_level_equivalent_m"] = pick([](const AnnualRecord& r) { return r.greenland_sle_m; });
    s["sea_level_rise_m"] = pick([](const AnnualRecord& r) { return r.sea_level_rise_m; });
    s["greenland_area_km2"] = pick([](const AnnualRecord& r) { return r.greenland_area_km2; });
    s["greenland_smb_gt_yr"] = pick([](const AnnualRecord& r) { return r.smb_gt; });
    s["greenland_melt_gt_yr"] = pick([](const AnnualRecord& r) { return r.melt_gt; });
    s["ocean_thermal_forcing_k"] = pick([](const AnnualRecord& r) { return r.thermal_forcing_k; });
    s["arctic_ice_march_m"] = pick([](const AnnualRecord& r) { return r.arctic_march_m; });
    s["arctic_ice_september_m"] = pick([](const AnnualRecord& r) { return r.arctic_september_m; });
    s["arctic_min_concentration"] = pick([](const AnnualRecord& r) { return r.arctic_min_concentration; });
    s["arctic_ice_free_days"] = pick([](const AnnualRecord& r) { return r.arctic_ice_free_days; });
    s["arctic_summer_water_temperature_k"] = pick([](const AnnualRecord& r) { return r.arctic_max_temperature_k; });
    s["arctic_atlantic_heat_w_m2"] = pick([](const AnnualRecord& r) { return r.arctic_deep_heat_w_m2; });
    // Ecosystem-relevant indicators relative to the start.
    s["north_atlantic_temperature_change_k"] = pick([&](const AnnualRecord& r) { return r.north_temperature_k - m.north_reference_k; });
    s["tropical_atlantic_temperature_change_k"] = pick([&](const AnnualRecord& r) { return r.south_temperature_k - m.south_reference_k; });
    s["north_oxygen_change_pct"] = pick([&](const AnnualRecord& r) { return (r.oxygen_north - m.oxygen_north_reference) / m.oxygen_north_reference * 100.0; });
    s["south_oxygen_change_pct"] = pick([&](const AnnualRecord& r) { return (r.oxygen_south - m.oxygen_south_reference) / m.oxygen_south_reference * 100.0; });
    return s;
}

Json events_json(const MemberResult& m) {
    // Each event with the year and the global warming reached in that year.
    auto warming = [&](int year) {
        for (const auto& r : m.years) if (r.year == year) return Json(tidy(r.warming, 3));
        return Json(nullptr);
    };
    Json out = Json::object();
    const std::pair<const char*, int> events[] = {{"first_ice_free_september", m.ice_free_year}, {"greenland_negative_smb", m.negative_smb_year}, {"threshold_crossed", m.crossing_year},
                                                  {"overturning_weakened_25pct", m.weakening25_year}, {"overturning_weakened_50pct", m.weakening50_year}, {"overturning_collapsed", m.collapse_year}};
    for (const auto& [name, year] : events) out[name] = year ? Json{{"year", year}, {"global_warming_k", warming(year)}} : Json(nullptr);
    return out;
}

double record_at(const MemberResult& m, int year, double (*get)(const AnnualRecord&)) {
    for (const auto& r : m.years) if (r.year == year) return get(r);
    return std::numeric_limits<double>::quiet_NaN();
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
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.polar.melt-run/v1") throw std::invalid_argument("schema_version must be terraflux.polar.melt-run/v1");
        const Request r = parse_request(request);
        if (!r.greenland.enabled) throw std::invalid_argument("the run needs a greenland section (the map frames are the ice sheet)");
        const Baseline base = spin_up(r);

        std::vector<Scenario> scenarios = r.scenarios;
        if (r.control) {
            Scenario control;
            control.name = "control (start climate held)";
            control.warming.years = {static_cast<double>(r.start_year)};
            control.warming.values = {scenarios.front().warming.at(r.start_year)};
            scenarios.insert(scenarios.begin(), control);
        }
        std::vector<int> output_years;
        for (int y = r.start_year; y < r.end_year; y += r.output_every_years) output_years.push_back(y);
        if (output_years.back() != r.end_year - 1) output_years.push_back(r.end_year - 1);

        const double central_salinity = r.overturning.calibration.salinity_difference_psu, central_amplification = r.greenland.warming_amplification;
        Json scenario_blocks = Json::array();
        MemberResult map_member;
        std::vector<double> control_sea_level;   // the spin-up drift, removed from the scenarios' sea-level rise
        for (const auto& sc : scenarios) {
            const bool maps = sc.name == r.map_scenario, is_control = r.control && &sc == &scenarios.front();
            MemberResult central = run_member(r, sc, base, central_salinity, central_amplification, maps);
            if (is_control) for (const auto& y : central.years) control_sea_level.push_back(y.sea_level_rise_m);
            Json block = {{"name", sc.name}, {"control", is_control}, {"series", series_json(central, output_years)}, {"events", events_json(central)},
                          {"initial_overturning_sv", tidy(central.initial_overturning_sv)}};
            if (!control_sea_level.empty()) {
                Json relative = Json::array();
                for (int y : output_years) {
                    const std::size_t k = static_cast<std::size_t>(y - r.start_year);
                    relative.push_back(k < central.years.size() && k < control_sea_level.size() ? Json(tidy(central.years[k].sea_level_rise_m - control_sea_level[k])) : Json(nullptr));
                }
                block["series"]["sea_level_rise_vs_control_m"] = relative;
            }
            // Early-warning statistics of the annual overturning when the run is noisy.
            if (r.overturning.noise_sv > 0.0 && r.overturning.early_warning_window_years >= 4.0) {
                const auto window = static_cast<std::size_t>(r.overturning.early_warning_window_years);
                std::size_t end = central.overturning_annual.size();
                if (central.collapse_year) end = std::min(end, static_cast<std::size_t>(central.collapse_year - r.start_year));
                std::vector<double> record(central.overturning_annual.begin(), central.overturning_annual.begin() + static_cast<long>(end));
                if (record.size() > window + 2) {
                    const auto ews = early_warning_indicators(record, window);
                    const std::vector<double> ar1(ews.autocorrelation.begin() + static_cast<long>(window), ews.autocorrelation.end()), var(ews.variance.begin() + static_cast<long>(window), ews.variance.end());
                    block["early_warning"] = {{"window_years", window}, {"until_year", r.start_year + static_cast<int>(end)}, {"autocorrelation", tidy_array(ews.autocorrelation, 4)}, {"variance", tidy_array(ews.variance, 4)},
                                              {"kendall_tau_autocorrelation", tidy(kendall_tau(ar1), 3)}, {"kendall_tau_variance", tidy(kendall_tau(var), 3)},
                                              {"caveat", "the overturning recovers over decades to centuries; windows of a century or two resolve its slowing down only partly"}};
                }
            }
            // Equilibrium path for the diagram: annual forcing against the overturning.
            Json path = Json::array();
            for (std::size_t k = 0; k < central.years.size(); k += static_cast<std::size_t>(r.output_every_years)) path.push_back({tidy(central.forcing_path_sv[k]), tidy(central.overturning_path_sv[k])});
            block["trajectory"] = path;
            // Ensemble.
            Json members = Json::array();
            std::vector<int> collapses, crossings;
            std::size_t member_count = 0;
            const auto salinities = r.ensemble_salinity.empty() ? std::vector<double>{central_salinity} : r.ensemble_salinity;
            const auto amplifications = r.ensemble_greenland_amplification.empty() ? std::vector<double>{central_amplification} : r.ensemble_greenland_amplification;
            if (!is_control && (!r.ensemble_salinity.empty() || !r.ensemble_greenland_amplification.empty())) {
                for (double sal : salinities) {
                    for (double amp : amplifications) {
                        const MemberResult m = (sal == central_salinity && amp == central_amplification) ? central : run_member(r, sc, base, sal, amp, false);
                        ++member_count;
                        if (m.collapse_year) collapses.push_back(m.collapse_year);
                        if (m.crossing_year) crossings.push_back(m.crossing_year);
                        const auto sle = [](const AnnualRecord& x) { return x.sea_level_rise_m; };
                        const auto q = [](const AnnualRecord& x) { return x.overturning_sv; };
                        members.push_back({{"salinity_difference_psu", sal}, {"greenland_warming_amplification", amp}, {"events", events_json(m)},
                                           {"overturning_2100_sv", maybe(record_at(m, 2100, q))}, {"final_overturning_sv", tidy(m.years.back().overturning_sv)},
                                           {"sea_level_rise_2100_m", maybe(record_at(m, 2100, sle))}, {"final_sea_level_rise_m", tidy(m.years.back().sea_level_rise_m)}});
                    }
                }
                std::sort(collapses.begin(), collapses.end());
                std::sort(crossings.begin(), crossings.end());
                auto median = [](const std::vector<int>& v) { return v.empty() ? Json(nullptr) : Json(v[v.size() / 2]); };
                block["ensemble"] = {{"members", members}, {"member_count", member_count}, {"collapsed_members", collapses.size()}, {"threshold_crossed_members", crossings.size()},
                                     {"earliest_collapse", collapses.empty() ? Json(nullptr) : Json(collapses.front())}, {"median_collapse_of_collapsed", median(collapses)},
                                     {"latest_collapse", collapses.empty() ? Json(nullptr) : Json(collapses.back())},
                                     {"earliest_threshold_crossing", crossings.empty() ? Json(nullptr) : Json(crossings.front())}};
            }
            scenario_blocks.push_back(block);
            if (maps) map_member = std::move(central);
        }

        // Equilibrium diagram of the central calibration at the start climate and at the warmest scenario's end.
        const CalibratedOverturning amoc = calibrate_overturning(r.overturning.calibration, r.overturning.box);
        auto branches = [&](double d_star) {
            Json strong = Json::array(), weak = Json::array(), reversed = Json::array();
            const double f_crit = critical_freshwater_m3_s(amoc.config, d_star);
            for (int k = 0; k <= 60; ++k) {
                const double f = 1.4 * std::max(f_crit, 1.0e5) * k / 60.0;
                for (const auto& e : overturning_equilibria(amoc.config, f, d_star)) {
                    const Json point = {tidy(f / 1.0e6), tidy(e.overturning_m3_s / 1.0e6)};
                    if (e.overturning_m3_s < 0.0) reversed.push_back(point); else if (e.stable) strong.push_back(point); else weak.push_back(point);
                }
            }
            double q_at = 0.0;
            critical_freshwater_m3_s(amoc.config, d_star, &q_at);
            return Json{{"target_difference_k", tidy(d_star)}, {"threshold_sv", tidy(f_crit / 1.0e6)}, {"overturning_at_threshold_sv", tidy(q_at / 1.0e6)},
                        {"stable_strong", strong}, {"unstable", weak}, {"reversed", reversed}};
        };
        double max_warming = 0.0;
        for (const auto& sc : r.scenarios) max_warming = std::max(max_warming, sc.warming.at(r.end_year) - sc.warming.at(r.start_year));
        const double d0 = amoc.south_target_k - amoc.north_target_k;
        const Json diagram = {{"present_freshwater_sv", tidy(amoc.freshwater_sv)}, {"start", branches(d0)},
                              {"warmest", branches(d0 - (r.overturning.north_amplification - r.overturning.south_amplification) * max_warming)}, {"warmest_warming_k", tidy(max_warming)}};

        // Frames: Greenland maps of the map scenario.
        const auto& g = r.greenland.config.grid;
        const std::size_t count = g.nx * g.ny;
        Json grid_json = {{"nx", g.nx}, {"ny", g.ny}, {"origin_east_m", g.origin_east_m}, {"origin_north_m", g.origin_north_m}, {"spacing_east_m", g.spacing_east_m}, {"spacing_north_m", g.spacing_north_m},
                          {"geographic_anchor_longitude_deg", g.geographic_anchor_longitude_deg}, {"geographic_anchor_latitude_deg", g.geographic_anchor_latitude_deg},
                          {"coordinate_system", "local_azimuthal_equidistant_m"}, {"array_order", "row_major_y_then_x"}};
        const std::string start_time = std::to_string(r.start_year) + "-01-01T00:00:00Z";
        Json frames = Json::array(), map_frames = Json::array();
        const std::vector<double> zeros(count, 0.0);
        for (int y : output_years) {
            const AnnualRecord* rec = nullptr;
            for (const auto& x : map_member.years) if (x.year == y) { rec = &x; break; }
            if (!rec) continue;
            const double elapsed = static_cast<double>(y + 1 - r.start_year) * kYear;
            frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", start_time}, {"elapsed_seconds", elapsed}, {"grid", grid_json}, {"observation_updates", Json::array()},
                              {"fields", {{"air_temperature", {{"units", "K"}, {"values", tidy_array(rec->air_k, 4)}}}, {"wind_east", {{"units", "m/s"}, {"values", zeros}}}, {"wind_north", {{"units", "m/s"}, {"values", zeros}}}}}});
            Json smb = Json::array(), melt = Json::array();
            for (std::size_t i = 0; i < count; ++i) { smb.push_back(tidy(rec->smb_m_yr[i], 3)); melt.push_back(tidy(rec->melt_m_yr[i], 3)); }
            map_frames.push_back({{"year", y}, {"elapsed_seconds", elapsed}, {"ice_thickness_m", tidy_array(rec->thickness, 4)}, {"surface_elevation_m", tidy_array(rec->surface, 4)},
                                  {"smb_m_ice_yr", smb}, {"melt_m_ice_yr", melt}, {"volume_m3", rec->greenland_volume_m3}, {"sea_level_rise_m", tidy(rec->sea_level_rise_m)}});
        }
        if (frames.size() < 2) throw std::invalid_argument("the run is too short for a timeline of at least two frames");
        Json mask = Json::array();
        for (unsigned char v : r.greenland.config.ocean) mask.push_back(v == 0);
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"terrain", {{"schema_version", "terraflux.terrain.grid/v1"}, {"grid", grid_json}, {"units", "m"}, {"vertical_reference", "synthetic_local_datum"}, {"source_id", "synthetic_greenland_bed"},
                         {"values", tidy_array(r.greenland.config.bed_elevation_m, 5)}}},
            {"polar", {
                {"schema_version", "terraflux.polar.melt/v1"},
                {"method", "ice_sheet_sia_pdd_frontal_sea_ice_column_stommel_box"},
                {"source_kind", "engine_output"},
                {"request", r.echo},
                {"start_year", r.start_year}, {"end_year", r.end_year}, {"timestep_days", r.step_days},
                {"output_years", output_years},
                {"map_scenario", r.map_scenario},
                {"land_mask", mask},
                {"spinup", base.spinup},
                {"calibration", {{"flow_constant_m3_s", tidy(amoc.config.flow_constant_m3_s)}, {"present_freshwater_sv", tidy(amoc.freshwater_sv)}, {"north_target_k", tidy(amoc.north_target_k)},
                                 {"south_target_k", tidy(amoc.south_target_k)}, {"threshold_sv", tidy(critical_freshwater_m3_s(amoc.config, d0) / 1.0e6)},
                                 {"recovery_time_years", tidy(equilibrium_recovery_time_s(amoc.config, amoc.freshwater_sv * 1.0e6, d0) / kYear)}}},
                {"scenarios", scenario_blocks},
                {"equilibrium_diagram", diagram},
                {"greenland_frames", map_frames},
                {"limitations", Json::array({"conceptual two-box overturning: thresholds and dates depend on its calibration; read them as scenario-relative, not as forecasts",
                                             "Greenland on a coarse synthetic grid with degree-day melt and shallow-ice flow; no ice shelves, sliding or isostasy",
                                             "one Arctic sea-ice column stands for the basin; no ice drift",
                                             "ecosystem indicators are physical stressors (temperature, oxygen solubility, sea ice, sea level), not an ecosystem model"})},
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
