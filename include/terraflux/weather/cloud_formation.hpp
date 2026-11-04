#pragma once

#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Cloud formation v1: fronts and convection diagnosed on the coupled layer.
//
// Two lifting mechanisms produce cloud, separately or combined:
//
//  * Frontal lifting (horizontal collision of air masses). A frontal zone is
//    a band of strong horizontal temperature gradient. Temperature advection
//    tells which air mass advances: cold air arriving (-V.grad T < 0) marks a
//    cold front, warm air arriving a warm front, neither a stationary front.
//    Boundary-layer convergence lifts air at the rate w = -H div V; over the
//    lifting time scale this gives a lifting depth. Ahead of a warm front the
//    warm air overruns the cold wedge (gentle slope, wide layered cloud:
//    nimbostratus -> altostratus -> cirrostratus with distance); behind a cold
//    front the wedge is steep and the cloud band narrow.
//
//  * Convective lifting (surface heating). The surface parcel is warmed by
//    the configured heating, lifted dry-adiabatically, cools, condenses at
//    its LCL and, if it reaches its level of free convection, keeps rising to
//    the equilibrium level: cumulus -> towering cumulus -> cumulonimbus
//    (over-development, local thunderstorms).
//
//  * Combined: where frontal lifting carries a parcel with convective energy
//    above its level of free convection, convection is released regardless
//    of convective inhibition (squall-line / frontal thunderstorms).
//
// Every quantity is a rule-of-thumb diagnostic on the horizontal state and
// the column model; there is no cloud microphysics, precipitation, radiation
// or feedback on the flow.

struct CloudFormationConfig {
    ColumnConfig column;
    // Frontal zone: |grad T| at or above this value (2 K per 100 km).
    double frontal_gradient_threshold_k_m{2.0e-5};
    // Cold/warm classification: |temperature advection| above this (0.1 K/h).
    double advection_threshold_k_s{0.1 / 3600.0};
    // Boundary-layer depth H for w = -H div V, and the lifting time scale.
    double boundary_layer_depth_m{1500.0};
    double lifting_time_scale_s{6.0 * 3600.0};
    // Surface heating deepens the mixed layer reached by thermals:
    // mixed-layer top = H + growth * heating.
    double mixed_layer_growth_m_per_k{300.0};
    // Frontal surface slopes (rise per horizontal distance) and the horizontal
    // reach of frontal cloud on the cold side.
    double warm_front_slope{1.0 / 200.0};
    double cold_front_slope{1.0 / 75.0};
    double warm_front_reach_m{800000.0};
    double cold_front_reach_m{200000.0};
    // Lifting on the warm side within this distance of a front counts as frontal.
    double frontal_band_width_m{150000.0};
    // Convection is released freely when CIN is at or below this value and
    // CAPE is at least cape_threshold_j_kg.
    double cin_trigger_j_kg{20.0};
    double cape_threshold_j_kg{100.0};
    // Cloud tops are capped here (tropopause).
    double max_cloud_top_m{12000.0};
};

struct FrontAnalysis {
    std::vector<double> temperature_gradient_k_m;   // |grad T|
    std::vector<double> temperature_advection_k_s;  // -V . grad T
    std::vector<double> divergence_s;               // du/dx + dv/dy
    std::vector<double> lifting_m_s;                // max(0, -H div)
    std::vector<std::string> front_type;            // none, cold, warm, stationary
    std::vector<double> distance_to_front_m;        // NaN when no front exists
    std::vector<std::size_t> nearest_front_index;   // valid when distance is finite
    std::vector<int> side;                          // +1 warm side, -1 cold side, 0 on a front / no front
    std::size_t front_cell_count{0};
};

struct CloudField {
    std::vector<std::string> cloud_type;   // none, cumulus, towering_cumulus, cumulonimbus,
                                           // stratocumulus, stratus, nimbostratus, altocumulus,
                                           // altostratus, cirrus, cirrostratus
    std::vector<std::string> mechanism;    // none, convective, frontal, frontal_convective
    std::vector<std::string> trigger;      // none, free, thermals, frontal, convergence, overrunning
    std::vector<double> base_m;            // NaN when none
    std::vector<double> top_m;             // NaN when none
    std::vector<double> cloud_fraction;    // 0..1 rule of thumb
    std::vector<unsigned char> thunderstorm;   // 1 for cumulonimbus
    std::vector<double> lifting_depth_m;   // boundary-layer convergence lifting over the time scale
    std::vector<double> heated_cape_j_kg;  // surface parcel after heating
    std::vector<double> heated_cin_j_kg;
    double surface_heating_k{0.0};
    std::size_t cloudy_cells{0};
    std::size_t thunderstorm_cells{0};
};

void validate_cloud_formation_config(const CloudFormationConfig& config);

// Rule-of-thumb cloud type from base, top and whether the cloud is convective.
std::string classify_cloud(double base_m, double top_m, bool convective);
double cloud_fraction_for_type(const std::string& cloud_type);

// Frontal analysis of the horizontal state (temperature, wind).
FrontAnalysis analyse_fronts(
    const CoupledWindState& state,
    const TransportGrid2D& grid,
    const CloudFormationConfig& config);

// Diurnal surface heating: amplitude * max(0, sin(2 pi (t - phase) / 86400)).
double diurnal_surface_heating_k(double elapsed_seconds, double amplitude_k, double phase_seconds);

// Cloud diagnosis combining frontal and convective lifting. surface_heating_k
// is added to the surface temperature of the convective parcel only.
CloudField diagnose_cloud_formation(
    const CoupledWindState& state,
    const TransportGrid2D& grid,
    const CloudFormationConfig& config,
    double surface_heating_k,
    const FrontAnalysis& fronts);

}  // namespace terraflux::weather
