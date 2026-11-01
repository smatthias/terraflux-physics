#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace terraflux::weather {
namespace {

std::string next_token(std::istream& input) {
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

long long parse_integer(const std::string& token, const char* name) {
    if (token.empty()) {
        throw std::runtime_error(std::string("missing PPM ") + name);
    }

    std::size_t parsed = 0;
    long long value = 0;
    try {
        value = std::stoll(token, &parsed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid PPM ") + name + ": " + token);
    }
    if (parsed != token.size()) {
        throw std::runtime_error(std::string("invalid PPM ") + name + ": " + token);
    }
    return value;
}

std::size_t parse_dimension(const std::string& token, const char* name) {
    const long long value = parse_integer(token, name);
    if (value <= 0 || static_cast<unsigned long long>(value) >
                          static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error(std::string("invalid PPM ") + name);
    }
    return static_cast<std::size_t>(value);
}

std::uint32_t color_key(const Rgb8& rgb) {
    return (static_cast<std::uint32_t>(rgb[0]) << 16U) |
           (static_cast<std::uint32_t>(rgb[1]) << 8U) |
           static_cast<std::uint32_t>(rgb[2]);
}

void validate_metadata(const RadarMapMetadata& metadata, const DecodeLimits& limits) {
    if (metadata.source_id.empty()) {
        throw std::invalid_argument("map metadata source_id is required");
    }
    if (metadata.source_kind != "synthetic_fixture" &&
        metadata.source_kind != "licensed_feed" &&
        metadata.source_kind != "public_dataset" &&
        metadata.source_kind != "user_supplied") {
        throw std::invalid_argument("unsupported map metadata source_kind");
    }
    if (metadata.valid_time_utc.empty() || metadata.valid_time_utc.back() != 'Z') {
        throw std::invalid_argument("valid_time_utc must be an ISO-8601 UTC timestamp ending in Z");
    }
    if (metadata.raster_width == 0 || metadata.raster_height == 0 ||
        metadata.raster_height > limits.max_pixels ||
        metadata.raster_width > limits.max_pixels / metadata.raster_height) {
        throw std::invalid_argument("raster dimensions are zero or exceed the configured pixel limit");
    }
    if (metadata.crs.empty()) {
        throw std::invalid_argument("map metadata CRS is required");
    }
    if (metadata.vertical_reference.empty()) {
        throw std::invalid_argument("map metadata vertical_reference is required");
    }
    for (double coefficient : metadata.pixel_to_world) {
        if (!std::isfinite(coefficient)) {
            throw std::invalid_argument("georeferencing transform must contain finite values");
        }
    }
    if (metadata.palette.empty()) {
        throw std::invalid_argument("map metadata palette is required");
    }

    std::unordered_map<std::uint32_t, bool> seen_colors;
    seen_colors.reserve(metadata.palette.size());
    for (const PaletteEntry& entry : metadata.palette) {
        if (entry.status != ObservationStatus::decoded &&
            entry.status != ObservationStatus::no_data &&
            entry.status != ObservationStatus::ambiguous) {
            throw std::invalid_argument("map metadata palette contains an unsupported status");
        }
        if (!seen_colors.emplace(color_key(entry.rgb), true).second) {
            throw std::invalid_argument("map metadata palette contains a duplicate RGB color");
        }
        if (entry.status == ObservationStatus::decoded) {
            if (!entry.value_dbz || !std::isfinite(*entry.value_dbz)) {
                throw std::invalid_argument("decoded palette colors require a finite dBZ value");
            }
            if (!entry.reason.empty()) {
                throw std::invalid_argument("decoded palette colors must not include an ambiguity reason");
            }
        } else if (entry.value_dbz) {
            throw std::invalid_argument("no-data and ambiguous palette colors must not include a dBZ value");
        }
        if (entry.status == ObservationStatus::ambiguous && entry.reason.empty()) {
            throw std::invalid_argument("ambiguous palette colors require a reason");
        }
    }
}

Rgb8 read_rgb(std::istream& input, unsigned int max_value) {
    Rgb8 rgb{};
    for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
        const long long sample = parse_integer(next_token(input), "sample");
        if (sample < 0 || static_cast<unsigned long long>(sample) > max_value) {
            throw std::runtime_error("PPM sample is outside the declared range");
        }
        // PPM permits maxval up to 65535. Palette keys use normalized 8-bit RGB.
        rgb[channel] = static_cast<std::uint8_t>(
            (static_cast<unsigned long long>(sample) * 255ULL + max_value / 2ULL) /
            max_value);
    }
    return rgb;
}

}  // namespace

std::vector<WeatherObservation> decode_reflectivity_ppm_p3(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits) {
    validate_metadata(metadata, limits);

    if (next_token(raster) != "P3") {
        throw std::runtime_error("unsupported raster format: expected ASCII PPM P3");
    }

    const std::size_t width = parse_dimension(next_token(raster), "width");
    const std::size_t height = parse_dimension(next_token(raster), "height");
    const long long max_value_token = parse_integer(next_token(raster), "maxval");
    if (max_value_token <= 0 || max_value_token > 65535) {
        throw std::runtime_error("PPM maxval must be between 1 and 65535");
    }
    const auto max_value = static_cast<unsigned int>(max_value_token);

    if (width != metadata.raster_width || height != metadata.raster_height) {
        throw std::runtime_error("PPM raster dimensions do not match sidecar metadata");
    }

    std::unordered_map<std::uint32_t, const PaletteEntry*> palette;
    palette.reserve(metadata.palette.size());
    for (const PaletteEntry& entry : metadata.palette) {
        palette.emplace(color_key(entry.rgb), &entry);
    }

    const std::size_t pixel_count = width * height;
    std::vector<WeatherObservation> observations;
    observations.reserve(pixel_count);

    for (std::size_t row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < width; ++column) {
            const Rgb8 rgb = read_rgb(raster, max_value);
            const auto found = palette.find(color_key(rgb));

            WeatherObservation observation;
            observation.source_id = metadata.source_id;
            observation.source_kind = metadata.source_kind;
            observation.product_id = metadata.product_id;
            observation.sidecar = metadata.sidecar;
            observation.valid_time_utc = metadata.valid_time_utc;
            observation.vertical_reference = metadata.vertical_reference;
            observation.location.crs = metadata.crs;

            const double pixel_x = static_cast<double>(column) + 0.5;
            const double pixel_y = static_cast<double>(row) + 0.5;
            observation.location.x =
                metadata.pixel_to_world[0] +
                pixel_x * metadata.pixel_to_world[1] +
                pixel_y * metadata.pixel_to_world[2];
            observation.location.y =
                metadata.pixel_to_world[3] +
                pixel_x * metadata.pixel_to_world[4] +
                pixel_y * metadata.pixel_to_world[5];
            if (!std::isfinite(observation.location.x) ||
                !std::isfinite(observation.location.y)) {
                throw std::invalid_argument("georeferencing transform produced a non-finite coordinate");
            }

            if (found == palette.end()) {
                observation.status = ObservationStatus::ambiguous;
                observation.reason = "pixel RGB color is absent from the declared palette";
            } else {
                const PaletteEntry& entry = *found->second;
                observation.status = entry.status;
                observation.value = entry.value_dbz;
                observation.reason = entry.reason;
            }
            observations.push_back(std::move(observation));
        }
    }

    if (!next_token(raster).empty()) {
        throw std::runtime_error("PPM raster contains unexpected trailing samples");
    }
    return observations;
}

}  // namespace terraflux::weather
