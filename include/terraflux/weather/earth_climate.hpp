#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Coupled climate v1: which drivers change the climate most, what the change does, and where CO2 comes from, goes to
// and hits hardest. It combines the forcing agents (greenhouse gases, aerosols, land use, the Sun, volcanoes and the
// space drivers of the other TerraFlux models) with one energy balance, splits the warming by driver (the response is
// linear in the forcing) and by feedback, and drives the impact indicators with it.
//
// 1. Forcing (effective radiative forcing, W/m2, relative to 1750/pre-industrial):
//      CO2   5.35 ln(C / C0)                                                         (Myhre et al. 1998)
//      CH4   0.036 (sqrt M - sqrt M0) - [f(M, N0) - f(M0, N0)]
//      N2O   0.12 (sqrt N - sqrt N0) - [f(M0, N) - f(M0, N0)]
//      f(M, N) = 0.47 ln(1 + 2.01e-5 (M N)^0.75 + 5.31e-15 M (M N)^1.52)          (the CH4-N2O band overlap)
//    stratospheric water vapour from methane oxidation as a share of the CH4 forcing; the other agents follow indices
//    scaled to their 2019 assessed values (AR6 best estimates, rounded).
// 2. Response: the two-layer energy balance of the solar model (mixed layer + deep ocean). The climate feedback
//    lambda = lambda_Planck - (water vapour + lapse rate) - surface albedo - clouds; the warming that each feedback
//    adds is T f_i / lambda_Planck (T lambda = F and T lambda_Planck = F + T sum f_i).
// 3. Impacts from the warming path: land and Arctic amplification, ocean heat, heat extremes (a shifted normal
//    distribution calibrated to the AR6 frequencies), heavy precipitation, Arctic sea ice, and ocean pH from the
//    carbonate chemistry.

// ---- Forcing ------------------------------------------------------------------------------------------------------

double ghg_band_overlap(double ch4_ppb, double n2o_ppb);
double methane_forcing_w_m2(double ch4_ppb, double n2o_reference_ppb = 270.0, double ch4_reference_ppb = 722.0);
double nitrous_oxide_forcing_w_m2(double n2o_ppb, double ch4_reference_ppb = 722.0, double n2o_reference_ppb = 270.0);

// A time series given by [year, value] points, linear in between and constant beyond the ends.
double interpolate_points(const std::vector<std::array<double, 2>>& points, double year);

// ---- Feedbacks and the response -----------------------------------------------------------------------------------

struct FeedbackSet {
    double planck_w_m2_k{3.22};              // restoring (positive here)
    double water_vapour_lapse_rate_w_m2_k{1.30};   // amplifying
    double surface_albedo_w_m2_k{0.35};
    double clouds_w_m2_k{0.42};
    double other_w_m2_k{0.0};
};
void validate_feedbacks(const FeedbackSet& feedbacks);
double net_feedback_w_m2_k(const FeedbackSet& feedbacks);
// Warming (K) added by each amplifying feedback for a total warming T: T f_i / lambda_Planck; the rest is the
// no-feedback (Planck) response T lambda / lambda_Planck.
struct FeedbackShares {
    double no_feedback_k{0.0}, water_vapour_lapse_rate_k{0.0}, surface_albedo_k{0.0}, clouds_k{0.0}, other_k{0.0};
};
FeedbackShares feedback_shares(double warming_k, const FeedbackSet& feedbacks);

struct TwoLayerClimate {
    double upper_heat_capacity{7.3};         // W yr m-2 K-1
    double deep_heat_capacity{106.0};
    double exchange_w_m2_k{0.74};
    double feedback_w_m2_k{1.15};
};
struct TwoLayerState {
    double surface_k{0.0}, deep_k{0.0};
};
TwoLayerState step_two_layer(const TwoLayerState& state, double forcing_w_m2, double dt_years, const TwoLayerClimate& climate);
// Transient climate response: warming at the time of doubling in a 1 %/yr CO2 run (70 years), and the equilibrium.
double transient_climate_response_k(const TwoLayerClimate& climate, double doubling_forcing_w_m2 = 3.71);

// ---- Impacts ------------------------------------------------------------------------------------------------------

// How much more often the pre-industrial 1-in-N-year hot extreme occurs after a shift of the mean by warming_k, for a
// normal distribution of standard deviation sigma_k (the same shape for every N): P'/P.
double heat_extreme_factor(double warming_k, double sigma_k, double return_period_years);
// Heavier precipitation: frequency of the pre-industrial 1-in-10-year daily rainfall ~ base^warming (AR6: x1.3 per K).
double heavy_rain_frequency_factor(double warming_k, double factor_per_k);
// Arctic September sea-ice extent (million km2): linear in the warming above a reference, never negative.
double september_sea_ice_mkm2(double warming_k, double extent_at_reference_mkm2, double reference_warming_k, double loss_per_k_mkm2);
// Standard normal cumulative distribution.
double standard_normal_cdf(double z);

// ---- Geography of warming -----------------------------------------------------------------------------------------

struct WarmingPattern {
    double land_factor{1.45};                // land warms faster than the ocean
    double arctic_factor{2.7};               // warming poleward of 66 N relative to the global mean
    double southern_ocean_factor{0.5};       // the Southern Ocean lags (deep water wells up)
    double north_atlantic_cooling_per_sv_k{0.15};   // the "warming hole" when the overturning weakens, per Sv
};
// Unnormalised local factor of a cell (normalise over the grid so that the area mean is 1).
double warming_pattern_factor(double latitude_deg, bool land, const WarmingPattern& pattern);

// ---- Carbon fluxes ------------------------------------------------------------------------------------------------

// Ocean CO2 uptake (mol m-2 s-1, positive into the sea) from the wind, the sea-surface temperature and the air-sea
// pCO2 difference (pCO2_air - pCO2_sea, uatm), with the gas transfer and solubility of the ocean carbon kernel.
double ocean_co2_uptake_mol_m2_s(double sea_surface_temperature_k, double wind_speed_m_s, double air_minus_sea_pco2_uatm, double salinity_psu = 35.0);

// Steady surface concentration anomaly of a well-mixed tracer on a global latitude-longitude grid (periodic in
// longitude), dC/dt = K lap C + S - (C - mean C)/tau = 0, solved by Gauss-Seidel iteration. S in ppm/s per cell.
std::vector<double> global_steady_anomaly(const std::vector<double>& source_ppm_s, std::size_t nx, std::size_t ny, double diffusivity_m2_s, double relaxation_s,
                                          std::size_t iterations = 4000);

}  // namespace terraflux::weather
