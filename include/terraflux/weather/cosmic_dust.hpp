#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Cosmic dust v1: the interplanetary dust and small meteoroids that enter the atmosphere every day (tens of tonnes),
// what burns up, what reaches the ground, where the extraterrestrial water goes, and how much it all matters.
//
// 1. Sizes. The interplanetary flux at 1 AU of Gruen et al. (1985), cumulative number of particles heavier than m
//    (grams) per square metre and second:
//        F(m) = (c4 m^g4 + c5)^g5 + c6 (m + c7 m^g6 + c8 m^g7)^g8 + c9 (m + c10 m^g6)^g9
//    gives the shape of the mass distribution (most mass between 10^-7 and 10^-4 g, 50-500 um). Integrated over the
//    Earth it gives about 20 t/day (30 with gravitational focusing); the total is a scenario input because estimates
//    range from about 5 to 300 t/day.
//
// 2. Entry. Each particle is a homogeneous sphere on a straight path through the US Standard Atmosphere 1976:
//        m dv/dt = -Gamma rho_a A v^2 + m g cos(z)                                 (drag)
//        c m dT/dt = Lambda rho_a A v^3 / 2 - 4 pi r^2 eps sigma (T^4 - T_a^4) - L J 4 pi r^2   (heating, radiation,
//        dm/dt = -4 pi r^2 J,   J = p_v(T) sqrt(mu / (2 pi R T)),  p_v = 10^(A - B/T) Pa       evaporation)
//    (Love & Brownlee 1991 in simplified form). The temperature step is implicit. Micrometeoroids heat and cool within
//    seconds between 120 and 70 km: small or slow ones survive unmelted, larger ones melt into cosmic spherules, and
//    the fastest and largest evaporate, releasing metal atoms that recondense as meteoric smoke.
//
// 3. Water. A mass fraction of the dust is water bound in hydrated minerals (carbonaceous, CI/CM-like material) and
//    in solar-wind-implanted rims. It is released progressively as the particle heats from T_d1 to T_d2 (or with the
//    evaporating mass): to the mesosphere and lower thermosphere. What stays cool reaches the ground.

// ---- Sizes --------------------------------------------------------------------------------------------------------

double gruen_cumulative_flux(double mass_g);

struct DustMassBin {
    double lower_g{0.0}, upper_g{0.0}, mass_g{0.0};   // mass_g: geometric middle
    double mass_fraction{0.0};                        // of the total mass flux between the limits
    double number_fraction{0.0};
};
// Logarithmic bins between min_g and max_g with the Gruen mass and number fractions (normalised over the range).
std::vector<DustMassBin> gruen_mass_bins(double min_g, double max_g, std::size_t bins);
// The Gruen mass flux at Earth (t/day) between the limits, without gravitational focusing.
double gruen_mass_flux_t_per_day(double min_g, double max_g, double earth_radius_m = 6.371e6);

// ---- The atmosphere (US Standard Atmosphere 1976, log-linear between 10 km nodes to 200 km) ----------------------

double standard_air_density_kg_m3(double altitude_m);
double standard_air_pressure_pa(double altitude_m);

// ---- Entry --------------------------------------------------------------------------------------------------------

struct DustEntryConfig {
    double particle_density_kg_m3{2000.0};
    double specific_heat_j_kg_k{1000.0};
    double heat_transfer{1.0};                   // Lambda: share of the incident air's energy that heats the particle
    double drag{1.0};                            // Gamma
    double emissivity{1.0};
    double latent_heat_j_kg{6.0e6};              // evaporation of silicate melt
    double vapour_a{8.7};                        // log10 p_v (Pa) = A - B / T: calibrated so that about a quarter of the mass
                                                 // reaches the ground, as the Antarctic micrometeorite collections suggest
    double vapour_b{13500.0};
    double vapour_molar_mass_kg{0.045};
    double melt_start_k{1200.0};                 // partially melted (scoriaceous) above
    double melt_full_k{1650.0};                  // fully melted (cosmic spherule) above
    double dehydration_start_k{700.0};           // phyllosilicates lose their bound water at about 500-800 C
    double dehydration_end_k{1100.0};
    double start_altitude_m{180.0e3};
    double air_temperature_k{220.0};
    double max_step_m{40.0};
};
void validate_dust_entry(const DustEntryConfig& config);

enum class DustFate : unsigned char { unmelted = 0, partially_melted = 1, melted = 2, vaporised = 3 };
const char* dust_fate_name(DustFate fate);

