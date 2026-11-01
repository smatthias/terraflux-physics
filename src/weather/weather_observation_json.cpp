#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraflux::weather {
namespace {

using Json = nlohmann::json;

const char* status_name(ObservationStatus status) {
    switch (status) {
        case ObservationStatus::decoded:
            return "decoded";
        case ObservationStatus::no_data:
            return "no_data";
        case ObservationStatus::ambiguous:
            return "ambiguous";
    }
    throw std::invalid_argument("observation has an unsupported status");
}

bool supported_source_kind(const std::string& kind) {
    return kind == "synthetic_fixture" || kind == "licensed_feed" ||
           kind == "public_dataset" || kind == "user_supplied";
}

bool supported_interpretation(const std::string& interpretation) {
    return interpretation == "palette_lookup" ||
           interpretation == "structured_grid_value" ||
           interpretation == "direct_measurement" ||
           interpretation == "contour_interpolation" ||
           interpretation == "derived_empirical_conversion" ||
           interpretation == "derived_temporal_integration" ||
           interpretation == "manual_annotation";
}

Json serialize_observation(const WeatherObservation& observation) {
    if (observation.source_id.empty() || !supported_source_kind(observation.source_kind)) {
        throw std::invalid_argument("observation requires a supported source_id and source_kind");
    }
    if (observation.valid_time_utc.empty() || observation.valid_time_utc.back() != 'Z') {
        throw std::invalid_argument("observation valid_time_utc must end in Z");
    }
    if (observation.variable.empty() || observation.units.empty() ||
        observation.location.crs.empty() || observation.vertical_reference.empty()) {
        throw std::invalid_argument("observation variable, units, CRS, and vertical reference are required");
    }
    if (!std::isfinite(observation.location.x) || !std::isfinite(observation.location.y)) {
        throw std::invalid_argument("observation coordinates must be finite");
    }
    if (observation.confidence &&
        (!std::isfinite(*observation.confidence) ||
         *observation.confidence < 0.0 || *observation.confidence > 1.0)) {
        throw std::invalid_argument("observation confidence must be finite and within 0..1");
    }

    Json value = nullptr;
    switch (observation.status) {
        case ObservationStatus::decoded:
            if (!observation.value || !std::isfinite(*observation.value)) {
                throw std::invalid_argument("decoded observations require a finite numeric value");
            }
            value = *observation.value;
            break;
        case ObservationStatus::no_data:
            if (observation.value) {
                throw std::invalid_argument("no_data observations must not have a numeric value");
            }
            break;
        case ObservationStatus::ambiguous:
            if (observation.value || observation.reason.empty()) {
                throw std::invalid_argument("ambiguous observations require a reason and no numeric value");
            }
            break;
    }

    if (!supported_interpretation(observation.interpretation)) {
        throw std::invalid_argument("observation interpretation is unsupported");
    }

    Json source = {
        {"source_id", observation.source_id},
        {"source_kind", observation.source_kind},
        {"interpretation", observation.interpretation},
    };
    if (!observation.product_id.empty()) {
        source["product_id"] = observation.product_id;
    }
    if (!observation.derivation.empty()) {
        source["derivation"] = observation.derivation;
    }
    if (!observation.sidecar.empty()) {
        source["sidecar"] = observation.sidecar;
    }

    Json quality = {{"status", status_name(observation.status)}};
    if (observation.confidence) {
        quality["confidence"] = *observation.confidence;
    }
    if (!observation.reason.empty()) {
        quality["reason"] = observation.reason;
    }

    Json record = {
        {"schema_version", "terraflux.weather.observation/v1"},
        {"source", std::move(source)},
        {"valid_time_utc", observation.valid_time_utc},
        {"variable", observation.variable},
        {"value", std::move(value)},
        {"units", observation.units},
        {"location", {
            {"crs", observation.location.crs},
            {"coordinates", {observation.location.x, observation.location.y}},
        }},
        {"vertical_reference", observation.vertical_reference},
        {"quality", std::move(quality)},
    };
    if (!observation.period_start_utc.empty()) {
        if (observation.period_start_utc.back() != 'Z') {
            throw std::invalid_argument("period_start_utc must end in Z");
        }
        record["period_start_utc"] = observation.period_start_utc;
    }
    return record;
}


const Json& required_member(const Json& object, const char* name) {
    if (!object.is_object() || !object.contains(name)) {
        throw std::invalid_argument(std::string("missing canonical observation field: ") + name);
    }
    return object.at(name);
}

std::string required_string(const Json& object, const char* name) {
    const Json& value = required_member(object, name);
    if (!value.is_string()) {
        throw std::invalid_argument(std::string("canonical observation field must be a string: ") + name);
    }
    return value.get<std::string>();
}

std::string optional_string(const Json& object, const char* name) {
    if (!object.is_object() || !object.contains(name)) {
        return {};
    }
    if (!object.at(name).is_string()) {
        throw std::invalid_argument(std::string("canonical observation field must be a string: ") + name);
    }
    return object.at(name).get<std::string>();
}

double required_finite_number(const Json& value, const char* name) {
    if (!value.is_number()) {
        throw std::invalid_argument(std::string("canonical observation field must be numeric: ") + name);
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        throw std::invalid_argument(std::string("canonical observation field must be finite: ") + name);
    }
    return number;
}

}  // namespace

