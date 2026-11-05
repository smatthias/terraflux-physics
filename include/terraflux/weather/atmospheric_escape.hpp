#pragma once

namespace terraflux::weather {

// Atmospheric escape v1: what the Earth's atmosphere loses to space, and which part leaves through the polar caps.
//
// 1. Hydrogen. Every hydrogen-bearing molecule that crosses the cold tropopause (water vapour, methane, H2) is split
//    higher up, and the hydrogen diffuses to the exobase. Today the escape is limited by this diffusion (Hunten 1973):
//        Phi = 2.5e13 f_T  H2 molecules cm^-2 s^-1,  f_T = f(H2) + f(H2O) + 2 f(CH4)
//    (about 2e27 H atoms/s, 3-4 kg/s). Above the exobase the hydrogen leaves by three routes that share this supply:
//    Jeans escape (thermal, rising steeply with the exospheric temperature and so with solar activity), charge exchange
//    with hot plasmaspheric protons, and the polar wind: H+ flowing out along the open field lines of the polar caps.
//    The exospheric hydrogen (a reservoir of about half a day's escape) adjusts so that the routes together carry the
//    supply; in a storm they briefly carry more.
// 2. Heavy ions. Oxygen is far too heavy to escape thermally. It leaves only as O+ accelerated out of the ionosphere
//    along open or stretched field lines: the cleft ion fountain at the cusp and the nightside auroral oval. The rate
//    rises exponentially with geomagnetic activity and with solar EUV (after Yau et al. 1988, Slapak et al. 2017); part
//    of it returns to the ring current and the plasma sheet.
// 3. Helium (radiogenic He from the crust) escapes mainly as He+ in the polar wind.

struct HydrogenEscapeConfig {
    double water_ppmv{3.8};                      // entering the stratosphere (above the cold trap)
    double methane_ppmv{1.93};
    double hydrogen_ppmv{0.53};
    double limiting_flux_coefficient{2.5e13};    // H2 molecules cm^-2 s^-1 per unit mixing ratio (Hunten 1973)
    double homopause_altitude_m{100.0e3};
    double exobase_altitude_m{500.0e3};
    double jeans_share_at_1000k{0.3};            // calibrates the charge-exchange route
    double polar_wind_velocity_m_s{14.0};        // H+ outflow per exobase H density on open field lines (about 2e8 cm^-2 s^-1)
};
void validate_hydrogen_escape(const HydrogenEscapeConfig& config);

double total_hydrogen_mixing_ratio(const HydrogenEscapeConfig& config);    // H2 equivalents (mole fraction)
double diffusion_limited_hydrogen_per_s(const HydrogenEscapeConfig& config);   // H atoms per second
// Jeans effusion velocity (m/s): the escape flux divided by the exobase density.
double jeans_effusion_velocity_m_s(double temperature_k, double mass_amu, double radius_m);
double charge_exchange_velocity_m_s(const HydrogenEscapeConfig& config);

struct HydrogenEscape {
    double supply_per_s{0.0};
    double jeans_per_s{0.0}, charge_exchange_per_s{0.0}, polar_wind_per_s{0.0};
    double total_per_s{0.0};
    double exobase_density_m3{0.0};
    double reservoir_atoms{0.0};
};
HydrogenEscape hydrogen_escape_equilibrium(double exospheric_k, double open_area_m2, const HydrogenEscapeConfig& config);
// One step of the exospheric reservoir (relaxes towards equilibrium within about half a day).
HydrogenEscape step_hydrogen_escape(double reservoir_atoms, double exospheric_k, double open_area_m2, double hours, const HydrogenEscapeConfig& config);

struct IonOutflowConfig {
    double reference_rate_per_s{5.0e24};         // O+ upflow at Kp 0 and F10.7 = reference
    double kp_coefficient{0.45};                 // rate ~ exp(a Kp)
    double f107_coefficient{0.012};              // rate ~ exp(b (F10.7 - reference))
    double f107_reference{100.0};
    double escape_fraction{0.5};                 // the rest returns to the ring current and plasma sheet
    double helium_rate_per_s{7.5e24};            // He escape at the quiet open area and the reference F10.7
};
void validate_ion_outflow(const IonOutflowConfig& config);
// Upflowing O+ (ions/s); area_ratio scales with the cap-and-oval area (used for other dipole strengths).
double oxygen_upflow_per_s(double kp, double f107, double area_ratio, const IonOutflowConfig& config);
double oxygen_escape_per_s(double kp, double f107, double area_ratio, const IonOutflowConfig& config);
// Expectation of exp(a Kp) over a month with Kp spread as a normal distribution (the escape is convex in Kp).
double oxygen_escape_monthly_per_s(double mean_kp, double kp_sd, double f107, double area_ratio, const IonOutflowConfig& config);
double helium_escape_per_s(double open_area_ratio, double f107, const IonOutflowConfig& config);

}  // namespace terraflux::weather
