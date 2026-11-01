#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include "decoder_internal.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraflux::weather {
namespace {

using Json = nlohmann::json;

std::string read_limited(std::istream& input, std::size_t max_bytes) {
    std::string contents;
    contents.reserve(4096);
    char byte = 0;
    while (input.get(byte)) {
        if (contents.size() == max_bytes) {
            throw std::runtime_error("map sidecar exceeds the configured size limit");
        }
        contents.push_back(byte);
    }
    if (!input.eof()) {
        throw std::runtime_error("failed while reading map sidecar");
    }
    return contents;
}

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw std::invalid_argument(std::string("map sidecar is missing field: ") + key);
    }
    return object.at(key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(std::string("map sidecar field must be a non-empty string: ") + key);
    }
    return value.get<std::string>();
}

std::size_t positive_size(const Json& value, const char* field) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string("map sidecar field must be a positive integer: ") + field);
    }
    try {
        const long long parsed = value.get<long long>();
        if (parsed <= 0 || static_cast<unsigned long long>(parsed) >
                               static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
            throw std::invalid_argument(std::string("map sidecar field is out of range: ") + field);
        }
        return static_cast<std::size_t>(parsed);
    } catch (const nlohmann::json::exception&) {
        throw std::invalid_argument(std::string("map sidecar field is out of range: ") + field);
    }
}

double finite_number(const Json& value, const char* field) {
    if (!value.is_number()) {
        throw std::invalid_argument(std::string("map sidecar field must be numeric: ") + field);
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed)) {
        throw std::invalid_argument(std::string("map sidecar field must be finite: ") + field);
    }
    return parsed;
}

std::uint8_t channel_value(const Json& value, const char* field) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string("palette channel must be an integer: ") + field);
    }
    try {
        const long long parsed = value.get<long long>();
        if (parsed < 0 || parsed > 255) {
            throw std::invalid_argument(std::string("palette channel is outside 0..255: ") + field);
        }
        return static_cast<std::uint8_t>(parsed);
    } catch (const nlohmann::json::exception&) {
        throw std::invalid_argument(std::string("palette channel is outside 0..255: ") + field);
    }
}

}  // namespace

RadarMapMetadata read_radar_map_sidecar_json(
    std::istream& sidecar_json,
    std::string sidecar_name) {
    constexpr std::size_t max_sidecar_bytes = 1024U * 1024U;
    const std::string encoded = read_limited(sidecar_json, max_sidecar_bytes);

    Json root;
    try {
        root = Json::parse(encoded);
    } catch (const Json::exception& error) {
        throw std::invalid_argument(std::string("invalid map sidecar JSON: ") + error.what());
    }
    if (!root.is_object()) {
        throw std::invalid_argument("map sidecar JSON root must be an object");
    }
    if (required_string(root, "schema_version") != "terraflux.weather.map-sidecar/v1") {
        throw std::invalid_argument("unsupported map sidecar schema_version");
    }

    RadarMapMetadata metadata;
    metadata.sidecar = std::move(sidecar_name);
    metadata.valid_time_utc = required_string(root, "valid_time_utc");

    const Json& source = required(root, "source");
    metadata.source_id = required_string(source, "source_id");
    metadata.source_kind = required_string(source, "source_kind");
    if (source.contains("product_id")) {
        metadata.product_id = required_string(source, "product_id");
    }

    const Json& raster = required(root, "raster");
    metadata.raster_file = required_string(raster, "file");
    metadata.raster_width = positive_size(required(raster, "width"), "raster.width");
    metadata.raster_height = positive_size(required(raster, "height"), "raster.height");
    const std::string format = required_string(raster, "format");
    if (format == "ppm-p3") {
        metadata.raster_format = RasterFormat::ppm_p3;
    } else if (format == "png") {
        metadata.raster_format = RasterFormat::png;
    } else {
        throw std::invalid_argument("unsupported map sidecar raster format");
    }
    const std::string encoding = required_string(raster, "color_encoding");
    if (encoding != "rgb" && encoding != "indexed") {
        throw std::invalid_argument("unsupported map sidecar color_encoding");
    }

    const Json& georeferencing = required(root, "georeferencing");
    metadata.crs = required_string(georeferencing, "crs");
    const Json& axis_order = required(georeferencing, "axis_order");
    if (!axis_order.is_array() || axis_order.size() != 2 ||
        axis_order[0] != "longitude" || axis_order[1] != "latitude") {
        throw std::invalid_argument("v1 map sidecar axis_order must be longitude, latitude");
    }
    if (required_string(georeferencing, "transform_convention") != "pixel_corner_affine") {
        throw std::invalid_argument("unsupported map sidecar transform_convention");
    }
    const Json& transform = required(georeferencing, "pixel_to_world");
    if (!transform.is_array() || transform.size() != metadata.pixel_to_world.size()) {
        throw std::invalid_argument("pixel_to_world must contain exactly six coefficients");
    }
    for (std::size_t i = 0; i < metadata.pixel_to_world.size(); ++i) {
        metadata.pixel_to_world[i] = finite_number(transform[i], "georeferencing.pixel_to_world");
    }

    const Json& observation = required(root, "observation");
    if (required_string(observation, "variable") != "radar_reflectivity" ||
        required_string(observation, "units") != "dBZ") {
        throw std::invalid_argument("v1 map sidecar must describe radar_reflectivity in dBZ");
    }
    metadata.vertical_reference = required_string(observation, "vertical_reference");

    const Json& palette = required(root, "palette");
    if (!palette.is_array() || palette.empty()) {
        throw std::invalid_argument("map sidecar palette must be a non-empty array");
    }
    metadata.palette.reserve(palette.size());
    for (const Json& item : palette) {
        PaletteEntry entry;
        const Json& rgb = required(item, "rgb");
        if (!rgb.is_array() || rgb.size() != 3) {
            throw std::invalid_argument("palette rgb must contain exactly three channels");
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            entry.rgb[channel] = channel_value(rgb[channel], "palette.rgb");
        }
        if (item.contains("alpha")) {
            entry.alpha = channel_value(item.at("alpha"), "palette.alpha");
        }

        const std::string status = required_string(item, "status");
        if (status == "decoded") {
            entry.status = ObservationStatus::decoded;
            entry.value_dbz = finite_number(required(item, "value"), "palette.value");
        } else if (status == "no_data") {
            entry.status = ObservationStatus::no_data;
            if (item.contains("value")) {
                throw std::invalid_argument("no_data palette entries must not include value");
            }
        } else if (status == "ambiguous") {
            entry.status = ObservationStatus::ambiguous;
            if (item.contains("value")) {
                throw std::invalid_argument("ambiguous palette entries must not include value");
            }
            entry.reason = required_string(item, "reason");
        } else {
            throw std::invalid_argument("unsupported palette status");
        }
        metadata.palette.push_back(std::move(entry));
    }

    detail::validate_metadata(metadata, DecodeLimits{});
    return metadata;
}

}  // namespace terraflux::weather
