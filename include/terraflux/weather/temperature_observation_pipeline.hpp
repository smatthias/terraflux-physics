#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <iosfwd>

namespace terraflux::weather {

// Read canonical observation/v1 JSON, group air-temperature records by valid
// time, map them to the solver grid, and advance the forecast. Records for
// other variables or units (including radar reflectivity in dBZ) are rejected.
DryTransportState advance_dry_transport_from_observation_json(
    std::istream& observation_json,
    const DryTransportState& initial,
    const DryTransportConfig& config);

}  // namespace terraflux::weather
