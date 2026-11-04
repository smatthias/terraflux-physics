#include "terraflux/weather/synthetic_terrain.hpp"

#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

double terrain_noise(std::size_t index, std::uint32_t seed) {
    // 32-bit integer hash (Wang/Jenkins style) for platform-independent noise.
    std::uint32_t h = static_cast<std::uint32_t>(index & 0xffffffffu) * 0x9E3779B1u + seed * 0x85EBCA77u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return static_cast<double>(h) / 4294967295.0 * 2.0 - 1.0;
}

std::vector<double> build_synthetic_terrain(const SyntheticTerrainSpec& spec, const TransportGrid2D& grid) {
    validate_transport_grid(grid);
    if (!std::isfinite(spec.base_elevation_m) || !std::isfinite(spec.slope_east_m_per_m) || !std::isfinite(spec.slope_north_m_per_m) ||
        !std::isfinite(spec.roughness_m) || spec.roughness_m < 0.0) {
        throw std::invalid_argument("synthetic terrain base, slopes and roughness must be finite (roughness >= 0)");
    }
    for (const auto& r : spec.ridges) {
        if (!std::isfinite(r.orientation_deg) || !std::isfinite(r.center_east_m) || !std::isfinite(r.center_north_m) ||
            !std::isfinite(r.height_m) || !std::isfinite(r.half_width_m) || !std::isfinite(r.length_m) || r.half_width_m <= 0.0 || r.length_m <= 0.0) {
            throw std::invalid_argument("ridge half_width_m and length_m must be positive and all ridge values finite");
        }
    }
    for (const auto& b : spec.basins) {
        if (!std::isfinite(b.center_east_m) || !std::isfinite(b.center_north_m) || !std::isfinite(b.depth_m) || !std::isfinite(b.radius_m) || b.radius_m <= 0.0) {
            throw std::invalid_argument("basin radius_m must be positive and all basin values finite");
        }
    }
    constexpr double pi = 3.14159265358979323846;
    const std::size_t count = grid.nx * grid.ny;
    std::vector<double> z(count, spec.base_elevation_m);
    for (std::size_t i = 0; i < count; ++i) {
        const double east = grid.origin_east_m + (static_cast<double>(i % grid.nx) + 0.5) * grid.spacing_east_m;
        const double north = grid.origin_north_m + (static_cast<double>(i / grid.nx) + 0.5) * grid.spacing_north_m;
        z[i] += spec.slope_east_m_per_m * (east - grid.origin_east_m) + spec.slope_north_m_per_m * (north - grid.origin_north_m);
        for (const auto& r : spec.ridges) {
            const double ax = std::sin(r.orientation_deg * pi / 180.0), ay = std::cos(r.orientation_deg * pi / 180.0);
            const double dx = east - r.center_east_m, dy = north - r.center_north_m;
            const double along = dx * ax + dy * ay, across = dx * ay - dy * ax;
            const double end = along / (0.5 * r.length_m);
            z[i] += r.height_m * std::exp(-(across / r.half_width_m) * (across / r.half_width_m)) * std::exp(-end * end * end * end);
        }
        for (const auto& b : spec.basins) {
            const double dx = east - b.center_east_m, dy = north - b.center_north_m;
            z[i] -= b.depth_m * std::exp(-(dx * dx + dy * dy) / (b.radius_m * b.radius_m));
        }
        if (spec.roughness_m > 0.0) z[i] += spec.roughness_m * terrain_noise(i, spec.roughness_seed);
        if (!std::isfinite(z[i])) throw std::invalid_argument("synthetic terrain overflow");
    }
    return z;
}

}  // namespace terraflux::weather
