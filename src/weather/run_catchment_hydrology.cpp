// terraflux-run-catchment-hydrology: where the rain goes.
//
// Reads a terraflux.hydrology.catchment-run/v1 request (time steps, grid,
// terrain, soil / groundwater / channel / snow / hazard parameters and a
// rainfall + temperature forcing), runs the catchment hydrology kernel and
// writes a viewer-compatible timeline with a `hydrology` block: the drainage
// network (rivers, lakes, basins, ocean outlets), the water stores per frame
// (snow, surface, soil, groundwater, channels, lakes), the discharge, and the
// hazard layer (flooding, erosion / deposition, landslides).
#include "terraflux/weather/catchment_hydrology.hpp"
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

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw std::invalid_argument(std::string("run request is missing field: ") + key);
    }
    return object.at(key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(std::string("run request field must be a non-empty string: ") + key);
    }
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

bool optional_bool(const Json& object, const char* key, bool fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const Json& value = object.at(key);
    if (!value.is_boolean()) throw std::invalid_argument(std::string("run request field must be boolean: ") + key);
    return value.get<bool>();
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

// Round to five significant digits so the timeline stays readable and compact.
double sig(double v) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    const double scale = std::pow(10.0, std::floor(std::log10(std::abs(v))) - 4.0);
    return std::round(v / scale) * scale;
}

