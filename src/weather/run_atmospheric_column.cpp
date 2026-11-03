#include "terraflux/weather/atmospheric_column.hpp"

#include <nlohmann/json.hpp>

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
        throw std::invalid_argument(std::string("column request is missing field: ") + key);
    }
    return object.at(key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(std::string("column request field must be a non-empty string: ") + key);
    }
    return value.get<std::string>();
}

double required_number(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number()) {
        throw std::invalid_argument(std::string("column request field must be numeric: ") + key);
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::invalid_argument(std::string("column request field must be finite: ") + key);
    }
    return result;
}

double optional_number(const Json& object, const char* key, double fallback) {
    return object.is_object() && object.contains(key) ? required_number(object, key) : fallback;
}

std::size_t required_positive_size(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string("column request field must be a positive integer: ") + key);
    }
    const long long parsed = value.get<long long>();
    if (parsed <= 0 ||
        static_cast<unsigned long long>(parsed) >
            static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(std::string("column request field is outside the supported range: ") + key);
    }
    return static_cast<std::size_t>(parsed);
}

Json optional_level(const std::optional<double>& value) {
    return value ? Json(*value) : Json(nullptr);
}

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable << " <column-request.json> <output.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 2;
    }
    try {
        std::ifstream request_file(argv[1]);
        if (!request_file) {
            throw std::runtime_error(std::string("cannot open column request: ") + argv[1]);
        }
        Json request;
        try {
            request = Json::parse(request_file);
        } catch (const Json::exception& error) {
            throw std::invalid_argument(std::string("invalid column request JSON: ") + error.what());
        }
        if (required_string(request, "schema_version") != "terraflux.weather.column-run/v1") {
            throw std::invalid_argument("unsupported column run request schema");
        }
        const Json& surface = required(request, "surface");
        const double surface_pressure = required_number(surface, "pressure_pa");
        const double surface_temperature = required_number(surface, "air_temperature_k");
        const double surface_humidity = required_number(surface, "specific_humidity_kg_kg");

        const Json& column_json = required(request, "column");
        terraflux::weather::ColumnConfig defaults;
        terraflux::weather::ColumnConfig config;
        config.layer_count = required_positive_size(column_json, "layer_count");
        config.layer_thickness_m = required_number(column_json, "layer_thickness_m");
        config.environment_lapse_rate_k_m = required_number(column_json, "environment_lapse_rate_k_m");
        config.tropopause_height_m = optional_number(column_json, "tropopause_height_m", defaults.tropopause_height_m);
        config.humidity_scale_height_m = required_number(column_json, "humidity_scale_height_m");
        config.parcel_step_m = optional_number(column_json, "parcel_step_m", defaults.parcel_step_m);
        terraflux::weather::validate_column_config(config);

        auto column = terraflux::weather::build_hydrostatic_column(
            surface_pressure, surface_temperature, surface_humidity, config);
        const auto initial = column;
        const auto report = terraflux::weather::adjust_column_convection(column);
        const auto ascent = terraflux::weather::lift_surface_parcel(column, config);

        const auto layers_json = [](const terraflux::weather::AtmosphericColumn& c) {
            return Json{
                {"interface_height_m", c.interface_height_m},
                {"interface_pressure_pa", c.interface_pressure_pa},
                {"height_m", c.height_m},
                {"pressure_pa", c.pressure_pa},
                {"air_temperature_k", c.air_temperature_k},
                {"specific_humidity_kg_kg", c.specific_humidity_kg_kg},
                {"cloud_water_kg_kg", c.cloud_water_kg_kg},
                {"air_density_kg_m3", c.air_density_kg_m3},
                {"potential_temperature_k", c.potential_temperature_k},
                {"relative_humidity_fraction", c.relative_humidity_fraction},
                {"layer_mass_kg_m2", c.layer_mass_kg_m2},
            };
        };

        Json output = {
            {"schema_version", "terraflux.weather.column-profile/v1"},
            {"model", "hydrostatic_layers_convective_adjustment_surface_parcel_v1"},
            {"surface", {
                {"pressure_pa", surface_pressure},
                {"air_temperature_k", surface_temperature},
                {"specific_humidity_kg_kg", surface_humidity},
            }},
            {"config", {
                {"layer_count", config.layer_count},
                {"layer_thickness_m", config.layer_thickness_m},
                {"environment_lapse_rate_k_m", config.environment_lapse_rate_k_m},
                {"tropopause_height_m", config.tropopause_height_m},
                {"humidity_scale_height_m", config.humidity_scale_height_m},
                {"parcel_step_m", config.parcel_step_m},
            }},
            {"initial_layers", layers_json(initial)},
            {"adjusted_layers", layers_json(column)},
            {"convective_adjustment", {
                {"dry_mixing_events", report.dry_mixing_events},
                {"condensing_layers", report.condensing_layers},
                {"condensed_water_kg_m2", report.condensed_water_kg_m2},
                {"latent_heating_j_m2", report.latent_heating_j_m2},
                {"iterations", report.iterations},
            }},
            {"surface_parcel", {
                {"height_m", ascent.height_m},
                {"pressure_pa", ascent.pressure_pa},
                {"temperature_k", ascent.temperature_k},
                {"specific_humidity_kg_kg", ascent.specific_humidity_kg_kg},
                {"condensed_water_kg_kg", ascent.condensed_water_kg_kg},
                {"environment_temperature_k", ascent.environment_temperature_k},
                {"buoyancy_m_s2", ascent.buoyancy_m_s2},
                {"lcl_height_m", optional_level(ascent.lcl_height_m)},
                {"lfc_height_m", optional_level(ascent.lfc_height_m)},
                {"equilibrium_level_m", optional_level(ascent.equilibrium_level_m)},
                {"cape_j_kg", ascent.cape_j_kg},
                {"cin_j_kg", ascent.cin_j_kg},
                {"convective_potential", ascent.convective_potential},
            }},
        };
        std::ofstream output_file(argv[2]);
        if (!output_file) {
            throw std::runtime_error(std::string("cannot write output: ") + argv[2]);
        }
        output_file << output.dump(2) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
