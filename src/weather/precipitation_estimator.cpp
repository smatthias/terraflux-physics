#include "terraflux/weather/precipitation_estimator.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace terraflux::weather {
namespace {

std::string relation_description(const ReflectivityRainfallRelation& relation) {
    std::ostringstream description;
    description << std::setprecision(17)
                << "Z=" << relation.coefficient << "*R^" << relation.exponent
                << "; Z=10^(dBZ/10); R in mm/h; empirical radar estimate";
    return description.str();
}

}  // namespace

std::vector<WeatherObservation> estimate_rain_rate(
    const std::vector<WeatherObservation>& reflectivity,
    ReflectivityRainfallRelation relation) {
    if (!std::isfinite(relation.coefficient) || relation.coefficient <= 0.0 ||
        !std::isfinite(relation.exponent) || relation.exponent <= 0.0) {
        throw std::invalid_argument(
            "Z-R coefficient and exponent must be finite and greater than zero");
    }

    const std::string derivation = relation_description(relation);
    std::vector<WeatherObservation> estimates;
    estimates.reserve(reflectivity.size());

    for (const WeatherObservation& input : reflectivity) {
        if (input.variable != "radar_reflectivity" || input.units != "dBZ") {
            throw std::invalid_argument(
                "rain-rate estimation requires radar_reflectivity observations in dBZ");
        }

        WeatherObservation output = input;
        output.variable = "precipitation_rate";
        output.units = "mm/h";
        output.interpretation = "derived_empirical_conversion";
        output.derivation = derivation;

        if (input.status == ObservationStatus::decoded) {
            if (!input.value || !std::isfinite(*input.value)) {
                throw std::invalid_argument(
                    "decoded reflectivity observations require finite dBZ values");
            }
            const double reflectivity_factor = std::pow(10.0, *input.value / 10.0);
            const double normalized_factor = reflectivity_factor / relation.coefficient;
            const double rain_rate =
                std::pow(normalized_factor, 1.0 / relation.exponent);
            if (!std::isfinite(reflectivity_factor) || !std::isfinite(rain_rate)) {
                throw std::overflow_error(
                    "reflectivity produced a non-finite rain-rate estimate");
            }
            output.value = rain_rate;
            output.reason.clear();
        } else {
            output.value.reset();
        }

        estimates.push_back(std::move(output));
    }

    return estimates;
}

}  // namespace terraflux::weather
