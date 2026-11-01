#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace terraflux::weather {

using Rgb8 = std::array<std::uint8_t, 3>;

enum class ObservationStatus {
    decoded,
    no_data,
    ambiguous,
};

enum class RasterFormat {
    ppm_p3,
    png,
};

struct PaletteEntry {
    Rgb8 rgb{};
    std::uint8_t alpha{255};
    ObservationStatus status{ObservationStatus::ambiguous};
    std::optional<double> value_dbz;
    std::string reason;
};

struct RadarMapMetadata {
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
    // Coordinates refer to pixel corners; observations are located at centers.
    std::array<double, 6> pixel_to_world{};

    std::string vertical_reference;
    std::vector<PaletteEntry> palette;
};

struct GeoLocation {
    std::string crs;
    // The observation-grid adapter treats EPSG:4326 x/y as longitude/latitude degrees.
    double x{0.0};
    double y{0.0};
};

struct WeatherObservation {
    std::string source_id;
    std::string source_kind;
    std::string product_id;
    std::string sidecar;
    std::string valid_time_utc;
    std::string variable{"radar_reflectivity"};
    std::optional<double> value;
    std::string units{"dBZ"};
    GeoLocation location;
    std::string vertical_reference;
    ObservationStatus status{ObservationStatus::ambiguous};
    std::optional<double> confidence;
    std::string reason;
};

struct DecodeLimits {
    std::size_t max_pixels{1'000'000};
    std::size_t max_encoded_bytes{64U * 1024U * 1024U};
};

// Reads and type-checks the v1 JSON sidecar. The caller supplies sidecar_name
// so emitted observations retain the metadata source path.
RadarMapMetadata read_radar_map_sidecar_json(
    std::istream& sidecar_json,
    std::string sidecar_name = {});

// Reads an ASCII PPM P3 raster and converts exact RGBA palette matches to
// georeferenced dBZ observations.
std::vector<WeatherObservation> decode_reflectivity_ppm_p3(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits = {});

// Reads a PNG raster through libpng and converts exact RGBA palette matches.
std::vector<WeatherObservation> decode_reflectivity_png(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits = {});

// Dispatches by the raster format declared in the validated sidecar.
std::vector<WeatherObservation> decode_reflectivity_map(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits = {});

// Writes an array of canonical v1 weather observation records as JSON.
// Invalid or internally inconsistent records are rejected.
void write_weather_observations_json(
    std::ostream& output,
    const std::vector<WeatherObservation>& observations);

// Reads and validates an array of canonical v1 observation records.
std::vector<WeatherObservation> read_weather_observations_json(
    std::istream& input);

}  // namespace terraflux::weather
