#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/cloud_formation.hpp"
#include "terraflux/weather/precipitation_outcomes.hpp"
#include "terraflux/weather/severe_convection.hpp"
#include "terraflux/weather/storm_systems.hpp"
#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/pressure_wind.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw std::invalid_argument(std::string("run request is missing field: ") + key);
    }
    return object.at(key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(
            std::string("run request field must be a non-empty string: ") + key);
    }
    return value.get<std::string>();
}

double required_number(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number()) {
        throw std::invalid_argument(std::string("run request field must be numeric: ") + key);
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::invalid_argument(std::string("run request field must be finite: ") + key);
    }
    return result;
}

double optional_number(const Json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key)) {
        return fallback;
    }
    return required_number(object, key);
}

std::string optional_string(const Json& object, const char* key, const std::string& fallback) {
    if (!object.is_object() || !object.contains(key)) {
        return fallback;
    }
    return required_string(object, key);
}

std::size_t required_positive_size(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number_integer()) {
        throw std::invalid_argument(
            std::string("run request field must be a positive integer: ") + key);
    }
    const long long parsed = value.get<long long>();
    if (parsed <= 0 ||
        static_cast<unsigned long long>(parsed) >
            static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(
            std::string("run request field is outside the supported range: ") + key);
    }
    return static_cast<std::size_t>(parsed);
}

std::vector<double> required_number_array(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_array()) {
        throw std::invalid_argument(std::string("run request field must be an array: ") + key);
    }
    std::vector<double> result;
    result.reserve(value.size());
    for (const Json& item : value) {
        if (!item.is_number()) {
            throw std::invalid_argument(
                std::string("run request array must contain numbers: ") + key);
        }
        const double number = item.get<double>();
        if (!std::isfinite(number)) {
            throw std::invalid_argument(
                std::string("run request array values must be finite: ") + key);
        }
        result.push_back(number);
    }
    return result;
}

// Optional vertical-column diagnostics configuration.
std::optional<terraflux::weather::ColumnConfig> parse_column(const Json& request) {
    if (!request.contains("column")) {
        return std::nullopt;
    }
    const Json& column = required(request, "column");
    terraflux::weather::ColumnConfig defaults;
    terraflux::weather::ColumnConfig config;
    config.layer_count = required_positive_size(column, "layer_count");
    config.layer_thickness_m = required_number(column, "layer_thickness_m");
    config.environment_lapse_rate_k_m = required_number(column, "environment_lapse_rate_k_m");
    config.tropopause_height_m = optional_number(column, "tropopause_height_m", defaults.tropopause_height_m);
    config.humidity_scale_height_m = required_number(column, "humidity_scale_height_m");
    config.parcel_step_m = optional_number(column, "parcel_step_m", defaults.parcel_step_m);
    config.inversion_base_m = optional_number(column, "inversion_base_m", defaults.inversion_base_m);
    config.inversion_depth_m = optional_number(column, "inversion_depth_m", defaults.inversion_depth_m);
    config.inversion_strength_k = optional_number(column, "inversion_strength_k", defaults.inversion_strength_k);
    config.lapse_rate_above_inversion_k_m = optional_number(column, "lapse_rate_above_inversion_k_m", defaults.lapse_rate_above_inversion_k_m);
    terraflux::weather::validate_column_config(config);
    return config;
}

// Optional cloud formation (fronts + convection) diagnostics; needs a column.
struct CloudRequest {
    terraflux::weather::CloudFormationConfig config;
    double heating_amplitude_k{0.0};
    double heating_phase_seconds{0.0};
};

std::optional<CloudRequest> parse_cloud_formation(
    const Json& request, const std::optional<terraflux::weather::ColumnConfig>& column) {
    if (!request.contains("cloud_formation")) {
        return std::nullopt;
    }
    if (!column) {
        throw std::invalid_argument("cloud_formation requires a column section");
    }
    const Json& c = required(request, "cloud_formation");
    CloudRequest result;
    terraflux::weather::CloudFormationConfig defaults;
    auto& cfg = result.config;
    cfg.column = *column;
    cfg.frontal_gradient_threshold_k_m = optional_number(c, "frontal_gradient_threshold_k_m", defaults.frontal_gradient_threshold_k_m);
    cfg.advection_threshold_k_s = optional_number(c, "advection_threshold_k_s", defaults.advection_threshold_k_s);
    cfg.boundary_layer_depth_m = optional_number(c, "boundary_layer_depth_m", defaults.boundary_layer_depth_m);
    cfg.lifting_time_scale_s = optional_number(c, "lifting_time_scale_s", defaults.lifting_time_scale_s);
    cfg.mixed_layer_growth_m_per_k = optional_number(c, "mixed_layer_growth_m_per_k", defaults.mixed_layer_growth_m_per_k);
    cfg.warm_front_slope = optional_number(c, "warm_front_slope", defaults.warm_front_slope);
    cfg.cold_front_slope = optional_number(c, "cold_front_slope", defaults.cold_front_slope);
    cfg.warm_front_reach_m = optional_number(c, "warm_front_reach_m", defaults.warm_front_reach_m);
    cfg.cold_front_reach_m = optional_number(c, "cold_front_reach_m", defaults.cold_front_reach_m);
    cfg.frontal_band_width_m = optional_number(c, "frontal_band_width_m", defaults.frontal_band_width_m);
    cfg.cin_trigger_j_kg = optional_number(c, "cin_trigger_j_kg", defaults.cin_trigger_j_kg);
    cfg.cape_threshold_j_kg = optional_number(c, "cape_threshold_j_kg", defaults.cape_threshold_j_kg);
    cfg.max_cloud_top_m = optional_number(c, "max_cloud_top_m", defaults.max_cloud_top_m);
    if (c.contains("surface_heating")) {
        const Json& heating = required(c, "surface_heating");
        result.heating_amplitude_k = required_number(heating, "amplitude_k");
        result.heating_phase_seconds = optional_number(heating, "phase_seconds", 0.0);
    }
    terraflux::weather::validate_cloud_formation_config(cfg);
    (void)terraflux::weather::diurnal_surface_heating_k(0.0, result.heating_amplitude_k, result.heating_phase_seconds);
    return result;
}

// Optional precipitation outcomes; needs cloud formation. The accumulation
// interval defaults to the snapshot interval.
std::optional<terraflux::weather::PrecipitationConfig> parse_precipitation(
    const Json& request, bool has_clouds, double snapshot_interval_seconds) {
    if (!request.contains("precipitation")) {
        return std::nullopt;
    }
    if (!has_clouds) {
        throw std::invalid_argument("precipitation requires a cloud_formation section");
    }
    const Json& p = required(request, "precipitation");
    terraflux::weather::PrecipitationConfig defaults;
    terraflux::weather::PrecipitationConfig config;
    config.stratiform_efficiency = optional_number(p, "stratiform_efficiency", defaults.stratiform_efficiency);
    config.convective_efficiency = optional_number(p, "convective_efficiency", defaults.convective_efficiency);
    config.convective_updraft_fraction = optional_number(p, "convective_updraft_fraction", defaults.convective_updraft_fraction);
    config.updraft_parcel_factor = optional_number(p, "updraft_parcel_factor", defaults.updraft_parcel_factor);
    config.interval_seconds = optional_number(p, "interval_seconds", snapshot_interval_seconds);
    config.snow_wet_bulb_k = optional_number(p, "snow_wet_bulb_k", defaults.snow_wet_bulb_k);
    config.sleet_wet_bulb_k = optional_number(p, "sleet_wet_bulb_k", defaults.sleet_wet_bulb_k);
    config.lightning_top_temperature_k = optional_number(p, "lightning_top_temperature_k", defaults.lightning_top_temperature_k);
    config.hail_min_diameter_cm = optional_number(p, "hail_min_diameter_cm", defaults.hail_min_diameter_cm);
    config.hail_melting_cm_per_km = optional_number(p, "hail_melting_cm_per_km", defaults.hail_melting_cm_per_km);
    config.hail_max_diameter_cm = optional_number(p, "hail_max_diameter_cm", defaults.hail_max_diameter_cm);
    config.drizzle_max_rate_mm_h = optional_number(p, "drizzle_max_rate_mm_h", defaults.drizzle_max_rate_mm_h);
    config.virga_surface_rh = optional_number(p, "virga_surface_rh", defaults.virga_surface_rh);
    config.virga_max_rate_mm_h = optional_number(p, "virga_max_rate_mm_h", defaults.virga_max_rate_mm_h);
    config.virga_min_base_m = optional_number(p, "virga_min_base_m", defaults.virga_min_base_m);
    config.minimum_rate_mm_h = optional_number(p, "minimum_rate_mm_h", defaults.minimum_rate_mm_h);
    terraflux::weather::validate_precipitation_config(config);
    return config;
}

