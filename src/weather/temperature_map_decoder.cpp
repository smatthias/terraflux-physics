#include "terraflux/weather/temperature_map_decoder.hpp"

#include <nlohmann/json.hpp>
#include <png.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraflux::weather {
namespace {

using Json = nlohmann::json;
using Rgba8 = std::array<std::uint8_t, 4>;

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
        throw std::invalid_argument(std::string("temperature map sidecar is missing field: ") + key);
    }
    return object.at(key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        throw std::invalid_argument(
            std::string("temperature map sidecar field must be a non-empty string: ") + key);
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
    } catch (const Json::exception&) {
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
    } catch (const Json::exception&) {
        throw std::invalid_argument(std::string("palette channel is outside 0..255: ") + field);
    }
}

std::uint32_t color_key(const Rgba8& color) {
    return (static_cast<std::uint32_t>(color[0]) << 24U) |
           (static_cast<std::uint32_t>(color[1]) << 16U) |
           (static_cast<std::uint32_t>(color[2]) << 8U) |
           static_cast<std::uint32_t>(color[3]);
}

std::uint32_t color_key(const TemperaturePaletteEntry& entry) {
    return color_key({entry.rgb[0], entry.rgb[1], entry.rgb[2], entry.alpha});
}

void validate_metadata(
    const TemperatureMapMetadata& metadata,
    const DecodeLimits& limits) {
    if (metadata.source_id.empty()) {
        throw std::invalid_argument("temperature map source_id is required");
    }
    if (metadata.source_kind != "synthetic_fixture" &&
        metadata.source_kind != "licensed_feed" &&
        metadata.source_kind != "public_dataset" &&
        metadata.source_kind != "user_supplied") {
        throw std::invalid_argument("unsupported temperature map source_kind");
    }
    if (metadata.valid_time_utc.empty() || metadata.valid_time_utc.back() != 'Z') {
        throw std::invalid_argument("temperature map valid_time_utc must end in Z");
    }
    if (metadata.raster_width == 0 || metadata.raster_height == 0 ||
        limits.max_pixels == 0 || metadata.raster_height > limits.max_pixels ||
        metadata.raster_width > limits.max_pixels / metadata.raster_height) {
        throw std::invalid_argument("temperature raster dimensions exceed the pixel limit");
    }
    if (metadata.crs.empty() || metadata.vertical_reference.empty()) {
        throw std::invalid_argument("temperature map CRS and vertical reference are required");
    }
    for (double coefficient : metadata.pixel_to_world) {
        if (!std::isfinite(coefficient)) {
            throw std::invalid_argument("temperature map transform must be finite");
        }
    }
    if (metadata.palette.empty()) {
        throw std::invalid_argument("temperature map palette is required");
    }

    std::unordered_map<std::uint32_t, bool> seen;
    for (const TemperaturePaletteEntry& entry : metadata.palette) {
        if (!seen.emplace(color_key(entry), true).second) {
            throw std::invalid_argument("temperature map palette has duplicate RGBA colors");
        }
        if (entry.status == ObservationStatus::decoded) {
            if (!entry.value_kelvin || !std::isfinite(*entry.value_kelvin) ||
                *entry.value_kelvin <= 0.0 || !entry.reason.empty()) {
                throw std::invalid_argument(
                    "decoded temperature palette colors require a finite value above 0 K");
            }
        } else if (entry.status == ObservationStatus::no_data) {
            if (entry.value_kelvin || !entry.reason.empty()) {
                throw std::invalid_argument(
                    "no-data temperature palette colors cannot have a value or reason");
            }
        } else if (entry.status == ObservationStatus::ambiguous) {
            if (entry.value_kelvin || entry.reason.empty()) {
                throw std::invalid_argument(
                    "ambiguous temperature palette colors require a reason and no value");
            }
        } else {
            throw std::invalid_argument("temperature palette has an unsupported status");
        }
    }
}

