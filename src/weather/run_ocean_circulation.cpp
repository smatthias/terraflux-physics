// terraflux-run-ocean-circulation: the streams, their salt and the fresh water.
//
// Reads a terraflux.ocean.circulation-run/v1 request (time steps, grid, basin
// from terrain or a mask, layer properties, physics, wind / surface-temperature
// / precipitation forcing and river sources, optionally fed by a catchment
// hydrology timeline) and writes a viewer timeline with an `ocean` block: the
// currents, thickness, salinity and temperature per frame, the deep-water
// formation sites, the detected streams with their core streamlines and
// transport, and the river exchange points with their plumes.
//
// Optional extensions: `forcing.rain` (rain events, moving storms or the
// precipitation of a coupled-wind timeline, falling on the sea and on coastal
// land that drains to it), `forcing.air_sea` (bulk evaporation and heat
// exchange with an analytic or coupled atmosphere, shortwave from the sun's
// position) and `physics.thermal_layers` (the layered temperature and salinity
// column). Each adds its own per-frame block; without them the output is the
// 1.5-layer slab output unchanged.
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/ocean_circulation.hpp"
#include "terraflux/weather/synthetic_terrain.hpp"
#include "utc_time.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) throw std::invalid_argument(std::string("run request is missing field: ") + key);
    return object.at(key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) throw std::invalid_argument(std::string("run request field must be a non-empty string: ") + key);
    return value.get<std::string>();
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
    return required_string(object, key);
}

std::size_t required_positive_size(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number_integer()) throw std::invalid_argument(std::string("run request field must be a positive integer: ") + key);
    const long long parsed = value.get<long long>();
    if (parsed <= 0 || static_cast<unsigned long long>(parsed) > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string("run request field is outside the supported range: ") + key);
    }
    return static_cast<std::size_t>(parsed);
}

std::size_t optional_size(const Json& object, const char* key, std::size_t fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_number_integer() || value.get<long long>() < 0) throw std::invalid_argument(std::string("run request field must be a non-negative integer: ") + key);
    return static_cast<std::size_t>(value.get<long long>());
}

std::vector<double> required_number_array(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_array()) throw std::invalid_argument(std::string("run request field must be an array: ") + key);
    std::vector<double> result;
    result.reserve(value.size());
    for (const Json& item : value) {
        if (!item.is_number()) throw std::invalid_argument(std::string("run request array must contain numbers: ") + key);
        const double number = item.get<double>();
        if (!std::isfinite(number)) throw std::invalid_argument(std::string("run request array values must be finite: ") + key);
        result.push_back(number);
    }
    return result;
}

double sig(double v, int digits = 5) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    const double scale = std::pow(10.0, std::floor(std::log10(std::abs(v))) - (digits - 1));
    return std::round(v / scale) * scale;
}

// Significant-digit rounding that lands on the double nearest the decimal value (dividing by an exact
// power of ten), so the JSON prints "301.59" rather than "301.59000000000003". Used by the extension blocks.
double tidy(double v, int digits) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    const int exponent = static_cast<int>(std::floor(std::log10(std::abs(v)))) - (digits - 1);
    if (exponent < 0) {
        const double inverse = std::pow(10.0, -exponent);
        return std::round(v * inverse) / inverse;
    }
    const double scale = std::pow(10.0, exponent);
    return std::round(v / scale) * scale;
}

Json tidy_array(const std::vector<double>& values, int digits) {
    Json result = Json::array();
    for (double v : values) result.push_back(tidy(v, digits));
    return result;
}

Json rounded_array(const std::vector<double>& values, int digits = 5) {
    Json result = Json::array();
    for (double v : values) result.push_back(sig(v, digits));
    return result;
}

Json flag_array(const std::vector<unsigned char>& values) {
    Json result = Json::array();
    for (unsigned char v : values) result.push_back(v != 0);
    return result;
}

TransportGrid2D parse_grid(const Json& grid) {
    TransportGrid2D result;
    result.nx = required_positive_size(grid, "nx");
    result.ny = required_positive_size(grid, "ny");
    result.origin_east_m = required_number(grid, "origin_east_m");
    result.origin_north_m = required_number(grid, "origin_north_m");
    result.spacing_east_m = required_number(grid, "spacing_east_m");
    result.spacing_north_m = required_number(grid, "spacing_north_m");
    result.geographic_anchor_longitude_deg = required_number(grid, "geographic_anchor_longitude_deg");
    result.geographic_anchor_latitude_deg = required_number(grid, "geographic_anchor_latitude_deg");
    return result;
}

Json grid_json(const TransportGrid2D& grid) {
    return {
        {"nx", grid.nx}, {"ny", grid.ny},
        {"origin_east_m", grid.origin_east_m}, {"origin_north_m", grid.origin_north_m},
        {"spacing_east_m", grid.spacing_east_m}, {"spacing_north_m", grid.spacing_north_m},
        {"geographic_anchor_longitude_deg", grid.geographic_anchor_longitude_deg},
        {"geographic_anchor_latitude_deg", grid.geographic_anchor_latitude_deg},
        {"coordinate_system", "local_azimuthal_equidistant_m"},
        {"array_order", "row_major_y_then_x"},
    };
}

struct TimeRequest {
    std::string start_time_utc;
    std::size_t steps{0};
    std::size_t spinup_steps{0};
    std::size_t snapshot_every{1};
    bool initial_sst_from_target{false};   // layered column: start from the surface-temperature target
};

template <typename T>
void read_into(const Json& section, const char* key, T& target) {
    target = static_cast<T>(optional_number(section, key, static_cast<double>(target)));
}

// Basin: ocean where a terrain (prescribed or synthetic) lies at or below sea level, or an explicit mask.
std::vector<double> parse_basin(const Json& request, const TransportGrid2D& grid, OceanConfig& c, Json& provenance) {
    const Json& basin = required(request, "basin");
    const std::size_t count = grid.nx * grid.ny;
    std::vector<double> elevation;
    if (basin.contains("ocean_mask")) {
        const auto mask = required_number_array(basin, "ocean_mask");
        if (mask.size() != count) throw std::invalid_argument("basin.ocean_mask must contain one flag per grid cell");
        c.ocean.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            if (mask[i] != 0.0 && mask[i] != 1.0) throw std::invalid_argument("basin.ocean_mask values must be 0 or 1");
            c.ocean[i] = mask[i] == 1.0 ? 1 : 0;
        }
        elevation.assign(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) elevation[i] = c.ocean[i] ? -4000.0 : 500.0;
        provenance = {{"kind", "prescribed_ocean_mask"}};
        return elevation;
    }
    const Json& terrain = required(basin, "terrain");
    const double sea_level = optional_number(basin, "sea_level_m", 0.0);
    if (terrain.contains("values")) {
        elevation = required_number_array(terrain, "values");
        if (elevation.size() != count) throw std::invalid_argument("basin.terrain.values must contain one elevation per grid cell");
        provenance = {{"kind", "prescribed_cell_values"}, {"sea_level_m", sea_level}};
    } else {
        SyntheticTerrainSpec spec;
        spec.base_elevation_m = optional_number(terrain, "base_elevation_m", -4000.0);
        spec.slope_east_m_per_m = optional_number(terrain, "slope_east_m_per_m", 0.0);
        spec.slope_north_m_per_m = optional_number(terrain, "slope_north_m_per_m", 0.0);
        spec.roughness_m = optional_number(terrain, "roughness_m", 0.0);
        spec.roughness_seed = static_cast<std::uint32_t>(optional_number(terrain, "roughness_seed", 1.0));
        Json ridges = Json::array(), basins = Json::array();
        if (terrain.contains("ridges")) {
            const Json& list = required(terrain, "ridges");
            if (!list.is_array()) throw std::invalid_argument("basin.terrain.ridges must be an array");
            for (const Json& r : list) {
                SyntheticRidge ridge;
                ridge.orientation_deg = required_number(r, "orientation_deg");
                ridge.center_east_m = required_number(r, "center_east_m");
                ridge.center_north_m = required_number(r, "center_north_m");
                ridge.height_m = required_number(r, "height_m");
                ridge.half_width_m = required_number(r, "half_width_m");
                ridge.length_m = required_number(r, "length_m");
                spec.ridges.push_back(ridge);
                ridges.push_back({{"orientation_deg", ridge.orientation_deg}, {"center_east_m", ridge.center_east_m}, {"center_north_m", ridge.center_north_m},
                                  {"height_m", ridge.height_m}, {"half_width_m", ridge.half_width_m}, {"length_m", ridge.length_m}});
            }
        }
        if (terrain.contains("basins")) {
            const Json& list = required(terrain, "basins");
            if (!list.is_array()) throw std::invalid_argument("basin.terrain.basins must be an array");
            for (const Json& b : list) {
                SyntheticBasin bowl;
                bowl.center_east_m = required_number(b, "center_east_m");
                bowl.center_north_m = required_number(b, "center_north_m");
                bowl.depth_m = required_number(b, "depth_m");
                bowl.radius_m = required_number(b, "radius_m");
                spec.basins.push_back(bowl);
                basins.push_back({{"center_east_m", bowl.center_east_m}, {"center_north_m", bowl.center_north_m}, {"depth_m", bowl.depth_m}, {"radius_m", bowl.radius_m}});
            }
        }
        elevation = build_synthetic_terrain(spec, grid);
        provenance = {{"kind", "synthetic_ridges_and_basins"}, {"sea_level_m", sea_level}, {"base_elevation_m", spec.base_elevation_m},
                      {"slope_east_m_per_m", spec.slope_east_m_per_m}, {"slope_north_m_per_m", spec.slope_north_m_per_m},
                      {"roughness_m", spec.roughness_m}, {"roughness_seed", spec.roughness_seed}, {"ridges", ridges}, {"basins", basins}};
    }
    c.ocean.resize(count);
    for (std::size_t i = 0; i < count; ++i) c.ocean[i] = elevation[i] <= sea_level ? 1 : 0;
    return elevation;
}

