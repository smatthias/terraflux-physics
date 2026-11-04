#include "terraflux/weather/pressure_wind.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw std::invalid_argument(
            std::string("run request is missing field: ") + key);
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
        throw std::invalid_argument(
            std::string("run request field must be numeric: ") + key);
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::invalid_argument(
            std::string("run request field must be finite: ") + key);
    }
    return result;
}

double optional_number(const Json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key)) {
        return fallback;
    }
    return required_number(object, key);
}

std::string optional_string(
    const Json& object, const char* key, const std::string& fallback) {
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
            static_cast<unsigned long long>(
                std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(
            std::string("run request field is outside the supported range: ") + key);
    }
    return static_cast<std::size_t>(parsed);
}

std::vector<double> required_number_array(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_array()) {
        throw std::invalid_argument(
            std::string("run request field must be an array: ") + key);
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

terraflux::weather::TransportGrid2D parse_grid(const Json& grid) {
    terraflux::weather::TransportGrid2D result;
    result.nx = required_positive_size(grid, "nx");
    result.ny = required_positive_size(grid, "ny");
    result.origin_east_m = required_number(grid, "origin_east_m");
    result.origin_north_m = required_number(grid, "origin_north_m");
    result.spacing_east_m = required_number(grid, "spacing_east_m");
    result.spacing_north_m = required_number(grid, "spacing_north_m");
    result.geographic_anchor_longitude_deg =
        required_number(grid, "geographic_anchor_longitude_deg");
    result.geographic_anchor_latitude_deg =
        required_number(grid, "geographic_anchor_latitude_deg");
    return result;
}

terraflux::weather::PressureWindConfig parse_config(const Json& request) {
    if (required_string(request, "schema_version") !=
        "terraflux.weather.pressure-wind-run/v1") {
        throw std::invalid_argument("unsupported pressure wind run request schema");
    }
    terraflux::weather::PressureWindConfig config;

    const Json& time = required(request, "time");
    config.time.start_time_utc = required_string(time, "start_time_utc");
    config.time.timestep_seconds = required_number(time, "timestep_seconds");
    config.time.steps = required_positive_size(time, "steps");

    config.grid = parse_grid(required(request, "grid"));

    const Json& atmosphere = required(request, "atmosphere");
    terraflux::weather::AtmosphereConfig defaults;
    config.atmosphere.air_density_kg_m3 =
        required_number(atmosphere, "air_density_kg_m3");
    config.atmosphere.surface_friction_coefficient_s =
        required_number(atmosphere, "surface_friction_coefficient_s");
    config.atmosphere.coriolis_mode = required_string(atmosphere, "coriolis_mode");
    if (config.atmosphere.coriolis_mode == "prescribed") {
        config.atmosphere.coriolis_parameter_s =
            required_number(atmosphere, "coriolis_parameter_s");
    } else {
        config.atmosphere.coriolis_parameter_s =
            optional_number(atmosphere, "coriolis_parameter_s", 0.0);
    }
    config.atmosphere.earth_angular_speed_rad_s = optional_number(
        atmosphere, "earth_angular_speed_rad_s", defaults.earth_angular_speed_rad_s);
    config.atmosphere.earth_radius_m =
        optional_number(atmosphere, "earth_radius_m", defaults.earth_radius_m);

    config.pressure_gradient_boundary =
        optional_string(request, "pressure_gradient_boundary", "one_sided");
    terraflux::weather::validate_pressure_wind_config(config);
    return config;
}

// Pressure comes either from explicit cell values or from idealized centers.
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
    systems.background_pressure_pa =
        required_number(systems_json, "background_pressure_pa");
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

terraflux::weather::PressureWindState parse_initial_state(
    const Json& request,
    std::vector<double> pressure) {
    terraflux::weather::PressureWindState state;
    state.pressure_pa = std::move(pressure);
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
    return state;
}

Json grid_json(const terraflux::weather::TransportGrid2D& grid) {
    return {
        {"nx", grid.nx},
        {"ny", grid.ny},
        {"origin_east_m", grid.origin_east_m},
        {"origin_north_m", grid.origin_north_m},
        {"spacing_east_m", grid.spacing_east_m},
        {"spacing_north_m", grid.spacing_north_m},
        {"geographic_anchor_longitude_deg",
         grid.geographic_anchor_longitude_deg},
        {"geographic_anchor_latitude_deg",
         grid.geographic_anchor_latitude_deg},
        {"coordinate_system", "local_azimuthal_equidistant_m"},
        {"array_order", "row_major_y_then_x"},
    };
}

Json atmosphere_json(const terraflux::weather::AtmosphereConfig& atmosphere) {
    return {
        {"air_density_kg_m3", atmosphere.air_density_kg_m3},
        {"surface_friction_coefficient_s", atmosphere.surface_friction_coefficient_s},
        {"coriolis_mode", atmosphere.coriolis_mode},
        {"coriolis_parameter_s", atmosphere.coriolis_parameter_s},
        {"earth_angular_speed_rad_s", atmosphere.earth_angular_speed_rad_s},
        {"earth_radius_m", atmosphere.earth_radius_m},
    };
}

Json wind_json(
    const std::vector<double>& east,
    const std::vector<double>& north) {
    return {
        {"wind_east", {{"units", "m/s"}, {"values", east}}},
        {"wind_north", {{"units", "m/s"}, {"values", north}}},
    };
}

// Analytic balances are reported only where they are defined, so a run
// without rotation does not fail merely because geostrophy is undefined.
Json optional_balance_json(
    const std::vector<double>& pressure,
    const terraflux::weather::PressureWindConfig& config,
    bool geostrophic) {
    try {
        const auto wind = geostrophic
            ? terraflux::weather::geostrophic_wind(pressure, config)
            : terraflux::weather::balanced_wind(pressure, config);
        return wind_json(wind.east_m_s, wind.north_m_s);
    } catch (const std::invalid_argument& error) {
        return {{"undefined", error.what()}};
    }
}

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <run-request.json> <output.json>\n";
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
            throw std::invalid_argument(
                std::string("invalid run request JSON: ") + error.what());
        }
        if (!request.is_object()) {
            throw std::invalid_argument("run request root must be a JSON object");
        }

        const auto config = parse_config(request);
        Json pressure_provenance;
        auto pressure = parse_pressure(request, config.grid, pressure_provenance);
        const auto initial = parse_initial_state(request, std::move(pressure));
        terraflux::weather::validate_pressure_wind_state(initial, config);

        // Optional intermediate snapshots every N steps; the final state is
        // always included as the last snapshot.
        std::size_t snapshot_every = config.time.steps;
        if (request.contains("snapshot_every_steps")) {
            snapshot_every = required_positive_size(request, "snapshot_every_steps");
            if (snapshot_every > config.time.steps) {
                throw std::invalid_argument(
                    "snapshot_every_steps must not exceed the configured step count");
            }
        }

        Json snapshots = Json::array();
        auto state = initial;
        auto chunk = config;
        std::size_t remaining = config.time.steps;
        while (remaining > 0) {
            chunk.time.steps = std::min(snapshot_every, remaining);
            state = terraflux::weather::advance_pressure_wind(state, chunk);
            remaining -= chunk.time.steps;
            Json snapshot = {{"elapsed_seconds", state.elapsed_seconds}};
            snapshot["fields"] = wind_json(state.wind_east_m_s, state.wind_north_m_s);
            snapshots.push_back(snapshot);
        }

        Json diagnostics = {
            {"coriolis_parameter_s",
             terraflux::weather::coriolis_parameter(config.grid, config.atmosphere)},
            {"geostrophic_wind", optional_balance_json(initial.pressure_pa, config, true)},
            {"balanced_wind", optional_balance_json(initial.pressure_pa, config, false)},
        };

        Json output = {
            {"schema_version", "terraflux.weather.pressure-wind-forecast/v1"},
            {"model", "pressure_gradient_coriolis_friction_v1"},
            {"start_time_utc", config.time.start_time_utc},
            {"elapsed_seconds", state.elapsed_seconds},
            {"time", {
                {"start_time_utc", config.time.start_time_utc},
                {"timestep_seconds", config.time.timestep_seconds},
                {"steps", config.time.steps},
            }},
            {"grid", grid_json(config.grid)},
            {"atmosphere", atmosphere_json(config.atmosphere)},
            {"pressure_gradient_boundary", config.pressure_gradient_boundary},
            {"pressure", {
                {"units", "Pa"},
                {"source", pressure_provenance},
                {"values", initial.pressure_pa},
            }},
            {"fields", wind_json(state.wind_east_m_s, state.wind_north_m_s)},
            {"snapshots", snapshots},
            {"diagnostics", diagnostics},
        };

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
