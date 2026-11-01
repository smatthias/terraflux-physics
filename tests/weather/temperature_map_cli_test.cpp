#define main terraflux_run_temperature_map_cli_main
#include "../../src/weather/run_temperature_map.cpp"
#undef main

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path() /
                ("terraflux-temperature-map-cli-test-" +
                 std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int run_cli(const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    return terraflux_run_temperature_map_cli_main(
        static_cast<int>(argv.size()), argv.data());
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot read test fixture: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write test fixture: " + path.string());
    }
    output << contents;
    if (!output) {
        throw std::runtime_error("failed writing test fixture: " + path.string());
    }
}

void valid_request_writes_forecast_json(const std::filesystem::path& temporary) {
    const std::filesystem::path root(TERRAFLUX_SOURCE_DIR);
    const auto sidecar = root / "examples/temperature-map-sidecar.json";
    const auto raster = root / "examples/temperature-map.ppm";
    const auto request = root / "examples/temperature-run-request.json";
    const auto forecast_path = temporary / "forecast.json";

    require(run_cli({"terraflux-run-temperature-map",
                     sidecar.string(),
                     raster.string(),
                     request.string(),
                     forecast_path.string()}) == 0,
            "valid CLI request should succeed");

    std::ifstream forecast_file(forecast_path);
    require(static_cast<bool>(forecast_file),
            "successful CLI run must create its forecast output");
    const Json forecast = Json::parse(forecast_file);
    require(forecast.at("schema_version") == "terraflux.weather.forecast/v1",
            "forecast must use the documented v1 schema");
    require(forecast.at("elapsed_seconds") == 60.0,
            "forecast must include the configured elapsed time");
    require(forecast.at("grid").at("nx") == 4 &&
                forecast.at("grid").at("ny") == 4,
            "forecast must preserve the requested grid dimensions");

    const Json& fields = forecast.at("fields");
    const auto& temperatures = fields.at("air_temperature").at("values");
    const auto& east_wind = fields.at("wind_east").at("values");
    const auto& north_wind = fields.at("wind_north").at("values");
    require(fields.at("air_temperature").at("units") == "K",
            "temperature forecast units must be kelvin");
    require(temperatures.size() == 16 && east_wind.size() == 16 &&
                north_wind.size() == 16,
            "all forecast arrays must cover every grid cell");
    bool found_280_k = false;
    bool found_290_k = false;
    for (const auto& value : temperatures) {
        found_280_k = found_280_k || value.get<double>() == 280.0;
        found_290_k = found_290_k || value.get<double>() == 290.0;
    }
    require(found_280_k && found_290_k,
            "forecast must contain the map's expected 280 K and 290 K values");
}

void invalid_request_schema_is_rejected(const std::filesystem::path& temporary) {
    const std::filesystem::path root(TERRAFLUX_SOURCE_DIR);
    Json request = Json::parse(read_file(
        root / "examples/temperature-run-request.json"));
    request["schema_version"] = "terraflux.weather.temperature-run/v999";
    const auto request_path = temporary / "unsupported-schema.json";
    const auto output_path = temporary / "unsupported-schema-forecast.json";
    write_file(request_path, request.dump());

    require(run_cli({"terraflux-run-temperature-map",
                     (root / "examples/temperature-map-sidecar.json").string(),
                     (root / "examples/temperature-map.ppm").string(),
                     request_path.string(),
                     output_path.string()}) == 1,
            "unsupported request schema must fail");
    require(!std::filesystem::exists(output_path),
            "invalid request must not produce a forecast");
}

void incomplete_initial_state_is_rejected(const std::filesystem::path& temporary) {
    const std::filesystem::path root(TERRAFLUX_SOURCE_DIR);
    Json request = Json::parse(read_file(
        root / "examples/temperature-run-request.json"));
    request["initial_state"]["wind_north_m_s"].erase(0);
    const auto request_path = temporary / "incomplete-state.json";
    const auto output_path = temporary / "incomplete-state-forecast.json";
    write_file(request_path, request.dump());

    require(run_cli({"terraflux-run-temperature-map",
                     (root / "examples/temperature-map-sidecar.json").string(),
                     (root / "examples/temperature-map.ppm").string(),
                     request_path.string(),
                     output_path.string()}) == 1,
            "initial arrays that do not cover the grid must fail validation");
    require(!std::filesystem::exists(output_path),
            "invalid initial state must not produce a forecast");
}

}  // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        valid_request_writes_forecast_json(temporary.path());
        invalid_request_schema_is_rejected(temporary.path());
        incomplete_initial_state_is_rejected(temporary.path());
        std::cout << "Temperature map CLI tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Temperature map CLI test failed: " << error.what() << "\n";
        return 1;
    }
}
