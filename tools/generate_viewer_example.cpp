// Deterministic synthetic scenario, evaluated by the production physics kernels.
#include "terraflux/weather/transport_model.hpp"
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
