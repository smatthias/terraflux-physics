#include "terraflux/weather/moist_air.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace terraflux::weather;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

void reference_states_match_standard_values() {
    // ISA sea level, dry: 1.225 kg/m^3.
    require_near(moist_air_density_kg_m3(101325.0, 288.15, 0.0), 1.2250, "dry ISA density", 5.0e-4);
    // Magnus form is anchored at 610.94 Pa at 0 °C; ~2.34 kPa at 20 °C.
    require_near(saturation_vapour_pressure_pa(273.15), 610.94, "e_s at 0 °C", 1.0e-9);
    require_near(saturation_vapour_pressure_pa(293.15), 2333.0, "e_s at 20 °C", 5.0);
    // Saturation specific humidity at 20 °C, sea level is ~14.45 g/kg (mixing ratio ~14.7 g/kg).
    require_near(saturation_specific_humidity_kg_kg(293.15, 101325.0), 0.01445, "q_s at 20 °C", 1.0e-4);
    // 8 g/kg at 15 °C is about 76 % relative humidity.
    require_near(relative_humidity_fraction(288.15, 0.008, 101325.0), 0.762, "RH at 15 °C, 8 g/kg", 2.0e-3);
}

void humidity_conversions_round_trip() {
    for (double q : {0.0, 0.002, 0.008, 0.02}) {
        const double e = vapour_pressure_pa(q, 95000.0);
        require_near(specific_humidity_kg_kg(e, 95000.0), q, "q -> e -> q round trip", 1.0e-15);
    }
    require_near(virtual_temperature_k(290.0, 0.010), 290.0 * 1.00608, "virtual temperature", 1.0e-12);
}

void moist_air_is_lighter_and_warm_air_is_lighter() {
    const double dry = moist_air_density_kg_m3(101325.0, 288.15, 0.0);
    const double moist = moist_air_density_kg_m3(101325.0, 288.15, 0.010);
    const double warm = moist_air_density_kg_m3(101325.0, 298.15, 0.0);
    require(moist < dry, "adding water vapour at fixed p and T must lower density");
    require(warm < dry, "warming at fixed p and q must lower density");
    require_near((dry - moist) / dry, 0.00604, "10 g/kg lowers density by ~0.6 %", 1.0e-4);
    require_near((dry - warm) / dry, 10.0 / 298.15, "10 K lowers density by T0/T1", 1.0e-12);
}

void sensitivities_match_finite_differences() {
    const double p = 101325.0, t = 288.15, q = 0.008;
    const auto s = moist_air_sensitivities(p, t, q);
    const double dt = 1.0e-3, dq = 1.0e-6, dp = 1.0;
    const auto central = [](double plus, double minus, double h) { return (plus - minus) / (2.0 * h); };

    require_near(s.density_per_kelvin,
                 central(moist_air_density_kg_m3(p, t + dt, q), moist_air_density_kg_m3(p, t - dt, q), dt),
                 "d rho / dT", 1.0e-8);
    require_near(s.density_per_specific_humidity,
                 central(moist_air_density_kg_m3(p, t, q + dq), moist_air_density_kg_m3(p, t, q - dq), dq),
                 "d rho / dq", 1.0e-6);
    require_near(s.density_per_pascal,
                 central(moist_air_density_kg_m3(p + dp, t, q), moist_air_density_kg_m3(p - dp, t, q), dp),
                 "d rho / dp", 1.0e-12);
    require_near(s.log_saturation_pressure_per_kelvin,
                 central(std::log(saturation_vapour_pressure_pa(t + dt)),
                         std::log(saturation_vapour_pressure_pa(t - dt)), dt),
                 "d ln e_s / dT", 1.0e-8);
    require_near(s.relative_humidity_per_kelvin,
                 central(relative_humidity_fraction(t + dt, q, p), relative_humidity_fraction(t - dt, q, p), dt),
                 "d RH / dT", 1.0e-8);
    require_near(s.relative_humidity_per_specific_humidity,
                 central(relative_humidity_fraction(t, q + dq, p), relative_humidity_fraction(t, q - dq, p), dq),
                 "d RH / dq", 1.0e-5);

    // Documented magnitudes (docs/weather/moist-air-relations.md).
    require_near(s.air_density_kg_m3, 1.2191, "reference density", 1.0e-3);
    require_near(s.density_per_kelvin / s.air_density_kg_m3, -0.00347, "-0.35 % per K", 2.0e-5);
    require_near(s.density_per_specific_humidity / s.air_density_kg_m3 * 0.010, -0.00605, "-0.6 % per 10 g/kg", 1.0e-4);
    require_near(s.density_per_pascal / s.air_density_kg_m3 * 1000.0, 0.00987, "+1 % per 10 hPa", 1.0e-4);
    require_near(s.log_saturation_pressure_per_kelvin, 0.0643, "e_s grows ~6.4 % per K at 15 °C", 2.0e-4);
    require_near(s.relative_humidity_per_kelvin, -0.049, "RH falls ~4.9 points per K at fixed q", 1.0e-3);

    std::cout << "state p=" << p << " Pa, T=" << t << " K, q=" << q << " kg/kg\n"
              << "  rho = " << s.air_density_kg_m3 << " kg/m^3, RH = " << s.relative_humidity_fraction << '\n'
              << "  d rho/dT = " << s.density_per_kelvin << " kg/m^3/K ("
              << 100.0 * s.density_per_kelvin / s.air_density_kg_m3 << " %/K)\n"
              << "  d rho/dq = " << s.density_per_specific_humidity << " kg/m^3 per kg/kg ("
              << 100.0 * s.density_per_specific_humidity / s.air_density_kg_m3 * 0.01 << " % per 10 g/kg)\n"
              << "  d rho/dp = " << s.density_per_pascal << " kg/m^3/Pa ("
              << 100.0 * s.density_per_pascal / s.air_density_kg_m3 * 1000.0 << " % per 10 hPa)\n"
              << "  d ln e_s/dT = " << s.log_saturation_pressure_per_kelvin << " 1/K\n"
              << "  d RH/dT = " << s.relative_humidity_per_kelvin << " 1/K, d RH/dq = "
              << s.relative_humidity_per_specific_humidity << " per kg/kg\n";
}

void invalid_inputs_are_rejected() {
    require_rejected([] { (void)saturation_vapour_pressure_pa(0.0); }, "0 K must be rejected");
    require_rejected([] { (void)saturation_vapour_pressure_pa(20.0); }, "temperature below the Magnus singularity must be rejected");
    require_rejected([] { (void)moist_air_density_kg_m3(0.0, 288.0, 0.0); }, "zero pressure must be rejected");
    require_rejected([] { (void)moist_air_density_kg_m3(1.0e5, 288.0, -0.001); }, "negative humidity must be rejected");
    require_rejected([] { (void)moist_air_density_kg_m3(1.0e5, 288.0, 1.0); }, "humidity of 1 kg/kg must be rejected");
    require_rejected([] { (void)specific_humidity_kg_kg(1.0e5, 1.0e5); }, "vapour pressure at total pressure must be rejected");
    require_rejected([] { (void)saturation_specific_humidity_kg_kg(380.0, 50000.0); }, "boiling layer must be rejected");
}

}  // namespace

int main() {
    try {
        reference_states_match_standard_values();
        humidity_conversions_round_trip();
        moist_air_is_lighter_and_warm_air_is_lighter();
        sensitivities_match_finite_differences();
        invalid_inputs_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "moist air test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "moist air relation cases passed\n";
    return 0;
}