Json rounded_array(const std::vector<double>& values) {
    Json result = Json::array();
    for (double v : values) result.push_back(sig(v));
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

// Terrain: prescribed values or the shared synthetic ridges/basins builder.
std::vector<double> parse_terrain(const Json& request, const TransportGrid2D& grid, Json& provenance) {
    const Json& terrain = required(request, "terrain");
    const std::size_t count = grid.nx * grid.ny;
    if (terrain.contains("values")) {
        auto values = required_number_array(terrain, "values");
        if (values.size() != count) throw std::invalid_argument("terrain.values must contain one elevation per grid cell");
        provenance = {{"kind", "prescribed_cell_values"}};
        return values;
    }
    SyntheticTerrainSpec spec;
    spec.base_elevation_m = optional_number(terrain, "base_elevation_m", 0.0);
    spec.slope_east_m_per_m = optional_number(terrain, "slope_east_m_per_m", 0.0);
    spec.slope_north_m_per_m = optional_number(terrain, "slope_north_m_per_m", 0.0);
    spec.roughness_m = optional_number(terrain, "roughness_m", 0.0);
    spec.roughness_seed = static_cast<std::uint32_t>(optional_number(terrain, "roughness_seed", 1.0));
    Json ridges = Json::array(), basins = Json::array();
    if (terrain.contains("ridges")) {
        const Json& list = required(terrain, "ridges");
        if (!list.is_array()) throw std::invalid_argument("terrain.ridges must be an array");
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
        if (!list.is_array()) throw std::invalid_argument("terrain.basins must be an array");
        for (const Json& b : list) {
            SyntheticBasin basin;
            basin.center_east_m = required_number(b, "center_east_m");
            basin.center_north_m = required_number(b, "center_north_m");
            basin.depth_m = required_number(b, "depth_m");
            basin.radius_m = required_number(b, "radius_m");
            spec.basins.push_back(basin);
            basins.push_back({{"center_east_m", basin.center_east_m}, {"center_north_m", basin.center_north_m}, {"depth_m", basin.depth_m}, {"radius_m", basin.radius_m}});
        }
    }
    provenance = {
        {"kind", "synthetic_ridges_and_basins"}, {"base_elevation_m", spec.base_elevation_m},
        {"slope_east_m_per_m", spec.slope_east_m_per_m}, {"slope_north_m_per_m", spec.slope_north_m_per_m},
        {"roughness_m", spec.roughness_m}, {"roughness_seed", spec.roughness_seed},
        {"ridges", ridges}, {"basins", basins},
    };
    return build_synthetic_terrain(spec, grid);
}

template <typename T>
void read_into(const Json& section, const char* key, T& target) {
    target = static_cast<T>(optional_number(section, key, static_cast<double>(target)));
}

CatchmentConfig parse_config(const Json& request, TimeRequest& time) {
    CatchmentConfig c;
    const Json& t = required(request, "time");
    time.start_time_utc = required_string(t, "start_time_utc");
    c.timestep_seconds = required_number(t, "timestep_seconds");
    time.steps = required_positive_size(t, "steps");
    time.spinup_steps = optional_size(t, "spinup_steps", 0);
    time.snapshot_every = optional_size(request, "snapshot_every_steps", 1);
    if (time.snapshot_every == 0) throw std::invalid_argument("snapshot_every_steps must be >= 1");
    if (time.steps / time.snapshot_every + 2 > 240) throw std::invalid_argument("the timeline would exceed 240 frames; raise snapshot_every_steps");
    c.grid = parse_grid(required(request, "grid"));
    if (request.contains("domain")) {
        const Json& d = required(request, "domain");
        c.sea_level_m = optional_number(d, "sea_level_m", c.sea_level_m);
        c.open_edges = optional_bool(d, "open_edges", c.open_edges);
    }
    if (request.contains("soil")) {
        const Json& s = required(request, "soil");
        read_into(s, "regolith_depth_m", c.soil.regolith_depth_m);
        read_into(s, "slope_thinning_tan", c.soil.slope_thinning_tan);
        read_into(s, "minimum_depth_fraction", c.soil.minimum_depth_fraction);
        read_into(s, "porosity", c.soil.porosity);
        read_into(s, "field_capacity_fraction", c.soil.field_capacity_fraction);
        read_into(s, "infiltration_max_m_s", c.soil.infiltration_max_m_s);
        read_into(s, "infiltration_min_m_s", c.soil.infiltration_min_m_s);
        read_into(s, "percolation_rate_m_s", c.soil.percolation_rate_m_s);
        read_into(s, "potential_evapotranspiration_m_s", c.soil.potential_evapotranspiration_m_s);
        read_into(s, "detention_storage_m", c.soil.detention_storage_m);
        read_into(s, "initial_saturation", c.soil.initial_saturation);
    }
    if (request.contains("groundwater")) {
        const Json& s = required(request, "groundwater");
        read_into(s, "aquifer_thickness_m", c.groundwater.aquifer_thickness_m);
        read_into(s, "specific_yield", c.groundwater.specific_yield);
        read_into(s, "hydraulic_conductivity_m_s", c.groundwater.hydraulic_conductivity_m_s);
        read_into(s, "baseflow_time_scale_s", c.groundwater.baseflow_time_scale_s);
        read_into(s, "initial_fill_fraction", c.groundwater.initial_fill_fraction);
    }
    if (request.contains("channels")) {
        const Json& s = required(request, "channels");
        read_into(s, "channel_initiation_area_m2", c.channels.channel_initiation_area_m2);
        read_into(s, "manning_overland", c.channels.manning_overland);
        read_into(s, "manning_channel", c.channels.manning_channel);
        read_into(s, "width_coefficient_m", c.channels.width_coefficient_m);
        read_into(s, "width_exponent", c.channels.width_exponent);
        read_into(s, "bankfull_depth_coefficient_m", c.channels.bankfull_depth_coefficient_m);
        read_into(s, "bankfull_depth_exponent", c.channels.bankfull_depth_exponent);
        read_into(s, "minimum_slope", c.channels.minimum_slope);
        read_into(s, "lake_initial_fill_fraction", c.channels.lake_initial_fill_fraction);
    }
    if (request.contains("snow")) {
        const Json& s = required(request, "snow");
        read_into(s, "rain_snow_threshold_k", c.snow.rain_snow_threshold_k);
        read_into(s, "degree_day_factor_m_k_s", c.snow.degree_day_factor_m_k_s);
        read_into(s, "initial_snow_m", c.snow.initial_snow_m);
    }
    if (request.contains("hazards")) {
        const Json& s = required(request, "hazards");
        read_into(s, "erodibility_kg_j", c.hazards.erodibility_kg_j);
        read_into(s, "critical_stream_power_w_m2", c.hazards.critical_stream_power_w_m2);
        read_into(s, "transport_capacity_kg_j", c.hazards.transport_capacity_kg_j);
        read_into(s, "settling_velocity_m_s", c.hazards.settling_velocity_m_s);
        read_into(s, "bulk_density_kg_m3", c.hazards.bulk_density_kg_m3);
        read_into(s, "cohesion_pa", c.hazards.cohesion_pa);
        read_into(s, "friction_angle_deg", c.hazards.friction_angle_deg);
        read_into(s, "soil_unit_weight_n_m3", c.hazards.soil_unit_weight_n_m3);
        read_into(s, "water_unit_weight_n_m3", c.hazards.water_unit_weight_n_m3);
        read_into(s, "minimum_failure_tan", c.hazards.minimum_failure_tan);
        read_into(s, "runout_tan", c.hazards.runout_tan);
        read_into(s, "residual_depth_fraction", c.hazards.residual_depth_fraction);
        read_into(s, "failure_area_fraction", c.hazards.failure_area_fraction);
        read_into(s, "restabilisation_factor_of_safety", c.hazards.restabilisation_factor_of_safety);
        read_into(s, "flood_depth_threshold_m", c.hazards.flood_depth_threshold_m);
        read_into(s, "intensity_duration_a_mm_h", c.hazards.intensity_duration_a_mm_h);
        read_into(s, "intensity_duration_b", c.hazards.intensity_duration_b);
        read_into(s, "dry_spell_reset_s", c.hazards.dry_spell_reset_s);
    }
    validate_catchment_config(c);
    return c;
}

Json config_json(const CatchmentConfig& c, const TimeRequest& time) {
    return {
        {"time", {{"start_time_utc", time.start_time_utc}, {"timestep_seconds", c.timestep_seconds}, {"steps", time.steps}, {"spinup_steps", time.spinup_steps}}},
        {"grid", grid_json(c.grid)},
        {"domain", {{"sea_level_m", c.sea_level_m}, {"open_edges", c.open_edges}}},
        {"soil", {
            {"regolith_depth_m", c.soil.regolith_depth_m}, {"slope_thinning_tan", c.soil.slope_thinning_tan}, {"minimum_depth_fraction", c.soil.minimum_depth_fraction},
            {"porosity", c.soil.porosity}, {"field_capacity_fraction", c.soil.field_capacity_fraction}, {"infiltration_max_m_s", c.soil.infiltration_max_m_s},
            {"infiltration_min_m_s", c.soil.infiltration_min_m_s}, {"percolation_rate_m_s", c.soil.percolation_rate_m_s},
            {"potential_evapotranspiration_m_s", c.soil.potential_evapotranspiration_m_s}, {"detention_storage_m", c.soil.detention_storage_m}, {"initial_saturation", c.soil.initial_saturation}}},
        {"groundwater", {
            {"aquifer_thickness_m", c.groundwater.aquifer_thickness_m}, {"specific_yield", c.groundwater.specific_yield}, {"hydraulic_conductivity_m_s", c.groundwater.hydraulic_conductivity_m_s},
            {"baseflow_time_scale_s", c.groundwater.baseflow_time_scale_s}, {"initial_fill_fraction", c.groundwater.initial_fill_fraction}}},
        {"channels", {
            {"channel_initiation_area_m2", c.channels.channel_initiation_area_m2}, {"manning_overland", c.channels.manning_overland}, {"manning_channel", c.channels.manning_channel},
            {"width_coefficient_m", c.channels.width_coefficient_m}, {"width_exponent", c.channels.width_exponent}, {"bankfull_depth_coefficient_m", c.channels.bankfull_depth_coefficient_m},
            {"bankfull_depth_exponent", c.channels.bankfull_depth_exponent}, {"minimum_slope", c.channels.minimum_slope}, {"lake_initial_fill_fraction", c.channels.lake_initial_fill_fraction}}},
        {"snow", {{"rain_snow_threshold_k", c.snow.rain_snow_threshold_k}, {"degree_day_factor_m_k_s", c.snow.degree_day_factor_m_k_s}, {"initial_snow_m", c.snow.initial_snow_m}}},
        {"hazards", {
            {"erodibility_kg_j", c.hazards.erodibility_kg_j}, {"critical_stream_power_w_m2", c.hazards.critical_stream_power_w_m2}, {"transport_capacity_kg_j", c.hazards.transport_capacity_kg_j},
            {"settling_velocity_m_s", c.hazards.settling_velocity_m_s}, {"bulk_density_kg_m3", c.hazards.bulk_density_kg_m3}, {"cohesion_pa", c.hazards.cohesion_pa},
            {"friction_angle_deg", c.hazards.friction_angle_deg}, {"soil_unit_weight_n_m3", c.hazards.soil_unit_weight_n_m3}, {"water_unit_weight_n_m3", c.hazards.water_unit_weight_n_m3},
            {"minimum_failure_tan", c.hazards.minimum_failure_tan}, {"runout_tan", c.hazards.runout_tan}, {"residual_depth_fraction", c.hazards.residual_depth_fraction},
            {"failure_area_fraction", c.hazards.failure_area_fraction}, {"restabilisation_factor_of_safety", c.hazards.restabilisation_factor_of_safety},
            {"flood_depth_threshold_m", c.hazards.flood_depth_threshold_m}, {"intensity_duration_a_mm_h", c.hazards.intensity_duration_a_mm_h},
            {"intensity_duration_b", c.hazards.intensity_duration_b}, {"dry_spell_reset_s", c.hazards.dry_spell_reset_s}}},
    };
}

// Forcing: analytic temperature (sea-level value, lapse rate, diurnal cycle), rainfall events
// (uniform or Gaussian in space, optionally enhanced with elevation) and, optionally, the
// precipitation and temperature of a coupled-wind timeline sampled at the hydrology cells.
struct RainfallEvent {
    double start_seconds{0.0}, duration_seconds{0.0}, rate_mm_h{0.0};
    std::optional<double> center_east_m, center_north_m, radius_m;
    double orographic_enhancement_per_km{0.0};
};

struct CoupledForcing {
    std::vector<double> elapsed;                     // frame times
    std::vector<std::vector<double>> rate_mm_h;       // per frame, per hydrology cell
    std::vector<std::vector<double>> temperature_k;
    bool has_temperature{false};
};

struct Forcing {
    double sea_level_temperature_k{287.0};
    double lapse_rate_k_m{0.0065};
    double diurnal_amplitude_k{0.0};
    double diurnal_phase_seconds{0.0};
    std::vector<RainfallEvent> events;
    std::optional<CoupledForcing> coupled;
    Json provenance;
};

CoupledForcing load_coupled_forcing(const std::string& path, double offset_seconds, const TransportGrid2D& grid, Json& provenance) {
    std::ifstream file(path);
    if (!file) throw std::invalid_argument("cannot open coupled timeline: " + path);
    Json timeline;
    try { timeline = Json::parse(file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid coupled timeline JSON: ") + error.what()); }
    if (!timeline.is_object() || timeline.value("schema_version", "") != "terraflux.weather.timeline/v1") throw std::invalid_argument("coupled timeline must be a terraflux.weather.timeline/v1 document");
    const Json& frames = required(timeline, "frames");
    const Json& atmosphere = required(timeline, "coupled_atmosphere");
    const bool has_orography = atmosphere.contains("orography"), has_precipitation = atmosphere.contains("precipitation");
    if (!has_orography && !has_precipitation) throw std::invalid_argument("coupled timeline carries neither orography nor precipitation frames");
    if (!frames.is_array() || frames.empty()) throw std::invalid_argument("coupled timeline has no frames");
    const TransportGrid2D source = parse_grid(required(frames.at(static_cast<std::size_t>(0)), "grid"));
    validate_transport_grid(source);
    // Nearest coupled cell for every hydrology cell; the hydrology grid must lie inside the coupled grid.
    std::vector<std::size_t> map(grid.nx * grid.ny);
    for (std::size_t i = 0; i < map.size(); ++i) {
        const double east = grid.origin_east_m + (static_cast<double>(i % grid.nx) + 0.5) * grid.spacing_east_m;
        const double north = grid.origin_north_m + (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m;
        const double fx = (east - source.origin_east_m) / source.spacing_east_m, fy = (north - source.origin_north_m) / source.spacing_north_m;
        if (fx < 0.0 || fy < 0.0 || fx >= static_cast<double>(source.nx) || fy >= static_cast<double>(source.ny)) {
            throw std::invalid_argument("the hydrology grid must lie within the coupled timeline grid (same local frame)");
        }
        map[i] = static_cast<std::size_t>(fy) * source.nx + static_cast<std::size_t>(fx);
    }
    CoupledForcing result;
    result.has_temperature = true;
    const std::size_t source_count = source.nx * source.ny;
    for (std::size_t k = 0; k < frames.size(); ++k) {
        const Json& frame = frames.at(k);
        result.elapsed.push_back(required_number(frame, "elapsed_seconds") + offset_seconds);
        std::vector<double> rate(source_count, 0.0);
        if (has_orography) {
            const auto oro = required_number_array(required(atmosphere, "orography").at("frames").at(k), "orographic_precipitation_rate_mm_h");
            if (oro.size() != source_count) throw std::invalid_argument("coupled orography frame does not match its grid");
            for (std::size_t j = 0; j < source_count; ++j) rate[j] += std::max(0.0, oro[j]);
        }
        if (has_precipitation) {
            // rate_mm_h is the intensity while it precipitates; the cell-mean rate over the interval is
            // rate x duration_fraction (the outcome contract: accumulation = rate x duration x interval).
            const Json& outcome = required(atmosphere, "precipitation").at("frames").at(k);
            const auto pr = required_number_array(outcome, "rate_mm_h");
            if (pr.size() != source_count) throw std::invalid_argument("coupled precipitation frame does not match its grid");
            std::vector<double> duration(source_count, 1.0);
            if (outcome.contains("duration_fraction")) {
                duration = required_number_array(outcome, "duration_fraction");
                if (duration.size() != source_count) throw std::invalid_argument("coupled precipitation duration does not match its grid");
            }
            for (std::size_t j = 0; j < source_count; ++j) rate[j] += std::max(0.0, pr[j]) * std::min(1.0, std::max(0.0, duration[j]));
            const auto pr = required_number_array(required(atmosphere, "precipitation").at("frames").at(k), "rate_mm_h");
            if (pr.size() != source_count) throw std::invalid_argument("coupled precipitation frame does not match its grid");
            for (std::size_t j = 0; j < source_count; ++j) rate[j] += std::max(0.0, pr[j]);
        }
        const auto temperature = required_number_array(required(required(frame, "fields"), "air_temperature"), "values");
        if (temperature.size() != source_count) throw std::invalid_argument("coupled temperature frame does not match its grid");
        std::vector<double> mapped_rate(map.size()), mapped_temperature(map.size());
        for (std::size_t i = 0; i < map.size(); ++i) { mapped_rate[i] = rate[map[i]]; mapped_temperature[i] = temperature[map[i]]; }
        result.rate_mm_h.push_back(std::move(mapped_rate));
        result.temperature_k.push_back(std::move(mapped_temperature));
    }
    provenance = {
        {"kind", "coupled_timeline"}, {"path", path}, {"offset_seconds", offset_seconds}, {"frames", frames.size()},
        {"sources", Json::array()}, {"sampling", "nearest_cell_piecewise_constant_in_time"},
    };
    if (has_orography) provenance["sources"].push_back("coupled_atmosphere.orography.orographic_precipitation_rate_mm_h");
    if (has_precipitation) provenance["sources"].push_back("coupled_atmosphere.precipitation.rate_mm_h x duration_fraction");
    if (has_precipitation) provenance["sources"].push_back("coupled_atmosphere.precipitation.rate_mm_h");
    provenance["sources"].push_back("frames.fields.air_temperature");
    return result;
}

Forcing parse_forcing(const Json& request, const TransportGrid2D& grid) {
    Forcing f;
    const Json& section = required(request, "forcing");
    Json temperature_provenance = {{"kind", "none"}};
    if (section.contains("air_temperature")) {
        const Json& t = required(section, "air_temperature");
        f.sea_level_temperature_k = optional_number(t, "sea_level_k", f.sea_level_temperature_k);
        f.lapse_rate_k_m = optional_number(t, "lapse_rate_k_m", f.lapse_rate_k_m);
        f.diurnal_amplitude_k = optional_number(t, "diurnal_amplitude_k", f.diurnal_amplitude_k);
        f.diurnal_phase_seconds = optional_number(t, "diurnal_phase_seconds", f.diurnal_phase_seconds);
        if (f.sea_level_temperature_k < 200.0 || f.sea_level_temperature_k > 330.0 || std::abs(f.lapse_rate_k_m) > 0.02 || f.diurnal_amplitude_k < 0.0 || f.diurnal_amplitude_k > 30.0) {
            throw std::invalid_argument("forcing.air_temperature values are outside the supported ranges");
        }
        temperature_provenance = {{"kind", "lapse_rate_diurnal"}, {"sea_level_k", f.sea_level_temperature_k}, {"lapse_rate_k_m", f.lapse_rate_k_m},
                                  {"diurnal_amplitude_k", f.diurnal_amplitude_k}, {"diurnal_phase_seconds", f.diurnal_phase_seconds}};
    } else {
        temperature_provenance = {{"kind", "lapse_rate_diurnal"}, {"sea_level_k", f.sea_level_temperature_k}, {"lapse_rate_k_m", f.lapse_rate_k_m},
                                  {"diurnal_amplitude_k", 0.0}, {"diurnal_phase_seconds", 0.0}};
    }
    const Json& rain = required(section, "rainfall");
    Json events = Json::array();
    if (rain.contains("events")) {
        const Json& list = required(rain, "events");
        if (!list.is_array()) throw std::invalid_argument("forcing.rainfall.events must be an array");
        for (const Json& e : list) {
            RainfallEvent event;
            event.start_seconds = required_number(e, "start_seconds");
            event.duration_seconds = required_number(e, "duration_seconds");
            event.rate_mm_h = required_number(e, "rate_mm_h");
            event.orographic_enhancement_per_km = optional_number(e, "orographic_enhancement_per_km", 0.0);
            if (event.start_seconds < 0.0 || event.duration_seconds <= 0.0 || event.rate_mm_h < 0.0 || event.rate_mm_h > 500.0 || event.orographic_enhancement_per_km < 0.0 || event.orographic_enhancement_per_km > 5.0) {
                throw std::invalid_argument("rainfall events need start >= 0, duration > 0, rate within 0-500 mm/h and enhancement within 0-5 per km");
            }
            Json provenance = {{"start_seconds", event.start_seconds}, {"duration_seconds", event.duration_seconds}, {"rate_mm_h", event.rate_mm_h}, {"orographic_enhancement_per_km", event.orographic_enhancement_per_km}};
            if (e.contains("radius_m")) {
                event.center_east_m = required_number(e, "center_east_m");
                event.center_north_m = required_number(e, "center_north_m");
                event.radius_m = required_number(e, "radius_m");
                if (*event.radius_m <= 0.0) throw std::invalid_argument("rainfall event radius_m must be positive");
                provenance["center_east_m"] = *event.center_east_m;
                provenance["center_north_m"] = *event.center_north_m;
                provenance["radius_m"] = *event.radius_m;
            }
            events.push_back(provenance);
            f.events.push_back(event);
        }
    }
    Json coupled_provenance = {{"kind", "none"}};
    if (rain.contains("coupled_timeline_path")) {
        f.coupled = load_coupled_forcing(required_string(rain, "coupled_timeline_path"), optional_number(rain, "coupled_time_offset_seconds", 0.0), grid, coupled_provenance);
    }
    if (f.events.empty() && !f.coupled) throw std::invalid_argument("forcing.rainfall needs events and/or a coupled_timeline_path");
    f.provenance = {{"air_temperature", temperature_provenance}, {"rainfall_events", events}, {"coupled", coupled_provenance}};
    return f;
}

// Forcing at the interval [elapsed, elapsed + dt): events are evaluated at the interval midpoint.
CatchmentForcing forcing_at(const Forcing& f, double elapsed, double dt, const std::vector<double>& z, const TransportGrid2D& grid) {
    const std::size_t count = z.size();
    CatchmentForcing out;
    out.precipitation_rate_m_s.assign(count, 0.0);
    out.air_temperature_k.assign(count, 0.0);
    const double mid = elapsed + 0.5 * dt;
    const double diurnal = f.diurnal_amplitude_k * std::sin(2.0 * 3.14159265358979323846 * (mid - f.diurnal_phase_seconds) / 86400.0);
    for (std::size_t i = 0; i < count; ++i) out.air_temperature_k[i] = f.sea_level_temperature_k - f.lapse_rate_k_m * std::max(0.0, z[i]) + diurnal;
    if (f.coupled) {
        const auto& c = *f.coupled;
        std::size_t k = 0;
        while (k + 1 < c.elapsed.size() && c.elapsed[k + 1] <= mid) ++k;
        for (std::size_t i = 0; i < count; ++i) {
            out.precipitation_rate_m_s[i] += c.rate_mm_h[k][i] / 3.6e6;
            if (c.has_temperature) out.air_temperature_k[i] = c.temperature_k[k][i];
        }
    }
    for (const auto& e : f.events) {
        if (mid < e.start_seconds || mid >= e.start_seconds + e.duration_seconds) continue;
        for (std::size_t i = 0; i < count; ++i) {
            double weight = 1.0;
            if (e.radius_m) {
                const double east = grid.origin_east_m + (static_cast<double>(i % grid.nx) + 0.5) * grid.spacing_east_m - *e.center_east_m;
                const double north = grid.origin_north_m + (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m - *e.center_north_m;
                weight = std::exp(-(east * east + north * north) / (*e.radius_m * *e.radius_m));
            }
            out.precipitation_rate_m_s[i] += e.rate_mm_h * weight * (1.0 + e.orographic_enhancement_per_km * std::max(0.0, z[i]) / 1000.0) / 3.6e6;
        }
    }
    for (double& r : out.precipitation_rate_m_s) r = std::min(r, 1.0e-3);
    return out;
}

Json network_json(const CatchmentNetwork& n) {
    Json downstream = Json::array(), direction = Json::array(), terminal = Json::array(), ocean = Json::array(), area = Json::array(), order = Json::array(), channel = Json::array();
    Json lake = Json::array(), basin = Json::array(), distance = Json::array(), slope = Json::array(), width = Json::array(), bankfull = Json::array(), regolith = Json::array(), target = Json::array(), filled = Json::array();
    for (std::size_t i = 0; i < n.elevation_m.size(); ++i) {
        downstream.push_back(n.downstream[i]);
        direction.push_back(n.flow_direction[i] == 8 ? Json(nullptr) : Json(static_cast<int>(n.flow_direction[i]) * 45));
        terminal.push_back(terminal_kind_name(n.terminal[i]));
        ocean.push_back(n.ocean[i] != 0);
        area.push_back(sig(n.upstream_area_m2[i] / 1.0e6));
        order.push_back(n.strahler_order[i]);
        channel.push_back(n.channel[i] != 0);
        lake.push_back(n.lake_id[i]);
        basin.push_back(n.basin_id[i]);
        distance.push_back(sig(n.distance_to_outlet_m[i] / 1000.0));
        slope.push_back(sig(n.slope[i]));
        width.push_back(sig(n.channel_width_m[i]));
        bankfull.push_back(sig(n.bankfull_depth_m[i]));
        regolith.push_back(sig(n.regolith_depth_m[i]));
        target.push_back(n.channel_target[i]);
        filled.push_back(sig(n.filled_elevation_m[i] - n.elevation_m[i]));
    }
    Json lakes = Json::array();
    for (const auto& l : n.lakes) {
        lakes.push_back({{"id", l.id}, {"cell_count", l.cells.size()}, {"area_km2", sig(l.area_m2 / 1.0e6)}, {"capacity_m3", sig(l.capacity_m3)},
                         {"spill_elevation_m", sig(l.spill_elevation_m)}, {"outlet_cell", l.outlet_cell}, {"outlet_kind", l.outlet_cell < 0 ? terminal_kind_name(l.outlet_kind) : (n.ocean[static_cast<std::size_t>(l.outlet_cell)] ? "ocean" : "land")}});
    }
    Json basins = Json::array();
    for (const auto& b : n.basins) basins.push_back({{"id", b.id}, {"outlet_cell", b.outlet_cell}, {"area_km2", sig(b.area_m2 / 1.0e6)}, {"terminal", terminal_kind_name(b.terminal)}});
    return {
        {"flow_direction_code", "D8 bearing: 0 = east, counter-clockwise in degrees; null = terminal or ocean"},
        {"downstream", downstream}, {"flow_direction_deg", direction}, {"terminal", terminal}, {"ocean", ocean},
        {"upstream_area_km2", area}, {"strahler_order", order}, {"channel", channel}, {"lake_id", lake}, {"basin_id", basin},
        {"distance_to_outlet_km", distance}, {"slope", slope}, {"channel_width_m", width}, {"bankfull_depth_m", bankfull},
        {"regolith_depth_m", regolith}, {"channel_target", target}, {"filled_depth_m", filled},
        {"lakes", lakes}, {"basins", basins},
    };
}

Json frame_json(const CatchmentState& s, const CatchmentNetwork& n, const CatchmentConfig& c) {
    const std::size_t count = n.elevation_m.size();
    const double area = c.grid.spacing_east_m * c.grid.spacing_north_m;
    std::vector<double> rain(count), snow(count), surface(count), saturation(count), table(count), erosion(count), deposition(count), fs(count);
    Json flood_class = Json::array();
    std::size_t class_counts[5] = {0, 0, 0, 0, 0};
    double flooded_area = 0.0, snow_m3 = 0.0, surface_m3 = 0.0, soil_m3 = 0.0, groundwater_m3 = 0.0, channel_m3 = 0.0, lake_m3 = 0.0, max_q = 0.0;
    std::size_t peak = 0, slides = 0, thresholds = 0, unstable = 0;
    for (std::size_t i = 0; i < count; ++i) {
        rain[i] = s.rainfall_rate_m_s[i] * 3.6e6;
        snow[i] = s.snow_m[i] * 1000.0;
        surface[i] = s.surface_m[i] * 1000.0;
        const double capacity = c.soil.porosity * s.regolith_depth_m[i];
        saturation[i] = capacity > 0.0 ? std::min(1.0, s.soil_m[i] / capacity) : 0.0;
        table[i] = n.ocean[i] ? 0.0 : std::max(0.0, n.elevation_m[i] - s.water_table_head_m[i]);
        erosion[i] = s.erosion_m[i] * 1000.0;
        deposition[i] = s.deposition_m[i] * 1000.0;
        fs[i] = s.factor_of_safety[i] > 0.0 ? s.factor_of_safety[i] : std::numeric_limits<double>::quiet_NaN();
        const FloodClass cls = n.ocean[i] || n.lake_id[i] >= 0 ? FloodClass::none : classify_flood(s.surface_m[i], s.overland_velocity_m_s[i], c.hazards);
        flood_class.push_back(static_cast<int>(cls));
        ++class_counts[static_cast<int>(cls)];
        if (cls != FloodClass::none) flooded_area += area;
        snow_m3 += s.snow_m[i] * area;
        surface_m3 += s.surface_m[i] * area;
        soil_m3 += s.soil_m[i] * area;
        groundwater_m3 += s.groundwater_m[i] * area;
        channel_m3 += s.channel_m3[i];
        if (s.discharge_m3_s[i] > max_q) { max_q = s.discharge_m3_s[i]; peak = i; }
        slides += s.landslide[i];
        thresholds += s.threshold_exceeded[i];
        unstable += s.unstable[i];
    }
    for (double v : s.lake_volume_m3) lake_m3 += v;
    Json fs_json = Json::array();
    for (double v : fs) fs_json.push_back(std::isnan(v) ? Json(nullptr) : Json(sig(v)));
    return {
        {"elapsed_seconds", s.elapsed_seconds},
        {"rainfall_rate_mm_h", rounded_array(rain)},
        {"snow_mm", rounded_array(snow)},
        {"surface_water_mm", rounded_array(surface)},
        {"soil_saturation", rounded_array(saturation)},
        {"water_table_depth_m", rounded_array(table)},
        {"discharge_m3_s", rounded_array(s.discharge_m3_s)},
        {"channel_depth_m", rounded_array(s.channel_depth_m)},
        {"flood_class", flood_class},
        {"erosion_mm", rounded_array(erosion)},
        {"deposition_mm", rounded_array(deposition)},
        {"sediment_flux_kg_s", rounded_array(s.sediment_flux_kg_s)},
        {"factor_of_safety", fs_json},
        {"landslide", flag_array(s.landslide)},
        {"unstable", flag_array(s.unstable)},
        {"rainfall_threshold_exceeded", flag_array(s.threshold_exceeded)},
        {"lake_level_m", rounded_array(s.lake_level_m)},
        {"lake_volume_m3", rounded_array(s.lake_volume_m3)},
        {"storage_m3", {{"snow", snow_m3}, {"surface", surface_m3}, {"soil", soil_m3}, {"groundwater", groundwater_m3}, {"channels", channel_m3}, {"lakes", lake_m3}, {"total", total_storage_m3(s, c)}}},
        {"cumulative_m3", {
            {"precipitation", s.totals.precipitation_m3}, {"evapotranspiration", s.totals.evapotranspiration_m3}, {"ocean_discharge", s.totals.ocean_discharge_m3},
            {"edge_outflow", s.totals.edge_outflow_m3}, {"infiltration", s.totals.infiltration_m3}, {"percolation", s.totals.percolation_m3},
            {"baseflow", s.totals.baseflow_m3}, {"snowmelt", s.totals.snowmelt_m3}}},
        {"step_precipitation_m3", s.step_precipitation_m3},
        {"balance_error_m3", s.balance_error_m3},
        {"sediment_yield_kg", s.totals.sediment_yield_kg},
        {"landslide_count", s.totals.landslide_count},
        {"landslide_volume_m3", s.totals.landslide_volume_m3},
        {"landslide_cells", slides},
        {"unstable_cells", unstable},
        {"threshold_cells", thresholds},
        {"flooded_area_km2", flooded_area / 1.0e6},
        {"flood_class_cells", {{"none", class_counts[0]}, {"low", class_counts[1]}, {"medium", class_counts[2]}, {"high", class_counts[3]}, {"extreme", class_counts[4]}}},
        {"max_discharge_m3_s", sig(max_q)},
        {"peak_discharge_cell", peak},
    };
}

Json forecast_frame_json(const CatchmentForcing& forcing, double elapsed, const TimeRequest& time, const TransportGrid2D& grid) {
    std::vector<double> zeros(forcing.air_temperature_k.size(), 0.0);
    return {
        {"schema_version", "terraflux.weather.forecast/v1"},
        {"start_time_utc", time.start_time_utc},
        {"elapsed_seconds", elapsed},
        {"grid", grid_json(grid)},
        {"observation_updates", Json::array()},
        {"fields", {
            {"air_temperature", {{"units", "K"}, {"values", rounded_array(forcing.air_temperature_k)}}},
            {"wind_east", {{"units", "m/s"}, {"values", zeros}}},
            {"wind_north", {{"units", "m/s"}, {"values", zeros}}},
        }},
    };
}

Json events_json(const std::vector<LandslideEvent>& events) {
    Json out = Json::array();
    for (const auto& e : events) {
        out.push_back({{"kind", "landslide"}, {"elapsed_seconds", e.elapsed_seconds}, {"cell", e.cell}, {"runout_cell", e.runout_cell},
                       {"factor_of_safety", sig(e.factor_of_safety)}, {"volume_m3", sig(e.volume_m3)}, {"runout_length_m", sig(e.runout_length_m)}, {"deposited_in", e.deposited_in}});
    }
    return out;
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
        if (request.value("schema_version", "") != "terraflux.hydrology.catchment-run/v1") throw std::invalid_argument("schema_version must be terraflux.hydrology.catchment-run/v1");

        TimeRequest time;
        const CatchmentConfig config = parse_config(request, time);
        Json terrain_provenance;
        const auto elevation = parse_terrain(request, config.grid, terrain_provenance);
        const Forcing forcing = parse_forcing(request, config.grid);
        const CatchmentNetwork network = build_catchment_network(elevation, config);
        CatchmentState state = initialize_catchment(network, config);

        // Spin-up: dry steps so the rivers carry baseflow and the stores settle; not recorded.
        for (std::size_t k = 0; k < time.spinup_steps; ++k) {
            auto dry = forcing_at(forcing, -static_cast<double>(time.spinup_steps - k) * config.timestep_seconds, config.timestep_seconds, elevation, config.grid);
            dry.precipitation_rate_m_s.assign(elevation.size(), 0.0);
            state = advance_catchment(state, dry, network, config);
        }
        state.elapsed_seconds = 0.0;
        state.totals = CatchmentTotals{};
        state.events.clear();
        std::fill(state.erosion_m.begin(), state.erosion_m.end(), 0.0);
        std::fill(state.deposition_m.begin(), state.deposition_m.end(), 0.0);
        std::fill(state.storm_duration_s.begin(), state.storm_duration_s.end(), 0.0);
        std::fill(state.storm_depth_m.begin(), state.storm_depth_m.end(), 0.0);
        state.balance_error_m3 = 0.0;
        state.step_precipitation_m3 = 0.0;
        const double initial_storage = total_storage_m3(state, config);

        Json frames = Json::array(), hydrology_frames = Json::array();
        auto record = [&](const CatchmentForcing& f) {
            frames.push_back(forecast_frame_json(f, state.elapsed_seconds, time, config.grid));
            hydrology_frames.push_back(frame_json(state, network, config));
        };
        CatchmentForcing current = forcing_at(forcing, 0.0, config.timestep_seconds, elevation, config.grid);
        current.precipitation_rate_m_s.assign(elevation.size(), 0.0);
        state.rainfall_rate_m_s.assign(elevation.size(), 0.0);
        record(current);
        for (std::size_t k = 0; k < time.steps; ++k) {
            current = forcing_at(forcing, state.elapsed_seconds, config.timestep_seconds, elevation, config.grid);
            state = advance_catchment(state, current, network, config);
            if ((k + 1) % time.snapshot_every == 0 || k + 1 == time.steps) record(current);
        }

        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"terrain", {
                {"schema_version", "terraflux.terrain.grid/v1"},
                {"grid", grid_json(config.grid)},
                {"units", "m"},
                {"vertical_reference", "synthetic_local_datum"},
                {"source_id", terrain_provenance.at("kind").get<std::string>()},
                {"values", elevation},
            }},
            {"hydrology", {
                {"schema_version", "terraflux.hydrology.catchment/v1"},
                {"method", "priority_flood_d8_kinematic_cascade_darcy_infinite_slope_stream_power"},
                {"source_kind", "engine_output"},
                {"request", config_json(config, time)},
                {"terrain_source", terrain_provenance},
                {"forcing_source", forcing.provenance},
                {"snapshot_every_steps", time.snapshot_every},
                {"initial_storage_m3", initial_storage},
                {"network", network_json(network)},
                {"frames", hydrology_frames},
                {"events", events_json(state.events)},
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
