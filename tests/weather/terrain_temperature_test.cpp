#include "terraflux/weather/terrain_temperature.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace terraflux::weather;
void require(bool c){if(!c)throw std::runtime_error("terrain temperature assertion failed");}
void rejects(const std::function<void()>& f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}require(failed);}
int main(){try{
    TerrainTemperatureConfig c{true,0.0065,100,"synthetic"};
    const std::vector<double> t{290,290,290}, z{100,1100,-900};
    const auto result=adjust_temperature_for_terrain(t,z,"synthetic",c);
    require(result==std::vector<double>({290,283.5,296.5}));
    require(t==std::vector<double>({290,290,290}));
    c.enabled=false;require(adjust_temperature_for_terrain(t,z,"synthetic",c)==t);
    c.enabled=true;c.lapse_rate_k_per_m=0;require(adjust_temperature_for_terrain(t,z,"synthetic",c)==t);
    c.lapse_rate_k_per_m=-0.001;require(adjust_temperature_for_terrain(t,z,"synthetic",c)[1]==291);
    rejects([&]{adjust_temperature_for_terrain(t,z,"other",c);});
    rejects([&]{adjust_temperature_for_terrain(t,{1},"synthetic",c);});
    rejects([&]{adjust_temperature_for_terrain({0,290,290},z,"synthetic",c);});
    rejects([&]{adjust_temperature_for_terrain(t,{1,2,std::numeric_limits<double>::infinity()},"synthetic",c);});
    c.lapse_rate_k_per_m=1;rejects([&]{adjust_temperature_for_terrain(t,z,"synthetic",c);});
    c.lapse_rate_k_per_m=std::numeric_limits<double>::quiet_NaN();rejects([&]{adjust_temperature_for_terrain(t,z,"synthetic",c);});
    std::cout<<"terrain temperature checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
