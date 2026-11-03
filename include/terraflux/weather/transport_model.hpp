#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// v1 is a single horizontal layer with prescribed, time-invariant wind.
// Coordinates are local east/north distances in metres, stored at cell centers.
struct TransportGrid2D {
    std::size_t nx{0};
    std::size_t ny{0};
    double origin_east_m{0.0};
    double origin_north_m{0.0};
    double spacing_east_m{0.0};
    double spacing_north_m{0.0};
    // Center of the spherical azimuthal-equidistant projection used to map
    // EPSG:4326 observations into the local grid frame.
    double geographic_anchor_longitude_deg{0.0};
    double geographic_anchor_latitude_deg{0.0};
};

struct DryTransportConfig {
    std::string start_time_utc;
    double timestep_seconds{0.0};
    std::size_t steps{0};
    double horizontal_mixing_diffusivity_m2_s{0.0};
    TransportGrid2D grid;
    // v1 supports periodic boundaries in both horizontal directions.
    std::string boundary_condition{"periodic"};
};

struct DryTransportState {
    double elapsed_seconds{0.0};
    // Row-major arrays: index = y * nx + x.
    std::vector<double> air_temperature_k;
    std::vector<double> wind_east_m_s;
    std::vector<double> wind_north_m_s;
};

// Validate grid dimensions, finite positive spacing, extent, and anchor
// bounds. Shared by every model that uses TransportGrid2D.
void validate_transport_grid(const TransportGrid2D& grid);

// Validate structure, units' physical ranges, and grid/array size consistency.
void validate_dry_transport_config(const DryTransportConfig& config);
void validate_dry_transport_state(
    const DryTransportState& state,
    const DryTransportConfig& config);

// Apply all configured steps using conservative first-order upwind advection
// and centered diffusion with periodic boundaries. Wind is fixed and must be
// discretely divergence-free. Throws if the combined advection/diffusion
// stability bound is exceeded or an update becomes non-finite/non-positive.
DryTransportState advance_dry_transport(
    const DryTransportState& initial,
    const DryTransportConfig& config);

}  // namespace terraflux::weather
