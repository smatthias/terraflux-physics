#include "terraflux/weather/terrain_temperature.hpp"
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {
std::vector<double> adjust_temperature_for_terrain(
    const std::vector<double>& reference_temperature_k,
    const std::vector<double>& elevation_m,
    const std::string& elevation_vertical_reference,
    const TerrainTemperatureConfig& config) {
    if (reference_temperature_k.empty() || reference_temperature_k.size()!=elevation_m.size())
        throw std::invalid_argument("temperature and elevation arrays must be nonempty and aligned");
    if (config.vertical_reference.empty() || config.vertical_reference!=elevation_vertical_reference)
        throw std::invalid_argument("temperature reference and terrain elevation datums must match");
    if (!std::isfinite(config.reference_elevation_m) || !std::isfinite(config.lapse_rate_k_per_m))
        throw std::invalid_argument("lapse rate and reference elevation must be finite");
    auto result=reference_temperature_k;
    for (std::size_t i=0;i<result.size();++i) {
        if (!std::isfinite(result[i]) || result[i]<=0 || !std::isfinite(elevation_m[i]))
            throw std::invalid_argument("temperature must be positive finite kelvin and elevation finite metres");
        if (config.enabled) {
            result[i]-=config.lapse_rate_k_per_m*(elevation_m[i]-config.reference_elevation_m);
            if (!std::isfinite(result[i]) || result[i]<=0)
                throw std::invalid_argument("terrain correction produced invalid kelvin temperature");
        }
    }
    return result;
}
}
