#pragma once

#include "terraflux/weather/cloud_formation.hpp"
#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Severe convection v1: supercell / tornado potential from a capping
// temperature inversion, low-level wind shear and storm-relative helicity.
//
// A capping inversion (column configuration) stores convective energy behind
// convective inhibition; when surface heating or lifting breaks the cap the
// stored CAPE is released explosively. Tornadoes need that energy plus
// rotation: low-level shear and storm-relative helicity (SRH). In the single
// layer the 0-1 km shear is estimated from the Ekman turning between the
// frictional surface wind and the geostrophic wind aloft, S1 = V_g - V_s; the
// deep (0-6 km) shear adds the thermal wind of the horizontal temperature
// gradient. Storm motion follows Bunkers et al. (2000): the mean wind plus
// 7.5 m/s to the right (NH) / left (SH) of the deep shear vector. For a
// straight low-level hodograph SRH_0-1 = [S1 x (V_s - c)]_z.
//
// The Significant Tornado Parameter (Thompson et al. 2003, fixed-layer form):
//   STP = (CAPE/1500) (2000 - LCL)/1000 (|SRH|/150) (S6/20) (200 - CIN)/150
// with the published clamps (LCL term 1 below 1 km and 0 above 2 km; CIN term
// 1 below 50 J/kg and 0 above 200 J/kg; shear term 0 below 12.5 m/s and
// capped at 1.5 above 30 m/s). Risk classes: none (< 0.5), low (< 1),
// moderate (< 3), significant (>= 3). A tornado is "possible" only where the
// class is at least moderate and a cumulonimbus exists (convection released).
// Rule-of-thumb diagnostics; no storm-scale dynamics.

struct SevereConvectionConfig {
    double deep_shear_depth_m{6000.0};
    double bunkers_deviation_m_s{7.5};
    double geostrophic_air_density_kg_m3{1.2};
    double stp_low{0.5};
    double stp_moderate{1.0};
    double stp_significant{3.0};
};

struct SevereConvectionField {
    std::vector<double> low_level_shear_m_s;     // |V_g - V_s|
    std::vector<double> deep_shear_m_s;          // |S1 + thermal wind|
    std::vector<double> storm_motion_east_m_s;
    std::vector<double> storm_motion_north_m_s;
    std::vector<double> helicity_m2_s2;          // signed SRH 0-1 km
    std::vector<double> significant_tornado_parameter;
    std::vector<double> heated_cape_j_kg;
    std::vector<double> heated_cin_j_kg;
    std::vector<double> cap_strength_j_kg;       // ambient (unheated) CIN
    std::vector<double> lcl_height_m;            // heated parcel; NaN when none
    std::vector<unsigned char> capped;           // ambient CIN above the trigger with CAPE available
    std::vector<unsigned char> cap_broken;       // capped and heated CIN at or below the trigger
    std::vector<std::string> tornado_risk;       // none, low, moderate, significant
    std::vector<unsigned char> tornado_possible; // risk >= moderate and cumulonimbus present
    std::size_t capped_cells{0};
    std::size_t cap_broken_cells{0};
    std::size_t tornado_possible_cells{0};
    double max_stp{0.0};
};

void validate_severe_convection_config(const SevereConvectionConfig& config);

// Helpers exposed for tests and documentation.
double significant_tornado_parameter(
    double cape_j_kg, double lcl_m, double helicity_m2_s2, double deep_shear_m_s, double cin_j_kg);
std::string tornado_risk_class(double stp, const SevereConvectionConfig& config);

SevereConvectionField diagnose_severe_convection(
    const CoupledWindState& state, const TransportGrid2D& grid,
    const CloudFormationConfig& cloud_config, const CloudField& clouds,
    const SevereConvectionConfig& config);

}  // namespace terraflux::weather