Json precipitation_config_json(const terraflux::weather::PrecipitationConfig& c) {
    return {
        {"stratiform_efficiency", c.stratiform_efficiency},
        {"convective_efficiency", c.convective_efficiency},
        {"convective_updraft_fraction", c.convective_updraft_fraction},
        {"updraft_parcel_factor", c.updraft_parcel_factor},
        {"interval_seconds", c.interval_seconds},
        {"snow_wet_bulb_k", c.snow_wet_bulb_k},
        {"sleet_wet_bulb_k", c.sleet_wet_bulb_k},
        {"lightning_top_temperature_k", c.lightning_top_temperature_k},
        {"hail_min_diameter_cm", c.hail_min_diameter_cm},
        {"hail_melting_cm_per_km", c.hail_melting_cm_per_km},
        {"hail_max_diameter_cm", c.hail_max_diameter_cm},
        {"drizzle_max_rate_mm_h", c.drizzle_max_rate_mm_h},
        {"virga_surface_rh", c.virga_surface_rh},
        {"virga_max_rate_mm_h", c.virga_max_rate_mm_h},
        {"virga_min_base_m", c.virga_min_base_m},
        {"minimum_rate_mm_h", c.minimum_rate_mm_h},
    };
}

// Optional orography: terrain per cell or synthetic ridges/basins, and the
// orographic process parameters. Returns the terrain provenance and the
// initial lapse rate applied to the temperature field.
struct OrographyRequest {
    terraflux::weather::OrographyConfig config;
    Json terrain_source;
    double initial_lapse_rate_k_m{0.0065};
};

