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

// Validate structure, units' physical ranges, and grid/array size consistency.
// Numerical stability constraints belong to the solver selected in a later step.
void validate_dry_transport_config(const DryTransportConfig& config);
void validate_dry_transport_state(
    const DryTransportState& state,
    const DryTransportConfig& config);

}  // namespace terraflux::weather
