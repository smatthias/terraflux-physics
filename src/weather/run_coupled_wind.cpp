#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/cloud_formation.hpp"
#include "terraflux/weather/precipitation_outcomes.hpp"
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
            }
        };

        record(state);
        auto chunk = config;
        std::size_t remaining = config.time.steps;
        while (remaining > 0) {
            chunk.time.steps = std::min(snapshot_every, remaining);
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
