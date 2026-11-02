#include "terraflux/weather/terrain_derivatives.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace terraflux::weather;
void require(bool c){if(!c)throw std::runtime_error("terrain derivative assertion failed");}
void near(double a,double b){require(std::abs(a-b)<1e-10);}
void rejects(const std::function<void()>& f){bool fail=false;try{f();}catch(const std::invalid_argument&){fail=true;}require(fail);}
int main(){try{
    TransportGrid2D g{3,3,0,0,10,20,0,0};
    auto flat=calculate_terrain_derivatives(std::vector<double>(9,123),g);
    for(std::size_t i=0;i<9;i++){near(flat.slope_deg[i],0);require(!flat.aspect_deg[i]);}
    // z=2*east+3*north; interior and one-sided boundaries recover the plane.
    std::vector<double> plane;
    for(int y=0;y<3;y++)for(int x=0;x<3;x++)plane.push_back(2*x*10+3*y*20);
    auto d=calculate_terrain_derivatives(plane,g);
    for(std::size_t i=0;i<9;i++){near(d.gradient_east[i],2);near(d.gradient_north[i],3);
        near(d.slope_deg[i],std::atan(std::sqrt(13.0))*180/3.14159265358979323846);
        near(*d.aspect_deg[i],213.6900675259798);}
    g.nx=2;g.ny=2;g.spacing_east_m=1;g.spacing_north_m=1;
    const std::vector<std::vector<double>> cardinal{{0,1,0,1},{0,-1,0,-1},{0,0,1,1},{0,0,-1,-1}};
    const double bearings[]{270,90,180,0};
    for(int k=0;k<4;k++){auto c=calculate_terrain_derivatives(cardinal[k],g);for(auto a:c.aspect_deg)near(*a,bearings[k]);}
    rejects([&]{calculate_terrain_derivatives({1},g);});
    rejects([&]{calculate_terrain_derivatives({0,0,0,std::numeric_limits<double>::quiet_NaN()},g);});
    g.spacing_east_m=0;rejects([&]{calculate_terrain_derivatives({0,0,0,0},g);});
    std::cout<<"terrain derivative checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
