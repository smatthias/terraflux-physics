#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include "decoder_internal.hpp"

#include <cctype>
#include <stdexcept>
#include <string>

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
    if (value <= 0) {
        throw std::runtime_error(std::string("invalid PPM ") + name);
    }
    return static_cast<std::size_t>(value);
}

detail::Rgba8 read_rgba(std::istream& input, unsigned int max_value) {
    detail::Rgba8 rgba{0, 0, 0, 255};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const long long sample = parse_integer(next_token(input), "sample");
        if (sample < 0 || static_cast<unsigned long long>(sample) > max_value) {
            throw std::runtime_error("PPM sample is outside the declared range");
        }
        // PPM permits maxval up to 65535; normalize samples to 8-bit channels.
        rgba[channel] = static_cast<std::uint8_t>(
            (static_cast<unsigned long long>(sample) * 255ULL + max_value / 2ULL) /
            max_value);
    }
    return rgba;
}

}  // namespace

std::vector<WeatherObservation> decode_reflectivity_ppm_p3(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits) {
    detail::validate_metadata(metadata, limits);
    if (metadata.raster_format != RasterFormat::ppm_p3) {
        throw std::invalid_argument("sidecar raster format is not ppm-p3");
    }

    if (next_token(raster) != "P3") {
        throw std::runtime_error("unsupported raster format: expected ASCII PPM P3");
    }

    const std::size_t width = parse_dimension(next_token(raster), "width");
    const std::size_t height = parse_dimension(next_token(raster), "height");
    const long long max_value_token = parse_integer(next_token(raster), "maxval");
    if (max_value_token <= 0 || max_value_token > 65535) {
        throw std::runtime_error("PPM maxval must be between 1 and 65535");
    }
    if (width != metadata.raster_width || height != metadata.raster_height) {
        throw std::runtime_error("PPM raster dimensions do not match sidecar metadata");
    }

    const std::size_t pixel_count = width * height;
    std::vector<detail::Rgba8> pixels;
    pixels.reserve(pixel_count);
    const auto max_value = static_cast<unsigned int>(max_value_token);
    for (std::size_t index = 0; index < pixel_count; ++index) {
        pixels.push_back(read_rgba(raster, max_value));
    }
    if (!next_token(raster).empty()) {
        throw std::runtime_error("PPM raster contains unexpected trailing samples");
    }

    return detail::decode_pixels(pixels, width, height, metadata, limits);
}

}  // namespace terraflux::weather
