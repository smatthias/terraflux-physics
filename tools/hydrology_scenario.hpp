#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace terraflux::example {
inline nlohmann::json default_hydrology_scenario(){
    return {{"schema_version","terraflux.water.scenario/v1"},{"timestep_seconds",30},
        {"routing_speed_m_s",0.002},{"infiltration_rate_m_s",0.000001},
        {"soil_storage_capacity_m",0.0015},{"evaporation_rate_m_s",0.00000005},
        {"drainage_rate_m_s",0.0000001},{"boundary","closed"},
        {"missing_rainfall_policy","impermeable_mask"},
        {"process_order","rain_route_infiltrate_evaporate_drain"}};
}
inline void validate_hydrology_scenario(const nlohmann::json& value){
    const auto defaults=default_hydrology_scenario();
    if(!value.is_object()||value.size()!=defaults.size())throw std::invalid_argument("scenario must contain exactly the supported keys");
    for(auto item=defaults.begin();item!=defaults.end();++item){
        if(!value.contains(item.key()))throw std::invalid_argument("scenario missing key: "+item.key());
        if(item.value().is_string()){
            if(value.at(item.key())!=item.value())throw std::invalid_argument("unsupported scenario setting: "+item.key());
        }else{
            const auto& number=value.at(item.key());
            if(!number.is_number()||!std::isfinite(number.get<double>())||number.get<double>()<0)
                throw std::invalid_argument("scenario requires a finite nonnegative number: "+item.key());
        }
    }
    const auto& dt=value.at("timestep_seconds");
    if(!dt.is_number_integer()||dt.get<double>()<1||dt.get<double>()>600||600%dt.get<int>()!=0)
        throw std::invalid_argument("water timestep must be an integer divisor of 600 seconds, from 1 to 600");
}
}
