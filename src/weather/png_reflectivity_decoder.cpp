#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include "decoder_internal.hpp"

#include <png.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {
namespace {

std::vector<std::uint8_t> read_bounded(
    std::istream& input,
    std::size_t max_bytes) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(64U * 1024U);
    char byte = 0;
    while (input.get(byte)) {
        if (bytes.size() == max_bytes) {
            throw std::runtime_error("encoded raster exceeds the configured byte limit");
        }
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
    }
    if (!input.eof()) {
        throw std::runtime_error("failed while reading encoded raster");
    }
    return bytes;
}

std::uint32_t read_big_endian_u32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

struct PngImageGuard {
    png_image image{};
    ~PngImageGuard() { png_image_free(&image); }
};

}  // namespace

std::vector<WeatherObservation> decode_reflectivity_png(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits) {
    detail::validate_metadata(metadata, limits);
    if (metadata.raster_format != RasterFormat::png) {
        throw std::invalid_argument("sidecar raster format is not png");
    }

    const std::vector<std::uint8_t> encoded = read_bounded(raster, limits.max_encoded_bytes);
    constexpr std::array<std::uint8_t, 8> signature{
        137, 80, 78, 71, 13, 10, 26, 10};
    if (encoded.size() < 29 ||
        !std::equal(signature.begin(), signature.end(), encoded.begin()) ||
        read_big_endian_u32(encoded.data() + 8) != 13 ||
        encoded[12] != 'I' || encoded[13] != 'H' ||
        encoded[14] != 'D' || encoded[15] != 'R') {
        throw std::runtime_error("invalid PNG signature or IHDR header");
    }

    const std::uint32_t header_width = read_big_endian_u32(encoded.data() + 16);
    const std::uint32_t header_height = read_big_endian_u32(encoded.data() + 20);
    const std::uint8_t bit_depth = encoded[24];
    const std::uint8_t color_type = encoded[25];
    if (bit_depth > 8 ||
        (color_type != 0 && color_type != 2 && color_type != 3 &&
         color_type != 4 && color_type != 6)) {
        throw std::runtime_error("PNG must use a supported color type with at most 8 bits per channel");
    }
    if (header_width != metadata.raster_width || header_height != metadata.raster_height) {
        throw std::runtime_error("PNG dimensions do not match sidecar metadata");
    }

    PngImageGuard guard;
    guard.image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(
            &guard.image, encoded.data(), encoded.size())) {
        throw std::runtime_error(std::string("libpng could not read PNG header: ") +
                                 guard.image.message);
    }
    if (guard.image.width != header_width || guard.image.height != header_height) {
        throw std::runtime_error("libpng dimensions disagree with the PNG header");
    }

    guard.image.format = PNG_FORMAT_RGBA;
    const std::size_t width = static_cast<std::size_t>(guard.image.width);
    const std::size_t height = static_cast<std::size_t>(guard.image.height);
    if (height == 0 || width > static_cast<std::size_t>(-1) / height ||
        width * height > static_cast<std::size_t>(-1) / 4U) {
        throw std::runtime_error("PNG dimensions overflow the decoded buffer size");
    }
    const std::size_t pixel_count = width * height;
    if (pixel_count > limits.max_pixels) {
        throw std::runtime_error("PNG dimensions exceed the configured pixel limit");
    }
    const std::size_t row_bytes = width * 4U;
    std::vector<png_byte> rgba_bytes(pixel_count * 4U);
    if (!png_image_finish_read(
            &guard.image, nullptr, rgba_bytes.data(),
            static_cast<png_int_32>(row_bytes), nullptr)) {
        throw std::runtime_error(std::string("libpng could not decode PNG pixels: ") +
                                 guard.image.message);
    }

    std::vector<detail::Rgba8> pixels;
    pixels.reserve(pixel_count);
    for (std::size_t i = 0; i < rgba_bytes.size(); i += 4) {
        pixels.push_back({
            rgba_bytes[i],
            rgba_bytes[i + 1],
            rgba_bytes[i + 2],
            rgba_bytes[i + 3]});
    }
    return detail::decode_pixels(pixels, width, height, metadata, limits);
}

std::vector<WeatherObservation> decode_reflectivity_map(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits) {
    switch (metadata.raster_format) {
        case RasterFormat::ppm_p3:
            return decode_reflectivity_ppm_p3(raster, metadata, limits);
        case RasterFormat::png:
            return decode_reflectivity_png(raster, metadata, limits);
    }
    throw std::invalid_argument("unsupported map raster format");
}

}  // namespace terraflux::weather