std::string next_ppm_token(std::istream& input) {
    while (true) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof()) {
            return {};
        }
        const unsigned char ch = static_cast<unsigned char>(next);
        if (std::isspace(ch)) {
            input.get();
            continue;
        }
        if (ch == '#') {
            std::string ignored;
            std::getline(input, ignored);
            continue;
        }
        break;
    }
    std::string token;
    while (true) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof()) {
            break;
        }
        const unsigned char ch = static_cast<unsigned char>(next);
        if (std::isspace(ch) || ch == '#') {
            break;
        }
        token.push_back(static_cast<char>(input.get()));
    }
    return token;
}

long long parse_ppm_integer(const std::string& token, const char* field) {
    if (token.empty()) {
        throw std::runtime_error(std::string("missing PPM ") + field);
    }
    std::size_t parsed = 0;
    long long value = 0;
    try {
        value = std::stoll(token, &parsed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid PPM ") + field);
    }
    if (parsed != token.size()) {
        throw std::runtime_error(std::string("invalid PPM ") + field);
    }
    return value;
}

std::vector<Rgba8> read_ppm_p3(
    std::istream& raster,
    const TemperatureMapMetadata& metadata,
    const DecodeLimits& limits) {
    validate_metadata(metadata, limits);
    if (metadata.raster_format != RasterFormat::ppm_p3) {
        throw std::invalid_argument("temperature sidecar raster format is not ppm-p3");
    }
    if (next_ppm_token(raster) != "P3") {
        throw std::runtime_error("temperature raster is not ASCII PPM P3");
    }
    const long long width = parse_ppm_integer(next_ppm_token(raster), "width");
    const long long height = parse_ppm_integer(next_ppm_token(raster), "height");
    const long long max_value = parse_ppm_integer(next_ppm_token(raster), "maxval");
    if (width <= 0 || height <= 0 || max_value <= 0 || max_value > 65535 ||
        static_cast<std::size_t>(width) != metadata.raster_width ||
        static_cast<std::size_t>(height) != metadata.raster_height) {
        throw std::runtime_error("PPM dimensions or maxval are invalid for temperature sidecar");
    }

    std::vector<Rgba8> pixels;
    pixels.reserve(metadata.raster_width * metadata.raster_height);
    for (std::size_t pixel = 0;
         pixel < metadata.raster_width * metadata.raster_height;
         ++pixel) {
        Rgba8 rgba{0, 0, 0, 255};
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const long long sample =
                parse_ppm_integer(next_ppm_token(raster), "sample");
            if (sample < 0 || sample > max_value) {
                throw std::runtime_error("PPM sample is outside the declared maxval");
            }
            rgba[channel] = static_cast<std::uint8_t>(
                (static_cast<unsigned long long>(sample) * 255ULL +
                 static_cast<unsigned long long>(max_value / 2)) /
                static_cast<unsigned long long>(max_value));
        }
        pixels.push_back(rgba);
    }
    if (!next_ppm_token(raster).empty()) {
        throw std::runtime_error("PPM raster contains unexpected trailing samples");
    }
    return pixels;
}

