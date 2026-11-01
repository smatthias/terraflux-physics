#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace terraflux::weather {

using Rgb8 = std::array<std::uint8_t, 3>;

enum class ObservationStatus {
    decoded,
    no_data,
    ambiguous,
};

struct PaletteEntry {
    Rgb8 rgb{};
    ObservationStatus status{ObservationStatus::ambiguous};
    std::optional<double> value_dbz;
    std::string reason;
};

// Values are supplied by a source adapter after it reads and validates the
// map sidecar. The decoder itself does not depend on a JSON library.
struct RadarMapMetadata {
    std::string source_id;
    std::string source_kind;
    std::string product_id;
    std::string sidecar;
    std::string valid_time_utc;

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
};

// Reads an ASCII PPM P3 raster and converts exact RGB palette matches to
// georeferenced dBZ observations. Unknown colors are preserved as ambiguous;
// explicit no-data and ambiguous palette entries produce null values.
std::vector<WeatherObservation> decode_reflectivity_ppm_p3(
    std::istream& raster,
    const RadarMapMetadata& metadata,
    DecodeLimits limits = {});

}  // namespace terraflux::weather
