#include "terraflux/weather/temperature_observation_pipeline.hpp"

#include "terraflux/weather/observation_grid.hpp"
#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <vector>

namespace terraflux::weather {

DryTransportState advance_dry_transport_from_observation_json(
    std::istream& observation_json,
    const DryTransportState& initial,
    const DryTransportConfig& config) {
    const std::vector<WeatherObservation> observations =
        read_weather_observations_json(observation_json);
    const std::vector<TimeIndexedTemperatureField> updates =
        map_air_temperature_observations_to_updates(observations, config);
    return advance_dry_transport_with_temperature_observations(
        initial, config, updates);
}

}  // namespace terraflux::weather
