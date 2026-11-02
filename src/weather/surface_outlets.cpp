#include "terraflux/weather/surface_outlets.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace terraflux::weather {
SurfaceOutletResult apply_surface_outlets(const std::vector<double>& z,const std::vector<double>& h,
    const std::vector<unsigned char>& active,const TransportGrid2D& g,double dt,const std::vector<SurfaceOutlet>& outlets){
    if(g.nx<2||g.ny<2||g.nx>std::numeric_limits<std::size_t>::max()/g.ny||z.size()!=g.nx*g.ny||h.size()!=z.size()||active.size()!=z.size())
        throw std::invalid_argument("invalid outlet grid or aligned arrays");
    if(!std::isfinite(dt)||dt<=0||!std::isfinite(g.spacing_east_m)||!std::isfinite(g.spacing_north_m)||g.spacing_east_m<=0||g.spacing_north_m<=0)
        throw std::invalid_argument("invalid outlet timestep or spacing");
    const double area=g.spacing_east_m*g.spacing_north_m;
    if(!std::isfinite(area))throw std::invalid_argument("outlet cell area overflow");
    for(std::size_t i=0;i<h.size();i++)if(!std::isfinite(z[i])||!std::isfinite(h[i])||h[i]<0||active[i]>1||(!active[i]&&h[i]!=0))
        throw std::invalid_argument("invalid outlet elevation, depth or mask");
    std::set<std::size_t> cells;
    for(const auto& o:outlets){
        if(o.x>=g.nx||o.y>=g.ny||(o.x!=0&&o.x!=g.nx-1&&o.y!=0&&o.y!=g.ny-1))throw std::invalid_argument("outlet must lie on a domain boundary cell");
        const auto i=o.y*g.nx+o.x;
        if(!active[i]||!cells.insert(i).second)throw std::invalid_argument("outlet must be active and unique");
        if(!std::isfinite(o.crest_elevation_m)||!std::isfinite(o.max_rate_m_s)||o.max_rate_m_s<0)throw std::invalid_argument("invalid outlet crest or capacity");
    }
    SurfaceOutletResult result{h,std::vector<double>(h.size(),0),0};long double volume=0;
    for(const auto& o:outlets){
        const auto i=o.y*g.nx+o.x;
        const long double head=static_cast<long double>(z[i])+h[i];
        const auto available=std::max(0.0L,head-o.crest_elevation_m);
        const double removed=static_cast<double>(std::min({static_cast<long double>(h[i]),available,static_cast<long double>(o.max_rate_m_s)*dt}));
        result.depth_m[i]-=removed;result.discharged_depth_m[i]=removed;volume+=static_cast<long double>(removed)*area;
    }
    result.discharged_volume_m3=static_cast<double>(volume);
    if(!std::isfinite(result.discharged_volume_m3))throw std::invalid_argument("outlet volume overflow");
    return result;
}
}
