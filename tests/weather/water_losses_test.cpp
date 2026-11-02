#include "terraflux/weather/water_losses.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace terraflux::weather;
void require(bool c){if(!c)throw std::runtime_error("water loss assertion failed");}
void near(double a,double b){require(std::abs(a-b)<1e-12);}
void rejects(const std::function<void()>& f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}require(failed);}
int main(){try{
 const std::vector<double> h{.01,.001,0,0},s{.02,.02,0,0};
 auto r=apply_water_losses(h,s,{1,1,1,0},{10,.0005,.0002});
 near(r.evaporated_depth_m[0],.005);near(r.soil_storage_m[0],.018);
 near(r.surface_depth_m[1],0);near(r.soil_storage_m[1],.014);
 for(std::size_t i=0;i<4;i++)near(h[i]+s[i],r.surface_depth_m[i]+r.soil_storage_m[i]+r.evaporated_depth_m[i]+r.drained_depth_m[i]);
 require(h[0]==.01&&s[0]==.02);
 auto zero=apply_water_losses(h,s,{1,1,1,0},{10,0,0});require(zero.surface_depth_m==h&&zero.soil_storage_m==s);
 auto dry=apply_water_losses({.001},{.002},{1},{10,100,100});near(dry.evaporated_depth_m[0],.003);near(dry.drained_depth_m[0],0);near(dry.soil_storage_m[0],0);
 auto drainage=apply_water_losses({.001},{.002},{1},{10,0,100});near(drainage.surface_depth_m[0],.001);near(drainage.drained_depth_m[0],.002);
 rejects([&]{apply_water_losses({.1},{0},{0},{10,1,1});});
 rejects([&]{apply_water_losses({.1},{0},{1},{10,-1,1});});
 rejects([&]{apply_water_losses({.1},{0},{1},{0,1,1});});
 std::cout<<"water loss checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
