#pragma once

namespace terraflux::weather {

// Ocean carbon v1: seawater carbonate chemistry, air-sea gas exchange and the habitability indices.
//
// Carbonate system on the total pH scale from dissolved inorganic carbon (DIC) and total alkalinity (TA):
//   TA = [HCO3-] + 2 [CO3--] + [B(OH)4-] + [OH-] - [H+]         (phosphate, silicate, sulfate and fluoride omitted)
// solved for [H+] (Newton with bisection safeguard); the constants are the usual CO2SYS choices:
//   K0 Weiss (1974), K1 K2 Lueker et al. (2000), KB Dickson (1990), KW Millero (1995), total boron Uppstrom (1974),
//   aragonite and calcite solubility Mucci (1983), calcium Riley & Tongudai (1967).
// pCO2 = [CO2*] / K0 (the fugacity correction of ~0.3 % is neglected). The saturation state
// Omega = [Ca++][CO3--] / Ksp: below 1 shells and skeletons of aragonite dissolve (corrosive water).
//
// Air-sea exchange (Wanninkhof 2014): k = 0.251 U10^2 (Sc / 660)^-1/2 cm/h with the seawater Schmidt numbers of
// CO2 and O2; F_CO2 = k K0 (pCO2_air - pCO2_sea), F_O2 = k (O2_sat - O2) (positive into the ocean).
//
// Metabolic index (Deutsch et al. 2015): Phi = pO2 / pO2_crit(T), pO2_crit(T) = pO2_crit(T_ref) exp(-E0/kB (1/T - 1/T_ref));
// warm water raises the oxygen an animal needs. Sustained activity needs Phi >= Phi_crit (about 3).

struct CarbonateConstants {
    double k0{0.0};          // CO2 solubility, mol kg-1 atm-1
    double k1{0.0}, k2{0.0}; // carbonic acid, mol/kg (total scale)
    double kb{0.0};          // boric acid
    double kw{0.0};          // water, (mol/kg)^2
    double ksp_aragonite{0.0}, ksp_calcite{0.0};   // (mol/kg)^2
    double total_boron{0.0}; // mol/kg
    double calcium{0.0};     // mol/kg
};

// Throws std::invalid_argument outside -2..40 degC or 0..45 psu.
CarbonateConstants carbonate_constants(double temperature_k, double salinity_psu);

struct CarbonateState {
    double ph_total{8.1};
    double co2_umol_kg{0.0}, bicarbonate_umol_kg{0.0}, carbonate_umol_kg{0.0};
    double pco2_uatm{0.0};
    double omega_aragonite{0.0}, omega_calcite{0.0};
};

// Speciation from DIC and TA (umol/kg). Throws std::invalid_argument for non-physical inputs.
CarbonateState solve_carbonate(double dic_umol_kg, double alkalinity_umol_kg, double temperature_k, double salinity_psu);
// DIC (umol/kg) of water with the given alkalinity in equilibrium with pCO2 (uatm).
double dic_at_equilibrium(double pco2_uatm, double alkalinity_umol_kg, double temperature_k, double salinity_psu);
// Revelle (buffer) factor (dpCO2/pCO2)/(dDIC/DIC) at constant alkalinity.
double revelle_factor(double dic_umol_kg, double alkalinity_umol_kg, double temperature_k, double salinity_psu);

// Schmidt numbers in seawater (Wanninkhof 2014), temperature in K (valid -2..40 degC).
double co2_schmidt_number(double temperature_k);
double o2_schmidt_number(double temperature_k);
// Gas transfer velocity (m/s) for a 10 m wind speed and a Schmidt number.
double gas_transfer_velocity_m_s(double wind_speed_m_s, double schmidt_number);

// Metabolic index of a water parcel: pO2 (kPa) from the oxygen content and its saturation, against the
// temperature-dependent critical pO2 of a representative species.
struct MetabolicIndexConfig {
    double critical_po2_kpa{2.0};      // resting hypoxic threshold at the reference temperature
    double reference_temperature_k{288.15};
    double temperature_sensitivity_ev{0.4};   // E0
    double ecological_threshold{3.0};  // Phi_crit for sustained activity
};
double oxygen_partial_pressure_kpa(double oxygen_umol_kg, double temperature_k, double salinity_psu);
double metabolic_index(double oxygen_umol_kg, double temperature_k, double salinity_psu, const MetabolicIndexConfig& config);

}  // namespace terraflux::weather
