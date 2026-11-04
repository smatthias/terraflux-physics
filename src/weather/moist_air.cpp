#include "terraflux/weather/moist_air.hpp"

#include <cmath>
#include <stdexcept>

namespace terraflux::weather {
namespace {

using namespace moist_air;

void require_temperature(double temperature_k) {
    if (!std::isfinite(temperature_k) || temperature_k <= 0.0) {
        throw std::invalid_argument("air temperature must be finite and greater than 0 K");
    }
}

void require_pressure(double pressure_pa) {
    if (!std::isfinite(pressure_pa) || pressure_pa <= 0.0) {
        throw std::invalid_argument("pressure must be finite and greater than 0 Pa");
    }
}

void require_specific_humidity(double q) {
    if (!std::isfinite(q) || q < 0.0 || q >= 1.0) {
        throw std::invalid_argument("specific humidity must be finite and within [0, 1) kg/kg");
    }
}

}  // namespace

double saturation_vapour_pressure_pa(double air_temperature_k) {
    require_temperature(air_temperature_k);
    const double celsius = air_temperature_k - kCelsiusOffsetK;
    if (celsius + kMagnusC <= 0.0) {
        throw std::invalid_argument(
            "saturation vapour pressure is undefined below the Magnus singularity (-243.04 °C)");
    }
    const double value = kMagnusA * std::exp(kMagnusB * celsius / (celsius + kMagnusC));
    if (!std::isfinite(value)) {
        throw std::invalid_argument("saturation vapour pressure overflow");
    }
    return value;
}

double vapour_pressure_pa(double specific_humidity_kg_kg, double pressure_pa) {
    require_specific_humidity(specific_humidity_kg_kg);
    require_pressure(pressure_pa);
    return specific_humidity_kg_kg * pressure_pa /
           (kEpsilon + (1.0 - kEpsilon) * specific_humidity_kg_kg);
}

double specific_humidity_kg_kg(double vapour_pressure, double pressure_pa) {
    require_pressure(pressure_pa);
    if (!std::isfinite(vapour_pressure) || vapour_pressure < 0.0 ||
        vapour_pressure >= pressure_pa) {
        throw std::invalid_argument(
            "vapour pressure must be finite, non-negative, and below the total pressure");
    }
    return kEpsilon * vapour_pressure / (pressure_pa - (1.0 - kEpsilon) * vapour_pressure);
}

double saturation_specific_humidity_kg_kg(double air_temperature_k, double pressure_pa) {
    const double e_s = saturation_vapour_pressure_pa(air_temperature_k);
    require_pressure(pressure_pa);
    if (e_s >= pressure_pa) {
        throw std::invalid_argument(
            "saturation vapour pressure reaches the total pressure; the layer would boil");
    }
    return specific_humidity_kg_kg(e_s, pressure_pa);
}

double virtual_temperature_k(double air_temperature_k, double specific_humidity_kg_kg) {
    require_temperature(air_temperature_k);
    require_specific_humidity(specific_humidity_kg_kg);
    return air_temperature_k * (1.0 + kVirtualTemperatureFactor * specific_humidity_kg_kg);
}

double moist_air_density_kg_m3(
    double pressure_pa,
    double air_temperature_k,
    double specific_humidity_kg_kg) {
    require_pressure(pressure_pa);
    const double density =
        pressure_pa /
        (kDryAirGasConstantJkgK *
         virtual_temperature_k(air_temperature_k, specific_humidity_kg_kg));
    if (!std::isfinite(density) || density <= 0.0) {
        throw std::invalid_argument("moist air density must be finite and positive");
    }
    return density;
}

double relative_humidity_fraction(
    double air_temperature_k,
    double specific_humidity_kg_kg,
    double pressure_pa) {
    return vapour_pressure_pa(specific_humidity_kg_kg, pressure_pa) /
           saturation_vapour_pressure_pa(air_temperature_k);
}

double wet_bulb_temperature_k(
    double air_temperature_k,
    double specific_humidity_kg_kg,
    double pressure_pa) {
    require_temperature(air_temperature_k);
    require_specific_humidity(specific_humidity_kg_kg);
    require_pressure(pressure_pa);
    const double e = vapour_pressure_pa(specific_humidity_kg_kg, pressure_pa);
    const double gamma = 1004.6 * pressure_pa / (kEpsilon * 2.501e6);  // psychrometric constant, Pa/K
    double t_w = air_temperature_k;
    for (int iteration = 0; iteration < 50; ++iteration) {
        const double f = saturation_vapour_pressure_pa(t_w) - e - gamma * (air_temperature_k - t_w);
        const double celsius = t_w - kCelsiusOffsetK;
        const double des_dt = saturation_vapour_pressure_pa(t_w) * kMagnusB * kMagnusC /
                              ((celsius + kMagnusC) * (celsius + kMagnusC));
        const double step = f / (des_dt + gamma);
        t_w -= step;
        if (std::abs(step) < 1.0e-10) {
            break;
        }
    }
    if (!std::isfinite(t_w) || t_w > air_temperature_k + 1.0e-9) {
        throw std::invalid_argument("wet-bulb iteration failed");
    }
    return t_w;
}

MoistAirSensitivities moist_air_sensitivities(
    double pressure_pa,
    double air_temperature_k,
    double specific_humidity_kg_kg) {
    MoistAirSensitivities s;
    const double q = specific_humidity_kg_kg;
    const double rho = moist_air_density_kg_m3(pressure_pa, air_temperature_k, q);
    const double e = vapour_pressure_pa(q, pressure_pa);
    const double e_s = saturation_vapour_pressure_pa(air_temperature_k);
    const double celsius = air_temperature_k - kCelsiusOffsetK;

    s.air_density_kg_m3 = rho;
    s.relative_humidity_fraction = e / e_s;
    // rho = p / (R_d T (1 + 0.608 q))
    s.density_per_kelvin = -rho / air_temperature_k;
    s.density_per_specific_humidity =
        -rho * kVirtualTemperatureFactor / (1.0 + kVirtualTemperatureFactor * q);
    s.density_per_pascal = rho / pressure_pa;
    // ln e_s = ln A + B t / (t + C)  =>  d/dT = B C / (t + C)^2
    s.log_saturation_pressure_per_kelvin =
        kMagnusB * kMagnusC / ((celsius + kMagnusC) * (celsius + kMagnusC));
    // RH = e(q, p) / e_s(T)
    s.relative_humidity_per_kelvin =
        -s.relative_humidity_fraction * s.log_saturation_pressure_per_kelvin;
    // de/dq = p eps / (eps + (1 - eps) q)^2
    const double denominator = kEpsilon + (1.0 - kEpsilon) * q;
    s.relative_humidity_per_specific_humidity =
        pressure_pa * kEpsilon / (denominator * denominator) / e_s;
    return s;
}

}  // namespace terraflux::weather