OceanConfig parse_config(const Json& request, TimeRequest& time, std::vector<double>& elevation, Json& basin_provenance) {
    OceanConfig c;
    const Json& t = required(request, "time");
    time.start_time_utc = required_string(t, "start_time_utc");
    c.timestep_seconds = required_number(t, "timestep_seconds");
    time.steps = required_positive_size(t, "steps");
    time.spinup_steps = optional_size(t, "spinup_steps", 0);
    time.snapshot_every = optional_size(request, "snapshot_every_steps", 1);
    if (time.snapshot_every == 0) throw std::invalid_argument("snapshot_every_steps must be >= 1");
    if (time.steps / time.snapshot_every + 2 > 240) throw std::invalid_argument("the timeline would exceed 240 frames; raise snapshot_every_steps");
    c.grid = parse_grid(required(request, "grid"));
    validate_transport_grid(c.grid);
    elevation = parse_basin(request, c.grid, c, basin_provenance);
    basin_provenance["sponge"] = {{"kind", "none"}};
    if (required(request, "basin").contains("sponge")) {
        const Json& sp = required(request, "basin").at("sponge");
        const std::size_t south = optional_size(sp, "south_rows", 0), north = optional_size(sp, "north_rows", 0);
        const std::size_t count = c.grid.nx * c.grid.ny;
        c.sponge.assign(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            if (!c.ocean[i]) continue;
            const std::size_t y = i / c.grid.nx;
            double strength = 0.0;
            if (south > 0 && y < south) strength = std::max(strength, 1.0 - static_cast<double>(y) / static_cast<double>(south));
            if (north > 0 && y + north >= c.grid.ny) strength = std::max(strength, 1.0 - static_cast<double>(c.grid.ny - 1 - y) / static_cast<double>(north));
            c.sponge[i] = strength;
        }
        basin_provenance["sponge"] = {{"kind", "edge_rows"}, {"south_rows", south}, {"north_rows", north}, {"profile", "linear ramp from 1 at the wall to 0"}};
    }
    if (request.contains("ocean")) {
        const Json& o = required(request, "ocean");
        read_into(o, "initial_thickness_m", c.initial_thickness_m);
        read_into(o, "initial_salinity_psu", c.initial_salinity_psu);
        read_into(o, "initial_temperature_k", c.initial_temperature_k);
    }
    if (request.contains("physics")) {
        const Json& p = required(request, "physics");
        auto& ph = c.physics;
        read_into(p, "reference_density_kg_m3", ph.reference_density_kg_m3);
        read_into(p, "gravity_m_s2", ph.gravity_m_s2);
        read_into(p, "thermal_expansion_per_k", ph.thermal_expansion_per_k);
        read_into(p, "haline_contraction_per_psu", ph.haline_contraction_per_psu);
        read_into(p, "reference_temperature_k", ph.reference_temperature_k);
        read_into(p, "reference_salinity_psu", ph.reference_salinity_psu);
        read_into(p, "deep_temperature_k", ph.deep_temperature_k);
        read_into(p, "deep_salinity_psu", ph.deep_salinity_psu);
        read_into(p, "minimum_reduced_gravity_m_s2", ph.minimum_reduced_gravity_m_s2);
        read_into(p, "linear_friction_s", ph.linear_friction_s);
        read_into(p, "lateral_viscosity_m2_s", ph.lateral_viscosity_m2_s);
        read_into(p, "tracer_diffusivity_m2_s", ph.tracer_diffusivity_m2_s);
        read_into(p, "minimum_thickness_m", ph.minimum_thickness_m);
        read_into(p, "temperature_relaxation_s", ph.temperature_relaxation_s);
        read_into(p, "formation_reduced_gravity_m_s2", ph.formation_reduced_gravity_m_s2);
        read_into(p, "convection_time_scale_s", ph.convection_time_scale_s);
        read_into(p, "sponge_time_scale_s", ph.sponge_time_scale_s);
        read_into(p, "mixed_layer_depth_m", ph.mixed_layer_depth_m);
        read_into(p, "freshwater_mixing_time_s", ph.freshwater_mixing_time_s);
        if (p.contains("upwelling_in_sponge")) {
            const Json& v = p.at("upwelling_in_sponge");
            if (!v.is_boolean()) throw std::invalid_argument("physics.upwelling_in_sponge must be boolean");
            ph.upwelling_in_sponge = v.get<bool>();
        }
        ph.coriolis_mode = optional_string(p, "coriolis_mode", ph.coriolis_mode);
        read_into(p, "coriolis_parameter_s", ph.coriolis_parameter_s);
        read_into(p, "stream_speed_threshold_m_s", ph.stream_speed_threshold_m_s);
        read_into(p, "plume_salinity_deficit_psu", ph.plume_salinity_deficit_psu);
        read_into(p, "front_gradient_psu_per_100km", ph.front_gradient_psu_per_100km);
        read_into(p, "heat_capacity_j_kg_k", ph.heat_capacity_j_kg_k);
        read_into(p, "lens_salinity_deficit_psu", ph.lens_salinity_deficit_psu);
        read_into(p, "lens_memory_s", ph.lens_memory_s);
        if (p.contains("thermal_layers")) {
            const Json& t = required(p, "thermal_layers");
            auto& th = c.thermal;
            th.count = required_positive_size(t, "count");
            read_into(t, "surface_layer_m", th.surface_layer_m);
            read_into(t, "background_diffusivity_m2_s", th.background_diffusivity_m2_s);
            read_into(t, "wind_mixing_efficiency", th.wind_mixing_efficiency);
            read_into(t, "bulk_richardson_number", th.bulk_richardson_number);
            read_into(t, "initial_mixed_layer_depth_m", th.initial_mixed_layer_depth_m);
            read_into(t, "initial_thermocline_scale_m", th.initial_thermocline_scale_m);
            read_into(t, "shortwave_red_fraction", th.shortwave_red_fraction);
            read_into(t, "shortwave_red_depth_m", th.shortwave_red_depth_m);
            read_into(t, "shortwave_blue_depth_m", th.shortwave_blue_depth_m);
            read_into(t, "mld_density_threshold_kg_m3", th.mld_density_threshold_kg_m3);
            read_into(t, "ild_temperature_threshold_k", th.ild_temperature_threshold_k);
            const std::string initial = optional_string(t, "initial_surface_temperature", "uniform");
            if (initial != "uniform" && initial != "surface_temperature_target") throw std::invalid_argument("thermal_layers.initial_surface_temperature must be uniform or surface_temperature_target");
            time.initial_sst_from_target = initial == "surface_temperature_target";
        }
    }
    validate_ocean_config(c);
    return c;
}

Json config_json(const OceanConfig& c, const TimeRequest& time) {
    const auto& p = c.physics;
    return {
        {"time", {{"start_time_utc", time.start_time_utc}, {"timestep_seconds", c.timestep_seconds}, {"steps", time.steps}, {"spinup_steps", time.spinup_steps}}},
        {"grid", grid_json(c.grid)},
        {"ocean", {{"initial_thickness_m", c.initial_thickness_m}, {"initial_salinity_psu", c.initial_salinity_psu}, {"initial_temperature_k", c.initial_temperature_k}}},
        {"physics", {
            {"reference_density_kg_m3", p.reference_density_kg_m3}, {"gravity_m_s2", p.gravity_m_s2}, {"thermal_expansion_per_k", p.thermal_expansion_per_k},
            {"haline_contraction_per_psu", p.haline_contraction_per_psu}, {"reference_temperature_k", p.reference_temperature_k}, {"reference_salinity_psu", p.reference_salinity_psu},
            {"deep_temperature_k", p.deep_temperature_k}, {"deep_salinity_psu", p.deep_salinity_psu}, {"minimum_reduced_gravity_m_s2", p.minimum_reduced_gravity_m_s2},
            {"linear_friction_s", p.linear_friction_s}, {"lateral_viscosity_m2_s", p.lateral_viscosity_m2_s}, {"tracer_diffusivity_m2_s", p.tracer_diffusivity_m2_s},
            {"minimum_thickness_m", p.minimum_thickness_m}, {"temperature_relaxation_s", p.temperature_relaxation_s}, {"formation_reduced_gravity_m_s2", p.formation_reduced_gravity_m_s2},
            {"convection_time_scale_s", p.convection_time_scale_s}, {"sponge_time_scale_s", p.sponge_time_scale_s}, {"upwelling_in_sponge", p.upwelling_in_sponge}, {"mixed_layer_depth_m", p.mixed_layer_depth_m}, {"freshwater_mixing_time_s", p.freshwater_mixing_time_s}, {"coriolis_mode", p.coriolis_mode}, {"coriolis_parameter_s", p.coriolis_parameter_s},
            {"stream_speed_threshold_m_s", p.stream_speed_threshold_m_s}, {"plume_salinity_deficit_psu", p.plume_salinity_deficit_psu}, {"front_gradient_psu_per_100km", p.front_gradient_psu_per_100km}}},
    };
}

// Echo of the extension settings (only written when an extension is active, so slab runs are unchanged).
void extension_config_json(Json& request, const OceanConfig& c, bool air_sea, bool rain, const TimeRequest& time) {
    const auto& p = c.physics;
    request["physics"]["heat_capacity_j_kg_k"] = p.heat_capacity_j_kg_k;
    request["physics"]["lens_salinity_deficit_psu"] = p.lens_salinity_deficit_psu;
    request["physics"]["lens_memory_s"] = p.lens_memory_s;
    if (c.thermal.count >= 2) {
        const auto& t = c.thermal;
        request["physics"]["thermal_layers"] = {{"count", t.count}, {"surface_layer_m", t.surface_layer_m}, {"background_diffusivity_m2_s", t.background_diffusivity_m2_s},
            {"wind_mixing_efficiency", t.wind_mixing_efficiency}, {"bulk_richardson_number", t.bulk_richardson_number}, {"initial_mixed_layer_depth_m", t.initial_mixed_layer_depth_m}, {"initial_thermocline_scale_m", t.initial_thermocline_scale_m},
            {"shortwave_red_fraction", t.shortwave_red_fraction}, {"shortwave_red_depth_m", t.shortwave_red_depth_m}, {"shortwave_blue_depth_m", t.shortwave_blue_depth_m},
            {"mld_density_threshold_kg_m3", t.mld_density_threshold_kg_m3}, {"ild_temperature_threshold_k", t.ild_temperature_threshold_k},
            {"initial_surface_temperature", time.initial_sst_from_target ? "surface_temperature_target" : "uniform"}};
    }
    if (air_sea) {
        const auto& a = c.air_sea;
        request["air_sea"] = {{"air_density_kg_m3", a.air_density_kg_m3}, {"air_heat_capacity_j_kg_k", a.air_heat_capacity_j_kg_k}, {"heat_transfer_coefficient", a.heat_transfer_coefficient},
            {"moisture_transfer_coefficient", a.moisture_transfer_coefficient}, {"minimum_wind_m_s", a.minimum_wind_m_s}, {"surface_albedo", a.surface_albedo}, {"surface_emissivity", a.surface_emissivity},
            {"longwave_cloud_factor", a.longwave_cloud_factor}, {"saline_vapour_factor", a.saline_vapour_factor}, {"fresh_water_density_kg_m3", a.fresh_water_density_kg_m3}};
    }
    if (rain) request["coastal"] = {{"band_m", c.coastal.band_m}, {"runoff_coefficient", c.coastal.runoff_coefficient}, {"runoff_time_scale_s", c.coastal.runoff_time_scale_s}};
}

// Forcing: analytic gyre wind (or uniform, or a coupled-wind timeline), meridional surface-temperature
// and net-precipitation profiles, and rivers with constant, event-driven or catchment-fed discharge.
struct DischargeEvent { double start_seconds, duration_seconds, discharge_m3_s; };

struct RiverRequest {
    RiverSource source;                    // cell, base discharge, temperature, salinity, name
    std::vector<DischargeEvent> events;
    std::vector<double> series_time;       // catchment-fed discharge, piecewise constant
    std::vector<double> series_rate;
    double scale{1.0};
};

struct CoupledWind {
    std::vector<double> elapsed;
    std::vector<std::vector<double>> stress_east, stress_north, temperature;
    bool linear{false};                    // linear in time between frames (default: piecewise constant)
};

// A coupled-wind timeline sampled at the ocean cells for the air-sea exchange and the rain.
struct CoupledAtmosphere {
    std::vector<double> elapsed;
    std::vector<std::vector<double>> air_temperature, specific_humidity, cloud, rain_mm_h, wet_bulb, wind_speed, storm_wind;
    std::vector<double> pressure;          // static
    bool has_humidity{false}, has_cloud{false}, has_rain{false}, has_wet_bulb{false}, has_storm_wind{false};
    bool linear{false};
};