std::vector<Rgba8> read_png(
    std::istream& raster,
    const TemperatureMapMetadata& metadata,
    const DecodeLimits& limits) {
    validate_metadata(metadata, limits);
    if (metadata.raster_format != RasterFormat::png) {
        throw std::invalid_argument("temperature sidecar raster format is not png");
    }
    std::vector<std::uint8_t> encoded;
    encoded.reserve(64U * 1024U);
    char byte = 0;
    while (raster.get(byte)) {
        if (encoded.size() == limits.max_encoded_bytes) {
            throw std::runtime_error("encoded temperature raster exceeds byte limit");
        }
        encoded.push_back(static_cast<std::uint8_t>(
            static_cast<unsigned char>(byte)));
    }
    if (!raster.eof()) {
        throw std::runtime_error("failed while reading temperature raster");
    }

    constexpr std::array<std::uint8_t, 8> signature{
        137, 80, 78, 71, 13, 10, 26, 10};
    if (encoded.size() < signature.size() ||
        !std::equal(signature.begin(), signature.end(), encoded.begin())) {
        throw std::runtime_error("invalid PNG signature for temperature raster");
    }
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, encoded.data(), encoded.size())) {
        throw std::runtime_error(std::string("libpng could not read temperature map: ") +
                                 image.message);
    }
    struct ImageGuard {
        png_image* image;
        ~ImageGuard() { png_image_free(image); }
    } guard{&image};

    if (image.width != metadata.raster_width ||
        image.height != metadata.raster_height) {
        throw std::runtime_error("PNG dimensions do not match temperature sidecar");
    }
    image.format = PNG_FORMAT_RGBA;
    const std::size_t count = metadata.raster_width * metadata.raster_height;
    std::vector<png_byte> bytes(count * 4U);
    if (!png_image_finish_read(
            &image, nullptr, bytes.data(),
            static_cast<png_int_32>(metadata.raster_width * 4U), nullptr)) {
        throw std::runtime_error(std::string("libpng could not decode temperature map: ") +
                                 image.message);
    }
    std::vector<Rgba8> pixels;
    pixels.reserve(count);
    for (std::size_t index = 0; index < bytes.size(); index += 4) {
        pixels.push_back({bytes[index], bytes[index + 1],
                          bytes[index + 2], bytes[index + 3]});
    }
    return pixels;
}

std::vector<WeatherObservation> decode_pixels(
    const std::vector<Rgba8>& pixels,
    const TemperatureMapMetadata& metadata,
    const DecodeLimits& limits) {
    validate_metadata(metadata, limits);
    const std::size_t count = metadata.raster_width * metadata.raster_height;
    if (pixels.size() != count) {
        throw std::runtime_error("decoded temperature raster has an invalid pixel count");
    }
    std::unordered_map<std::uint32_t, const TemperaturePaletteEntry*> palette;
    palette.reserve(metadata.palette.size());
    for (const auto& entry : metadata.palette) {
        palette.emplace(color_key(entry), &entry);
    }

    std::vector<WeatherObservation> observations;
    observations.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t row = index / metadata.raster_width;
        const std::size_t column = index % metadata.raster_width;
        WeatherObservation observation;
        observation.source_id = metadata.source_id;
        observation.source_kind = metadata.source_kind;
        observation.product_id = metadata.product_id;
        observation.sidecar = metadata.sidecar;
        observation.valid_time_utc = metadata.valid_time_utc;
        observation.variable = "air_temperature";
        observation.units = "K";
        observation.location = {
            metadata.crs,
            metadata.pixel_to_world[0] +
                (static_cast<double>(column) + 0.5) * metadata.pixel_to_world[1] +
                (static_cast<double>(row) + 0.5) * metadata.pixel_to_world[2],
            metadata.pixel_to_world[3] +
                (static_cast<double>(column) + 0.5) * metadata.pixel_to_world[4] +
                (static_cast<double>(row) + 0.5) * metadata.pixel_to_world[5],
        };
        observation.vertical_reference = metadata.vertical_reference;
        if (!std::isfinite(observation.location.x) ||
            !std::isfinite(observation.location.y)) {
            throw std::invalid_argument(
                "temperature map transform produced a non-finite coordinate");
        }

        const auto found = palette.find(color_key(pixels[index]));
        if (found == palette.end()) {
            observation.status = ObservationStatus::ambiguous;
            observation.reason = "pixel RGBA color is absent from the declared temperature palette";
        } else {
            observation.status = found->second->status;
            observation.value = found->second->value_kelvin;
            observation.reason = found->second->reason;
        }
        observations.push_back(std::move(observation));
    }
    return observations;
}

}  // namespace