void write_weather_observations_json(
    std::ostream& output,
    const std::vector<WeatherObservation>& observations) {
    output << "[";
    for (std::size_t index = 0; index < observations.size(); ++index) {
        const Json record = serialize_observation(observations[index]);
        if (index != 0) {
            output << ",";
        }
        output << "\n" << record.dump(2);
    }
    if (!observations.empty()) {
        output << "\n";
    }
    output << "]\n";
    if (!output) {
        throw std::runtime_error("failed to write weather observations JSON");
    }
}


std::vector<WeatherObservation> read_weather_observations_json(
    std::istream& input) {
    Json document;
    try {
        document = Json::parse(input);
    } catch (const Json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid canonical weather observation JSON: ") + error.what());
    }
    if (!document.is_array()) {
        throw std::invalid_argument(
            "canonical weather observation document must be a JSON array");
    }

    std::vector<WeatherObservation> observations;
    observations.reserve(document.size());
    for (const Json& record : document) {
        if (!record.is_object() ||
            required_string(record, "schema_version") !=
                "terraflux.weather.observation/v1") {
            throw std::invalid_argument(
                "unsupported canonical weather observation schema version");
        }

        const Json& source = required_member(record, "source");
        if (!source.is_object() ||
            required_string(source, "interpretation").empty()) {
            throw std::invalid_argument(
                "canonical observation source metadata is invalid");
        }
        WeatherObservation observation;
        observation.source_id = required_string(source, "source_id");
        observation.source_kind = required_string(source, "source_kind");
        observation.interpretation = required_string(source, "interpretation");
        observation.product_id = optional_string(source, "product_id");
        observation.derivation = optional_string(source, "derivation");
        observation.sidecar = optional_string(source, "sidecar");
        if (observation.source_id.empty() ||
            !supported_source_kind(observation.source_kind) ||
            !supported_interpretation(observation.interpretation)) {
            throw std::invalid_argument(
                "canonical observation source identity or kind is invalid");
        }

        observation.valid_time_utc = required_string(record, "valid_time_utc");
        observation.period_start_utc = optional_string(record, "period_start_utc");
        if (!observation.period_start_utc.empty() &&
            observation.period_start_utc.back() != 'Z') {
            throw std::invalid_argument("canonical observation period_start_utc must end in Z");
        }
        if (observation.valid_time_utc.empty() ||
            observation.valid_time_utc.back() != 'Z') {
            throw std::invalid_argument(
                "canonical observation valid_time_utc must end in Z");
        }
        observation.variable = required_string(record, "variable");
        observation.units = required_string(record, "units");
        observation.vertical_reference =
            required_string(record, "vertical_reference");
        if (observation.variable.empty() || observation.units.empty() ||
            observation.vertical_reference.empty()) {
            throw std::invalid_argument(
                "canonical observation variable, units, and vertical reference are required");
        }

        const Json& location = required_member(record, "location");
        if (!location.is_object()) {
            throw std::invalid_argument("canonical observation location must be an object");
        }
        observation.location.crs = required_string(location, "crs");
        const Json& coordinates = required_member(location, "coordinates");
        if (observation.location.crs.empty() || !coordinates.is_array() ||
            coordinates.size() != 2) {
            throw std::invalid_argument(
                "canonical observation coordinates must contain an x/y pair and CRS");
        }
        observation.location.x =
            required_finite_number(coordinates[0], "location.coordinates[0]");
        observation.location.y =
            required_finite_number(coordinates[1], "location.coordinates[1]");

        const Json& quality = required_member(record, "quality");
        if (!quality.is_object()) {
            throw std::invalid_argument("canonical observation quality must be an object");
        }
        const std::string status = required_string(quality, "status");
        if (status == "decoded") {
            observation.status = ObservationStatus::decoded;
        } else if (status == "no_data") {
            observation.status = ObservationStatus::no_data;
        } else if (status == "ambiguous") {
            observation.status = ObservationStatus::ambiguous;
        } else {
            throw std::invalid_argument(
                "canonical observation has an unsupported quality status");
        }
        observation.reason = optional_string(quality, "reason");
        if (quality.contains("confidence")) {
            const double confidence =
                required_finite_number(quality.at("confidence"), "quality.confidence");
            if (confidence < 0.0 || confidence > 1.0) {
                throw std::invalid_argument(
                    "canonical observation confidence must be within 0..1");
            }
            observation.confidence = confidence;
        }

        const Json& value = required_member(record, "value");
        if (observation.status == ObservationStatus::decoded) {
            observation.value = required_finite_number(value, "value");
        } else {
            if (!value.is_null()) {
                throw std::invalid_argument(
                    "no-data and ambiguous canonical observations must have null values");
            }
            if (observation.status == ObservationStatus::ambiguous &&
                observation.reason.empty()) {
                throw std::invalid_argument(
                    "ambiguous canonical observations require a reason");
            }
        }
        observations.push_back(std::move(observation));
    }
    return observations;
}

}  // namespace terraflux::weather
