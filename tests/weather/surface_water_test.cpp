#include "terraflux/weather/surface_water.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace terraflux::weather;
void require(bool c){if(!c)throw std::runtime_error("surface water assertion failed");}
void rejects(const std::function<void()>& f){bool fail=false;try{f();}catch(const std::invalid_argument&){fail=true;}require(fail);}
int main(){try{
    TransportGrid2D g{2,2,0,0,10,20,0,0};
    std::vector<double> flat(4,0),dry(4,0),rain(4,.001);std::vector<unsigned char> active(4,1);
    auto r=advance_surface_water(flat,dry,rain,active,g,{10,.01});
    require(r.depth_m==rain);require(std::abs(r.final_volume_m3-.8)<1e-12);require(std::abs(r.balance_error_m3)<1e-12);
    auto noflow=advance_surface_water({3,2,1,0},rain,dry,active,g,{10,0});require(noflow.depth_m==rain);
    auto flow=advance_surface_water({3,2,1,0},{.1,0,0,0},dry,active,g,{100000,100});
    require(flow.depth_m[0]<.1 && flow.depth_m[1]>0 && flow.depth_m[2]>0);
    for(auto d:flow.depth_m){require(d>=0);}
    require(std::abs(flow.balance_error_m3)<1e-10);
    active={1,0,0,1};auto mask=advance_surface_water({3,2,1,0},{.1,0,0,0},dry,active,g,{10,1});require(mask.depth_m[0]==.1);
    rejects([&]{advance_surface_water(flat,dry,rain,active,g,{10,1});});
    active={1,1,1,1};auto still=advance_surface_water({3,2,1,0},dry,dry,active,g,{10,1});require(still.depth_m==dry);
    // Equal free-surface head produces no transfer despite nonuniform terrain/depth.
    auto lake=advance_surface_water({1,2,3,4},{4,3,2,1},dry,active,g,{10,1});require(lake.depth_m==std::vector<double>({4,3,2,1}));
    rejects([&]{advance_surface_water(flat,{-1,0,0,0},dry,active,g,{10,1});});
    rejects([&]{advance_surface_water(flat,dry,dry,active,g,{0,1});});
    std::cout<<"surface water checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