struct DustTrajectory {
    double initial_mass_kg{0.0}, speed_m_s{0.0}, zenith_deg{0.0};
    double peak_temperature_k{0.0}, peak_altitude_m{0.0};
    double surviving_fraction{1.0};
    double water_released_fraction{0.0};         // of the particle's water
    DustFate fate{DustFate::unmelted};
    std::vector<double> ablated_fraction_by_bin;   // of the initial mass, per altitude bin
    std::vector<double> water_fraction_by_bin;     // of the initial water, per altitude bin
    std::vector<double> energy_j_by_bin;           // kinetic energy given to the air, per altitude bin
};
// Altitude bins given by ascending edges (m); released mass or water below the lowest edge goes to the first bin.
DustTrajectory simulate_dust_entry(double mass_kg, double speed_m_s, double zenith_deg, const DustEntryConfig& config, const std::vector<double>& bin_edges_m);

// ---- The population -----------------------------------------------------------------------------------------------

struct DustSpeed {
    double speed_km_s{15.0};
    double weight{1.0};                          // share of the mass flux
};
struct DustComposition {
    double water{0.05};                          // mass fractions
    double iron{0.18};
    double sodium{0.005};
    double magnesium{0.096};
    double silicon{0.107};
    double nickel{0.011};
};
struct DustPopulationConfig {
    double influx_t_per_day{60.0};
    double min_mass_g{1.0e-12}, max_mass_g{1.0};
    std::size_t mass_bins{36};
    std::vector<DustSpeed> speeds;               // entry speeds (>= 11.2 km/s) and mass weights
    std::vector<double> zenith_deg{20.7, 37.8, 52.2, 69.3};   // quartiles of an isotropic flux on a horizontal surface
    DustComposition composition;
};
std::vector<DustSpeed> default_dust_speeds();
void validate_dust_population(const DustPopulationConfig& config);

struct DustBinResult {
    DustMassBin bin;
    double mass_flux_kg_s{0.0};
    double surviving_fraction{0.0};
    double fate_fraction[4]{0.0, 0.0, 0.0, 0.0};   // mass shares arriving unmelted, partly melted, melted, vaporised (by entry)
    double water_released_fraction{0.0};
    double mean_peak_temperature_k{0.0};
};

struct DustPopulationResult {
    double influx_kg_s{0.0};
    double ablated_kg_s{0.0};                    // evaporated: metal atoms and meteoric smoke
    double surviving_kg_s{0.0};                  // reaching the ground
    double surviving_by_fate_kg_s[4]{0.0, 0.0, 0.0, 0.0};   // of the surviving mass, by the state of the particle
    double water_in_kg_s{0.0}, water_released_kg_s{0.0}, water_to_surface_kg_s{0.0};
    double kinetic_power_w{0.0};
    std::vector<double> altitude_edges_m;
    std::vector<double> ablation_kg_s_by_bin, water_kg_s_by_bin, energy_w_by_bin;
    std::vector<DustBinResult> bins;
    double water_released_above_80km_kg_s{0.0};
    double survivors_over_50um_per_s{0.0}, survivors_over_100um_per_s{0.0};   // particles reaching the ground (final diameter)
    double median_ablation_altitude_m{0.0}, median_water_altitude_m{0.0};
};
DustPopulationResult simulate_dust_population(const DustPopulationConfig& population, const DustEntryConfig& entry, const std::vector<double>& altitude_edges_m);

// ---- Where it arrives: the sporadic meteoroid sources --------------------------------------------------------------

// Point sources on the sky, fixed relative to the apex of the Earth's way (90 deg west of the Sun on the ecliptic):
// apex, helion, antihelion and the toroidal sources. Flux on a horizontal surface ~ max(0, cos psi) for a source at
// zenith distance psi, plus an isotropic share for the sources' spread; the map is normalised to a global mean of 1.
struct MeteoroidSource {
    std::string name;
    double longitude_from_apex_deg{0.0};         // along the ecliptic, positive toward the Sun
    double ecliptic_latitude_deg{0.0};
    double weight{0.0};
};
std::vector<MeteoroidSource> default_meteoroid_sources();
double solar_ecliptic_longitude_deg(double days_since_j2000);
double greenwich_sidereal_deg(double days_since_j2000);
// Relative influx (global mean 1) at a point and time; days since 2000-01-01 12:00 UT.
double relative_dust_influx(double latitude_deg, double longitude_deg, double days_since_j2000, const std::vector<MeteoroidSource>& sources, double isotropic_share);
// The sources' right ascension and declination at a time (degrees).
std::array<double, 2> meteoroid_source_equatorial(const MeteoroidSource& source, double days_since_j2000);

}  // namespace terraflux::weather
