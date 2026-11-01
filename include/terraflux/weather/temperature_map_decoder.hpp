#pragma once

#include "terraflux/weather/radar_reflectivity_decoder.hpp"

namespace terraflux::weather {

struct TemperaturePaletteEntry {
    Rgb8 rgb{};
    std::uint8_t alpha{255};
    ObservationStatus status{ObservationStatus::ambiguous};
    std::optional<double> value_kelvin;
    std::string reason;
};

struct TemperatureMapMetadata {
    std::string source_id;
    std::string source_kind;
    std::string product_id;
    std::string sidecar;
    std::string valid_time_utc;
    std::string raster_file;
    RasterFormat raster_format{RasterFormat::ppm_p3};
    std::size_t raster_width{0};
    std::size_t raster_height{0};
    std::string crs;
    // [x_origin, x_pixel, x_line, y_origin, y_pixel, y_line].
    std::array<double, 6> pixel_to_world{};
    std::string vertical_reference;
    std::vector<TemperaturePaletteEntry> palette;
};

// Reads the shared v1 map-sidecar layout but accepts only air_temperature/K.
TemperatureMapMetadata read_temperature_map_sidecar_json(
    std::istream& sidecar_json,
    std::string sidecar_name = {});

// Decode exact RGBA palette matches into canonical air_temperature observations
// in kelvin. Unsupported palette colors remain ambiguous.
std::vector<WeatherObservation> decode_temperature_map(
    std::istream& raster,
    const TemperatureMapMetadata& metadata,
    DecodeLimits limits = {});

}  // namespace terraflux::weather
