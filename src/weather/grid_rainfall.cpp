#include "terraflux/weather/precipitation_grid.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    using Json = nlohmann::json;
    using namespace terraflux::weather;
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <forecast.json> <accumulation.json> <gridded-rainfall.json>\n";
        return 2;
    }
    try {
        std::ifstream forecast_input(argv[1]), observations_input(argv[2]);
        if (!forecast_input || !observations_input) throw std::runtime_error("cannot open input files");
        const auto forecast = Json::parse(forecast_input);
        if (forecast.at("schema_version") != "terraflux.weather.forecast/v1")
            throw std::invalid_argument("unsupported forecast schema");
        const auto& grid = forecast.at("grid");
        if (grid.at("coordinate_system") != kTransportGridCoordinateSystem ||
            grid.at("array_order") != "row_major_y_then_x")
            throw std::invalid_argument("unsupported forecast grid coordinates or order");
        DryTransportConfig config;
        config.start_time_utc = forecast.at("start_time_utc").get<std::string>();
        config.steps = 1;
        config.timestep_seconds = 1;
        auto& g = config.grid;
        const auto dimension = [&](const char* key) {
            const auto& value = grid.at(key);
            if (!value.is_number_integer() || value.get<long long>() < 2 ||
                value.get<long long>() > 1000000)
                throw std::invalid_argument("grid dimensions must be integers between 2 and 1000000");
            return value.get<std::size_t>();
        };
        g.nx = dimension("nx"); g.ny = dimension("ny");
        if (g.nx > 1000000 / g.ny) throw std::invalid_argument("grid exceeds one million cells");
        g.origin_east_m = grid.at("origin_east_m").get<double>();
        g.origin_north_m = grid.at("origin_north_m").get<double>();
        g.spacing_east_m = grid.at("spacing_east_m").get<double>();
        g.spacing_north_m = grid.at("spacing_north_m").get<double>();
        g.geographic_anchor_longitude_deg = grid.at("geographic_anchor_longitude_deg").get<double>();
        g.geographic_anchor_latitude_deg = grid.at("geographic_anchor_latitude_deg").get<double>();
        const auto observations = read_weather_observations_json(observations_input);
        const auto field = map_precipitation_accumulation_to_grid(
            observations, config, forecast.at("elapsed_seconds").get<double>());
        Json values = Json::array(), quality = Json::array();
        for (const auto& cell : field.cells) {
            values.push_back(cell.value ? Json(*cell.value) : Json(nullptr));
            quality.push_back(cell.status == ObservationStatus::decoded ? "decoded" :
                cell.status == ObservationStatus::ambiguous ? "ambiguous" : "no_data");
        }
        const auto& reference = observations.front();
        Json layer = {{"period_start_utc", config.start_time_utc},
                      {"valid_time_utc", reference.valid_time_utc}, {"units", "mm"},
                      {"method", "trapezoidal_rate_integration"},
                      {"source_id", reference.source_id}, {"values", values}, {"quality", quality}};
        if (!reference.derivation.empty()) layer["derivation"] = reference.derivation;
        Json output = {{"schema_version", "terraflux.weather.gridded_accumulation/v1"},
                       {"grid", grid}, {"precipitation_accumulation", layer}};
        std::ofstream target(argv[3]);
        if (!target) throw std::runtime_error("cannot open output");
        target << output.dump(2) << '\n';
        target.close();
        if (!target) throw std::runtime_error("failed to write output");
        std::cerr << "Gridded " << observations.size() << " observations; outside domain: "
                  << field.outside_domain_count << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n'; return 1;
    }
}
