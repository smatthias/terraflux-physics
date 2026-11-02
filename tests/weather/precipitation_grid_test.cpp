#include "terraflux/weather/precipitation_grid.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace terraflux::weather;
void require(bool condition) { if (!condition) throw std::runtime_error("assertion failed"); }
void rejected(const std::function<void()>& run) {
    bool failed=false; try { run(); } catch(const std::invalid_argument&) { failed=true; }
    require(failed);
}
int main() {
    try {
        DryTransportConfig c; c.start_time_utc="2026-11-01T00:00:00Z";
        c.steps=1; c.timestep_seconds=60;
        c.grid={2,2,-100,-100,100,100,0,0};
        WeatherObservation a;
        a.source_id="synthetic";a.source_kind="user_supplied";
        a.variable="precipitation_accumulation";a.units="mm";
        a.interpretation="derived_temporal_integration";
        a.period_start_utc=c.start_time_utc;a.valid_time_utc="2026-11-01T00:01:00Z";
        a.location.crs="EPSG:4326";a.location.x=0.00045;a.location.y=0.00045;
        a.status=ObservationStatus::decoded;a.value=2.5;
        auto f=map_precipitation_accumulation_to_grid({a},c,60);
        require(f.cells.size()==4 && f.cells[3].value==2.5 &&
                f.cells[0].status==ObservationStatus::no_data);
        f=map_precipitation_accumulation_to_grid({a,a},c,60);
        require(f.cells[3].status==ObservationStatus::ambiguous && !f.cells[3].value);
        auto b=a;b.location.x=1;
        f=map_precipitation_accumulation_to_grid({b},c,60);
        require(f.outside_domain_count==1 && !f.cells[3].value);
        b=a;b.value.reset();b.status=ObservationStatus::no_data;
        f=map_precipitation_accumulation_to_grid({b},c,60);
        require(f.cells[3].status==ObservationStatus::no_data && !f.cells[3].value);
        b=a;b.value=0;require(map_precipitation_accumulation_to_grid({b},c,60).cells[3].value==0);
        rejected([&]{map_precipitation_accumulation_to_grid({a},c,59);});
        rejected([&]{map_precipitation_accumulation_to_grid({},c,60);});
        b=a;b.period_start_utc="2026-02-30T00:00:00Z";
        rejected([&]{map_precipitation_accumulation_to_grid({b},c,60);});
        b=a;b.value=-1;rejected([&]{map_precipitation_accumulation_to_grid({b},c,60);});
        b=a;b.source_id="different";rejected([&]{map_precipitation_accumulation_to_grid({a,b},c,60);});
        b=a;b.location.crs="other";rejected([&]{map_precipitation_accumulation_to_grid({b},c,60);});
        std::cout << "precipitation grid checks passed\n";
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
