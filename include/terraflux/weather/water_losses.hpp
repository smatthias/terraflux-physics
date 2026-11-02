#pragma once
#include <vector>
namespace terraflux::weather {
struct WaterLossConfig {
    double timestep_seconds{0};
    double evaporation_rate_m_s{0};
    double drainage_rate_m_s{0};
};
struct WaterLossResult {
    std::vector<double> surface_depth_m,soil_storage_m;
    std::vector<double> evaporated_depth_m,drained_depth_m;
};
// One shared evaporation demand per cell, removed from surface first, then
// soil. Drainage then leaves soil storage. Both losses are availability-limited.
// These prescribed rates do not compute an atmospheric energy balance.
WaterLossResult apply_water_losses(const std::vector<double>& surface_depth_m,
    const std::vector<double>& soil_storage_m,const std::vector<unsigned char>& active,
    const WaterLossConfig& config);
}
