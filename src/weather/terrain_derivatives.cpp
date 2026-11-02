#include "terraflux/weather/terrain_derivatives.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraflux::weather {
TerrainDerivatives calculate_terrain_derivatives(
    const std::vector<double>& elevation_m, const TransportGrid2D& g) {
    if (g.nx<2 || g.ny<2 || g.nx>std::numeric_limits<std::size_t>::max()/g.ny ||
        elevation_m.size()!=g.nx*g.ny)
        throw std::invalid_argument("terrain requires an aligned grid of at least 2 by 2 cells");
    if (!std::isfinite(g.spacing_east_m) || !std::isfinite(g.spacing_north_m) ||
        g.spacing_east_m<=0 || g.spacing_north_m<=0)
        throw std::invalid_argument("terrain grid spacing must be finite positive metres");
    for(double z:elevation_m)if(!std::isfinite(z))throw std::invalid_argument("terrain elevation must be finite");
    TerrainDerivatives out;
    constexpr double degrees=180.0/3.14159265358979323846;
    for(std::size_t y=0;y<g.ny;++y)for(std::size_t x=0;x<g.nx;++x){
        const auto left=x==0?x:x-1, right=x+1==g.nx?x:x+1;
        const auto south=y==0?y:y-1, north=y+1==g.ny?y:y+1;
        // Long double intermediates avoid overflow on finite double elevations.
        const double east=static_cast<double>((static_cast<long double>(elevation_m[y*g.nx+right])-elevation_m[y*g.nx+left])/
                          ((right-left)*static_cast<long double>(g.spacing_east_m)));
        const double northward=static_cast<double>((static_cast<long double>(elevation_m[north*g.nx+x])-elevation_m[south*g.nx+x])/
                          ((north-south)*static_cast<long double>(g.spacing_north_m)));
        if(!std::isfinite(east)||!std::isfinite(northward))throw std::invalid_argument("terrain gradient overflow");
        out.gradient_east.push_back(east);out.gradient_north.push_back(northward);
        const double magnitude=std::hypot(east,northward);
        if(!std::isfinite(magnitude))throw std::invalid_argument("terrain gradient magnitude overflow");
        out.slope_deg.push_back(std::atan(magnitude)*degrees);
        if(magnitude==0)out.aspect_deg.push_back(std::nullopt);
        else {const double bearing=std::atan2(-east,-northward)*degrees;
            const double normalized=bearing<0?bearing+360:bearing;
            out.aspect_deg.push_back(normalized>=360?0:normalized);}
    }
    return out;
}
}
