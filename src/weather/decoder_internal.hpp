#pragma once

#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace terraflux::weather::detail {

using Rgba8 = std::array<std::uint8_t, 4>;

inline std::uint32_t color_key(const Rgba8& rgba) {
    return (static_cast<std::uint32_t>(rgba[0]) << 24U) |
           (static_cast<std::uint32_t>(rgba[1]) << 16U) |
           (static_cast<std::uint32_t>(rgba[2]) << 8U) |
           static_cast<std::uint32_t>(rgba[3]);
}

inline std::uint32_t color_key(const PaletteEntry& entry) {
    return color_key({entry.rgb[0], entry.rgb[1], entry.rgb[2], entry.alpha});
}

inline void validate_metadata(const RadarMapMetadata& metadata, const DecodeLimits& limits) {
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
        limits.max_pixels == 0 || metadata.raster_height > limits.max_pixels ||
        metadata.raster_width > limits.max_pixels / metadata.raster_height) {
        throw std::invalid_argument("raster dimensions are zero or exceed the configured pixel limit");
    }
    if (metadata.crs.empty() || metadata.vertical_reference.empty()) {
        throw std::invalid_argument("map metadata CRS and vertical_reference are required");
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
        if (!seen_colors.emplace(color_key(entry), true).second) {
            throw std::invalid_argument("map metadata palette contains a duplicate RGBA color");
        }
        if (entry.status == ObservationStatus::decoded) {
            if (!entry.value_dbz || !std::isfinite(*entry.value_dbz) || !entry.reason.empty()) {
                throw std::invalid_argument("decoded palette colors require a finite dBZ value and no reason");
            }
        } else if (entry.value_dbz) {
            throw std::invalid_argument("no-data and ambiguous palette colors must not include a dBZ value");
        }
        if (entry.status == ObservationStatus::ambiguous && entry.reason.empty()) {
            throw std::invalid_argument("ambiguous palette colors require a reason");
        }
    }
}

inline std::vector<WeatherObservation> decode_pixels(
    const std::vector<Rgba8>& pixels,
    std::size_t width,
    std::size_t height,
    const RadarMapMetadata& metadata,
    const DecodeLimits& limits) {
    validate_metadata(metadata, limits);
    if (width != metadata.raster_width || height != metadata.raster_height) {
        throw std::runtime_error("raster dimensions do not match sidecar metadata");
    }
    if (height == 0 || width > static_cast<std::size_t>(-1) / height ||
        pixels.size() != width * height) {
        throw std::runtime_error("decoded raster pixel buffer has an invalid size");
    }

    std::unordered_map<std::uint32_t, const PaletteEntry*> palette;
    palette.reserve(metadata.palette.size());
    for (const PaletteEntry& entry : metadata.palette) {
        palette.emplace(color_key(entry), &entry);
    }

    std::vector<WeatherObservation> observations;
    observations.reserve(pixels.size());

    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::size_t row = index / width;
        const std::size_t column = index % width;
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

        const auto found = palette.find(color_key(pixels[index]));
        if (found == palette.end()) {
            observation.status = ObservationStatus::ambiguous;
            observation.reason = "pixel RGBA color is absent from the declared palette";
        } else {
            const PaletteEntry& entry = *found->second;
            observation.status = entry.status;
            observation.value = entry.value_dbz;
            observation.reason = entry.reason;
        }
        observations.push_back(std::move(observation));
    }
    return observations;
}

}  // namespace terraflux::weather::detail
