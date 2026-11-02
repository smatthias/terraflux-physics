#pragma once
#include "terraflux/weather/transport_model.hpp"
#include <cstddef>
#include <vector>
namespace terraflux::weather {
struct SurfaceOutlet { std::size_t x{0},y{0}; double crest_elevation_m{0},max_rate_m_s{0}; };
struct SurfaceOutletResult { std::vector<double> depth_m,discharged_depth_m; double discharged_volume_m3{0}; };
// Prescribed boundary sink, not a weir/discharge law. Elevations share a datum.
// Remove min(storage, head above crest, max_rate*dt); no external inflow.
SurfaceOutletResult apply_surface_outlets(const std::vector<double>& elevation_m,
    const std::vector<double>& depth_m,const std::vector<unsigned char>& active,
    const TransportGrid2D& grid,double timestep_seconds,const std::vector<SurfaceOutlet>& outlets);
}
