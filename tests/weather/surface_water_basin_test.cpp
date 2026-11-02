#include "terraflux/weather/surface_water.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace terraflux::weather;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Basin {
    TransportGrid2D grid{5,3,0,0,1,1,0,0};
    std::vector<double> elevation{5,5,5,5,5, 0,0,1,0,0, 5,5,5,5,5};
    std::vector<unsigned char> active=std::vector<unsigned char>(15,1);
    std::vector<double> depth=std::vector<double>(15,0);
    double expected_volume{0};
    explicit Basin(double level){depth[5]=depth[6]=level;expected_volume=2*level;}
    void step(double rainfall=0){
        std::vector<double> rain(15,0);rain[5]=rain[6]=rainfall;
        expected_volume+=2*rainfall;
        const auto out=advance_surface_water(elevation,depth,rain,active,grid,{0.1,0.1});
        require(std::abs(out.final_volume_m3-expected_volume)<1e-10,"closed basin lost or gained water");
        require(std::abs(out.balance_error_m3)<1e-11,"basin step budget failed");
        depth=out.depth_m;
        for(std::size_t i=0;i<depth.size();++i){
            require(std::isfinite(depth[i])&&depth[i]>=0,"invalid basin storage");
            if(i<5||i>=10)require(depth[i]==0,"water crossed the high side walls");
        }
    }
    void settle(int steps){for(int i=0;i<steps;++i)step();}
    double downstream()const{return depth[8]+depth[9];}
};
}
int main(){try{
    Basin below(0.5);below.settle(1000);
    require(below.depth[5]==0.5&&below.depth[6]==0.5,"sub-crest pond changed at rest");
    require(below.depth[7]==0&&below.downstream()==0,"sub-crest water leaked across spill point");
    Basin threshold(1);threshold.settle(1000);
    require(threshold.depth[7]==0&&threshold.downstream()==0,"water spilled with no positive head above crest");
    Basin overflowing(1.2);overflowing.settle(10000);
    require(overflowing.downstream()>0.1,"above-crest water did not reach downstream storage");
    require(overflowing.depth[5]>=1-1e-8&&overflowing.depth[6]>=1-1e-8,"resolved spill drained upstream pond below crest");
    Basin raised(1.2);raised.elevation[7]=1.5;raised.settle(1000);
    require(raised.downstream()==0,"raising spill elevation did not retain water");
    Basin filling(0.2);
    for(int pulse=0;pulse<10;++pulse){
        filling.step(0.1);filling.settle(200);
        // The first seven pulses remain unambiguously below crest. Avoid
        // asserting exact equality at the decimal floating-point threshold.
        if(pulse<7)require(filling.downstream()==0,"rainfall pond spilled below crest");
    }
    require(filling.downstream()>0,"rainfall did not trigger internal overflow");
    std::cout<<"basin checks passed: ponding, exact crest, internal overflow, raised spill and rainfall filling; closed-domain mass conserved\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
