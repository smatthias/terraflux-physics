#pragma once
#include <vector>
namespace terraflux::weather {
struct InfiltrationConfig {
    double timestep_seconds{0};
    double rate_m_s{0};
    double soil_capacity_m{0};
};
struct InfiltrationResult {
    std::vector<double> surface_depth_m;
    std::vector<double> soil_storage_m;
    std::vector<double> infiltrated_depth_m;
};
// Transfer min(surface water, rate*dt, remaining soil capacity) per active cell.
// Soil storage is equivalent water depth, not volumetric moisture content.
// Masked cells must have zero internal storage. Inputs are never modified.
InfiltrationResult infiltrate_surface_water(
    const std::vector<double>& surface_depth_m,
    const std::vector<double>& soil_storage_m,
    const std::vector<unsigned char>& active,
    const InfiltrationConfig& config);
}
