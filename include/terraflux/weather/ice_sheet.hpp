#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <vector>

namespace terraflux::weather {

// Land ice sheet v1 (e.g. Greenland): ice thickness on land cells, its flow, its surface mass balance and
// its loss to the ocean.
//
//   dH/dt = -div(q) + SMB - frontal ablation,     q = -D grad(s),   s = bed + H
//   D = 2 A (rho_i g)^n H^(n+2) |grad s|^(n-1) / (n + 2)          (shallow-ice approximation, Glen n = 3)
//
// Flow: the shallow-ice diffusivity on the cell faces (Mahaffy 1976), explicit with sub-steps below the
// diffusive limit dt <= dx^2 / (4 D_max) and donor limiting so no cell goes negative. Ice that flows onto a
// sea cell calves: it leaves as icebergs (discharge) into that cell.
//
// Surface mass balance (per cell, at the ice surface elevation s):
//   air temperature    T_s = T_sea_level - lapse * s      (thinning lowers the surface into warmer air:
//                                                          the melt-elevation feedback)
//   precipitation      snow below the threshold (linear over +-1 K), rain above
//   melt               positive degree days of a normal distribution around T_s (sigma for the weather
//                      the forcing does not resolve): E[max(0, T)] = sigma phi(mu/sigma) + mu Phi(mu/sigma);
//                      surface snow melts first with the snow degree-day factor, then ice with the ice factor
//                      (Reeh 1991); a fraction of the snowmelt refreezes in the firn
//   surface type       fresh snow turns into firn/ice on snow_to_ice_time_s (it only selects the factor)
//   runoff             the snowmelt that does not refreeze + ice melt + rain on the ice, routed to the
//                      nearest sea cell (breadth-first over the grid)
//
// Frontal ablation at marine margins (ice cells next to the sea), driven by the ocean (Rignot et al. 2016):
//   q_m = (a h q_sg^alpha + b) TF^beta   [m/day of the ice face]
// with the ocean thermal forcing TF = T_ocean - T_freezing at the front depth, the water depth h at the
// front and the subglacial discharge q_sg (m/day) of the meltwater that leaves under the front. A face of
// height H and width dx retreats at q_m: the cell loses q_m H / dx of thickness, as meltwater into the sea
// cell (the latent heat comes from the ocean). Warmer ocean and more surface melt both speed it up. On a coarse grid
// only a share of each margin cell's width is a glacier front (marine_front_fraction); with 1 every margin is one, a
// stand-in for all dynamic discharge when the flow cannot resolve outlet glaciers.
//
// Budget: every step the ice volume changes by accumulation - melt that runs off - calving - frontal
// ablation (ice equivalent); the residual is reported. The sea-level equivalent is the ice volume as water
// spread over the global ocean area (no isostasy, no marine ice below flotation).

struct IceSheetPhysics {
    double ice_density_kg_m3{917.0};
    double fresh_water_density_kg_m3{1000.0};
    double gravity_m_s2{9.81};
    double rate_factor_pa3_s{3.17e-24};                // Glen A (about 1e-16 Pa^-3 a^-1, ice near -10 degC)
    double degree_day_factor_snow_m_k_s{3.0e-3 / 86400.0};  // water equivalent per kelvin-second
    double degree_day_factor_ice_m_k_s{8.0e-3 / 86400.0};
    double pdd_standard_deviation_k{5.0};
    double refreezing_fraction{0.6};                   // of the snowmelt
    double snow_threshold_k{274.15};                   // snow/rain threshold (linear over +-1 K)
    double lapse_rate_k_m{0.0065};
    double snow_to_ice_time_s{365.0 * 86400.0};
    double frontal_a{3.0e-4};                          // Rignot et al. (2016)
    double frontal_alpha{0.39};
    double frontal_b{0.15};
    double frontal_beta{1.18};
    double front_water_depth_m{400.0};
    double marine_front_fraction{1.0};                // share of a margin cell's width that is glacier front (the rest is land-terminating)
    double global_ocean_area_m2{3.618e14};             // for the sea-level equivalent
};

struct IceSheetConfig {
    TransportGrid2D grid;
    std::vector<double> bed_elevation_m;               // per cell
    std::vector<unsigned char> ocean;                  // 1 = sea (no ice; receives runoff and icebergs)
    std::vector<unsigned char> ice_domain;             // optional: land cells where snow can build ice (empty = all land)
    IceSheetPhysics physics;
};

// Throws std::invalid_argument for an invalid grid, array sizes, no sea cell, or out-of-range parameters.
void validate_ice_sheet_config(const IceSheetConfig& config);

struct IceSheetForcing {
    std::vector<double> air_temperature_k;             // reduced to sea level, per cell
    std::vector<double> precipitation_m_s;             // water equivalent, per cell
    std::vector<double> ocean_thermal_forcing_k;       // per sea cell at the front depth (empty = no frontal ablation)
};

struct IceSheetState {
    double elapsed_seconds{0.0};
    std::vector<double> thickness_m;
    std::vector<double> snow_m;                        // fresh surface snow (ice equivalent, part of the thickness)
    // Diagnostics of the last step (per cell).
    std::vector<double> surface_elevation_m;
    std::vector<double> surface_air_temperature_k;
    std::vector<double> accumulation_m_s;              // snowfall, ice equivalent
    std::vector<double> melt_m_s;                      // surface melt, ice equivalent (before refreezing)
    std::vector<double> refreeze_m_s;
    std::vector<double> smb_m_s;                       // accumulation - melt + refreeze (ice equivalent)
    std::vector<double> frontal_ablation_m_s;          // thickness loss at marine margins
    std::vector<double> runoff_to_sea_m3_s;            // liquid water arriving at each sea cell (runoff + frontal melt)
    std::vector<double> icebergs_to_sea_m3_s;          // calved ice arriving at each sea cell (water equivalent)
    std::vector<double> frontal_melt_to_sea_m3_s;      // part of runoff_to_sea that is frontal melt (ocean heat)
    // Totals.
    double volume_m3{0.0};                             // ice
    double area_m2{0.0};                               // ice-covered (thickness > 1 m)
    double sea_level_equivalent_m{0.0};
    double accumulation_m3_s{0.0};                     // ice equivalent
    double melt_m3_s{0.0};
    double refreeze_m3_s{0.0};
    double runoff_m3_s{0.0};                           // water equivalent: runoff from the ice (incl. rain on it)
    double calving_m3_s{0.0};                          // water equivalent
    double frontal_ablation_m3_s{0.0};                 // water equivalent
    double smb_m3_s{0.0};                              // ice equivalent
    double mass_balance_error_m3{0.0};                 // ice equivalent, last step
    double cumulative_freshwater_m3{0.0};              // water equivalent delivered to the sea (runoff + calving + frontal)
    double cumulative_sea_level_rise_m{0.0};           // from the ice lost since initialisation
};

IceSheetState initialize_ice_sheet(const IceSheetConfig& config, const std::vector<double>& thickness_m);
IceSheetState advance_ice_sheet(const IceSheetState& state, const IceSheetForcing& forcing, const IceSheetConfig& config, double dt_seconds);

// Helpers exposed for tests and documentation.
// Expected positive degree days per unit time (K) of a normal distribution with mean mu (degC) and sigma.
double expected_positive_degrees_k(double mean_c, double sigma_k);
// Vialov (1958) steady dome of radius L and centre thickness H0 on the land cells:
//   H(r) = H0 (1 - (r/L)^((n+1)/n))^(n/(2n+2)),  n = 3.
std::vector<double> vialov_dome(const IceSheetConfig& config, double center_east_m, double center_north_m, double radius_m, double center_thickness_m);
// The same profile on an ellipse (r = sqrt((e/a)^2 + (n/b)^2)), for an elongated ice sheet such as Greenland.
std::vector<double> elliptical_dome(const IceSheetConfig& config, double center_east_m, double center_north_m, double radius_east_m, double radius_north_m, double center_thickness_m);
// Frontal melt rate (m/s of the ice face) from the ocean thermal forcing and the subglacial discharge (m/day).
double frontal_melt_rate_m_s(double thermal_forcing_k, double subglacial_discharge_m_day, const IceSheetPhysics& physics);
// Nearest sea cell of every cell by breadth-first search over the 4-neighbour grid (sea cells map to themselves).
std::vector<std::size_t> nearest_sea_cells(const std::vector<unsigned char>& ocean, std::size_t nx, std::size_t ny);

}  // namespace terraflux::weather
