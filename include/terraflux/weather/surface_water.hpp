#pragma once
#include "terraflux/weather/transport_model.hpp"
#include <vector>
namespace terraflux::weather {
struct SurfaceWaterConfig { double timestep_seconds{0}; double routing_speed_m_s{0}; };
struct SurfaceWaterResult {
    std::vector<double> depth_m;
    double initial_volume_m3{0}, rainfall_volume_m3{0}, final_volume_m3{0}, balance_error_m3{0};
};
// Closed-boundary, four-neighbor storage/routing prototype; not shallow water.
// Rain is an interval depth in metres, added once. Active=0 masks an unknown
// cell as impermeable: it must carry zero input depth/rain and stores no water.
// Head-driven transfers use donor limiting to preserve nonnegative storage.
SurfaceWaterResult advance_surface_water(
    const std::vector<double>& elevation_m, const std::vector<double>& depth_m,
    const std::vector<double>& rainfall_depth_m, const std::vector<unsigned char>& active,
    const TransportGrid2D& grid, const SurfaceWaterConfig& config);
}