// Day of the year (1 = 1 January 00:00 UTC) plus the fraction of the day, from days since 1970-01-01.
double fractional_day_of_year(std::int64_t days_since_epoch, double utc_hour) {
    using terraflux::weather::detail::days_from_civil;
    int year = 1970 + static_cast<int>(std::floor(static_cast<double>(days_since_epoch) / 365.2425));
    while (days_from_civil(year, 1, 1) > days_since_epoch) --year;
    while (days_from_civil(year + 1, 1, 1) <= days_since_epoch) ++year;
    return static_cast<double>(days_since_epoch - days_from_civil(year, 1, 1)) + 1.0 + utc_hour / 24.0;
}

// Frame index and weight of the next frame at time t (weight 0 for piecewise constant).
std::pair<std::size_t, double> frame_weight(const std::vector<double>& elapsed, double t, bool linear) {
    std::size_t k = 0;
    while (k + 1 < elapsed.size() && elapsed[k + 1] <= t) ++k;
    if (!linear || k + 1 >= elapsed.size() || t <= elapsed[k]) return {k, 0.0};
    return {k, (t - elapsed[k]) / (elapsed[k + 1] - elapsed[k])};
}

double sample(const std::vector<std::vector<double>>& series, std::pair<std::size_t, double> at, std::size_t i) {
    const double a = series[at.first][i];
    return at.second > 0.0 ? a + (series[at.first + 1][i] - a) * at.second : a;
}

struct AirMass {
    double start_seconds, duration_seconds, east_m, north_m, radius_m, velocity_east_m_s, velocity_north_m_s, temperature_anomaly_k;
    std::optional<double> relative_humidity;
};

struct RainEvent {
    double start_seconds, duration_seconds, rate_mm_h, east_m, north_m, velocity_east_m_s, velocity_north_m_s;
    std::optional<double> radius_m, temperature_k;
};

struct AirSeaRequest {
    bool enabled{false};
    double air_temperature_offset_k{-1.0};
    double diurnal_amplitude_k{0.0};
    double relative_humidity_south{0.8}, relative_humidity_north{0.8};
    double cloud_south{0.4}, cloud_north{0.4};
    double pressure_pa{101325.0};
    std::string shortwave_kind{"astronomical"};
    double shortwave_w_m2{200.0};
    bool rain_saturates_air{true};
    std::vector<AirMass> air_masses;
    std::optional<CoupledAtmosphere> coupled;
};

struct RainRequest {
    bool enabled{false};
    std::vector<RainEvent> events;
    std::optional<CoupledAtmosphere> coupled;
    double temperature_offset_k{-2.0};     // without air-sea exchange: rain at the surface-temperature target + offset
};

struct ForcingRequest {
    std::string wind_kind{"gyre"};
    double stress_amplitude_pa{0.1};
    int gyres{2};
    double wall_taper_m{0.0};
    double south_margin_m{0.0};   // the gyre profile starts here (the strip below keeps the southern value)
    double uniform_east{0.0}, uniform_north{0.0};
    std::optional<CoupledWind> coupled;
    double air_density{1.2}, drag_coefficient{1.3e-3};
    double temperature_south_k{295.15}, temperature_north_k{278.15};
    struct ColdPatch { double east_m, north_m, radius_m, temperature_k; };
    std::vector<ColdPatch> cold_patches;
    double precipitation_south{-1.0e-8}, precipitation_north{1.0e-8};
    std::vector<RiverRequest> rivers;
    AirSeaRequest air_sea;
    RainRequest rain;
    double start_epoch_seconds{0.0};
    Json provenance;
};

std::size_t nearest_ocean_cell(double east, double north, const OceanConfig& c) {
    const auto& g = c.grid;
    double best = std::numeric_limits<double>::infinity();
    std::size_t cell = 0;
    for (std::size_t i = 0; i < c.ocean.size(); ++i) {
        if (!c.ocean[i]) continue;
        const double ce = g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m, cn = g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m;
        const double d = std::hypot(ce - east, cn - north);
        if (d < best) { best = d; cell = i; }
    }
    return cell;
}

