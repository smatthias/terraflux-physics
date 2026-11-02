#include "terraflux/weather/infiltration.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace terraflux::weather;
void require(bool c){if(!c)throw std::runtime_error("infiltration assertion failed");}
void near(double a,double b){require(std::abs(a-b)<1e-12);}
void rejects(const std::function<void()>& f){bool fail=false;try{f();}catch(const std::invalid_argument&){fail=true;}require(fail);}
int main(){try{
 const std::vector<double> h{.1,.001,.1,0},soil{0,0,.009,0};
 auto r=infiltrate_surface_water(h,soil,{1,1,1,0},{10,.0005,.01});
 near(r.infiltrated_depth_m[0],.005);near(r.infiltrated_depth_m[1],.001);near(r.infiltrated_depth_m[2],.001);
 for(std::size_t i=0;i<4;i++){near(h[i]+soil[i],r.surface_depth_m[i]+r.soil_storage_m[i]);require(r.soil_storage_m[i]<=.01&&r.surface_depth_m[i]>=0);}
 require(h[0]==.1&&soil[0]==0);
 auto zero=infiltrate_surface_water(h,soil,{1,1,1,0},{10,0,.01});require(zero.surface_depth_m==h&&zero.soil_storage_m==soil);
 auto full=infiltrate_surface_water({.1},{.01},{1},{10,1,.01});near(full.surface_depth_m[0],.1);
 auto state=infiltrate_surface_water({.1},{0},{1},{10,1,.01});
 for(int i=0;i<10;i++)state=infiltrate_surface_water(state.surface_depth_m,state.soil_storage_m,{1},{10,1,.01});
 near(state.soil_storage_m[0],.01);near(state.surface_depth_m[0],.09);
 rejects([&]{infiltrate_surface_water({.1},{.02},{1},{10,1,.01});});
 rejects([&]{infiltrate_surface_water({.1},{0},{0},{10,1,.01});});
 rejects([&]{infiltrate_surface_water({.1},{0},{1},{10,-1,.01});});
 rejects([&]{infiltrate_surface_water({.1},{0},{1},{0,1,.01});});
 rejects([&]{infiltrate_surface_water({std::numeric_limits<double>::quiet_NaN()},{0},{1},{10,1,.01});});
 std::cout<<"infiltration checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
