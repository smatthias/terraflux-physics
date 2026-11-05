#pragma once

#include <vector>

namespace terraflux::weather {

// Sea-level rise v1: the global-mean budget from a warming path and the ice-sheet contribution of a polar-melt
// run, the regional rise with fingerprints and land motion, and the coastal helpers (extreme water levels, their
// change in frequency, wind setup, shoreline retreat).
//
// Budget (annual steps; warming dT relative to pre-industrial, K):
//   thermal expansion   dE/dt = beta (dT - T_d),  dT_d/dt = (dT - T_d) / tau      (the deep ocean takes up heat on
//                       tau; expansion slows as it warms: E -> beta tau dT, the equilibrium of beta tau m/K)
//   glaciers            dG/dt = gamma max(0, dT) (1 - G / G_max)                 (a finite store of G_max at the first year)
//   Antarctica          dA/dt = a0 + a1 max(0, dT - T_a)                          (ice discharge and surface melt;
//                       deep uncertainty: the defaults follow AR6 medians, not the high-end storylines)
//   land water          dW/dt = w (groundwater depletion minus reservoir storage)
//   Greenland           taken from the polar-melt run (sea_level_rise_vs_control_m)
// Every component is counted from the reference year (today), so the rise is the rise still to come.
// Regional rise = sum of component x fingerprint (an ice sheet losing mass pulls less water toward it: near
// Greenland the sea falls, far away it rises by up to ~1.2x the global mean; Mitrovica et al. 2001) + the land
// motion (subsidence adds to the relative rise).

struct SeaLevelBudgetConfig {
    int reference_year{2020};
    double thermal_expansion_m_per_k_yr{0.0011};    // beta
    double deep_ocean_time_scale_years{400.0};      // tau
    double glacier_m_per_k_yr{0.0010};             // gamma
    double glacier_store_m{0.40};                  // G_max: mountain glaciers at the first year (about 0.32 m remain today)
    double antarctic_base_m_yr{0.0005};            // a0
    double antarctic_m_per_k_yr{0.0005};           // a1
    double antarctic_threshold_k{1.2};             // T_a: today's warming
    double land_water_m_yr{0.0003};                // w
    double greenland_fingerprint{1.0};
    double antarctic_fingerprint{1.0};
    double glacier_fingerprint{1.0};
};

struct SeaLevelComponents {
    std::vector<int> years;                        // annual
    std::vector<double> warming_k;
    std::vector<double> thermal_expansion_m;
    std::vector<double> glaciers_m;
    std::vector<double> antarctic_m;
    std::vector<double> greenland_m;
    std::vector<double> land_water_m;
    std::vector<double> global_m;                  // sum (global mean), 0 at the reference year
    std::vector<double> regional_m;                // with the fingerprints, before land motion
};

void validate_sea_level_budget(const SeaLevelBudgetConfig& config);

// `years` must be consecutive (annual) and contain the reference year; `warming_k` and `greenland_m` are given
// for every year (greenland_m in any reference: it is re-counted from the reference year).
SeaLevelComponents sea_level_budget(const std::vector<int>& years, const std::vector<double>& warming_k, const std::vector<double>& greenland_m,
                                    const SeaLevelBudgetConfig& config);

// Extreme water levels: annual maxima follow a Gumbel distribution (location mu, scale sigma).
// Level with return period T years: mu - sigma ln(-ln(1 - 1/T)).
double gumbel_return_level(double location_m, double scale_m, double return_period_years);
// Return period (years) of a level z: 1 / (1 - exp(-exp(-(z - mu) / sigma))).
double gumbel_return_period(double location_m, double scale_m, double level_m);
// After a relative rise, how often today's T-year level is reached: the distribution shifts up by the rise, so
// the new return period is gumbel_return_period(mu + rise, sigma, z_T) ~ T exp(-rise / sigma) (Hunter 2012).
double return_period_after_rise(double location_m, double scale_m, double return_period_years, double rise_m);

// Wind setup of a steady onshore wind over water of uniform depth h and fetch L without bottom friction:
// d(eta)/dx = tau / (rho g (h + eta))  =>  (h + eta)^2 = h^2 + 2 tau L / (rho g).
double uniform_wind_setup_m(double depth_m, double fetch_m, double wind_stress_pa, double water_density_kg_m3 = 1025.0, double gravity_m_s2 = 9.81);

// Bruun rule: a sandy shore in equilibrium with its profile retreats R = S L / (B + h*) for a rise S, with the
// active profile width L, the berm height B and the closure depth h* (typically 50-100 m per metre of rise).
double bruun_retreat_m(double rise_m, double active_profile_width_m, double berm_height_m, double closure_depth_m);

}  // namespace terraflux::weather