CoupledWind load_coupled_wind(const std::string& path, double offset, const OceanConfig& c, double rho_air, double cd, Json& provenance) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("cannot open coupled timeline: " + path);
    Json timeline;
    try { timeline = Json::parse(file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid coupled timeline JSON: ") + error.what()); }
    if (!timeline.is_object() || timeline.value("schema_version", "") != "terraflux.weather.timeline/v1") throw std::invalid_argument("coupled timeline must be a terraflux.weather.timeline/v1 document");
    const Json& frames = required(timeline, "frames");
    if (!frames.is_array() || frames.empty()) throw std::invalid_argument("coupled timeline has no frames");
    const TransportGrid2D source = parse_grid(required(frames.at(static_cast<std::size_t>(0)), "grid"));
    validate_transport_grid(source);
    const std::size_t count = c.grid.nx * c.grid.ny, source_count = source.nx * source.ny;
    std::vector<std::size_t> map(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double east = c.grid.origin_east_m + (static_cast<double>(i % c.grid.nx) + 0.5) * c.grid.spacing_east_m;
        const double north = c.grid.origin_north_m + (static_cast<double>(i / c.grid.nx) + 0.5) * c.grid.spacing_north_m;
        const double fx = (east - source.origin_east_m) / source.spacing_east_m, fy = (north - source.origin_north_m) / source.spacing_north_m;
        if (fx < 0.0 || fy < 0.0 || fx >= static_cast<double>(source.nx) || fy >= static_cast<double>(source.ny)) throw std::invalid_argument("the ocean grid must lie within the coupled timeline grid (same local frame)");
        map[i] = static_cast<std::size_t>(fy) * source.nx + static_cast<std::size_t>(fx);
    }
    CoupledWind w;
    for (std::size_t k = 0; k < frames.size(); ++k) {
        const Json& frame = frames.at(k);
        const Json& fields = required(frame, "fields");
        const auto ue = required_number_array(required(fields, "wind_east"), "values"), un = required_number_array(required(fields, "wind_north"), "values"), t = required_number_array(required(fields, "air_temperature"), "values");
        if (ue.size() != source_count || un.size() != source_count || t.size() != source_count) throw std::invalid_argument("coupled frame arrays do not match its grid");
        std::vector<double> te, tn;
        wind_stress_from_wind(ue, un, rho_air, cd, te, tn);
        std::vector<double> me(count), mn(count), mt(count);
        for (std::size_t i = 0; i < count; ++i) { me[i] = te[map[i]]; mn[i] = tn[map[i]]; mt[i] = t[map[i]]; }
        w.elapsed.push_back(required_number(frame, "elapsed_seconds") + offset);
        w.stress_east.push_back(std::move(me));
        w.stress_north.push_back(std::move(mn));
        w.temperature.push_back(std::move(mt));
    }
    provenance = {{"kind", "coupled_timeline"}, {"path", path}, {"offset_seconds", offset}, {"frames", frames.size()}, {"air_density_kg_m3", rho_air}, {"drag_coefficient", cd},
                  {"sampling", "nearest_cell_piecewise_constant_in_time"}, {"sources", std::vector<std::string>{"frames.fields.wind_east", "frames.fields.wind_north", "frames.fields.air_temperature"}}};
    return w;
}

void load_catchment_series(const std::string& path, double scale, RiverRequest& river, Json& provenance) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("cannot open catchment timeline: " + path);
    Json timeline;
    try { timeline = Json::parse(file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid catchment timeline JSON: ") + error.what()); }
    if (!timeline.is_object() || !timeline.contains("hydrology")) throw std::invalid_argument("catchment timeline lacks a hydrology block");
    const Json& frames = required(required(timeline, "hydrology"), "frames");
    if (!frames.is_array() || frames.size() < 2) throw std::invalid_argument("catchment timeline needs at least two hydrology frames");
    double previous_time = 0.0, previous_volume = 0.0;
    for (std::size_t k = 0; k < frames.size(); ++k) {
        const Json& f = frames.at(k);
        const double t = required_number(f, "elapsed_seconds");
        const double volume = required_number(required(f, "cumulative_m3"), "ocean_discharge") + required_number(required(f, "cumulative_m3"), "edge_outflow");
        if (k > 0) {
            if (t <= previous_time) throw std::invalid_argument("catchment frames must have increasing times");
            river.series_time.push_back(previous_time);
            river.series_rate.push_back(std::max(0.0, (volume - previous_volume) / (t - previous_time)) * scale);
        }
        previous_time = t;
        previous_volume = volume;
    }
    river.scale = scale;
    provenance = {{"kind", "catchment_timeline"}, {"path", path}, {"scale", scale}, {"intervals", river.series_rate.size()}, {"source", "hydrology.frames[].cumulative_m3.ocean_discharge + edge_outflow"}};
}

// Nearest coupled cell of every ocean cell; the ocean grid must lie inside the coupled grid.
std::vector<std::size_t> coupled_map(const TransportGrid2D& source, const OceanConfig& c) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    std::vector<std::size_t> map(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double east = c.grid.origin_east_m + (static_cast<double>(i % c.grid.nx) + 0.5) * c.grid.spacing_east_m;
        const double north = c.grid.origin_north_m + (static_cast<double>(i / c.grid.nx) + 0.5) * c.grid.spacing_north_m;
        const double fx = (east - source.origin_east_m) / source.spacing_east_m, fy = (north - source.origin_north_m) / source.spacing_north_m;
        if (fx < 0.0 || fy < 0.0 || fx >= static_cast<double>(source.nx) || fy >= static_cast<double>(source.ny)) throw std::invalid_argument("the ocean grid must lie within the coupled timeline grid (same local frame)");
        map[i] = static_cast<std::size_t>(fy) * source.nx + static_cast<std::size_t>(fx);
    }
    return map;
}

std::string parse_interpolation(const Json& section) {
    const std::string mode = optional_string(section, "interpolation", "piecewise_constant");
    if (mode != "piecewise_constant" && mode != "linear") throw std::invalid_argument("coupled interpolation must be piecewise_constant or linear");
    return mode;
}

CoupledAtmosphere load_coupled_atmosphere(const std::string& path, double offset, bool linear, const OceanConfig& c, Json& provenance) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("cannot open coupled timeline: " + path);
    Json timeline;
    try { timeline = Json::parse(file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid coupled timeline JSON: ") + error.what()); }
    if (!timeline.is_object() || timeline.value("schema_version", "") != "terraflux.weather.timeline/v1") throw std::invalid_argument("coupled timeline must be a terraflux.weather.timeline/v1 document");
    const Json& frames = required(timeline, "frames");
    if (!frames.is_array() || frames.empty()) throw std::invalid_argument("coupled timeline has no frames");
    const TransportGrid2D source = parse_grid(required(frames.at(static_cast<std::size_t>(0)), "grid"));
    validate_transport_grid(source);
    const std::size_t count = c.grid.nx * c.grid.ny, source_count = source.nx * source.ny;
    const auto map = coupled_map(source, c);
    const Json empty = Json::object();
    const Json& atmosphere = timeline.contains("coupled_atmosphere") ? timeline.at("coupled_atmosphere") : empty;
    CoupledAtmosphere a;
    a.linear = linear;
    a.has_humidity = atmosphere.contains("specific_humidity_kg_kg_by_frame");
    a.has_cloud = atmosphere.contains("clouds");
    a.has_rain = atmosphere.contains("precipitation") || atmosphere.contains("orography");
    a.has_wet_bulb = atmosphere.contains("precipitation");
    a.has_storm_wind = atmosphere.contains("storms") && atmosphere.at("storms").contains("frames");
    auto mapped = [&](const std::vector<double>& values, const char* what) {
        if (values.size() != source_count) throw std::invalid_argument(std::string("coupled ") + what + " does not match its grid");
        std::vector<double> out(count);
        for (std::size_t i = 0; i < count; ++i) out[i] = values[map[i]];
        return out;
    };
    a.pressure.assign(count, 101325.0);
    if (atmosphere.contains("pressure_pa")) a.pressure = mapped(required_number_array(atmosphere, "pressure_pa"), "pressure");
    auto nth = [&](const char* section, std::size_t k) -> const Json& {
        const Json& list = required(required(atmosphere, section), "frames");
        if (!list.is_array() || list.size() != frames.size()) throw std::invalid_argument(std::string("coupled ") + section + " frames must match the timeline frames");
        return list.at(k);
    };
    for (std::size_t k = 0; k < frames.size(); ++k) {
        const Json& frame = frames.at(k);
        const double t = required_number(frame, "elapsed_seconds") + offset;
        if (!a.elapsed.empty() && t <= a.elapsed.back()) throw std::invalid_argument("coupled frames must have increasing times");
        a.elapsed.push_back(t);
        const Json& fields = required(frame, "fields");
        a.air_temperature.push_back(mapped(required_number_array(required(fields, "air_temperature"), "values"), "air temperature"));
        const auto ue = mapped(required_number_array(required(fields, "wind_east"), "values"), "wind"), un = mapped(required_number_array(required(fields, "wind_north"), "values"), "wind");
        std::vector<double> speed(count);
        for (std::size_t i = 0; i < count; ++i) speed[i] = std::hypot(ue[i], un[i]);
        a.wind_speed.push_back(std::move(speed));
        if (a.has_humidity) {
            const Json& list = required(atmosphere, "specific_humidity_kg_kg_by_frame");
            if (!list.is_array() || list.size() != frames.size() || !list.at(k).is_array()) throw std::invalid_argument("coupled humidity frames must match the timeline frames");
            std::vector<double> q;
            for (const Json& v : list.at(k)) { if (!v.is_number()) throw std::invalid_argument("coupled humidity must be numeric"); q.push_back(v.get<double>()); }
            a.specific_humidity.push_back(mapped(q, "humidity"));
        }
        if (a.has_cloud) a.cloud.push_back(mapped(required_number_array(nth("clouds", k), "cloud_fraction"), "cloud fraction"));
        if (a.has_storm_wind) a.storm_wind.push_back(mapped(required_number_array(nth("storms", k), "storm_wind_speed_m_s"), "storm wind"));
        if (a.has_rain) {
            std::vector<double> rate(count, 0.0);
            if (atmosphere.contains("precipitation")) {
                // rate_mm_h is the intensity while it precipitates; the cell-mean rate over the interval is
                // rate x duration_fraction (accumulation = rate x duration x interval).
                const Json& outcome = nth("precipitation", k);
                const auto pr = mapped(required_number_array(outcome, "rate_mm_h"), "precipitation");
                const auto duration = outcome.contains("duration_fraction") ? mapped(required_number_array(outcome, "duration_fraction"), "precipitation duration") : std::vector<double>(count, 1.0);
                for (std::size_t i = 0; i < count; ++i) rate[i] += std::max(0.0, pr[i]) * std::min(1.0, std::max(0.0, duration[i]));
                a.wet_bulb.push_back(mapped(required_number_array(nth("precipitation", k), "surface_wet_bulb_k"), "wet-bulb temperature"));
            }
            if (atmosphere.contains("orography")) {
                const auto oro = mapped(required_number_array(nth("orography", k), "orographic_precipitation_rate_mm_h"), "orographic precipitation");
                for (std::size_t i = 0; i < count; ++i) rate[i] += std::max(0.0, oro[i]);
            }
            a.rain_mm_h.push_back(std::move(rate));
        }
    }
    Json sources = {"frames.fields.air_temperature", "frames.fields.wind_east", "frames.fields.wind_north"};
    if (atmosphere.contains("pressure_pa")) sources.push_back("coupled_atmosphere.pressure_pa");
    if (a.has_humidity) sources.push_back("coupled_atmosphere.specific_humidity_kg_kg_by_frame");
    if (a.has_cloud) sources.push_back("coupled_atmosphere.clouds.frames[].cloud_fraction");
    if (atmosphere.contains("precipitation")) { sources.push_back("coupled_atmosphere.precipitation.frames[].rate_mm_h x duration_fraction"); sources.push_back("coupled_atmosphere.precipitation.frames[].surface_wet_bulb_k"); }
    if (a.has_storm_wind) sources.push_back("coupled_atmosphere.storms.frames[].storm_wind_speed_m_s (added in quadrature to the wind speed; drives the vertical mixing)");
    if (atmosphere.contains("orography")) sources.push_back("coupled_atmosphere.orography.frames[].orographic_precipitation_rate_mm_h");
    provenance = {{"kind", "coupled_timeline"}, {"path", path}, {"offset_seconds", offset}, {"frames", frames.size()},
                  {"sampling", linear ? "nearest_cell_linear_in_time" : "nearest_cell_piecewise_constant_in_time"}, {"sources", sources}};
    return a;
}

void parse_air_sea(const Json& section, ForcingRequest& f, OceanConfig& c, Json& provenance) {
    auto& a = f.air_sea;
    a.enabled = true;
    a.air_temperature_offset_k = optional_number(section, "air_temperature_offset_k", a.air_temperature_offset_k);
    a.diurnal_amplitude_k = optional_number(section, "diurnal_amplitude_k", a.diurnal_amplitude_k);
    a.relative_humidity_south = optional_number(section, "relative_humidity_south", a.relative_humidity_south);
    a.relative_humidity_north = optional_number(section, "relative_humidity_north", a.relative_humidity_north);
    a.cloud_south = optional_number(section, "cloud_fraction_south", a.cloud_south);
    a.cloud_north = optional_number(section, "cloud_fraction_north", a.cloud_north);
    a.pressure_pa = optional_number(section, "surface_pressure_pa", a.pressure_pa);
    if (std::abs(a.air_temperature_offset_k) > 30.0 || a.diurnal_amplitude_k < 0.0 || a.diurnal_amplitude_k > 15.0) throw std::invalid_argument("air_sea air temperature offset must lie within +-30 K and the diurnal amplitude within 0-15 K");
    for (double v : {a.relative_humidity_south, a.relative_humidity_north}) if (v < 0.05 || v > 1.0) throw std::invalid_argument("air_sea relative humidity must lie within 0.05-1");
    for (double v : {a.cloud_south, a.cloud_north}) if (v < 0.0 || v > 1.0) throw std::invalid_argument("air_sea cloud fraction must lie within 0-1");
    if (a.pressure_pa < 50000.0 || a.pressure_pa > 110000.0) throw std::invalid_argument("air_sea surface_pressure_pa must lie within 50-110 kPa");
    if (section.contains("rain_saturates_air")) {
        if (!section.at("rain_saturates_air").is_boolean()) throw std::invalid_argument("air_sea.rain_saturates_air must be boolean");
        a.rain_saturates_air = section.at("rain_saturates_air").get<bool>();
    }
    Json shortwave = {{"kind", "astronomical"}, {"clear_sky", "Zillman (1972)"}, {"cloud", "Reed (1977) 1 - 0.62 C"}};
    if (section.contains("shortwave")) {
        const Json& sw = required(section, "shortwave");
        a.shortwave_kind = optional_string(sw, "kind", "astronomical");
        if (a.shortwave_kind == "constant") {
            a.shortwave_w_m2 = required_number(sw, "w_m2");
            if (a.shortwave_w_m2 < 0.0 || a.shortwave_w_m2 > 1400.0) throw std::invalid_argument("air_sea.shortwave.w_m2 must lie within 0-1400");
            shortwave = {{"kind", "constant"}, {"w_m2", a.shortwave_w_m2}};
        } else if (a.shortwave_kind != "astronomical") {
            throw std::invalid_argument("air_sea.shortwave.kind must be astronomical or constant");
        }
    }
    Json masses = Json::array();
    if (section.contains("air_masses")) {
        const Json& list = required(section, "air_masses");
        if (!list.is_array()) throw std::invalid_argument("air_sea.air_masses must be an array");
        for (const Json& m : list) {
            AirMass am{required_number(m, "start_seconds"), required_number(m, "duration_seconds"), required_number(m, "center_east_m"), required_number(m, "center_north_m"),
                       required_number(m, "radius_m"), optional_number(m, "velocity_east_m_s", 0.0), optional_number(m, "velocity_north_m_s", 0.0), optional_number(m, "temperature_anomaly_k", 0.0), std::nullopt};
            if (m.contains("relative_humidity")) am.relative_humidity = required_number(m, "relative_humidity");
            if (am.start_seconds < -1.0e9 || am.duration_seconds <= 0.0 || am.radius_m <= 0.0 || std::abs(am.temperature_anomaly_k) > 40.0 || std::abs(am.velocity_east_m_s) > 50.0 || std::abs(am.velocity_north_m_s) > 50.0 ||
                (am.relative_humidity && (*am.relative_humidity < 0.05 || *am.relative_humidity > 1.0))) {
                throw std::invalid_argument("air masses need duration > 0, radius > 0, |anomaly| <= 40 K, |velocity| <= 50 m/s and relative humidity within 0.05-1");
            }
            Json echo = {{"start_seconds", am.start_seconds}, {"duration_seconds", am.duration_seconds}, {"center_east_m", am.east_m}, {"center_north_m", am.north_m}, {"radius_m", am.radius_m},
                         {"velocity_east_m_s", am.velocity_east_m_s}, {"velocity_north_m_s", am.velocity_north_m_s}, {"temperature_anomaly_k", am.temperature_anomaly_k}};
            if (am.relative_humidity) echo["relative_humidity"] = *am.relative_humidity;
            masses.push_back(echo);
            a.air_masses.push_back(am);
        }
    }
    Json coupled = {{"kind", "none"}};
    if (section.contains("coupled_timeline_path")) {
        a.coupled = load_coupled_atmosphere(required_string(section, "coupled_timeline_path"), optional_number(section, "coupled_time_offset_seconds", 0.0), parse_interpolation(section) == "linear", c, coupled);
    }
    if (section.contains("coefficients")) {
        const Json& k = required(section, "coefficients");
        auto& ac = c.air_sea;
        read_into(k, "air_density_kg_m3", ac.air_density_kg_m3);
        read_into(k, "air_heat_capacity_j_kg_k", ac.air_heat_capacity_j_kg_k);
        read_into(k, "heat_transfer_coefficient", ac.heat_transfer_coefficient);
        read_into(k, "moisture_transfer_coefficient", ac.moisture_transfer_coefficient);
        read_into(k, "minimum_wind_m_s", ac.minimum_wind_m_s);
        read_into(k, "surface_albedo", ac.surface_albedo);
        read_into(k, "surface_emissivity", ac.surface_emissivity);
        read_into(k, "longwave_cloud_factor", ac.longwave_cloud_factor);
        read_into(k, "saline_vapour_factor", ac.saline_vapour_factor);
        read_into(k, "fresh_water_density_kg_m3", ac.fresh_water_density_kg_m3);
        validate_ocean_config(c);
    }
    provenance = {{"air_temperature", a.coupled ? Json("coupled_timeline") : Json("surface_temperature_target + offset + diurnal cycle + air masses")},
                  {"air_temperature_offset_k", a.air_temperature_offset_k}, {"diurnal_amplitude_k", a.diurnal_amplitude_k},
                  {"relative_humidity", {{"south", a.relative_humidity_south}, {"north", a.relative_humidity_north}, {"profile", "linear in north"}}},
                  {"cloud_fraction", {{"south", a.cloud_south}, {"north", a.cloud_north}, {"profile", "linear in north"}}},
                  {"surface_pressure_pa", a.pressure_pa}, {"rain_saturates_air", a.rain_saturates_air}, {"wind_speed", "10 m wind equivalent to the wind stress"},
                  {"shortwave", shortwave}, {"air_masses", masses}, {"coupled", coupled},
                  {"bulk_formulae", "Dalton evaporation, Stanton sensible heat, Berliand longwave; surface energy balance implicit"}};
}

void parse_rain(const Json& section, ForcingRequest& f, OceanConfig& c, Json& provenance) {
    auto& r = f.rain;
    r.enabled = true;
    r.temperature_offset_k = optional_number(section, "temperature_offset_k", r.temperature_offset_k);
    if (std::abs(r.temperature_offset_k) > 20.0) throw std::invalid_argument("rain.temperature_offset_k must lie within +-20 K");
    Json events = Json::array();
    if (section.contains("events")) {
        const Json& list = required(section, "events");
        if (!list.is_array()) throw std::invalid_argument("forcing.rain.events must be an array");
        for (const Json& e : list) {
            RainEvent ev{required_number(e, "start_seconds"), required_number(e, "duration_seconds"), required_number(e, "rate_mm_h"), 0.0, 0.0,
                         optional_number(e, "velocity_east_m_s", 0.0), optional_number(e, "velocity_north_m_s", 0.0), std::nullopt, std::nullopt};
            if (ev.duration_seconds <= 0.0 || ev.rate_mm_h < 0.0 || ev.rate_mm_h > 500.0 || std::abs(ev.velocity_east_m_s) > 50.0 || std::abs(ev.velocity_north_m_s) > 50.0) {
                throw std::invalid_argument("rain events need duration > 0, a rate within 0-500 mm/h and |velocity| <= 50 m/s");
            }
            Json echo = {{"start_seconds", ev.start_seconds}, {"duration_seconds", ev.duration_seconds}, {"rate_mm_h", ev.rate_mm_h}, {"velocity_east_m_s", ev.velocity_east_m_s}, {"velocity_north_m_s", ev.velocity_north_m_s}};
            if (e.contains("radius_m")) {
                ev.radius_m = required_number(e, "radius_m");
                ev.east_m = required_number(e, "center_east_m");
                ev.north_m = required_number(e, "center_north_m");
                if (*ev.radius_m <= 0.0) throw std::invalid_argument("rain event radius_m must be positive");
                echo["radius_m"] = *ev.radius_m;
                echo["center_east_m"] = ev.east_m;
                echo["center_north_m"] = ev.north_m;
            }
            if (e.contains("temperature_k")) {
                ev.temperature_k = required_number(e, "temperature_k");
                if (*ev.temperature_k < 250.0 || *ev.temperature_k > 320.0) throw std::invalid_argument("rain event temperature_k must lie within 250-320 K");
                echo["temperature_k"] = *ev.temperature_k;
            }
            events.push_back(echo);
            r.events.push_back(ev);
        }
    }
    Json coupled = {{"kind", "none"}};
    if (section.contains("coupled_timeline_path")) {
        r.coupled = load_coupled_atmosphere(required_string(section, "coupled_timeline_path"), optional_number(section, "coupled_time_offset_seconds", 0.0), parse_interpolation(section) == "linear", c, coupled);
        if (!r.coupled->has_rain) throw std::invalid_argument("the rain coupled timeline carries no precipitation or orography frames");
    }
    if (r.events.empty() && !r.coupled) throw std::invalid_argument("forcing.rain needs events and/or a coupled_timeline_path");
    Json coastal = {{"band_m", 0.0}};
    if (section.contains("coastal_runoff")) {
        const Json& cr = required(section, "coastal_runoff");
        c.coastal.band_m = optional_number(cr, "band_m", c.coastal.band_m);
        c.coastal.runoff_coefficient = optional_number(cr, "runoff_coefficient", c.coastal.runoff_coefficient);
        c.coastal.runoff_time_scale_s = optional_number(cr, "time_scale_s", c.coastal.runoff_time_scale_s);
        validate_ocean_config(c);
        coastal = {{"band_m", c.coastal.band_m}, {"runoff_coefficient", c.coastal.runoff_coefficient}, {"time_scale_s", c.coastal.runoff_time_scale_s},
                   {"routing", "nearest sea cell within the band; linear reservoir; the rest of the rain is a land loss; land beyond the band is left to rivers"}};
    }
    provenance = {{"events", events}, {"coupled", coupled}, {"temperature_offset_k", r.temperature_offset_k}, {"coastal_runoff", coastal},
                  {"temperature", "event temperature if given; else the coupled wet-bulb temperature; else the wet-bulb of the air-sea atmosphere; else the surface-temperature target + offset"}};
}

ForcingRequest parse_forcing(const Json& request, OceanConfig& c, const TimeRequest& time) {
    ForcingRequest f;
    f.start_epoch_seconds = terraflux::weather::detail::utc_seconds(time.start_time_utc);
    const Json& section = required(request, "forcing");
    Json wind_provenance;
    if (section.contains("wind")) {
        const Json& w = required(section, "wind");
        f.wind_kind = optional_string(w, "kind", "gyre");
        f.air_density = optional_number(w, "air_density_kg_m3", f.air_density);
        f.drag_coefficient = optional_number(w, "drag_coefficient", f.drag_coefficient);
        if (f.air_density <= 0.0 || f.air_density > 2.0 || f.drag_coefficient <= 0.0 || f.drag_coefficient > 0.01) throw std::invalid_argument("wind air density / drag coefficient out of range");
        if (f.wind_kind == "gyre") {
            f.stress_amplitude_pa = optional_number(w, "stress_amplitude_pa", f.stress_amplitude_pa);
            f.gyres = static_cast<int>(optional_number(w, "gyres", 2.0));
            f.wall_taper_m = optional_number(w, "wall_taper_m", 0.0);
            f.south_margin_m = optional_number(w, "south_margin_m", 0.0);
            if (std::abs(f.stress_amplitude_pa) > 2.0 || f.gyres < 1 || f.gyres > 4 || f.wall_taper_m < 0.0 || f.south_margin_m < 0.0 || f.south_margin_m >= static_cast<double>(c.grid.ny) * c.grid.spacing_north_m) {
                throw std::invalid_argument("gyre wind needs |stress_amplitude_pa| <= 2, 1-4 gyres, wall_taper_m >= 0 and a south_margin_m inside the domain");
            }
            wind_provenance = {{"kind", "gyre"}, {"stress_amplitude_pa", f.stress_amplitude_pa}, {"gyres", f.gyres}, {"wall_taper_m", f.wall_taper_m}, {"south_margin_m", f.south_margin_m},
                               {"profile", "tau_x = -tau_0 cos(gyres pi y' / L') with y' = max(0, y - south_margin), L' = L_y - south_margin; times the wall taper (1 - exp(-(y/taper)^2)) (1 - exp(-((L_y - y)/taper)^2)) when set"}};
        } else if (f.wind_kind == "uniform") {
            f.uniform_east = required_number(w, "stress_east_pa");
            f.uniform_north = required_number(w, "stress_north_pa");
            wind_provenance = {{"kind", "uniform"}, {"stress_east_pa", f.uniform_east}, {"stress_north_pa", f.uniform_north}};
        } else if (f.wind_kind == "coupled_timeline") {
            f.coupled = load_coupled_wind(required_string(w, "path"), optional_number(w, "offset_seconds", 0.0), c, f.air_density, f.drag_coefficient, wind_provenance);
            if (w.contains("interpolation")) {
                f.coupled->linear = parse_interpolation(w) == "linear";
                wind_provenance["sampling"] = f.coupled->linear ? "nearest_cell_linear_in_time" : "nearest_cell_piecewise_constant_in_time";
            }
        } else {
            throw std::invalid_argument("forcing.wind.kind must be gyre, uniform or coupled_timeline");
        }
    } else {
        wind_provenance = {{"kind", "gyre"}, {"stress_amplitude_pa", f.stress_amplitude_pa}, {"gyres", f.gyres}, {"profile", "tau_x = -tau_0 cos(gyres pi y / L_y)"}};
    }
    if (section.contains("surface_temperature")) {
        const Json& t = required(section, "surface_temperature");
        f.temperature_south_k = optional_number(t, "south_k", f.temperature_south_k);
        f.temperature_north_k = optional_number(t, "north_k", f.temperature_north_k);
        if (f.temperature_south_k < 260.0 || f.temperature_south_k > 320.0 || f.temperature_north_k < 260.0 || f.temperature_north_k > 320.0) throw std::invalid_argument("surface temperature targets must lie within 260-320 K");
        if (t.contains("cold_patches")) {
            const Json& list = required(t, "cold_patches");
            if (!list.is_array()) throw std::invalid_argument("surface_temperature.cold_patches must be an array");
            for (const Json& cp : list) {
                ForcingRequest::ColdPatch patch{required_number(cp, "east_m"), required_number(cp, "north_m"), required_number(cp, "radius_m"), required_number(cp, "temperature_k")};
                if (patch.radius_m <= 0.0 || patch.temperature_k < 260.0 || patch.temperature_k > 320.0) throw std::invalid_argument("cold patches need radius_m > 0 and a temperature within 260-320 K");
                f.cold_patches.push_back(patch);
            }
        }
    }
    if (section.contains("net_precipitation")) {
        const Json& pe = required(section, "net_precipitation");
        f.precipitation_south = optional_number(pe, "south_m_s", f.precipitation_south);
        f.precipitation_north = optional_number(pe, "north_m_s", f.precipitation_north);
        if (std::abs(f.precipitation_south) > 1.0e-5 || std::abs(f.precipitation_north) > 1.0e-5) throw std::invalid_argument("net precipitation must lie within +-1e-5 m/s");
    }
    Json rivers = Json::array();
    if (section.contains("rivers")) {
        const Json& list = required(section, "rivers");
        if (!list.is_array()) throw std::invalid_argument("forcing.rivers must be an array");
        for (const Json& r : list) {
            RiverRequest river;
            river.source.name = optional_string(r, "name", "river");
            const double east = required_number(r, "east_m"), north = required_number(r, "north_m");
            river.source.cell = nearest_ocean_cell(east, north, c);
            river.source.discharge_m3_s = optional_number(r, "discharge_m3_s", 0.0);
            river.source.temperature_k = optional_number(r, "temperature_k", 283.15);
            river.source.salinity_psu = optional_number(r, "salinity_psu", 0.0);
            if (river.source.discharge_m3_s < 0.0 || river.source.discharge_m3_s > 1.0e7) throw std::invalid_argument("river discharge must lie within 0-1e7 m3/s");
            Json events = Json::array();
            if (r.contains("events")) {
                const Json& ev = required(r, "events");
                if (!ev.is_array()) throw std::invalid_argument("river events must be an array");
                for (const Json& e : ev) {
                    DischargeEvent d{required_number(e, "start_seconds"), required_number(e, "duration_seconds"), required_number(e, "discharge_m3_s")};
                    if (d.start_seconds < 0.0 || d.duration_seconds <= 0.0 || d.discharge_m3_s < 0.0 || d.discharge_m3_s > 1.0e7) throw std::invalid_argument("river events need start >= 0, duration > 0 and discharge within 0-1e7 m3/s");
                    river.events.push_back(d);
                    events.push_back({{"start_seconds", d.start_seconds}, {"duration_seconds", d.duration_seconds}, {"discharge_m3_s", d.discharge_m3_s}});
                }
            }
            Json catchment = {{"kind", "none"}};
            if (r.contains("catchment_timeline_path")) {
                const double scale = optional_number(r, "catchment_scale", 1.0);
                if (scale <= 0.0 || scale > 1.0e6) throw std::invalid_argument("catchment_scale must lie within (0, 1e6]");
                load_catchment_series(required_string(r, "catchment_timeline_path"), scale, river, catchment);
            }
            rivers.push_back({{"name", river.source.name}, {"east_m", east}, {"north_m", north}, {"mouth_cell", river.source.cell}, {"discharge_m3_s", river.source.discharge_m3_s},
                              {"temperature_k", river.source.temperature_k}, {"salinity_psu", river.source.salinity_psu}, {"events", events}, {"catchment", catchment}});
            f.rivers.push_back(std::move(river));
        }
    }
    Json patches = Json::array();
    for (const auto& cp : f.cold_patches) patches.push_back({{"east_m", cp.east_m}, {"north_m", cp.north_m}, {"radius_m", cp.radius_m}, {"temperature_k", cp.temperature_k}});
    f.provenance = {{"wind", wind_provenance},
                    {"surface_temperature", {{"south_k", f.temperature_south_k}, {"north_k", f.temperature_north_k}, {"profile", "linear in north, Gaussian cold patches blended in"}, {"cold_patches", patches}}},
                    {"net_precipitation", {{"south_m_s", f.precipitation_south}, {"north_m_s", f.precipitation_north}, {"profile", "linear in north"}}},
                    {"rivers", rivers}};
    if (section.contains("air_sea")) {
        Json provenance;
        parse_air_sea(required(section, "air_sea"), f, c, provenance);
        f.provenance["air_sea"] = provenance;
    }
    if (section.contains("rain")) {
        Json provenance;
        parse_rain(required(section, "rain"), f, c, provenance);
        f.provenance["rain"] = provenance;
    }
    if (c.coastal.band_m > 0.0 && !f.rain.enabled) throw std::invalid_argument("coastal runoff needs forcing.rain");
    return f;
}

// Forcing for the interval starting at `elapsed`.
OceanForcing forcing_at(const ForcingRequest& f, double elapsed, double dt, const OceanConfig& c, std::vector<double>& wind_east, std::vector<double>& wind_north) {
    const auto& g = c.grid;
    const std::size_t count = g.nx * g.ny;
    const double ly = static_cast<double>(g.ny) * g.spacing_north_m, mid = elapsed + 0.5 * dt;
    OceanForcing out;
    out.wind_stress_east_pa.assign(count, 0.0);
    out.wind_stress_north_pa.assign(count, 0.0);
    out.net_precipitation_m_s.assign(count, 0.0);
    out.target_temperature_k.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double y = (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m, frac = y / ly;
        out.target_temperature_k[i] = f.temperature_south_k + (f.temperature_north_k - f.temperature_south_k) * frac;
        for (const auto& cp : f.cold_patches) {
            const double east = g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m, north = g.origin_north_m + y;
            const double w = std::exp(-((east - cp.east_m) * (east - cp.east_m) + (north - cp.north_m) * (north - cp.north_m)) / (cp.radius_m * cp.radius_m));
            out.target_temperature_k[i] += (cp.temperature_k - out.target_temperature_k[i]) * w;
        }
        out.net_precipitation_m_s[i] = f.precipitation_south + (f.precipitation_north - f.precipitation_south) * frac;
        if (f.wind_kind == "gyre") {
            double taper = 1.0;
            if (f.wall_taper_m > 0.0) taper = (1.0 - std::exp(-(y / f.wall_taper_m) * (y / f.wall_taper_m))) * (1.0 - std::exp(-((ly - y) / f.wall_taper_m) * ((ly - y) / f.wall_taper_m)));
            const double yp = std::max(0.0, y - f.south_margin_m), lp = ly - f.south_margin_m;
            out.wind_stress_east_pa[i] = -f.stress_amplitude_pa * std::cos(static_cast<double>(f.gyres) * kPi * yp / lp) * taper;
        }
        else if (f.wind_kind == "uniform") { out.wind_stress_east_pa[i] = f.uniform_east; out.wind_stress_north_pa[i] = f.uniform_north; }
    }
    if (f.coupled) {
        const auto& w = *f.coupled;
        const auto at = frame_weight(w.elapsed, mid, w.linear);
        out.wind_stress_east_pa = w.stress_east[at.first];
        out.wind_stress_north_pa = w.stress_north[at.first];
        out.target_temperature_k = w.temperature[at.first];
        if (at.second > 0.0) {
            for (std::size_t i = 0; i < count; ++i) {
                out.wind_stress_east_pa[i] = sample(w.stress_east, at, i);
                out.wind_stress_north_pa[i] = sample(w.stress_north, at, i);
                out.target_temperature_k[i] = sample(w.temperature, at, i);
            }
        }
    }
    for (const auto& r : f.rivers) {
        RiverSource s = r.source;
        for (const auto& e : r.events) if (mid >= e.start_seconds && mid < e.start_seconds + e.duration_seconds) s.discharge_m3_s += e.discharge_m3_s;
        if (!r.series_time.empty()) {
            std::size_t k = 0;
            while (k + 1 < r.series_time.size() && r.series_time[k + 1] <= mid) ++k;
            s.discharge_m3_s += r.series_rate[k];
        }
        s.discharge_m3_s = std::min(s.discharge_m3_s, 1.0e7);
        out.rivers.push_back(s);
    }
    // Equivalent 10 m wind for the forecast frames.
    wind_east.assign(count, 0.0);
    wind_north.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double tau = std::hypot(out.wind_stress_east_pa[i], out.wind_stress_north_pa[i]);
        if (tau <= 0.0) continue;
        const double speed = std::sqrt(tau / (f.air_density * f.drag_coefficient));
        wind_east[i] = speed * out.wind_stress_east_pa[i] / tau;
        wind_north[i] = speed * out.wind_stress_north_pa[i] / tau;
    }
    auto cell_east = [&](std::size_t i) { return g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m; };
    auto cell_north = [&](std::size_t i) { return g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m; };
    // Rain: moving Gaussian (or uniform) events plus the coupled precipitation; explicit event temperatures
    // are blended by their share of the rain, the rest takes the default rain temperature set below.
    std::vector<double> explicit_rain(count, 0.0), explicit_heat(count, 0.0);
    if (f.rain.enabled) {
        out.rain_m_s.assign(count, 0.0);
        out.rain_temperature_k.assign(count, 0.0);
        if (f.rain.coupled) {
            const auto& a = *f.rain.coupled;
            const auto at = frame_weight(a.elapsed, mid, a.linear);
            for (std::size_t i = 0; i < count; ++i) out.rain_m_s[i] += std::max(0.0, sample(a.rain_mm_h, at, i)) / 3.6e6;
        }
        for (const auto& e : f.rain.events) {
            if (mid < e.start_seconds || mid >= e.start_seconds + e.duration_seconds) continue;
            const double ce = e.east_m + e.velocity_east_m_s * (mid - e.start_seconds), cn = e.north_m + e.velocity_north_m_s * (mid - e.start_seconds);
            for (std::size_t i = 0; i < count; ++i) {
                double weight = 1.0;
                if (e.radius_m) {
                    const double de = cell_east(i) - ce, dn = cell_north(i) - cn;
                    weight = std::exp(-(de * de + dn * dn) / (*e.radius_m * *e.radius_m));
                }
                const double rate = e.rate_mm_h * weight / 3.6e6;
                out.rain_m_s[i] += rate;
                if (e.temperature_k) { explicit_rain[i] += rate; explicit_heat[i] += rate * *e.temperature_k; }
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            const double scale = out.rain_m_s[i] > 500.0 / 3.6e6 ? (500.0 / 3.6e6) / out.rain_m_s[i] : 1.0;
            out.rain_m_s[i] *= scale;
            explicit_rain[i] *= scale;
            explicit_heat[i] *= scale;
        }
    }
    // Air-sea atmosphere: coupled fields or the analytic air (target + offset + diurnal cycle + air masses).
    if (f.air_sea.enabled) {
        const auto& a = f.air_sea;
        out.air_temperature_k.assign(count, 0.0);
        out.air_specific_humidity_kg_kg.assign(count, 0.0);
        out.surface_pressure_pa.assign(count, a.pressure_pa);
        out.wind_speed_m_s.assign(count, 0.0);
        out.cloud_fraction.assign(count, 0.0);
        out.shortwave_down_w_m2.assign(count, 0.0);
        const double epoch = f.start_epoch_seconds + mid, days = std::floor(epoch / 86400.0);
        const double utc_hour = (epoch - days * 86400.0) / 3600.0;
        const double day_of_year = fractional_day_of_year(static_cast<std::int64_t>(days), utc_hour);
        const double lat0 = g.geographic_anchor_latitude_deg, lon0 = g.geographic_anchor_longitude_deg;
        for (std::size_t i = 0; i < count; ++i) {
            const double lat = lat0 + cell_north(i) / 111195.0, lon = lon0 + cell_east(i) / (111195.0 * std::cos(lat0 * kPi / 180.0));
            const double frac = (static_cast<double>(i / g.nx) + 0.5) / static_cast<double>(g.ny);
            double ta, rh = a.relative_humidity_south + (a.relative_humidity_north - a.relative_humidity_south) * frac, cloud = a.cloud_south + (a.cloud_north - a.cloud_south) * frac;
            double q = -1.0, pressure = a.pressure_pa, wind = std::hypot(wind_east[i], wind_north[i]);
            if (a.coupled) {
                const auto& ca = *a.coupled;
                const auto at = frame_weight(ca.elapsed, mid, ca.linear);
                ta = sample(ca.air_temperature, at, i);
                pressure = ca.pressure[i];
                if (ca.has_humidity) q = sample(ca.specific_humidity, at, i);
                if (ca.has_cloud) cloud = sample(ca.cloud, at, i);
                wind = sample(ca.wind_speed, at, i);
                if (ca.has_storm_wind) {
                    // The vortex speed is known but not its direction: it enters the fluxes and the mixing, not the momentum.
                    const double storm = sample(ca.storm_wind, at, i);
                    wind = std::sqrt(wind * wind + storm * storm);
                    if (out.mixing_stress_pa.empty()) out.mixing_stress_pa.assign(count, 0.0);
                    out.mixing_stress_pa[i] = std::min(20.0, std::max(std::hypot(out.wind_stress_east_pa[i], out.wind_stress_north_pa[i]), f.air_density * f.drag_coefficient * wind * wind));
                }
            } else {
                const double local_hour = std::fmod(utc_hour + lon / 15.0 + 48.0, 24.0);
                ta = out.target_temperature_k[i] + a.air_temperature_offset_k + a.diurnal_amplitude_k * std::cos(2.0 * kPi * (local_hour - 15.0) / 24.0);
                for (const auto& m : a.air_masses) {
                    if (mid < m.start_seconds || mid >= m.start_seconds + m.duration_seconds) continue;
                    const double de = cell_east(i) - (m.east_m + m.velocity_east_m_s * (mid - m.start_seconds)), dn = cell_north(i) - (m.north_m + m.velocity_north_m_s * (mid - m.start_seconds));
                    const double w = std::exp(-(de * de + dn * dn) / (m.radius_m * m.radius_m));
                    ta += m.temperature_anomaly_k * w;
                    if (m.relative_humidity) rh += (*m.relative_humidity - rh) * w;
                }
            }
            // Rain moistens and clouds the analytic air; a coupled atmosphere carries its own humidity and cloud.
            if (!a.coupled && f.rain.enabled && a.rain_saturates_air && out.rain_m_s[i] > 0.1 / 3.6e6) {
                rh = std::max(rh, 0.95);
                cloud = std::max(cloud, 0.9);
            }
            ta = std::min(330.0, std::max(200.0, ta));
            if (q < 0.0) q = std::min(1.0, std::max(0.05, rh)) * saturation_specific_humidity_kg_kg(ta, pressure);
            q = std::min(q, 0.049);
            out.air_temperature_k[i] = ta;
            out.air_specific_humidity_kg_kg[i] = q;
            out.surface_pressure_pa[i] = pressure;
            out.wind_speed_m_s[i] = std::min(80.0, wind);
            out.cloud_fraction[i] = std::min(1.0, std::max(0.0, cloud));
            out.shortwave_down_w_m2[i] = a.shortwave_kind == "constant" ? a.shortwave_w_m2
                : std::min(1400.0, surface_shortwave_w_m2(solar_cosine_zenith(lat, lon, day_of_year, utc_hour), vapour_pressure_pa(q, pressure), out.cloud_fraction[i]));
        }
    }
    // Default rain temperature: coupled wet bulb, the wet bulb of the air-sea atmosphere, or the target + offset.
    if (f.rain.enabled) {
        std::pair<std::size_t, double> at{0, 0.0};
        const bool coupled_wet_bulb = f.rain.coupled && f.rain.coupled->has_wet_bulb;
        if (coupled_wet_bulb) at = frame_weight(f.rain.coupled->elapsed, mid, f.rain.coupled->linear);
        for (std::size_t i = 0; i < count; ++i) {
            double fallback = out.target_temperature_k[i] + f.rain.temperature_offset_k;
            if (coupled_wet_bulb) fallback = sample(f.rain.coupled->wet_bulb, at, i);
            else if (f.air_sea.enabled) fallback = wet_bulb_temperature_k(out.air_temperature_k[i], out.air_specific_humidity_kg_kg[i], out.surface_pressure_pa[i]);
            fallback = std::min(320.0, std::max(250.0, fallback));
            const double rest = out.rain_m_s[i] - explicit_rain[i];
            out.rain_temperature_k[i] = out.rain_m_s[i] > 0.0 ? (explicit_heat[i] + std::max(0.0, rest) * fallback) / (explicit_rain[i] + std::max(0.0, rest)) : fallback;
        }
    }
    return out;
}

Json frame_json(const OceanState& s, const OceanDiagnostics& d, const OceanConfig& c) {
    Json streams = Json::array();
    for (const auto& st : d.streams) {
        Json path = Json::array();
        for (const auto& p : st.path) path.push_back({sig(p.first), sig(p.second)});
        streams.push_back({{"id", st.id}, {"core_cell", st.core_cell}, {"cell_count", st.cell_count}, {"max_speed_m_s", sig(st.max_speed_m_s)}, {"mean_speed_m_s", sig(st.mean_speed_m_s)},
                           {"transport_sv", sig(st.transport_sv)}, {"mean_salinity_psu", sig(st.mean_salinity_psu)}, {"mean_temperature_k", sig(st.mean_temperature_k)}, {"direction", st.direction}, {"path", path}});
    }
    Json plumes = Json::array();
    for (const auto& p : d.plumes) {
        plumes.push_back({{"name", p.name}, {"mouth_cell", p.mouth_cell}, {"discharge_m3_s", sig(p.discharge_m3_s)}, {"mouth_salinity_psu", sig(p.mouth_salinity_psu)}, {"minimum_salinity_psu", sig(p.minimum_salinity_psu)},
                          {"plume_area_km2", sig(p.plume_area_m2 / 1.0e6)}, {"plume_freshwater_m3", sig(p.plume_freshwater_m3)}, {"plume_extent_km", sig(p.plume_extent_km)}, {"deflection_deg", sig(p.deflection_deg)}});
    }
    return {
        {"elapsed_seconds", s.elapsed_seconds},
        {"u_m_s", rounded_array(s.u_centre_m_s, 4)},
        {"v_m_s", rounded_array(s.v_centre_m_s, 4)},
        {"thickness_m", rounded_array(s.thickness_m, 4)},
        {"salinity_psu", rounded_array(s.salinity_psu)},
        {"surface_salinity_psu", rounded_array(s.surface_salinity_psu)},
        {"temperature_k", rounded_array(s.temperature_k, 4)},
        {"sinking_m_s", rounded_array(s.sinking_m_s, 4)},
        {"front", flag_array(s.front)},
        {"stream_id", s.stream_id},
        {"overturning_sv", sig(s.overturning_m3_s / 1.0e6)},
        {"mean_upwelling_m_s", sig(s.upwelling_m_s)},
        {"river_input_m3_s", sig(s.river_input_m3_s)},
        {"net_precipitation_m3_s", sig(s.net_precipitation_m3_s)},
        {"sponge_volume_m3_s", sig(s.sponge_volume_m3_s)},
        {"sponge_salt_kg_s", sig(s.sponge_salt_kg_s)},
        {"volume_m3", s.volume_m3},
        {"salt_kg", s.salt_kg},
        {"kinetic_energy_j", sig(s.kinetic_energy_j)},
        {"volume_balance_error_m3", s.volume_balance_error_m3},
        {"salt_balance_error_kg", s.salt_balance_error_kg},
        {"cumulative_m3", {{"river", s.cumulative_river_m3}, {"net_precipitation", s.cumulative_net_precipitation_m3}, {"sinking", s.cumulative_sinking_m3}, {"sponge", s.cumulative_sponge_m3}}},
        {"streams", streams},
        {"exchange_points", plumes},
        {"formation_cells", d.formation_cells},
        {"formation_area_km2", sig(d.formation_area_m2 / 1.0e6)},
        {"mean_salinity_psu", sig(d.mean_salinity_psu)},
        {"min_salinity_psu", sig(d.min_salinity_psu)},
        {"max_speed_m_s", sig(d.max_speed_m_s)},
        {"stream_count", d.streams.size()},
    };
    (void)c;
}

// Mean of a per-cell field over the sea.
double sea_mean(const std::vector<double>& v, const std::vector<unsigned char>& water) {
    long double sum = 0.0L;
    std::size_t n = 0;
    for (std::size_t i = 0; i < v.size(); ++i) if (water[i]) { sum += v[i]; ++n; }
    return n ? static_cast<double>(sum / n) : 0.0;
}

Json sea_array(const std::vector<double>& v, const std::vector<unsigned char>& water, double scale = 1.0, int digits = 4) {
    Json out = Json::array();
    for (std::size_t i = 0; i < v.size(); ++i) out.push_back(water[i] ? tidy(v[i] * scale, digits) : 0.0);
    return out;
}

// The extension blocks of one frame: rain and coastal runoff, the air-sea exchange, the layered column,
// the surface lenses and the heat budget.
void add_extension_blocks(Json& frame, const OceanState& s, const OceanDiagnostics& d, const OceanForcing& f, const OceanConfig& c, bool air_sea, bool rain, double heat_error) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    const auto& water = c.ocean;
    if (rain) {
        Json rate = Json::array(), store = Json::array();
        double max_rate = 0.0;
        std::size_t raining_sea = 0, raining_land = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double mm_h = f.rain_m_s.empty() ? 0.0 : f.rain_m_s[i] * 3.6e6;
            rate.push_back(tidy(mm_h, 4));
            store.push_back(water[i] ? 0.0 : tidy(s.coastal_store_m[i] * 1000.0, 4));
            max_rate = std::max(max_rate, mm_h);
            if (mm_h > 0.1) ++(water[i] ? raining_sea : raining_land);
        }
        frame["rain"] = {
            {"rate_mm_h", rate}, {"coastal_store_mm", store}, {"coastal_inflow_m3_s", sea_array(s.coastal_inflow_m3_s, water)},
            {"rain_on_sea_m3_s", tidy(s.rain_sea_m3_s, 5)}, {"rain_on_coastal_sea_m3_s", tidy(s.rain_coastal_sea_m3_s, 5)}, {"rain_on_coast_m3_s", tidy(s.rain_coast_m3_s, 5)},
            {"rain_inland_m3_s", tidy(s.rain_inland_m3_s, 5)}, {"coastal_runoff_m3_s", tidy(s.coastal_runoff_m3_s, 5)}, {"coastal_loss_m3_s", tidy(s.coastal_loss_m3_s, 5)},
            {"coastal_storage_m3", s.coastal_storage_m3}, {"coastal_balance_error_m3", s.coastal_balance_error_m3},
            {"max_rate_mm_h", tidy(max_rate, 5)}, {"raining_sea_cells", raining_sea}, {"raining_land_cells", raining_land},
        };
    }
    if (air_sea) {
        Json rh = Json::array(), evaporation = Json::array();
        double max_evaporation = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            rh.push_back(tidy(relative_humidity_fraction(f.air_temperature_k[i], f.air_specific_humidity_kg_kg[i], f.surface_pressure_pa[i]), 3));
            evaporation.push_back(water[i] ? tidy(s.evaporation_m_s[i] * 8.64e7, 3) : 0.0);
            if (water[i]) max_evaporation = std::max(max_evaporation, s.evaporation_m_s[i] * 8.64e7);
        }
        // The air temperature and the 10 m wind are the forecast frame's fields; the incoming shortwave is
        // shortwave_w_m2 / (1 - albedo). Fluxes carry three significant digits (about 1 W/m2).
        frame["air_sea"] = {
            {"relative_humidity", rh}, {"cloud_fraction", tidy_array(f.cloud_fraction, 3)},
            {"shortwave_w_m2", sea_array(s.shortwave_w_m2, water, 1.0, 3)}, {"longwave_w_m2", sea_array(s.longwave_w_m2, water, 1.0, 3)}, {"sensible_w_m2", sea_array(s.sensible_w_m2, water, 1.0, 3)},
            {"latent_w_m2", sea_array(s.latent_w_m2, water, 1.0, 3)}, {"rain_heat_w_m2", sea_array(s.rain_heat_w_m2, water, 1.0, 3)}, {"net_heat_w_m2", sea_array(s.net_heat_w_m2, water, 1.0, 3)},
            {"evaporation_mm_day", evaporation}, {"evaporation_m3_s", tidy(s.evaporation_m3_s, 5)},
            {"mean_evaporation_mm_day", tidy(sea_mean(s.evaporation_m_s, water) * 8.64e7, 5)}, {"max_evaporation_mm_day", tidy(max_evaporation, 5)},
            {"mean_shortwave_w_m2", tidy(sea_mean(s.shortwave_w_m2, water), 5)}, {"mean_longwave_w_m2", tidy(sea_mean(s.longwave_w_m2, water), 5)},
            {"mean_sensible_w_m2", tidy(sea_mean(s.sensible_w_m2, water), 5)}, {"mean_latent_w_m2", tidy(sea_mean(s.latent_w_m2, water), 5)},
            {"mean_rain_heat_w_m2", tidy(sea_mean(s.rain_heat_w_m2, water), 5)}, {"mean_net_heat_w_m2", tidy(sea_mean(s.net_heat_w_m2, water), 5)},
        };
    }
    if (c.thermal.count >= 2) {
        const std::size_t layers = c.thermal.count;
        Json temperature = Json::array(), salinity = Json::array();
        for (std::size_t k = 0; k < layers; ++k) {
            Json t = Json::array(), sal = Json::array();
            for (std::size_t i = 0; i < count; ++i) {
                t.push_back(water[i] ? tidy(s.layer_temperature_k[i * layers + k], 5) : 0.0);
                sal.push_back(water[i] ? tidy(s.layer_salinity_psu[i * layers + k], 5) : 0.0);
            }
            temperature.push_back(std::move(t));
            salinity.push_back(std::move(sal));
        }
        frame["thermal"] = {
            {"layer_temperature_k", temperature}, {"layer_salinity_psu", salinity}, {"sst_k", sea_array(s.sst_k, water, 1.0, 5)},
            {"mixed_layer_depth_m", sea_array(s.mixed_layer_depth_m, water)}, {"isothermal_layer_depth_m", sea_array(s.isothermal_layer_depth_m, water)},
            {"mean_sst_k", tidy(d.mean_sst_k, 5)}, {"min_sst_k", tidy(d.min_sst_k, 5)}, {"max_sst_k", tidy(d.max_sst_k, 5)},
            {"mean_mixed_layer_depth_m", tidy(d.mean_mixed_layer_depth_m, 5)}, {"max_mixed_layer_depth_m", tidy(d.max_mixed_layer_depth_m, 5)}, {"barrier_layer_cells", d.barrier_layer_cells},
        };
    }
    Json lenses = Json::array();
    for (std::size_t k = 0; k < d.lenses.size() && k < 20; ++k) {
        const auto& l = d.lenses[k];
        lenses.push_back({{"kind", l.kind}, {"source_shares", {{"rain", tidy(l.rain_share, 3)}, {"coastal_runoff", tidy(l.coastal_runoff_share, 3)}, {"river", tidy(l.river_share, 3)}}}, {"freshest_cell", l.freshest_cell}, {"cells", l.cell_count}, {"area_km2", tidy(l.area_m2 / 1.0e6, 5)}, {"min_surface_salinity_psu", tidy(l.minimum_surface_salinity_psu, 5)},
                          {"max_deficit_psu", tidy(l.maximum_deficit_psu, 5)}, {"freshwater_m3", tidy(l.freshwater_m3, 5)}, {"mean_sst_k", tidy(l.mean_sst_k, 5)}});
    }
    frame["surface_lenses"] = lenses;
    frame["heat_budget"] = {{"heat_content_j", s.heat_content_j}, {"heat_balance_error_j", heat_error}, {"surface_heat_w", tidy(s.surface_heat_w, 5)}, {"cumulative_surface_heat_j", s.cumulative_surface_heat_j}};
    frame["cumulative_m3"]["rain_sea"] = s.cumulative_rain_sea_m3;
    frame["cumulative_m3"]["rain_coast"] = s.cumulative_rain_coast_m3;
    frame["cumulative_m3"]["rain_inland"] = s.cumulative_rain_inland_m3;
    frame["cumulative_m3"]["coastal_runoff"] = s.cumulative_coastal_runoff_m3;
    frame["cumulative_m3"]["coastal_loss"] = s.cumulative_coastal_loss_m3;
    frame["cumulative_m3"]["evaporation"] = s.cumulative_evaporation_m3;
}

