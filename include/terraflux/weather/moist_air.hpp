#pragma once

namespace terraflux::weather {

// Thermodynamic relations between pressure, temperature, humidity, and air
// density for a single unsaturated layer. All inputs and outputs are SI:
// pressure in Pa, temperature in K, specific humidity in kg/kg (mass of
// water vapour per mass of moist air), density in kg/m^3.
//
//   e_s(T)        = 610.94 exp(17.625 t / (t + 243.04)),  t = T - 273.15 [°C]
//                   (Magnus form, Alduchov & Eskridge 1996, over liquid water)
//   e(q, p)       = q p / (eps + (1 - eps) q)
//   q(e, p)       = eps e / (p - (1 - eps) e)
//   T_v(T, q)     = T (1 + (1/eps - 1) q) = T (1 + 0.608 q)
//   rho(p, T, q)  = p / (R_d T_v)
//   RH(T, q, p)   = e / e_s
//
// with R_d = 287.05 J/(kg K) and eps = R_d / R_v = 0.622.

namespace moist_air {
constexpr double kDryAirGasConstantJkgK = 287.05;
constexpr double kWaterVapourGasConstantJkgK = 461.5;
constexpr double kEpsilon = 0.622;
constexpr double kVirtualTemperatureFactor = 0.608;  // 1/eps - 1
constexpr double kCelsiusOffsetK = 273.15;
constexpr double kMagnusA = 610.94;   // Pa
constexpr double kMagnusB = 17.625;   // dimensionless
constexpr double kMagnusC = 243.04;   // °C
}  // namespace moist_air

// Each function throws std::invalid_argument for non-finite or out-of-range
// inputs (T <= 0 K, p <= 0 Pa, q outside [0, 1), vapour pressure >= p).
double saturation_vapour_pressure_pa(double air_temperature_k);
double vapour_pressure_pa(double specific_humidity_kg_kg, double pressure_pa);
double specific_humidity_kg_kg(double vapour_pressure_pa, double pressure_pa);
double saturation_specific_humidity_kg_kg(double air_temperature_k, double pressure_pa);
double virtual_temperature_k(double air_temperature_k, double specific_humidity_kg_kg);
double moist_air_density_kg_m3(
    double pressure_pa,
    double air_temperature_k,
    double specific_humidity_kg_kg);
// Fraction (1.0 = saturated). Values above 1 indicate supersaturation, which
// this layer does not remove; no condensation is modelled.
double relative_humidity_fraction(
    double air_temperature_k,
    double specific_humidity_kg_kg,
    double pressure_pa);

// Wet-bulb temperature from the psychrometric relation
//   e = e_s(T_w) - (c_p p / (eps L)) (T - T_w), solved by Newton iteration.
// The wet-bulb temperature decides the precipitation phase at the ground.
double wet_bulb_temperature_k(
    double air_temperature_k,
    double specific_humidity_kg_kg,
    double pressure_pa);

// Analytic partial derivatives of the relations above at one state. They
// quantify how strongly the three coupled fields depend on each other; see
// docs/weather/moist-air-relations.md for the derivation and typical values.
struct MoistAirSensitivities {
    double air_density_kg_m3{0.0};
    double relative_humidity_fraction{0.0};
    // d(rho)/dT at fixed p and q                       [kg/m^3 per K]
    double density_per_kelvin{0.0};
    // d(rho)/dq at fixed p and T                       [kg/m^3 per kg/kg]
    double density_per_specific_humidity{0.0};
    // d(rho)/dp at fixed T and q                       [kg/m^3 per Pa]
    double density_per_pascal{0.0};
    // d(ln e_s)/dT                                     [1/K]
    double log_saturation_pressure_per_kelvin{0.0};
    // d(RH)/dT at fixed q and p                        [fraction per K]
    double relative_humidity_per_kelvin{0.0};
    // d(RH)/dq at fixed T and p                        [fraction per kg/kg]
    double relative_humidity_per_specific_humidity{0.0};
};

MoistAirSensitivities moist_air_sensitivities(
    double pressure_pa,
    double air_temperature_k,
    double specific_humidity_kg_kg);

}  // namespace terraflux::weather
