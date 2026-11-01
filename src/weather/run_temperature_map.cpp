#include "terraflux/weather/observation_grid.hpp"
#include "terraflux/weather/temperature_map_decoder.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <nlohmann/json.hpp>

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

terraflux::weather::DryTransportConfig parse_config(const Json& request) {
    if (required_string(request, "schema_version") !=
        "terraflux.weather.temperature-run/v1") {
        throw std::invalid_argument("unsupported temperature run request schema");
    }
    terraflux::weather::DryTransportConfig config;
    config.start_time_utc = required_string(request, "start_time_utc");
    config.timestep_seconds = required_number(request, "timestep_seconds");
    config.steps = required_positive_size(request, "steps");
    config.horizontal_mixing_diffusivity_m2_s =
        required_number(request, "horizontal_mixing_diffusivity_m2_s");
    config.boundary_condition = required_string(request, "boundary_condition");

    const Json& grid = required(request, "grid");
    config.grid.nx = required_positive_size(grid, "nx");
    config.grid.ny = required_positive_size(grid, "ny");
    config.grid.origin_east_m = required_number(grid, "origin_east_m");
    config.grid.origin_north_m = required_number(grid, "origin_north_m");
    config.grid.spacing_east_m = required_number(grid, "spacing_east_m");
    config.grid.spacing_north_m = required_number(grid, "spacing_north_m");
    config.grid.geographic_anchor_longitude_deg =
        required_number(grid, "geographic_anchor_longitude_deg");
    config.grid.geographic_anchor_latitude_deg =
        required_number(grid, "geographic_anchor_latitude_deg");
    terraflux::weather::validate_dry_transport_config(config);
    return config;
}

terraflux::weather::DryTransportState parse_initial_state(const Json& request) {
    const Json& initial = required(request, "initial_state");
    terraflux::weather::DryTransportState state;
    state.elapsed_seconds = required_number(initial, "elapsed_seconds");
    state.air_temperature_k =
        required_number_array(initial, "air_temperature_k");
    state.wind_east_m_s = required_number_array(initial, "wind_east_m_s");
    state.wind_north_m_s = required_number_array(initial, "wind_north_m_s");
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

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <temperature-sidecar.json> <raster.png|ppm>"
                 " <run-request.json> <forecast.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        print_usage(argv[0]);
        return 2;
    }
    try {
        const std::string sidecar_path = argv[1];
        const std::string raster_path = argv[2];
        const std::string request_path = argv[3];
        const std::string output_path = argv[4];

        std::ifstream sidecar(sidecar_path);
        if (!sidecar) {
            throw std::runtime_error("cannot open temperature sidecar: " + sidecar_path);
        }
        const auto metadata =
            terraflux::weather::read_temperature_map_sidecar_json(
                sidecar, sidecar_path);

        std::ifstream raster(raster_path, std::ios::binary);
        if (!raster) {
            throw std::runtime_error("cannot open temperature raster: " + raster_path);
        }

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
        const auto initial = parse_initial_state(request);
        terraflux::weather::validate_dry_transport_state(initial, config);

        const auto observations =
            terraflux::weather::decode_temperature_map(raster, metadata);
        const auto updates =
            terraflux::weather::map_air_temperature_observations_to_updates(
                observations, config);
        const auto forecast =
            terraflux::weather::advance_dry_transport_with_temperature_observations(
                initial, config, updates);

        Json output = {
            {"schema_version", "terraflux.weather.forecast/v1"},
            {"start_time_utc", config.start_time_utc},
            {"elapsed_seconds", forecast.elapsed_seconds},
            {"grid", grid_json(config.grid)},
            {"fields", {
                {"air_temperature", {
                    {"units", "K"},
                    {"values", forecast.air_temperature_k},
                }},
                {"wind_east", {
                    {"units", "m/s"},
                    {"values", forecast.wind_east_m_s},
                }},
                {"wind_north", {
                    {"units", "m/s"},
                    {"values", forecast.wind_north_m_s},
                }},
            }},
        };

        std::ofstream output_file(output_path);
        if (!output_file) {
            throw std::runtime_error("cannot open forecast output: " + output_path);
        }
        output_file << output.dump(2) << "\n";
        if (!output_file) {
            throw std::runtime_error("failed to write forecast output");
        }
        std::cerr << "Wrote temperature forecast to " << output_path << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