Json forecast_frame_json(const std::vector<double>& temperature, const std::vector<double>& wind_east, const std::vector<double>& wind_north, double elapsed, const TimeRequest& time, const TransportGrid2D& grid) {
    return {
        {"schema_version", "terraflux.weather.forecast/v1"},
        {"start_time_utc", time.start_time_utc},
        {"elapsed_seconds", elapsed},
        {"grid", grid_json(grid)},
        {"observation_updates", Json::array()},
        {"fields", {
            {"air_temperature", {{"units", "K"}, {"values", rounded_array(temperature, 4)}}},
            {"wind_east", {{"units", "m/s"}, {"values", rounded_array(wind_east, 4)}}},
            {"wind_north", {{"units", "m/s"}, {"values", rounded_array(wind_north, 4)}}},
        }},
    };
}

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable << " <run-request.json> <output-timeline.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 2;
    }
    try {
        const std::string request_path = argv[1], output_path = argv[2];
        std::ifstream request_file(request_path);
        if (!request_file) throw std::runtime_error("cannot open run request: " + request_path);
        Json request;
        try { request = Json::parse(request_file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid run request JSON: ") + error.what()); }
        if (!request.is_object()) throw std::invalid_argument("run request root must be a JSON object");
        if (request.value("schema_version", "") != "terraflux.ocean.circulation-run/v1") throw std::invalid_argument("schema_version must be terraflux.ocean.circulation-run/v1");

        TimeRequest time;
        std::vector<double> elevation;
        Json basin_provenance;
        OceanConfig config = parse_config(request, time, elevation, basin_provenance);
        const ForcingRequest forcing = parse_forcing(request, config, time);
        const bool air_sea = forcing.air_sea.enabled, rain = forcing.rain.enabled, layers = config.thermal.count >= 2;
        const bool extended = air_sea || rain || layers;
        std::vector<double> wind_east, wind_north;
        if (time.initial_sst_from_target) {
            // The column starts from the surface-temperature target of the first interval.
            const OceanForcing first = forcing_at(forcing, -static_cast<double>(time.spinup_steps) * config.timestep_seconds, config.timestep_seconds, config, wind_east, wind_north);
            config.initial_surface_temperature_k = first.target_temperature_k;
            for (std::size_t i = 0; i < config.ocean.size(); ++i) config.initial_surface_temperature_k[i] = std::min(310.0, std::max(270.0, config.initial_surface_temperature_k[i]));
            validate_ocean_config(config);
        }
        OceanState state = initialize_ocean(config);

        // Spin-up (not recorded): the gyres need the Rossby-wave crossing time to establish.
        for (std::size_t k = 0; k < time.spinup_steps; ++k) {
            const double elapsed = -static_cast<double>(time.spinup_steps - k) * config.timestep_seconds;
            state = advance_ocean(state, forcing_at(forcing, elapsed, config.timestep_seconds, config, wind_east, wind_north), config);
        }
        state.elapsed_seconds = 0.0;
        state.cumulative_river_m3 = state.cumulative_net_precipitation_m3 = state.cumulative_sinking_m3 = state.cumulative_sponge_m3 = 0.0;
        state.cumulative_rain_sea_m3 = state.cumulative_rain_coast_m3 = state.cumulative_rain_inland_m3 = 0.0;
        state.cumulative_coastal_runoff_m3 = state.cumulative_coastal_loss_m3 = state.cumulative_evaporation_m3 = state.cumulative_surface_heat_j = 0.0;
        const double initial_volume = state.volume_m3, initial_salt = state.salt_kg, initial_heat = state.heat_content_j, initial_store = state.coastal_storage_m3;

        Json frames = Json::array(), ocean_frames = Json::array();
        OceanForcing current = forcing_at(forcing, 0.0, config.timestep_seconds, config, wind_east, wind_north);
        double heat_error = 0.0;   // largest per-step heat-budget error since the previous frame
        auto record = [&]() {
            frames.push_back(forecast_frame_json(air_sea ? current.air_temperature_k : current.target_temperature_k, wind_east, wind_north, state.elapsed_seconds, time, config.grid));
            const OceanDiagnostics diagnostics = diagnose_ocean(state, current, config);
            Json frame = frame_json(state, diagnostics, config);
            if (extended) add_extension_blocks(frame, state, diagnostics, current, config, air_sea, rain, heat_error);
            ocean_frames.push_back(std::move(frame));
            heat_error = 0.0;
        };
        record();
        for (std::size_t k = 0; k < time.steps; ++k) {
            current = forcing_at(forcing, state.elapsed_seconds, config.timestep_seconds, config, wind_east, wind_north);
            state = advance_ocean(state, current, config);
            if (std::abs(state.heat_balance_error_j) > std::abs(heat_error)) heat_error = state.heat_balance_error_j;
            if ((k + 1) % time.snapshot_every == 0 || k + 1 == time.steps) record();
        }

        Json mask = Json::array(), sponge = Json::array();
        for (unsigned char v : config.ocean) mask.push_back(v != 0);
        for (std::size_t i = 0; i < config.ocean.size(); ++i) sponge.push_back(config.sponge.empty() ? 0.0 : sig(config.sponge[i]));
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"terrain", {
                {"schema_version", "terraflux.terrain.grid/v1"},
                {"grid", grid_json(config.grid)},
                {"units", "m"},
                {"vertical_reference", "synthetic_local_datum"},
                {"source_id", basin_provenance.at("kind").get<std::string>()},
                {"values", elevation},
            }},
            {"ocean", {
                {"schema_version", "terraflux.ocean.circulation/v1"},
                {"method", "reduced_gravity_c_grid_sverdrup_stommel_formation"},
                {"source_kind", "engine_output"},
                {"request", config_json(config, time)},
                {"basin_source", basin_provenance},
                {"forcing_source", forcing.provenance},
                {"snapshot_every_steps", time.snapshot_every},
                {"ocean_mask", mask},
                {"sponge", sponge},
                {"initial_volume_m3", initial_volume},
                {"initial_salt_kg", initial_salt},
                {"frames", ocean_frames},
            }},
        };
        if (extended) {
            Json& ocean = output["ocean"];
            Json processes = Json::array();
            if (rain) processes.push_back("rain");
            if (rain && config.coastal.band_m > 0.0) processes.push_back("coastal_runoff");
            if (air_sea) processes.push_back("air_sea");
            if (layers) processes.push_back("thermal_layers");
            ocean["processes"] = processes;
            ocean["initial_heat_content_j"] = initial_heat;
            extension_config_json(ocean["request"], config, air_sea, rain, time);
            if (layers) {
                Json fractions = Json::array();
                for (double v : thermal_layer_fractions(config)) fractions.push_back(v);
                ocean["thermal_layers"] = {{"count", config.thermal.count}, {"fractions", fractions}, {"coordinate", "sigma fractions of the active-layer thickness, top first"}};
            }
            if (rain) {
                Json receiver = Json::array();
                const auto receivers = coastal_receivers(config);
                for (std::size_t i = 0; i < receivers.size(); ++i) receiver.push_back(receivers[i] >= receivers.size() ? -1 : static_cast<long long>(receivers[i]));
                ocean["coastal"] = {{"band_m", config.coastal.band_m}, {"runoff_coefficient", config.coastal.runoff_coefficient}, {"runoff_time_scale_s", config.coastal.runoff_time_scale_s},
                                    {"receiver", receiver}, {"initial_storage_m3", initial_store}};
            }
        }
        std::ofstream output_file(output_path);
        if (!output_file) throw std::runtime_error("cannot write output: " + output_path);
        output_file << output.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
