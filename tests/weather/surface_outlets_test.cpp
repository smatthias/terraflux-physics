#include "terraflux/weather/surface_outlets.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace terraflux::weather;
void require(bool c){if(!c)throw std::runtime_error("outlet assertion failed");}
void near(double a,double b){require(std::abs(a-b)<1e-12);}
void rejects(const std::function<void()>& f){bool fail=false;try{f();}catch(const std::invalid_argument&){fail=true;}require(fail);}
int main(){try{
 TransportGrid2D g{3,3,0,0,10,20,0,0};std::vector<double> z(9,1),h(9,.5);std::vector<unsigned char> active(9,1);
 auto no=apply_surface_outlets(z,h,active,g,10,{});require(no.depth_m==h);near(no.discharged_volume_m3,0);
 auto below=apply_surface_outlets(z,h,active,g,10,{{0,1,2,1}});require(below.depth_m==h);
 auto crest=apply_surface_outlets(z,h,active,g,10,{{0,1,1.3,1}});near(crest.depth_m[3],.3);near(crest.discharged_volume_m3,40);
 auto limited=apply_surface_outlets(z,h,active,g,10,{{0,1,0,.01}});near(limited.depth_m[3],.4);near(limited.discharged_volume_m3,20);
 auto empty=apply_surface_outlets(z,h,active,g,10,{{0,1,0,100}});near(empty.depth_m[3],0);near(empty.discharged_volume_m3,100);
 for(int i=0;i<9;i++){near(h[i],empty.depth_m[i]+empty.discharged_depth_m[i]);}
 require(h[3]==.5);
 rejects([&]{apply_surface_outlets(z,h,active,g,10,{{1,1,0,1}});});
 rejects([&]{apply_surface_outlets(z,h,active,g,10,{{0,1,0,1},{0,1,0,1}});});
 active[3]=0;h[3]=0;rejects([&]{apply_surface_outlets(z,h,active,g,10,{{0,1,0,1}});});
 std::cout<<"surface outlet checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
