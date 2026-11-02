#include "terraflux/weather/surface_water.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace terraflux::weather {
SurfaceWaterResult advance_surface_water(
    const std::vector<double>& z,const std::vector<double>& h,
    const std::vector<double>& rain,const std::vector<unsigned char>& active,
    const TransportGrid2D& g,const SurfaceWaterConfig& c) {
    if(g.nx<2||g.ny<2||g.nx>std::numeric_limits<std::size_t>::max()/g.ny)
        throw std::invalid_argument("invalid water grid dimensions");
    const auto n=g.nx*g.ny;
    if(z.size()!=n||h.size()!=n||rain.size()!=n||active.size()!=n)
        throw std::invalid_argument("water arrays must match grid");
    if(!std::isfinite(g.spacing_east_m)||!std::isfinite(g.spacing_north_m)||
       g.spacing_east_m<=0||g.spacing_north_m<=0||
       !std::isfinite(c.timestep_seconds)||c.timestep_seconds<=0||
       !std::isfinite(c.routing_speed_m_s)||c.routing_speed_m_s<0)
        throw std::invalid_argument("invalid water spacing, timestep or routing speed");
    const double area=g.spacing_east_m*g.spacing_north_m;
    if(!std::isfinite(area))throw std::invalid_argument("water cell area overflow");
    SurfaceWaterResult result;result.depth_m.resize(n);
    std::vector<double> head(n);std::vector<long double> outgoing(n,0),delta(n,0);
    long double initial=0,added=0;
    for(std::size_t i=0;i<n;i++) {
        if(!std::isfinite(z[i])||!std::isfinite(h[i])||!std::isfinite(rain[i])||h[i]<0||rain[i]<0||active[i]>1)
            throw std::invalid_argument("water requires finite elevations, nonnegative depths and binary active mask");
        if(!active[i]&&(h[i]!=0||rain[i]!=0))throw std::invalid_argument("masked water cells cannot receive input storage or rain");
        result.depth_m[i]=h[i]+rain[i];head[i]=z[i]+result.depth_m[i];
        if(!std::isfinite(head[i])||!std::isfinite(result.depth_m[i]))throw std::invalid_argument("water head overflow");
        initial+=static_cast<long double>(h[i])*area;added+=static_cast<long double>(rain[i])*area;
    }
    struct Transfer {std::size_t from,to;long double depth;};
    std::vector<Transfer> transfers;
    auto edge=[&](std::size_t a,std::size_t b,double spacing){
        if(!active[a]||!active[b]||head[a]==head[b])return;
        if(head[a]<head[b])std::swap(a,b);
        const long double amount=static_cast<long double>(c.routing_speed_m_s)*c.timestep_seconds*
            ((static_cast<long double>(head[a])-head[b])/spacing);
        if(!std::isfinite(amount))throw std::invalid_argument("water flux overflow");
        transfers.push_back({a,b,amount});outgoing[a]+=amount;
    };
    for(std::size_t y=0;y<g.ny;y++)for(std::size_t x=0;x<g.nx;x++) {
        const auto i=y*g.nx+x;
        if(x+1<g.nx)edge(i,i+1,g.spacing_east_m);
        if(y+1<g.ny)edge(i,i+g.nx,g.spacing_north_m);
    }
    for(const auto& t:transfers) {
        const long double fraction=outgoing[t.from]>result.depth_m[t.from]?
            static_cast<long double>(result.depth_m[t.from])/outgoing[t.from]:1;
        const auto amount=t.depth*fraction;delta[t.from]-=amount;delta[t.to]+=amount;
    }
    long double final=0;
    for(std::size_t i=0;i<n;i++) {
        const long double updated=static_cast<long double>(result.depth_m[i])+delta[i];
        // Only roundoff below zero is clipped; donor limiting bounds the flux.
        if(updated < -1e-12L || !std::isfinite(updated) || updated>std::numeric_limits<double>::max())
            throw std::invalid_argument("invalid routed water depth");
        result.depth_m[i]=static_cast<double>(std::max(0.0L,updated));
        final+=static_cast<long double>(result.depth_m[i])*area;
    }
    result.initial_volume_m3=static_cast<double>(initial);result.rainfall_volume_m3=static_cast<double>(added);
    result.final_volume_m3=static_cast<double>(final);result.balance_error_m3=static_cast<double>(final-initial-added);
    if(!std::isfinite(result.initial_volume_m3)||!std::isfinite(result.rainfall_volume_m3)||!std::isfinite(result.final_volume_m3))
        throw std::invalid_argument("water volume overflow");
    return result;
}
}