std::optional<OrographyRequest> parse_orography(const Json& request, const terraflux::weather::TransportGrid2D& grid) {
    if (!request.contains("orography")) {
        return std::nullopt;
    }
    const Json& o = required(request, "orography");
    const std::size_t count = grid.nx * grid.ny;
    OrographyRequest result;
    auto& c = result.config;
    const terraflux::weather::OrographyConfig d;
    c.enabled = true;
    const Json& terrain = required(o, "terrain");
    if (terrain.contains("values")) {
        c.elevation_m = required_number_array(terrain, "values");
        result.terrain_source = {{"kind", "prescribed_cell_values"}};
    } else {
        const double base = optional_number(terrain, "base_elevation_m", 0.0);
        c.elevation_m.assign(count, base);
        Json ridges = Json::array(), basins = Json::array();
        if (terrain.contains("ridges")) {
            const Json& list = required(terrain, "ridges");
            if (!list.is_array()) throw std::invalid_argument("orography.terrain.ridges must be an array");
            for (const Json& r : list) {
                // orientation_deg is the direction of the ridge axis from north: 0 = north-south, 90 = east-west.
                const double orientation = required_number(r, "orientation_deg"), ce = required_number(r, "center_east_m"), cn = required_number(r, "center_north_m");
                const double height = required_number(r, "height_m"), half_width = required_number(r, "half_width_m"), length = required_number(r, "length_m");
                if (half_width <= 0.0 || length <= 0.0) throw std::invalid_argument("ridge half_width_m and length_m must be positive");
                const double ax = std::sin(orientation * 3.14159265358979323846 / 180.0), ay = std::cos(orientation * 3.14159265358979323846 / 180.0);
                for (std::size_t i = 0; i < count; ++i) {
                    const double dx = grid.origin_east_m + (static_cast<double>(i % grid.nx) + 0.5) * grid.spacing_east_m - ce;
                    const double dy = grid.origin_north_m + (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m - cn;
                    const double along = dx * ax + dy * ay, across = dx * ay - dy * ax;
                    const double end = along / (0.5 * length);
                    c.elevation_m[i] += height * std::exp(-(across / half_width) * (across / half_width)) * std::exp(-end * end * end * end);
                }
                ridges.push_back({{"orientation_deg", orientation}, {"center_east_m", ce}, {"center_north_m", cn}, {"height_m", height}, {"half_width_m", half_width}, {"length_m", length}});
            }
        }
        if (terrain.contains("basins")) {
            const Json& list = required(terrain, "basins");
            if (!list.is_array()) throw std::invalid_argument("orography.terrain.basins must be an array");
            for (const Json& b : list) {
                const double ce = required_number(b, "center_east_m"), cn = required_number(b, "center_north_m");
                const double depth = required_number(b, "depth_m"), radius = required_number(b, "radius_m");
                if (radius <= 0.0) throw std::invalid_argument("basin radius_m must be positive");
                for (std::size_t i = 0; i < count; ++i) {
                    const double dx = grid.origin_east_m + (static_cast<double>(i % grid.nx) + 0.5) * grid.spacing_east_m - ce;
                    const double dy = grid.origin_north_m + (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m - cn;
                    c.elevation_m[i] -= depth * std::exp(-(dx * dx + dy * dy) / (radius * radius));
                }
                basins.push_back({{"center_east_m", ce}, {"center_north_m", cn}, {"depth_m", depth}, {"radius_m", radius}});
            }
        }
        result.terrain_source = {{"kind", "synthetic_ridges_and_basins"}, {"base_elevation_m", base}, {"ridges", ridges}, {"basins", basins}};
    }
    c.blocking_time_scale_s = optional_number(o, "blocking_time_scale_s", d.blocking_time_scale_s);
    c.obstacle_radius_m = optional_number(o, "obstacle_radius_m", d.obstacle_radius_m);
    c.influence_radius_m = optional_number(o, "influence_radius_m", d.influence_radius_m);
    c.brunt_vaisala_s = optional_number(o, "brunt_vaisala_s", d.brunt_vaisala_s);
    c.boundary_layer_depth_m = optional_number(o, "boundary_layer_depth_m", d.boundary_layer_depth_m);
    c.daytime_heating_k_per_s = optional_number(o, "daytime_heating_k_per_s", d.daytime_heating_k_per_s);
    c.nocturnal_cooling_k_per_s = optional_number(o, "nocturnal_cooling_k_per_s", d.nocturnal_cooling_k_per_s);
    c.diurnal_phase_seconds = optional_number(o, "diurnal_phase_seconds", d.diurnal_phase_seconds);
    c.valley_pooling_per_km = optional_number(o, "valley_pooling_per_km", d.valley_pooling_per_km);
    result.initial_lapse_rate_k_m = optional_number(o, "initial_lapse_rate_k_m", 0.0065);
    if (!std::isfinite(result.initial_lapse_rate_k_m) || std::abs(result.initial_lapse_rate_k_m) > 0.05) {
        throw std::invalid_argument("orography initial_lapse_rate_k_m must be within +-0.05 K/m");
    }
    return result;
}

Json orography_config_json(const OrographyRequest& r) {
    const auto& c = r.config;
    return {
        {"blocking_time_scale_s", c.blocking_time_scale_s}, {"obstacle_radius_m", c.obstacle_radius_m},
        {"influence_radius_m", c.influence_radius_m}, {"brunt_vaisala_s", c.brunt_vaisala_s},
        {"boundary_layer_depth_m", c.boundary_layer_depth_m}, {"daytime_heating_k_per_s", c.daytime_heating_k_per_s},
        {"nocturnal_cooling_k_per_s", c.nocturnal_cooling_k_per_s}, {"diurnal_phase_seconds", c.diurnal_phase_seconds},
        {"valley_pooling_per_km", c.valley_pooling_per_km}, {"initial_lapse_rate_k_m", r.initial_lapse_rate_k_m},
        {"terrain", r.terrain_source},
    };
}

// Optional surface description (land mask and sea-surface temperature).
std::optional<terraflux::weather::SurfaceConfig> parse_surface(
    const Json& request, const terraflux::weather::TransportGrid2D& grid, Json& provenance) {
    if (!request.contains("surface")) {
        return std::nullopt;
    }
    const Json& surface = required(request, "surface");
    const std::size_t count = grid.nx * grid.ny;
    terraflux::weather::SurfaceConfig result;
    const Json& land = required(surface, "land");
    Json land_provenance;
    if (land.contains("values")) {
        const auto values = required_number_array(land, "values");
        for (double v : values) {
            if (v != 0.0 && v != 1.0) throw std::invalid_argument("surface.land.values must be 0 or 1");
            result.land.push_back(static_cast<unsigned char>(v));
        }
        land_provenance = {{"kind", "prescribed_cell_values"}};
    } else {
        const double coast = required_number(land, "coast_east_m");
        const std::string side = required_string(land, "land_side");
        if (side != "west" && side != "east") throw std::invalid_argument("surface.land.land_side must be west or east");
        for (std::size_t i = 0; i < count; ++i) {
            const double east = grid.origin_east_m + (static_cast<double>(i % grid.nx) + 0.5) * grid.spacing_east_m;
            result.land.push_back(static_cast<unsigned char>(side == "west" ? east < coast : east >= coast));
        }
        land_provenance = {{"kind", "straight_coast"}, {"coast_east_m", coast}, {"land_side", side}};
    }
    const Json& sst = required(surface, "sea_surface_temperature_k");
    Json sst_provenance;
    if (sst.contains("values")) {
        result.sea_surface_temperature_k = required_number_array(sst, "values");
        sst_provenance = {{"kind", "prescribed_cell_values"}};
    } else {
        const double background = required_number(sst, "background");
        const double gradient = optional_number(sst, "gradient_north_k_per_m", 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            const double north = grid.origin_north_m + (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m;
            result.sea_surface_temperature_k.push_back(background + gradient * north);
        }
        sst_provenance = {{"kind", "background_with_gradient"}, {"background", background}, {"gradient_north_k_per_m", gradient}};
    }
    terraflux::weather::validate_surface(result, grid);
    provenance = {{"land", land_provenance}, {"sea_surface_temperature", sst_provenance}};
    return result;
}

// Optional tropical-cyclone storm systems; needs surface and column.
struct StormRequest {
    terraflux::weather::StormSystemsConfig config;
    std::vector<terraflux::weather::StormSeed> seeds;
};

std::optional<StormRequest> parse_storms(
    const Json& request, bool has_surface, const std::optional<terraflux::weather::ColumnConfig>& column) {
    if (!request.contains("storms")) {
        return std::nullopt;
    }
    if (!has_surface || !column) {
        throw std::invalid_argument("storms require surface and column sections");
    }
    const Json& st = required(request, "storms");
    StormRequest result;
    auto& c = result.config;
    const terraflux::weather::StormSystemsConfig d;
    c.column = *column;
    c.enthalpy_exchange_ratio = optional_number(st, "enthalpy_exchange_ratio", d.enthalpy_exchange_ratio);
    c.surface_wind_factor = optional_number(st, "surface_wind_factor", d.surface_wind_factor);
    c.sst_threshold_k = optional_number(st, "sst_threshold_k", d.sst_threshold_k);
    c.minimum_coriolis_s = optional_number(st, "minimum_coriolis_s", d.minimum_coriolis_s);
    c.mid_level_height_m = optional_number(st, "mid_level_height_m", d.mid_level_height_m);
    c.shear_layer_depth_m = optional_number(st, "shear_layer_depth_m", d.shear_layer_depth_m);
    c.shear_limit_m_s = optional_number(st, "shear_limit_m_s", d.shear_limit_m_s);
    c.genesis_gpi_threshold = optional_number(st, "genesis_gpi_threshold", d.genesis_gpi_threshold);
    c.genesis_separation_m = optional_number(st, "genesis_separation_m", d.genesis_separation_m);
    c.genesis_max_wind_m_s = optional_number(st, "genesis_max_wind_m_s", d.genesis_max_wind_m_s);
    c.intensification_time_scale_s = optional_number(st, "intensification_time_scale_s", d.intensification_time_scale_s);
    c.decay_alpha_per_s = optional_number(st, "decay_alpha_per_s", d.decay_alpha_per_s);
    c.decay_background_wind_m_s = optional_number(st, "decay_background_wind_m_s", d.decay_background_wind_m_s);
    c.landfall_reduction = optional_number(st, "landfall_reduction", d.landfall_reduction);
    c.dissipation_wind_m_s = optional_number(st, "dissipation_wind_m_s", d.dissipation_wind_m_s);
    c.holland_b = optional_number(st, "holland_b", d.holland_b);
    c.steering_radius_m = optional_number(st, "steering_radius_m", d.steering_radius_m);
    c.steering_factor = optional_number(st, "steering_factor", d.steering_factor);
    c.beta_drift_west_m_s = optional_number(st, "beta_drift_west_m_s", d.beta_drift_west_m_s);
    c.beta_drift_poleward_m_s = optional_number(st, "beta_drift_poleward_m_s", d.beta_drift_poleward_m_s);
    c.merge_distance_m = optional_number(st, "merge_distance_m", d.merge_distance_m);
    c.induction_factor = optional_number(st, "induction_factor", d.induction_factor);
    c.genesis_edge_margin_m = optional_number(st, "genesis_edge_margin_m", d.genesis_edge_margin_m);
    c.surge_wind_coefficient_s2_m = optional_number(st, "surge_wind_coefficient_s2_m", d.surge_wind_coefficient_s2_m);
    c.rain_core_factor_mm_h_per_m_s = optional_number(st, "rain_core_factor_mm_h_per_m_s", d.rain_core_factor_mm_h_per_m_s);
    c.substep_seconds = optional_number(st, "substep_seconds", d.substep_seconds);
    if (st.contains("max_storms")) c.max_storms = required_positive_size(st, "max_storms");
    if (st.contains("seeds")) {
        const Json& seeds = required(st, "seeds");
        if (!seeds.is_array()) throw std::invalid_argument("storms.seeds must be an array");
        for (const Json& item : seeds) {
            terraflux::weather::StormSeed seed;
            seed.east_m = required_number(item, "east_m");
            seed.north_m = required_number(item, "north_m");
            seed.max_wind_m_s = required_number(item, "max_wind_m_s");
            seed.radius_max_wind_m = optional_number(item, "radius_max_wind_m", seed.radius_max_wind_m);
            result.seeds.push_back(seed);
        }
    }
    terraflux::weather::validate_storm_systems_config(c);
    return result;
}

Json storm_config_json(const StormRequest& r) {
    const auto& c = r.config;
    Json seeds = Json::array();
    for (const auto& seed : r.seeds) {
        seeds.push_back({{"east_m", seed.east_m}, {"north_m", seed.north_m}, {"max_wind_m_s", seed.max_wind_m_s}, {"radius_max_wind_m", seed.radius_max_wind_m}});
    }
    return {
        {"enthalpy_exchange_ratio", c.enthalpy_exchange_ratio}, {"surface_wind_factor", c.surface_wind_factor},
        {"sst_threshold_k", c.sst_threshold_k}, {"minimum_coriolis_s", c.minimum_coriolis_s},
        {"mid_level_height_m", c.mid_level_height_m}, {"shear_layer_depth_m", c.shear_layer_depth_m},
        {"shear_limit_m_s", c.shear_limit_m_s}, {"genesis_gpi_threshold", c.genesis_gpi_threshold},
        {"genesis_separation_m", c.genesis_separation_m}, {"genesis_max_wind_m_s", c.genesis_max_wind_m_s},
        {"intensification_time_scale_s", c.intensification_time_scale_s}, {"decay_alpha_per_s", c.decay_alpha_per_s},
        {"decay_background_wind_m_s", c.decay_background_wind_m_s}, {"landfall_reduction", c.landfall_reduction},
        {"dissipation_wind_m_s", c.dissipation_wind_m_s}, {"holland_b", c.holland_b},
        {"steering_radius_m", c.steering_radius_m}, {"steering_factor", c.steering_factor},
        {"beta_drift_west_m_s", c.beta_drift_west_m_s}, {"beta_drift_poleward_m_s", c.beta_drift_poleward_m_s},
        {"merge_distance_m", c.merge_distance_m}, {"induction_factor", c.induction_factor},
        {"genesis_edge_margin_m", c.genesis_edge_margin_m}, {"surge_wind_coefficient_s2_m", c.surge_wind_coefficient_s2_m},
        {"rain_core_factor_mm_h_per_m_s", c.rain_core_factor_mm_h_per_m_s}, {"substep_seconds", c.substep_seconds},
        {"max_storms", c.max_storms}, {"seeds", seeds},
    };
}

Json storm_json(const terraflux::weather::TropicalCyclone& s) {
    Json track = Json::array();
    for (const auto& point : s.track) track.push_back({point.first, point.second});
    return {
        {"id", s.id}, {"east_m", s.east_m}, {"north_m", s.north_m}, {"max_wind_m_s", s.max_wind_m_s},
        {"radius_max_wind_m", s.radius_max_wind_m}, {"central_pressure_pa", s.central_pressure_pa},
        {"environment_pressure_pa", s.environment_pressure_pa}, {"category", s.category}, {"over_land", s.over_land},
        {"age_seconds", s.age_seconds}, {"time_over_land_seconds", s.time_over_land_seconds},
        {"potential_intensity_m_s", s.potential_intensity_m_s}, {"surge_estimate_m", s.surge_estimate_m},
        {"core_rain_rate_mm_h", s.core_rain_rate_mm_h}, {"track", track},
    };
}

Json events_json(const std::vector<terraflux::weather::StormEvent>& events) {
    Json result = Json::array();
    for (const auto& e : events) {
        result.push_back({{"kind", e.kind}, {"elapsed_seconds", e.elapsed_seconds}, {"storm_id", e.storm_id}, {"other_id", e.other_id},
                          {"east_m", e.east_m}, {"north_m", e.north_m}, {"max_wind_m_s", e.max_wind_m_s}, {"category", e.category}, {"surge_m", e.surge_m}});
    }
    return result;
}

// Optional severe convection (tornado) diagnostics; needs cloud formation.
std::optional<terraflux::weather::SevereConvectionConfig> parse_severe(const Json& request, bool has_clouds) {
    if (!request.contains("severe")) {
        return std::nullopt;
    }
    if (!has_clouds) {
        throw std::invalid_argument("severe requires a cloud_formation section");
    }
    const Json& sv = required(request, "severe");
    terraflux::weather::SevereConvectionConfig config;
    const terraflux::weather::SevereConvectionConfig d;
    config.deep_shear_depth_m = optional_number(sv, "deep_shear_depth_m", d.deep_shear_depth_m);
    config.bunkers_deviation_m_s = optional_number(sv, "bunkers_deviation_m_s", d.bunkers_deviation_m_s);
    config.geostrophic_air_density_kg_m3 = optional_number(sv, "geostrophic_air_density_kg_m3", d.geostrophic_air_density_kg_m3);
    config.stp_low = optional_number(sv, "stp_low", d.stp_low);
    config.stp_moderate = optional_number(sv, "stp_moderate", d.stp_moderate);
    config.stp_significant = optional_number(sv, "stp_significant", d.stp_significant);
    terraflux::weather::validate_severe_convection_config(config);
    return config;
}

Json severe_config_json(const terraflux::weather::SevereConvectionConfig& c) {
    return {{"deep_shear_depth_m", c.deep_shear_depth_m}, {"bunkers_deviation_m_s", c.bunkers_deviation_m_s},
            {"geostrophic_air_density_kg_m3", c.geostrophic_air_density_kg_m3}, {"stp_low", c.stp_low},
            {"stp_moderate", c.stp_moderate}, {"stp_significant", c.stp_significant}};
}

Json cloud_config_json(const CloudRequest& r) {
    const auto& c = r.config;
    return {
        {"frontal_gradient_threshold_k_m", c.frontal_gradient_threshold_k_m},
        {"advection_threshold_k_s", c.advection_threshold_k_s},
        {"boundary_layer_depth_m", c.boundary_layer_depth_m},
        {"lifting_time_scale_s", c.lifting_time_scale_s},
        {"mixed_layer_growth_m_per_k", c.mixed_layer_growth_m_per_k},
        {"warm_front_slope", c.warm_front_slope},
        {"cold_front_slope", c.cold_front_slope},
        {"warm_front_reach_m", c.warm_front_reach_m},
        {"cold_front_reach_m", c.cold_front_reach_m},
        {"frontal_band_width_m", c.frontal_band_width_m},
        {"cin_trigger_j_kg", c.cin_trigger_j_kg},
        {"cape_threshold_j_kg", c.cape_threshold_j_kg},
        {"max_cloud_top_m", c.max_cloud_top_m},
        {"surface_heating", {{"amplitude_k", r.heating_amplitude_k}, {"phase_seconds", r.heating_phase_seconds}}},
    };
}

Json column_config_json(const terraflux::weather::ColumnConfig& config) {
    return {
        {"layer_count", config.layer_count},
        {"layer_thickness_m", config.layer_thickness_m},
        {"environment_lapse_rate_k_m", config.environment_lapse_rate_k_m},
        {"tropopause_height_m", config.tropopause_height_m},
        {"humidity_scale_height_m", config.humidity_scale_height_m},
        {"parcel_step_m", config.parcel_step_m},
        {"inversion_base_m", config.inversion_base_m},
        {"inversion_depth_m", config.inversion_depth_m},
        {"inversion_strength_k", config.inversion_strength_k},
        {"lapse_rate_above_inversion_k_m", config.lapse_rate_above_inversion_k_m},
    };
}

// NaN entries become JSON null (missing level).
Json nullable_array(const std::vector<double>& values) {
    Json result = Json::array();
    for (double value : values) {
        if (std::isnan(value)) {
            result.push_back(nullptr);
        } else {
            result.push_back(value);
        }
    }
    return result;
}

terraflux::weather::TransportGrid2D parse_grid(const Json& grid) {
    terraflux::weather::TransportGrid2D result;
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

terraflux::weather::CoupledWindConfig parse_config(const Json& request) {
    if (required_string(request, "schema_version") != "terraflux.weather.coupled-wind-run/v1") {
        throw std::invalid_argument("unsupported coupled wind run request schema");
    }
    terraflux::weather::CoupledWindConfig config;

    const Json& time = required(request, "time");
    config.time.start_time_utc = required_string(time, "start_time_utc");
    config.time.timestep_seconds = required_number(time, "timestep_seconds");
    config.time.steps = required_positive_size(time, "steps");

    config.grid = parse_grid(required(request, "grid"));

    const Json& atmosphere = required(request, "atmosphere");
    terraflux::weather::AtmosphereConfig defaults;
    config.atmosphere.air_density_kg_m3 = required_number(atmosphere, "air_density_kg_m3");
    config.atmosphere.surface_friction_coefficient_s =
        required_number(atmosphere, "surface_friction_coefficient_s");
    config.atmosphere.coriolis_mode = required_string(atmosphere, "coriolis_mode");
    config.atmosphere.coriolis_parameter_s =
        config.atmosphere.coriolis_mode == "prescribed"
            ? required_number(atmosphere, "coriolis_parameter_s")
            : optional_number(atmosphere, "coriolis_parameter_s", 0.0);
    config.atmosphere.earth_angular_speed_rad_s =
        optional_number(atmosphere, "earth_angular_speed_rad_s", defaults.earth_angular_speed_rad_s);
    config.atmosphere.earth_radius_m =
        optional_number(atmosphere, "earth_radius_m", defaults.earth_radius_m);

    const Json& diffusion = required(request, "diffusion");
    config.diffusion.temperature_m2_s = required_number(diffusion, "temperature_m2_s");
    config.diffusion.humidity_m2_s = required_number(diffusion, "humidity_m2_s");
    config.diffusion.momentum_m2_s = required_number(diffusion, "momentum_m2_s");

    config.pressure_gradient_boundary =
        optional_string(request, "pressure_gradient_boundary", "one_sided");
    config.scalar_boundary = optional_string(request, "scalar_boundary", "zero_gradient");
    config.density_mode = optional_string(request, "density_mode", "diagnosed");
    const auto orography = parse_orography(request, config.grid);
    if (orography) config.orography = orography->config;
    terraflux::weather::validate_coupled_wind_config(config);
    return config;
}

std::vector<terraflux::weather::GaussianAnomaly> parse_anomalies(const Json& array, const char* key) {
    if (!array.is_array()) {
        throw std::invalid_argument(std::string("anomalies must be an array: ") + key);
    }
    std::vector<terraflux::weather::GaussianAnomaly> anomalies;
    for (const Json& item : array) {
        terraflux::weather::GaussianAnomaly anomaly;
        anomaly.east_m = required_number(item, "east_m");
        anomaly.north_m = required_number(item, "north_m");
        anomaly.radius_m = required_number(item, "radius_m");
        anomaly.amplitude = required_number(item, "amplitude");
        anomalies.push_back(anomaly);
    }
    return anomalies;
}

Json anomalies_json(const std::vector<terraflux::weather::GaussianAnomaly>& anomalies) {
    Json result = Json::array();
    for (const auto& anomaly : anomalies) {
        result.push_back({
            {"east_m", anomaly.east_m},
            {"north_m", anomaly.north_m},
            {"radius_m", anomaly.radius_m},
            {"amplitude", anomaly.amplitude},
        });
    }
    return result;
}

// A scalar field is either explicit cell values or background + anomalies.
std::vector<double> parse_scalar_field(
    const Json& parent,
    const char* key,
    const terraflux::weather::TransportGrid2D& grid,
    Json& provenance) {
    const Json& field = required(parent, key);
    const bool has_values = field.is_object() && field.contains("values");
    const bool has_background = field.is_object() && field.contains("background");
    if (has_values == has_background) {
        throw std::invalid_argument(
            std::string("field must contain exactly one of values or background: ") + key);
    }
    if (has_values) {
        provenance = {{"kind", "prescribed_cell_values"}};
        return required_number_array(field, "values");
    }
    const double background = required_number(field, "background");
    const auto anomalies = field.contains("anomalies")
        ? parse_anomalies(field.at("anomalies"), key)
        : std::vector<terraflux::weather::GaussianAnomaly>{};
    provenance = {
        {"kind", "synthetic_gaussian_anomalies"},
        {"background", background},
        {"anomalies", anomalies_json(anomalies)},
    };
    return terraflux::weather::build_gaussian_field(background, anomalies, grid);
}

std::vector<double> parse_pressure(
    const Json& request,
    const terraflux::weather::TransportGrid2D& grid,
    Json& provenance) {
    const Json& pressure = required(request, "pressure");
    const bool has_values = pressure.is_object() && pressure.contains("pressure_pa");
    const bool has_systems = pressure.is_object() && pressure.contains("pressure_systems");
    if (has_values == has_systems) {
        throw std::invalid_argument(
            "pressure must contain exactly one of pressure_pa or pressure_systems");
    }
    if (has_values) {
        provenance = {{"kind", "prescribed_cell_values"}};
        return required_number_array(pressure, "pressure_pa");
    }
    const Json& systems_json = required(pressure, "pressure_systems");
    terraflux::weather::PressureSystemsConfig systems;
    systems.background_pressure_pa = required_number(systems_json, "background_pressure_pa");
    const Json& centers = required(systems_json, "centers");
    if (!centers.is_array()) {
        throw std::invalid_argument("pressure_systems.centers must be an array");
    }
    Json center_provenance = Json::array();
    for (const Json& item : centers) {
        terraflux::weather::PressureCenter center;
        center.kind = required_string(item, "kind");
        center.east_m = required_number(item, "east_m");
        center.north_m = required_number(item, "north_m");
        center.pressure_anomaly_pa = required_number(item, "pressure_anomaly_pa");
        center.radius_m = required_number(item, "radius_m");
        systems.centers.push_back(center);
        center_provenance.push_back({
            {"kind", center.kind},
            {"east_m", center.east_m},
            {"north_m", center.north_m},
            {"pressure_anomaly_pa", center.pressure_anomaly_pa},
            {"radius_m", center.radius_m},
        });
    }
    provenance = {
        {"kind", "synthetic_gaussian_systems"},
        {"background_pressure_pa", systems.background_pressure_pa},
        {"centers", center_provenance},
    };
    return terraflux::weather::build_pressure_field(systems, grid);
}

Json grid_json(const terraflux::weather::TransportGrid2D& grid) {
    return {
        {"nx", grid.nx},
        {"ny", grid.ny},
        {"origin_east_m", grid.origin_east_m},
        {"origin_north_m", grid.origin_north_m},
        {"spacing_east_m", grid.spacing_east_m},
        {"spacing_north_m", grid.spacing_north_m},
        {"geographic_anchor_longitude_deg", grid.geographic_anchor_longitude_deg},
        {"geographic_anchor_latitude_deg", grid.geographic_anchor_latitude_deg},
        {"coordinate_system", "local_azimuthal_equidistant_m"},
        {"array_order", "row_major_y_then_x"},
    };
}

Json config_json(const terraflux::weather::CoupledWindConfig& config, std::size_t snapshot_every) {
    return {
        {"time", {
            {"start_time_utc", config.time.start_time_utc},
            {"timestep_seconds", config.time.timestep_seconds},
            {"steps", config.time.steps},
        }},
        {"grid", grid_json(config.grid)},
        {"atmosphere", {
            {"air_density_kg_m3", config.atmosphere.air_density_kg_m3},
            {"surface_friction_coefficient_s", config.atmosphere.surface_friction_coefficient_s},
            {"coriolis_mode", config.atmosphere.coriolis_mode},
            {"coriolis_parameter_s", config.atmosphere.coriolis_parameter_s},
            {"earth_angular_speed_rad_s", config.atmosphere.earth_angular_speed_rad_s},
            {"earth_radius_m", config.atmosphere.earth_radius_m},
        }},
        {"diffusion", {
            {"temperature_m2_s", config.diffusion.temperature_m2_s},
            {"humidity_m2_s", config.diffusion.humidity_m2_s},
            {"momentum_m2_s", config.diffusion.momentum_m2_s},
        }},
        {"pressure_gradient_boundary", config.pressure_gradient_boundary},
        {"scalar_boundary", config.scalar_boundary},
        {"density_mode", config.density_mode},
        {"snapshot_every_steps", snapshot_every},
    };
}

// One viewer-compatible forecast/v1 frame.
Json frame_json(
    const terraflux::weather::CoupledWindState& state,
    const terraflux::weather::CoupledWindConfig& config) {
    return {
        {"schema_version", "terraflux.weather.forecast/v1"},
        {"start_time_utc", config.time.start_time_utc},
        {"elapsed_seconds", state.elapsed_seconds},
        {"grid", grid_json(config.grid)},
        {"observation_updates", Json::array()},
        {"fields", {
            {"air_temperature", {{"units", "K"}, {"values", state.air_temperature_k}}},
            {"wind_east", {{"units", "m/s"}, {"values", state.wind_east_m_s}}},
            {"wind_north", {{"units", "m/s"}, {"values", state.wind_north_m_s}}},
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
        const std::string request_path = argv[1];
        const std::string output_path = argv[2];

        std::ifstream request_file(request_path);
        if (!request_file) {
            throw std::runtime_error("cannot open run request: " + request_path);
        }
        Json request;
        try {
            request = Json::parse(request_file);
        } catch (const Json::exception& error) {
            throw std::invalid_argument(std::string("invalid run request JSON: ") + error.what());
        }
        if (!request.is_object()) {
            throw std::invalid_argument("run request root must be a JSON object");
        }

        const auto config = parse_config(request);
        const auto orography = parse_orography(request, config.grid);
        const auto column = parse_column(request);
        const auto clouds = parse_cloud_formation(request, column);
        Json pressure_provenance, temperature_provenance, humidity_provenance;
        terraflux::weather::CoupledWindState state;
        state.pressure_pa = parse_pressure(request, config.grid, pressure_provenance);
        const Json& initial_fields = required(request, "initial_fields");
        state.air_temperature_k =
            parse_scalar_field(initial_fields, "air_temperature_k", config.grid, temperature_provenance);
        state.specific_humidity_kg_kg = parse_scalar_field(
            initial_fields, "specific_humidity_kg_kg", config.grid, humidity_provenance);
        if (orography) {
            // Mountain tops start colder than the lowlands by the initial lapse rate.
            for (std::size_t i = 0; i < state.air_temperature_k.size(); ++i) {
                state.air_temperature_k[i] -= orography->initial_lapse_rate_k_m * config.orography.elevation_m[i];
            }
            state.orographic_precipitation_mm.assign(state.pressure_pa.size(), 0.0);
        }
        if (request.contains("initial_wind")) {
            const Json& initial = required(request, "initial_wind");
            state.elapsed_seconds = required_number(initial, "elapsed_seconds");
            state.wind_east_m_s = required_number_array(initial, "wind_east_m_s");
            state.wind_north_m_s = required_number_array(initial, "wind_north_m_s");
        } else {
            state.elapsed_seconds = 0.0;
            state.wind_east_m_s.assign(state.pressure_pa.size(), 0.0);
            state.wind_north_m_s.assign(state.pressure_pa.size(), 0.0);
        }
        terraflux::weather::validate_coupled_wind_state(state, config);

        std::size_t snapshot_every = config.time.steps;
        if (request.contains("snapshot_every_steps")) {
            snapshot_every = required_positive_size(request, "snapshot_every_steps");
            if (snapshot_every > config.time.steps) {
                throw std::invalid_argument(
                    "snapshot_every_steps must not exceed the configured step count");
            }
        }
        const auto precipitation = parse_precipitation(
            request, clouds.has_value(), static_cast<double>(snapshot_every) * config.time.timestep_seconds);
        Json surface_provenance;
        const auto surface = parse_surface(request, config.grid, surface_provenance);
        const auto storms = parse_storms(request, surface.has_value(), column);
        const auto severe = parse_severe(request, clouds.has_value());
        terraflux::weather::StormSystemsState storm_state;

        Json frames = Json::array();
        Json humidity_by_frame = Json::array();
        Json relative_humidity_by_frame = Json::array();
        Json density_by_frame = Json::array();
        Json supersaturated_by_frame = Json::array();
        Json stability_by_frame = Json::array();
        Json lcl_by_frame = Json::array(), lfc_by_frame = Json::array(), el_by_frame = Json::array();
        Json cape_by_frame = Json::array(), cin_by_frame = Json::array(), potential_by_frame = Json::array();
        Json condensed_by_frame = Json::array();
        Json column_mass = Json::array(), top_pressure = Json::array();
        Json cloud_frames = Json::array();
        Json precipitation_frames = Json::array();
        Json storm_frames = Json::array();
        Json severe_frames = Json::array();
        Json orography_frames = Json::array();
        std::vector<double> previous_orographic_precipitation;
        double previous_orographic_time = 0.0;
        const auto record = [&](const terraflux::weather::CoupledWindState& current) {
            const auto diagnostics = terraflux::weather::diagnose_moist_layer(current, config);
            std::vector<double> relative_humidity_pct(diagnostics.relative_humidity_fraction.size());
            for (std::size_t i = 0; i < relative_humidity_pct.size(); ++i) {
                relative_humidity_pct[i] = 100.0 * diagnostics.relative_humidity_fraction[i];
            }
            frames.push_back(frame_json(current, config));
            humidity_by_frame.push_back(current.specific_humidity_kg_kg);
            relative_humidity_by_frame.push_back(relative_humidity_pct);
            density_by_frame.push_back(diagnostics.air_density_kg_m3);
            supersaturated_by_frame.push_back(diagnostics.supersaturated_cells);
            stability_by_frame.push_back(
                terraflux::weather::coupled_scalar_stability_number(current, config));
            if (column) {
                const auto d = terraflux::weather::diagnose_column_field(
                    current.pressure_pa, current.air_temperature_k, current.specific_humidity_kg_kg, *column);
                lcl_by_frame.push_back(nullable_array(d.lcl_height_m));
                lfc_by_frame.push_back(nullable_array(d.lfc_height_m));
                el_by_frame.push_back(nullable_array(d.equilibrium_level_m));
                cape_by_frame.push_back(d.cape_j_kg);
                cin_by_frame.push_back(d.cin_j_kg);
                potential_by_frame.push_back(d.convective_potential);
                condensed_by_frame.push_back(d.condensed_water_kg_m2);
                if (column_mass.empty()) {
                    column_mass = d.column_mass_kg_m2;
                    top_pressure = d.top_pressure_pa;
                }
            }
            if (clouds) {
                const double heating = terraflux::weather::diurnal_surface_heating_k(
                    current.elapsed_seconds, clouds->heating_amplitude_k, clouds->heating_phase_seconds);
                const auto fronts = terraflux::weather::analyse_fronts(current, config.grid, clouds->config);
                const auto field = terraflux::weather::diagnose_cloud_formation(
                    current, config.grid, clouds->config, heating, fronts);
                Json thunderstorm = Json::array(), side = Json::array();
                for (unsigned char flag : field.thunderstorm) thunderstorm.push_back(flag != 0);
                for (int value : fronts.side) side.push_back(value);
                cloud_frames.push_back({
                    {"elapsed_seconds", current.elapsed_seconds},
                    {"surface_heating_k", heating},
                    {"cloud_type", field.cloud_type},
                    {"mechanism", field.mechanism},
                    {"trigger", field.trigger},
                    {"base_m", nullable_array(field.base_m)},
                    {"top_m", nullable_array(field.top_m)},
                    {"cloud_fraction", field.cloud_fraction},
                    {"thunderstorm", thunderstorm},
                    {"lifting_depth_m", field.lifting_depth_m},
                    {"heated_cape_j_kg", field.heated_cape_j_kg},
                    {"heated_cin_j_kg", field.heated_cin_j_kg},
                    {"front_type", fronts.front_type},
                    {"temperature_gradient_k_m", fronts.temperature_gradient_k_m},
                    {"temperature_advection_k_s", fronts.temperature_advection_k_s},
                    {"divergence_s", fronts.divergence_s},
                    {"distance_to_front_m", nullable_array(fronts.distance_to_front_m)},
                    {"side", side},
                    {"cloudy_cells", field.cloudy_cells},
                    {"thunderstorm_cells", field.thunderstorm_cells},
                    {"front_cells", fronts.front_cell_count},
                });
                if (precipitation) {
                    const auto outcome = terraflux::weather::diagnose_precipitation(
                        current, config.grid, clouds->config, field, *precipitation);
                    Json lightning = Json::array(), virga = Json::array(), hazards = Json::array();
                    for (unsigned char flag : outcome.lightning) lightning.push_back(flag != 0);
                    for (unsigned char flag : outcome.virga) virga.push_back(flag != 0);
                    for (const auto& list : outcome.hazards) hazards.push_back(list);
                    precipitation_frames.push_back({
                        {"elapsed_seconds", current.elapsed_seconds},
                        {"precipitation_type", outcome.precipitation_type},
                        {"phase", outcome.phase},
                        {"character", outcome.character},
                        {"intensity", outcome.intensity},
                        {"rate_mm_h", outcome.rate_mm_h},
                        {"duration_fraction", outcome.duration_fraction},
                        {"accumulation_mm", outcome.accumulation_mm},
                        {"hail_max_diameter_cm", outcome.hail_max_diameter_cm},
                        {"lightning", lightning},
                        {"virga", virga},
                        {"updraft_m_s", outcome.updraft_m_s},
                        {"freezing_level_m", nullable_array(outcome.freezing_level_m)},
                        {"surface_wet_bulb_k", outcome.surface_wet_bulb_k},
                        {"precipitable_water_mm", outcome.precipitable_water_mm},
                        {"cloud_base_temperature_k", nullable_array(outcome.cloud_base_temperature_k)},
                        {"cloud_top_temperature_k", nullable_array(outcome.cloud_top_temperature_k)},
                        {"hazards", hazards},
                        {"precipitating_cells", outcome.precipitating_cells},
                        {"lightning_cells", outcome.lightning_cells},
                        {"hail_cells", outcome.hail_cells},
                        {"snow_cells", outcome.snow_cells},
                        {"max_rate_mm_h", outcome.max_rate_mm_h},
                        {"total_accumulation_mm", outcome.total_accumulation_mm},
                    });
                }
                if (severe) {
                    const auto sv = terraflux::weather::diagnose_severe_convection(current, config.grid, clouds->config, field, *severe);
                    Json capped = Json::array(), broken = Json::array(), possible = Json::array();
                    for (unsigned char v : sv.capped) capped.push_back(v != 0);
                    for (unsigned char v : sv.cap_broken) broken.push_back(v != 0);
                    for (unsigned char v : sv.tornado_possible) possible.push_back(v != 0);
                    severe_frames.push_back({
                        {"elapsed_seconds", current.elapsed_seconds},
                        {"low_level_shear_m_s", sv.low_level_shear_m_s},
                        {"deep_shear_m_s", sv.deep_shear_m_s},
                        {"storm_motion_east_m_s", sv.storm_motion_east_m_s},
                        {"storm_motion_north_m_s", sv.storm_motion_north_m_s},
                        {"helicity_m2_s2", sv.helicity_m2_s2},
                        {"significant_tornado_parameter", sv.significant_tornado_parameter},
                        {"heated_cape_j_kg", sv.heated_cape_j_kg},
                        {"heated_cin_j_kg", sv.heated_cin_j_kg},
                        {"cap_strength_j_kg", sv.cap_strength_j_kg},
                        {"lcl_height_m", nullable_array(sv.lcl_height_m)},
                        {"capped", capped},
                        {"cap_broken", broken},
                        {"tornado_risk", sv.tornado_risk},
                        {"tornado_possible", possible},
                        {"capped_cells", sv.capped_cells},
                        {"cap_broken_cells", sv.cap_broken_cells},
                        {"tornado_possible_cells", sv.tornado_possible_cells},
                        {"max_stp", sv.max_stp},
                    });
                }
            }
            if (orography) {
                const auto o = terraflux::weather::diagnose_orography(current, config);
                const std::size_t n = current.pressure_pa.size();
                std::vector<double> rate(n, 0.0), interval_mm(n, 0.0);
                const double hours = (current.elapsed_seconds - previous_orographic_time) / 3600.0;
                for (std::size_t i = 0; i < n; ++i) {
                    const double before = previous_orographic_precipitation.empty() ? 0.0 : previous_orographic_precipitation[i];
                    interval_mm[i] = current.orographic_precipitation_mm[i] - before;
                    rate[i] = hours > 0.0 ? interval_mm[i] / hours : 0.0;
                }
                previous_orographic_precipitation = current.orographic_precipitation_mm;
                previous_orographic_time = current.elapsed_seconds;
                Json cold_pool = Json::array(), foehn = Json::array();
                for (unsigned char v : o.cold_pool) cold_pool.push_back(v != 0);
                for (unsigned char v : o.foehn) foehn.push_back(v != 0);
                orography_frames.push_back({
                    {"elapsed_seconds", current.elapsed_seconds},
                    {"upslope_lifting_m_s", o.upslope_lifting_m_s},
                    {"obstacle_height_m", o.obstacle_height_m},
                    {"froude_number", nullable_array(o.froude_number)},
                    {"blocked_fraction", o.blocked_fraction},
                    {"valley_depth_m", o.valley_depth_m},
                    {"cross_barrier_wind_m_s", o.cross_barrier_wind_m_s},
                    {"along_barrier_wind_m_s", o.along_barrier_wind_m_s},
                    {"ridge_orientation_deg", nullable_array(o.ridge_orientation_deg)},
                    {"surface_pressure_pa", o.surface_pressure_pa},
                    {"cold_pool_k", o.cold_pool_k},
                    {"cold_pool", cold_pool},
                    {"foehn", foehn},
                    {"foehn_warming_k", o.foehn_warming_k},
                    {"orographic_precipitation_mm", interval_mm},
                    {"orographic_precipitation_rate_mm_h", rate},
                    {"cumulative_orographic_precipitation_mm", current.orographic_precipitation_mm},
                    {"cold_pool_cells", o.cold_pool_cells},
                    {"foehn_cells", o.foehn_cells},
                    {"surface_tendency_k_s", terraflux::weather::diurnal_surface_tendency_k_s(current.elapsed_seconds, config.orography)},
                });
            }
            if (storms) {
                const auto env = terraflux::weather::diagnose_storm_environment(current, config.grid, *surface, storms->config);
                const auto fields = terraflux::weather::storm_fields(storm_state, config.grid, *surface, storms->config);
                Json list = Json::array();
                for (const auto& s : storm_state.storms) list.push_back(storm_json(s));
                storm_frames.push_back({
                    {"elapsed_seconds", current.elapsed_seconds},
                    {"storms", list},
                    {"potential_intensity_m_s", env.potential_intensity_m_s},
                    {"genesis_potential_index", env.genesis_potential_index},
                    {"vertical_shear_m_s", env.vertical_shear_m_s},
                    {"relative_vorticity_s", env.relative_vorticity_s},
                    {"mid_level_relative_humidity", env.mid_level_relative_humidity},
                    {"outflow_temperature_k", env.outflow_temperature_k},
                    {"enthalpy_disequilibrium_j_kg", env.enthalpy_disequilibrium_j_kg},
                    {"storm_wind_speed_m_s", fields.wind_speed_m_s},
                    {"storm_pressure_anomaly_pa", fields.pressure_anomaly_pa},
                    {"storm_rain_rate_mm_h", fields.rain_rate_mm_h},
                    {"storm_surge_m", fields.surge_m},
                    {"storm_count", storm_state.storms.size()},
                });
            }
        };

        if (storms) {
            // Seeds enter at the start; storms then evolve against each interval's starting state.
            const auto env = terraflux::weather::diagnose_storm_environment(state, config.grid, *surface, storms->config);
            terraflux::weather::advance_storm_systems(storm_state, storms->seeds, state, config.grid, *surface, env, storms->config, state.elapsed_seconds, 0.0);
        }
        record(state);
        auto chunk = config;
        std::size_t remaining = config.time.steps;
        while (remaining > 0) {
            chunk.time.steps = std::min(snapshot_every, remaining);
            if (storms) {
                const auto env = terraflux::weather::diagnose_storm_environment(state, config.grid, *surface, storms->config);
                terraflux::weather::advance_storm_systems(storm_state, {}, state, config.grid, *surface, env, storms->config,
                                                          state.elapsed_seconds, static_cast<double>(chunk.time.steps) * config.time.timestep_seconds);
            }
            state = terraflux::weather::advance_coupled_wind(state, chunk);
            remaining -= chunk.time.steps;
            record(state);
        }

        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"coupled_atmosphere", {
                {"schema_version", "terraflux.weather.coupled_atmosphere/v1"},
                {"model", "coupled_wind_moist_layer_v1"},
                {"source_kind", "engine_output"},
                {"request", config_json(config, snapshot_every)},
                {"pressure_source", pressure_provenance},
                {"temperature_source", temperature_provenance},
                {"humidity_source", humidity_provenance},
                {"pressure_pa", state.pressure_pa},
                {"coriolis_parameter_s",
                 terraflux::weather::coriolis_parameter(config.grid, config.atmosphere)},
                {"specific_humidity_kg_kg_by_frame", humidity_by_frame},
                {"relative_humidity_pct_by_frame", relative_humidity_by_frame},
                {"air_density_kg_m3_by_frame", density_by_frame},
                {"supersaturated_cells_by_frame", supersaturated_by_frame},
                {"scalar_stability_number_by_frame", stability_by_frame},
            }},
        };
        if (column) {
            output["coupled_atmosphere"]["columns"] = {
                {"schema_version", "terraflux.weather.column_diagnostics/v1"},
                {"method", "hydrostatic_layers_convective_adjustment_surface_parcel"},
                {"config", column_config_json(*column)},
                {"column_mass_kg_m2", column_mass},
                {"top_pressure_pa", top_pressure},
                {"lcl_height_m_by_frame", lcl_by_frame},
                {"lfc_height_m_by_frame", lfc_by_frame},
                {"equilibrium_level_m_by_frame", el_by_frame},
                {"cape_j_kg_by_frame", cape_by_frame},
                {"cin_j_kg_by_frame", cin_by_frame},
                {"convective_potential_by_frame", potential_by_frame},
                {"condensed_water_kg_m2_by_frame", condensed_by_frame},
            };
        }
        if (clouds) {
            output["coupled_atmosphere"]["clouds"] = {
                {"schema_version", "terraflux.weather.cloud_formation/v1"},
                {"method", "frontal_convergence_overrunning_heated_parcel_convection"},
                {"source_kind", "engine_output"},
                {"config", cloud_config_json(*clouds)},
                {"frames", cloud_frames},
            };
        }
        if (orography) {
            output["terrain"] = {
                {"schema_version", "terraflux.terrain.grid/v1"},
                {"grid", grid_json(config.grid)},
                {"units", "m"},
                {"vertical_reference", "synthetic_local_datum"},
                {"source_id", orography->terrain_source.at("kind").get<std::string>()},
                {"values", config.orography.elevation_m},
            };
            output["coupled_atmosphere"]["orography"] = {
                {"schema_version", "terraflux.weather.orography/v1"},
                {"method", "terrain_following_adiabats_froude_blocking_valley_pooling"},
                {"source_kind", "engine_output"},
                {"config", orography_config_json(*orography)},
                {"frames", orography_frames},
            };
        }
        if (surface) {
            Json land = Json::array();
            for (unsigned char v : surface->land) land.push_back(static_cast<int>(v));
            output["coupled_atmosphere"]["surface"] = {
                {"schema_version", "terraflux.weather.surface/v1"},
                {"source", surface_provenance},
                {"land", land},
                {"sea_surface_temperature_k", surface->sea_surface_temperature_k},
            };
        }
        if (storms) {
            output["coupled_atmosphere"]["storms"] = {
                {"schema_version", "terraflux.weather.storm_systems/v1"},
                {"method", "mpi_gpi_rankine_fujiwhara_holland_kaplan_demaria"},
                {"source_kind", "engine_output"},
                {"config", storm_config_json(*storms)},
                {"events", events_json(storm_state.events)},
                {"frames", storm_frames},
            };
        }
        if (severe) {
            output["coupled_atmosphere"]["severe"] = {
                {"schema_version", "terraflux.weather.severe_convection/v1"},
                {"method", "capping_inversion_ekman_shear_bunkers_stp"},
                {"source_kind", "engine_output"},
                {"config", severe_config_json(*severe)},
                {"frames", severe_frames},
            };
        }
        if (precipitation) {
            output["coupled_atmosphere"]["precipitation"] = {
                {"schema_version", "terraflux.weather.precipitation_outcomes/v1"},
                {"method", "genus_condensation_rate_wet_bulb_phase_updraft_hail"},
                {"source_kind", "engine_output"},
                {"config", precipitation_config_json(*precipitation)},
                {"frames", precipitation_frames},
            };
        }

        std::ofstream output_file(output_path);
        if (!output_file) {
            throw std::runtime_error("cannot write output: " + output_path);
        }
        output_file << output.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
