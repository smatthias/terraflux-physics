// terraflux-run-ocean-circulation: the streams, their salt and the fresh water.
//
// Reads a terraflux.ocean.circulation-run/v1 request (time steps, grid, basin
// from terrain or a mask, layer properties, physics, wind / surface-temperature
// / precipitation forcing and river sources, optionally fed by a catchment
// hydrology timeline) and writes a viewer timeline with an `ocean` block: the
// currents, thickness, salinity and temperature per frame, the deep-water
// formation sites, the detected streams with their core streamlines and
// transport, and the river exchange points with their plumes.
#include "terraflux/weather/ocean_circulation.hpp"
#include "terraflux/weather/synthetic_terrain.hpp"

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

ForcingRequest parse_forcing(const Json& request, const OceanConfig& c) {
    ForcingRequest f;
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
        std::size_t k = 0;
        while (k + 1 < w.elapsed.size() && w.elapsed[k + 1] <= mid) ++k;
        out.wind_stress_east_pa = w.stress_east[k];
        out.wind_stress_north_pa = w.stress_north[k];
        out.target_temperature_k = w.temperature[k];
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
        const OceanConfig config = parse_config(request, time, elevation, basin_provenance);
        const ForcingRequest forcing = parse_forcing(request, config);
        OceanState state = initialize_ocean(config);
        std::vector<double> wind_east, wind_north;

        // Spin-up (not recorded): the gyres need the Rossby-wave crossing time to establish.
        for (std::size_t k = 0; k < time.spinup_steps; ++k) {
            const double elapsed = -static_cast<double>(time.spinup_steps - k) * config.timestep_seconds;
            state = advance_ocean(state, forcing_at(forcing, elapsed, config.timestep_seconds, config, wind_east, wind_north), config);
        }
        state.elapsed_seconds = 0.0;
        state.cumulative_river_m3 = state.cumulative_net_precipitation_m3 = state.cumulative_sinking_m3 = state.cumulative_sponge_m3 = 0.0;
        const double initial_volume = state.volume_m3, initial_salt = state.salt_kg;

        Json frames = Json::array(), ocean_frames = Json::array();
        OceanForcing current = forcing_at(forcing, 0.0, config.timestep_seconds, config, wind_east, wind_north);
        auto record = [&]() {
            frames.push_back(forecast_frame_json(current.target_temperature_k, wind_east, wind_north, state.elapsed_seconds, time, config.grid));
            ocean_frames.push_back(frame_json(state, diagnose_ocean(state, current, config), config));
        };
        record();
        for (std::size_t k = 0; k < time.steps; ++k) {
            current = forcing_at(forcing, state.elapsed_seconds, config.timestep_seconds, config, wind_east, wind_north);
            state = advance_ocean(state, current, config);
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
        std::ofstream output_file(output_path);
        if (!output_file) throw std::runtime_error("cannot write output: " + output_path);
        output_file << output.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
