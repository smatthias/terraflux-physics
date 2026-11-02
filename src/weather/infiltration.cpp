#include "terraflux/weather/infiltration.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace terraflux::weather {
InfiltrationResult infiltrate_surface_water(
    const std::vector<double>& surface,const std::vector<double>& soil,
    const std::vector<unsigned char>& active,const InfiltrationConfig& c) {
    if(surface.empty()||surface.size()!=soil.size()||surface.size()!=active.size())
        throw std::invalid_argument("infiltration arrays must be nonempty and aligned");
    if(!std::isfinite(c.timestep_seconds)||c.timestep_seconds<=0||!std::isfinite(c.rate_m_s)||c.rate_m_s<0||
       !std::isfinite(c.soil_capacity_m)||c.soil_capacity_m<0)
        throw std::invalid_argument("invalid infiltration timestep, rate or capacity");
    InfiltrationResult out{surface,soil,std::vector<double>(surface.size(),0)};
    const long double rate_limit=static_cast<long double>(c.rate_m_s)*c.timestep_seconds;
    for(std::size_t i=0;i<surface.size();++i){
        if(!std::isfinite(surface[i])||surface[i]<0||!std::isfinite(soil[i])||soil[i]<0||soil[i]>c.soil_capacity_m||active[i]>1)
            throw std::invalid_argument("invalid surface depth, soil storage or mask");
        if(!active[i]&&(surface[i]!=0||soil[i]!=0))throw std::invalid_argument("masked cells cannot store surface or soil water");
        if(!active[i])continue;
        const double transfer=static_cast<double>(std::min({static_cast<long double>(surface[i]),
            rate_limit,static_cast<long double>(c.soil_capacity_m)-soil[i]}));
        out.surface_depth_m[i]-=transfer;out.soil_storage_m[i]+=transfer;out.infiltrated_depth_m[i]=transfer;
    }
    return out;
}
}
