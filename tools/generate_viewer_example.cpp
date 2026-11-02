// Deterministic synthetic scenario, evaluated by the production physics kernels.
#include "terraflux/weather/transport_model.hpp"
#include "terraflux/weather/terrain_temperature.hpp"
#include "terraflux/weather/surface_water.hpp"
#include "terraflux/weather/precipitation_grid.hpp"
#include "terraflux/weather/terrain_derivatives.hpp"
#include "terraflux/weather/precipitation_estimator.hpp"
#include "terraflux/weather/precipitation_accumulator.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace terraflux::weather;
    using Json = nlohmann::json;
    if (argc != 2) { std::cerr << "Usage: terraflux-generate-viewer-example <output-directory>\n"; return 2; }
    try {
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        DryTransportConfig c;
        c.start_time_utc="2026-11-01T12:00:00Z";
        c.timestep_seconds=30; c.steps=120;
        c.horizontal_mixing_diffusivity_m2_s=100;
        c.grid={12,10,-6000,-5000,1000,1000,0,0};
        DryTransportState initial;
        const std::vector<std::string> valid_times={
            "2026-11-01T12:00:00Z", "2026-11-01T12:10:00Z", "2026-11-01T12:20:00Z",
            "2026-11-01T12:30:00Z", "2026-11-01T12:40:00Z", "2026-11-01T12:50:00Z",
            "2026-11-01T13:00:00Z"};
        std::vector<WeatherObservation> reflectivity;
        for (std::size_t y=0;y<c.grid.ny;++y) {
            for (std::size_t x=0;x<c.grid.nx;++x) {
                const double dx=static_cast<double>(x)-4, dy=static_cast<double>(y)-5;
                initial.air_temperature_k.push_back(285+14*std::exp(-(dx*dx+dy*dy)/8));
                initial.wind_east_m_s.push_back(1.5);
                initial.wind_north_m_s.push_back(0.5);
                // Invert the same spherical azimuthal-equidistant projection at
                // the equatorial anchor to place samples at exact cell centers.
                const double east=-6000+(x+0.5)*1000, north=-5000+(y+0.5)*1000;
                const double rho=std::hypot(east,north), angle=rho/6371008.8;
                const double latitude=std::asin(north*std::sin(angle)/rho);
                const double longitude=std::atan2(east*std::sin(angle),rho*std::cos(angle));
                for (int frame=0;frame<7;++frame) {
                    WeatherObservation o;
                    o.source_id="terraflux_synthetic_viewer_fixture";
                    o.source_kind="user_supplied";
                    o.product_id="synthetic_reflectivity_not_live_data";
                    o.interpretation="structured_grid_value";
                    o.valid_time_utc=valid_times.at(frame);
                    o.variable="radar_reflectivity";o.units="dBZ";
                    o.vertical_reference="composite";
                    o.location.crs="EPSG:4326";
                    o.location.x=longitude*180/3.14159265358979323846;
                    o.location.y=latitude*180/3.14159265358979323846;
                    if (x<2 && y>7) {
                        o.status=ObservationStatus::no_data;o.reason="synthetic missing radar coverage";
                    } else {
                        o.status=ObservationStatus::decoded;
                        const double rx=static_cast<double>(x)-7, ry=static_cast<double>(y)-4;
                        o.value=15+25*std::exp(-(rx*rx+ry*ry)/10)+frame/3.0;
                    }
                    reflectivity.push_back(o);
                }
            }
        }
        std::vector<DryTransportState> snapshots{initial};
        auto state=initial;
        auto segment=c; segment.steps=20;
        for (int i=0;i<6;++i) {
            state=advance_dry_transport(state,segment);
            snapshots.push_back(state);
        }
        const auto& forecast=snapshots.back();
        const auto rates=estimate_rain_rate(reflectivity);
        const auto accumulation=accumulate_rainfall(rates);
        for (std::size_t i=1;i<valid_times.size();++i) {
            std::vector<WeatherObservation> prefix;
            for (const auto& rate : rates) {
                if (rate.valid_time_utc<=valid_times[i]) prefix.push_back(rate);
            }
            std::ofstream output(directory/("accumulation-"+std::to_string(i)+".json"));
            write_weather_observations_json(output,accumulate_rainfall(prefix));
            output.close();
            if (!output) throw std::runtime_error("failed to write prefix accumulation");
        }
        const auto& g=c.grid;
        Json grid={{"nx",g.nx},{"ny",g.ny},{"origin_east_m",g.origin_east_m},
          {"origin_north_m",g.origin_north_m},{"spacing_east_m",g.spacing_east_m},
          {"spacing_north_m",g.spacing_north_m},{"geographic_anchor_longitude_deg",0},
          {"geographic_anchor_latitude_deg",0},{"coordinate_system","local_azimuthal_equidistant_m"},
          {"array_order","row_major_y_then_x"}};
        Json result={{"schema_version","terraflux.weather.forecast/v1"},
          {"start_time_utc",c.start_time_utc},{"elapsed_seconds",forecast.elapsed_seconds},
          {"grid",grid},{"observation_updates",Json::array()},
          {"fields",{{"air_temperature",{{"units","K"},{"values",forecast.air_temperature_k}}},
                     {"wind_east",{{"units","m/s"},{"values",forecast.wind_east_m_s}}},
                     {"wind_north",{{"units","m/s"},{"values",forecast.wind_north_m_s}}}}}};
        Json frames=Json::array();
        for (const auto& snapshot : snapshots) {
            auto frame=result;
            frame["elapsed_seconds"]=snapshot.elapsed_seconds;
            frame["fields"]["air_temperature"]["values"]=snapshot.air_temperature_k;
            frame["fields"]["wind_east"]["values"]=snapshot.wind_east_m_s;
            frame["fields"]["wind_north"]["values"]=snapshot.wind_north_m_s;
            frames.push_back(frame);
        }
        Json elevations=Json::array();
        for (std::size_t y=0;y<g.ny;++y) for (std::size_t x=0;x<g.nx;++x) {
            const double dx=static_cast<double>(x)-7, dy=static_cast<double>(y)-5;
            elevations.push_back(80+700*std::exp(-(dx*dx/12+dy*dy/6)));
        }
        Json terrain={{"schema_version","terraflux.terrain.grid/v1"}, {"grid",grid},
            {"units","m"},{"vertical_reference","synthetic_local_datum"},
            {"source_id","synthetic_gaussian_hill"},{"values",elevations}};
        TerrainTemperatureConfig adjustment{true,0.0065,80,"synthetic_local_datum"};
        Json adjusted_frames=Json::array();
        for (const auto& snapshot : snapshots) {
            adjusted_frames.push_back(adjust_temperature_for_terrain(
                snapshot.air_temperature_k,elevations.get<std::vector<double>>(),
                "synthetic_local_datum",adjustment));
        }
        Json diagnostic={{"schema_version","terraflux.weather.terrain_temperature/v1"},
            {"units","K"},{"lapse_rate_k_per_m",adjustment.lapse_rate_k_per_m},
            {"reference_elevation_m",adjustment.reference_elevation_m},
            {"vertical_reference",adjustment.vertical_reference},{"values_by_frame",adjusted_frames}};
        std::ofstream diagnostic_file(directory/"terrain-temperature.json");
        diagnostic_file << diagnostic.dump(2) << '\n'; diagnostic_file.close();
        if (!diagnostic_file) throw std::runtime_error("failed to write terrain diagnostic");
        const auto derivatives=calculate_terrain_derivatives(elevations.get<std::vector<double>>(),g);
        Json aspect=Json::array();
        for(const auto& bearing:derivatives.aspect_deg)aspect.push_back(bearing?Json(*bearing):Json(nullptr));
        Json derived={{"schema_version","terraflux.terrain.derivatives/v1"},
            {"method","central_interior_one_sided_edges"},
            {"slope_deg",derivatives.slope_deg},{"aspect_deg",aspect}};
        std::ofstream derivatives_file(directory/"terrain-derivatives.json");
        derivatives_file << derived.dump(2) << '\n'; derivatives_file.close();
        if (!derivatives_file) throw std::runtime_error("failed to write terrain derivatives");
        std::vector<double> water_depth(g.nx*g.ny,0),previous_rain(g.nx*g.ny,0);
        std::vector<unsigned char> active(g.nx*g.ny,1);
        const auto elevation=elevations.get<std::vector<double>>();
        const auto covered=map_precipitation_accumulation_to_grid(accumulation,c,3600);
        for(std::size_t i=0;i<active.size();++i)active[i]=covered.cells[i].status==ObservationStatus::decoded;
        Json water_frames=Json::array(),volumes=Json::array(),rain_volumes=Json::array(),balance=Json::array();
        double cumulative_rain_volume=0;
        for(std::size_t frame_index=0;frame_index<snapshots.size();++frame_index) {
            if(frame_index>0) {
                std::vector<WeatherObservation> prefix;
                for(const auto& rate:rates)if(rate.valid_time_utc<=valid_times[frame_index])prefix.push_back(rate);
                const auto field=map_precipitation_accumulation_to_grid(
                    accumulate_rainfall(prefix),c,snapshots[frame_index].elapsed_seconds);
                std::vector<double> rain_increment(active.size(),0);
                for(std::size_t i=0;i<active.size();++i)if(active[i]) {
                    if(!field.cells[i].value)throw std::runtime_error("synthetic rainfall mask changed");
                    const double cumulative=*field.cells[i].value/1000;
                    rain_increment[i]=(cumulative-previous_rain[i])/20;
                    previous_rain[i]=cumulative;
                }
                // Uniform rainfall delivery inside each ten-minute interval,
                // integrated with twenty 30-second routing steps.
                for(int step=0;step<20;++step) {
                    const auto routed=advance_surface_water(elevation,water_depth,rain_increment,active,g,{30,0.002});
                    water_depth=routed.depth_m;cumulative_rain_volume+=routed.rainfall_volume_m3;
                }
            }
            Json depths=Json::array();double stored=0;
            for(std::size_t i=0;i<active.size();++i) {
                depths.push_back(active[i]?Json(water_depth[i]):Json(nullptr));
                stored+=water_depth[i]*g.spacing_east_m*g.spacing_north_m;
            }
            water_frames.push_back(depths);volumes.push_back(stored);
            rain_volumes.push_back(cumulative_rain_volume);balance.push_back(stored-cumulative_rain_volume);
        }
        Json water={{"schema_version","terraflux.water.timeline/v1"},{"units","m"},
            {"method","head_gradient_donor_limited"},{"boundary","closed"},
            {"missing_rainfall_policy","impermeable_mask"},{"routing_speed_m_s",0.002},
            {"timestep_seconds",30},{"values_by_frame",water_frames},
            {"stored_volume_m3",volumes},{"cumulative_rainfall_volume_m3",rain_volumes},
            {"balance_error_m3",balance}};
        std::ofstream water_file(directory/"surface-water.json");
        water_file<<water.dump(2)<<'\n';water_file.close();
        if(!water_file)throw std::runtime_error("failed to write water timeline");
        std::ofstream terrain_file(directory/"terrain.json");
        terrain_file << terrain.dump(2) << '\n'; terrain_file.close();
        if (!terrain_file) throw std::runtime_error("failed to write terrain");
        std::ofstream timeline_file(directory/"forecast-frames.json");
        timeline_file << frames.dump(2) << '\n'; timeline_file.close();
        if (!timeline_file) throw std::runtime_error("failed to write timeline");
        std::ofstream forecast_file(directory/"forecast.json");
        forecast_file << result.dump(2) << '\n'; forecast_file.close();
        std::ofstream accumulation_file(directory/"accumulation.json");
        write_weather_observations_json(accumulation_file,accumulation);
        accumulation_file.close();
        if (!forecast_file || !accumulation_file) throw std::runtime_error("failed to write example");
        std::cout << "Generated synthetic-input engine forecast and rainfall accumulation\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
