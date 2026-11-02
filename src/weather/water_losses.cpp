#include "terraflux/weather/water_losses.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace terraflux::weather {
WaterLossResult apply_water_losses(const std::vector<double>& surface,
    const std::vector<double>& soil,const std::vector<unsigned char>& active,const WaterLossConfig& c){
    if(surface.empty()||surface.size()!=soil.size()||surface.size()!=active.size())
        throw std::invalid_argument("water loss arrays must be nonempty and aligned");
    if(!std::isfinite(c.timestep_seconds)||c.timestep_seconds<=0||
       !std::isfinite(c.evaporation_rate_m_s)||c.evaporation_rate_m_s<0||
       !std::isfinite(c.drainage_rate_m_s)||c.drainage_rate_m_s<0)
        throw std::invalid_argument("water loss timestep and rates must be finite and nonnegative");
    WaterLossResult out{surface,soil,std::vector<double>(surface.size(),0),std::vector<double>(surface.size(),0)};
    const long double evap=static_cast<long double>(c.evaporation_rate_m_s)*c.timestep_seconds;
    const long double drain=static_cast<long double>(c.drainage_rate_m_s)*c.timestep_seconds;
    for(std::size_t i=0;i<surface.size();++i){
        if(!std::isfinite(surface[i])||!std::isfinite(soil[i])||surface[i]<0||soil[i]<0||active[i]>1)
            throw std::invalid_argument("invalid water loss storage or active mask");
        if(!active[i]){
            if(surface[i]!=0||soil[i]!=0)throw std::invalid_argument("masked cells cannot store water");
            continue;
        }
        const long double surface_loss=std::min(static_cast<long double>(surface[i]),evap);
        const double from_surface=static_cast<double>(surface_loss);
        const double from_soil=static_cast<double>(std::min(static_cast<long double>(soil[i]),evap-surface_loss));
        out.surface_depth_m[i]-=from_surface;out.soil_storage_m[i]-=from_soil;
        out.evaporated_depth_m[i]=from_surface+from_soil;
        const double drained=static_cast<double>(std::min(static_cast<long double>(out.soil_storage_m[i]),drain));
        out.soil_storage_m[i]-=drained;out.drained_depth_m[i]=drained;
    }
    return out;
}
}
