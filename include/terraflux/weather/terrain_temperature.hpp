#pragma once
#include <string>
#include <vector>

namespace terraflux::weather {
struct TerrainTemperatureConfig {
    bool enabled{false};
    double lapse_rate_k_per_m{0.0};
    double reference_elevation_m{0.0};
    std::string vertical_reference;
};
// Diagnostic adjustment of a reference-elevation temperature field:
// T_surface = T_reference - lapse_rate * (elevation - reference_elevation).
// Input arrays must already share the same cell order/grid. This does not
// mutate inputs, evolve wind, or feed a correction back into transport.
std::vector<double> adjust_temperature_for_terrain(
    const std::vector<double>& reference_temperature_k,
    const std::vector<double>& elevation_m,
    const std::string& elevation_vertical_reference,
    const TerrainTemperatureConfig& config);
}
