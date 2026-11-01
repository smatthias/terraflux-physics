#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

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

    Json source = {
        {"source_id", observation.source_id},
        {"source_kind", observation.source_kind},
        {"interpretation", "palette_lookup"},
    };
    if (!observation.product_id.empty()) {
        source["product_id"] = observation.product_id;
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

    return {
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

}  // namespace terraflux::weather