TemperatureMapMetadata read_temperature_map_sidecar_json(
    std::istream& sidecar_json,
    std::string sidecar_name) {
    constexpr std::size_t max_sidecar_bytes = 1024U * 1024U;
    const std::string encoded = read_limited(sidecar_json, max_sidecar_bytes);
    Json root;
    try {
        root = Json::parse(encoded);
    } catch (const Json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid temperature map sidecar JSON: ") + error.what());
    }
    if (!root.is_object() ||
        required_string(root, "schema_version") !=
            "terraflux.weather.map-sidecar/v1") {
        throw std::invalid_argument("unsupported temperature map sidecar schema");
    }

    TemperatureMapMetadata metadata;
    metadata.sidecar = std::move(sidecar_name);
    metadata.valid_time_utc = required_string(root, "valid_time_utc");
    const Json& source = required(root, "source");
    (void)required_string(source, "provider");
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
        throw std::invalid_argument("unsupported temperature map raster format");
    }
    const std::string encoding = required_string(raster, "color_encoding");
    if (encoding != "rgb" && encoding != "indexed") {
        throw std::invalid_argument("unsupported temperature map color encoding");
    }

    const Json& georeferencing = required(root, "georeferencing");
    metadata.crs = required_string(georeferencing, "crs");
    const Json& axis_order = required(georeferencing, "axis_order");
    if (!axis_order.is_array() || axis_order.size() != 2 ||
        axis_order[0] != "longitude" || axis_order[1] != "latitude") {
        throw std::invalid_argument(
            "temperature map axis_order must be longitude, latitude");
    }
    if (required_string(georeferencing, "transform_convention") !=
        "pixel_corner_affine") {
        throw std::invalid_argument("unsupported temperature map transform convention");
    }
    const Json& transform = required(georeferencing, "pixel_to_world");
    if (!transform.is_array() || transform.size() != metadata.pixel_to_world.size()) {
        throw std::invalid_argument("temperature pixel_to_world requires six coefficients");
    }
    for (std::size_t index = 0; index < metadata.pixel_to_world.size(); ++index) {
        metadata.pixel_to_world[index] =
            finite_number(transform[index], "georeferencing.pixel_to_world");
    }

    const Json& observation = required(root, "observation");
    if (required_string(observation, "variable") != "air_temperature" ||
        required_string(observation, "units") != "K") {
        throw std::invalid_argument(
            "temperature map sidecar must describe air_temperature in K");
    }
    metadata.vertical_reference =
        required_string(observation, "vertical_reference");

    const Json& palette = required(root, "palette");
    if (!palette.is_array() || palette.empty()) {
        throw std::invalid_argument("temperature map palette must be a non-empty array");
    }
    metadata.palette.reserve(palette.size());
    for (const Json& item : palette) {
        TemperaturePaletteEntry entry;
        const Json& rgb = required(item, "rgb");
        if (!rgb.is_array() || rgb.size() != 3) {
            throw std::invalid_argument(
                "temperature palette rgb must contain exactly three channels");
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
            entry.value_kelvin =
                finite_number(required(item, "value"), "palette.value");
        } else if (status == "no_data") {
            entry.status = ObservationStatus::no_data;
            if (item.contains("value")) {
                throw std::invalid_argument(
                    "no-data temperature palette entries must not include value");
            }
        } else if (status == "ambiguous") {
            entry.status = ObservationStatus::ambiguous;
            if (item.contains("value")) {
                throw std::invalid_argument(
                    "ambiguous temperature palette entries must not include value");
            }
            entry.reason = required_string(item, "reason");
        } else {
            throw std::invalid_argument(
                "temperature palette contains an unsupported status");
        }
        metadata.palette.push_back(std::move(entry));
    }
    validate_metadata(metadata, DecodeLimits{});
    return metadata;
}

std::vector<WeatherObservation> decode_temperature_map(
    std::istream& raster,
    const TemperatureMapMetadata& metadata,
    DecodeLimits limits) {
    std::vector<Rgba8> pixels;
    if (metadata.raster_format == RasterFormat::ppm_p3) {
        pixels = read_ppm_p3(raster, metadata, limits);
    } else if (metadata.raster_format == RasterFormat::png) {
        pixels = read_png(raster, metadata, limits);
    } else {
        throw std::invalid_argument("unsupported temperature map raster format");
    }
    return decode_pixels(pixels, metadata, limits);
}

}  // namespace terraflux::weather
