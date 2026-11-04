#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Catchment hydrology v1: where the rain goes once it has fallen.
//
// The kernel takes a terrain grid and a precipitation/temperature forcing and
// answers "where does the water go" with a drainage network and a chain of
// stores, then diagnoses what massive rain does to the landscape.
//
//   Network (static, from the terrain):
//   * depressions are filled with a priority flood so every land cell has a
//     downhill path to the ocean (cells at or below sea level) or, when the
//     domain is open, across the domain edge. Filled cells are lakes; each
//     lake has a spill elevation, a capacity and an outlet cell;
//   * the steepest of eight neighbours (D8) is the receiver of every cell;
//     accumulating the cell areas downstream gives the upstream area,
//     Strahler order and the channel mask (area above the initiation
//     threshold); cells draining to the same terminal cell form a basin;
//   * channel width and bankfull depth follow downstream hydraulic geometry
//     power laws of the upstream area; the regolith thins on steep slopes.
//
//   Water column per land cell (depths in metres over the cell area):
//   * snow (rain/snow split by air temperature, degree-day melt);
//   * surface (overland) water: rain and melt, minus infiltration at a
//     wetness-dependent Horton capacity f = f_min + (f_max - f_min)(1 - s),
//     minus evaporation; water above the detention storage runs off;
//   * soil water up to porosity x regolith depth; above field capacity it
//     percolates to the aquifer, otherwise it is held (saturation-excess
//     runoff when the store is full);
//   * groundwater: an unconfined aquifer of given thickness and specific
//     yield with explicit Darcy exchange between neighbours driven by the
//     water-table head (fixed at sea level in the ocean), a linear-reservoir
//     baseflow that reaches the nearest downstream channel, and springs
//     (exfiltration to the surface) where the aquifer is full.
//
//   Routing: a kinematic cascade in upstream-first order. Overland water
//   moves to its receiver at the Manning velocity (1/n) h^(2/3) S^(1/2), the
//   travel fraction min(1, v dt / L) letting water cross several cells per
//   step; channel water does the same with the channel geometry and a
//   discharge Q = v w d. Water above the bankfull depth spills on to the
//   floodplain (the cell's surface store) and returns when the channel has
//   room. Lakes fill to their spill level, then overflow at the outlet;
//   water delivered to the ocean or across an open edge leaves the budget
//   as discharge.
//
//   Hazards:
//   * erosion uses unit stream power omega = rho g q S: detachment
//     k_e (omega - omega_c) and a transport capacity k_t (omega - omega_c)
//     per unit width. Sediment travels with the water; where the load
//     exceeds capacity it settles (floodplains, lakes, ocean), giving
//     cumulative erosion and deposition depths and a sediment yield;
//   * landslides use the infinite-slope factor of safety
//     FS = [c + (gamma_s - m gamma_w) z cos^2(beta) tan(phi)] /
//          [gamma_s z sin(beta) cos(beta)]
//     with m the saturated fraction of the regolith from the soil store and
//     the water table. A cell fails when FS < 1: the regolith above a
//     residual depth, over the configured share of the cell area, becomes
//     debris that runs downslope until the slope is below the runout
//     gradient or a channel/lake/ocean is reached, where it is deposited;
//     the cell stays flagged unstable until its factor of safety recovers. The empirical rainfall intensity-duration threshold
//     I = a D^b (Caine) is tracked per cell as an independent warning;
//   * flooding is the water depth on the floodplain/surface with the
//     overland velocity, classified by depth x velocity.
//
// The budget closes: stores + outflows + evapotranspiration = initial +
// precipitation, reported per step as balance_error_m3. Terrain is static
// (erosion and deposition are bookkept, not fed back); the routing is a
// kinematic approximation without backwater, and the ocean has a fixed level.

struct CatchmentSoilConfig {
    double regolith_depth_m{1.0};                 // soil/regolith thickness on flat ground
    double slope_thinning_tan{1.5};               // depth reaches the minimum fraction at this tan(slope)
    double minimum_depth_fraction{0.3};
    double porosity{0.4};                         // storage capacity = porosity x depth
    double field_capacity_fraction{0.5};          // fraction of capacity held against gravity
    double infiltration_max_m_s{1.0e-5};          // dry-soil infiltration capacity (36 mm/h)
    double infiltration_min_m_s{1.0e-6};          // saturated infiltration capacity (3.6 mm/h)
    double percolation_rate_m_s{2.0e-6};          // drainage to the aquifer at full saturation
    double potential_evapotranspiration_m_s{3.5e-8};  // about 3 mm/day
    double detention_storage_m{0.002};            // surface water that does not run off
    double initial_saturation{0.3};
};

struct CatchmentGroundwaterConfig {
    double aquifer_thickness_m{20.0};
    double specific_yield{0.05};
    double hydraulic_conductivity_m_s{5.0e-5};
    double baseflow_time_scale_s{90.0 * 86400.0};
    double initial_fill_fraction{0.5};
};

struct CatchmentChannelConfig {
    double channel_initiation_area_m2{4.0e6};
    double manning_overland{0.1};
    double manning_channel{0.035};
    double width_coefficient_m{4.0};              // w = c A_km2^e
    double width_exponent{0.5};
    double bankfull_depth_coefficient_m{0.6};     // d_bf = c A_km2^e
    double bankfull_depth_exponent{0.3};
    double minimum_slope{1.0e-4};
    double lake_initial_fill_fraction{1.0};
};

struct CatchmentSnowConfig {
    double rain_snow_threshold_k{274.15};
    double degree_day_factor_m_k_s{4.0e-3 / 86400.0};  // 4 mm per kelvin-day
    double initial_snow_m{0.0};
};

struct CatchmentHazardConfig {
    double erodibility_kg_j{1.0e-6};              // detachment per joule of excess stream power
    double critical_stream_power_w_m2{30.0};
    double transport_capacity_kg_j{1.0e-3};       // kg m/J: capacity per unit width per unit excess stream power
    double settling_velocity_m_s{1.0e-3};
    double bulk_density_kg_m3{1600.0};
    double cohesion_pa{2000.0};
    double friction_angle_deg{32.0};
    double soil_unit_weight_n_m3{18000.0};
    double water_unit_weight_n_m3{9810.0};
    double minimum_failure_tan{0.05};             // slopes below this never fail
    double runout_tan{0.2};                       // debris stops where the downstream slope is gentler
    double residual_depth_fraction{0.2};
    double failure_area_fraction{0.05};           // share of a failing cell that actually mobilises
    double restabilisation_factor_of_safety{1.1}; // a failed cell may fail again once FS has recovered above this
    double flood_depth_threshold_m{0.1};
    double intensity_duration_a_mm_h{14.82};      // Caine (1980): I = a D^b, D in hours
    double intensity_duration_b{-0.39};
    double dry_spell_reset_s{6.0 * 3600.0};
};

struct CatchmentConfig {
    double timestep_seconds{600.0};
    TransportGrid2D grid;
    double sea_level_m{0.0};
    bool open_edges{true};
    CatchmentSoilConfig soil;
    CatchmentGroundwaterConfig groundwater;
    CatchmentChannelConfig channels;
    CatchmentSnowConfig snow;
    CatchmentHazardConfig hazards;
};

// Throws std::invalid_argument for non-finite or out-of-range values,
// including the explicit groundwater stability bound K B dt / (S_y L^2) <= 0.25.
void validate_catchment_config(const CatchmentConfig& config);

enum class TerminalKind : unsigned char { none = 0, ocean = 1, edge = 2 };

struct CatchmentLake {
    int id{0};
    std::vector<std::size_t> cells;
    double spill_elevation_m{0.0};
    double capacity_m3{0.0};
    double area_m2{0.0};
    long outlet_cell{-1};            // receiver of the spill; -1 leaves the domain
    TerminalKind outlet_kind{TerminalKind::none};  // set when outlet_cell is -1
};

struct CatchmentBasin {
    int id{0};
    std::size_t outlet_cell{0};
    double area_m2{0.0};
    TerminalKind terminal{TerminalKind::ocean};
};

struct CatchmentNetwork {
    std::vector<double> elevation_m;
    std::vector<double> filled_elevation_m;
    std::vector<long> downstream;                 // D8 receiver; -1 = terminal
    std::vector<unsigned char> flow_direction;    // 0 = E, counter-clockwise by 45 degrees; 8 = none
    std::vector<TerminalKind> terminal;           // where water leaving this cell goes when downstream is -1
    std::vector<unsigned char> ocean;             // sea cells (not routed)
    std::vector<double> reach_length_m;           // distance to the receiver
    std::vector<double> slope;                    // downstream gradient, floored
    std::vector<double> surface_slope_tan;        // gradient magnitude of the original terrain
    std::vector<double> upstream_area_m2;
    std::vector<int> strahler_order;
    std::vector<unsigned char> channel;
    std::vector<double> channel_width_m;
    std::vector<double> bankfull_depth_m;
    std::vector<long> channel_target;             // nearest channel cell at or downstream; -1 none
    std::vector<int> lake_id;                     // -1 none
    std::vector<int> basin_id;                    // -1 ocean
    std::vector<double> distance_to_outlet_m;
    std::vector<double> regolith_depth_m;
    std::vector<std::size_t> topological_order;   // upstream first
    std::vector<CatchmentLake> lakes;
    std::vector<CatchmentBasin> basins;           // sorted by area, largest first
};

CatchmentNetwork build_catchment_network(const std::vector<double>& elevation_m, const CatchmentConfig& config);

struct LandslideEvent {
    double elapsed_seconds{0.0};
    std::size_t cell{0};
    long runout_cell{-1};
    double factor_of_safety{0.0};
    double volume_m3{0.0};
    double runout_length_m{0.0};
    std::string deposited_in;  // "slope", "channel", "lake", "ocean", "edge"
};

struct CatchmentTotals {
    double precipitation_m3{0.0};      // cumulative on land and lakes
    double evapotranspiration_m3{0.0};
    double ocean_discharge_m3{0.0};
    double edge_outflow_m3{0.0};
    double infiltration_m3{0.0};
    double percolation_m3{0.0};
    double baseflow_m3{0.0};
    double snowmelt_m3{0.0};
    double sediment_yield_kg{0.0};     // delivered to the ocean or across the edge
    double landslide_volume_m3{0.0};
    std::size_t landslide_count{0};
};

struct CatchmentState {
    double elapsed_seconds{0.0};
    std::vector<double> snow_m;
    std::vector<double> surface_m;
    std::vector<double> soil_m;
    std::vector<double> groundwater_m;
    std::vector<double> channel_m3;
    std::vector<double> lake_volume_m3;           // per lake
    std::vector<double> regolith_depth_m;         // reduced by failures
    std::vector<double> sediment_kg;              // sediment in transit with the water
    // Diagnostics of the last step.
    std::vector<double> rainfall_rate_m_s;
    std::vector<double> overland_velocity_m_s;
    std::vector<double> discharge_m3_s;           // channel outflow
    std::vector<double> channel_depth_m;
    std::vector<double> water_table_head_m;
    std::vector<double> lake_level_m;              // per lake
    std::vector<double> lake_depth_m;              // per cell
    std::vector<double> erosion_m;                 // cumulative
    std::vector<double> deposition_m;              // cumulative
    std::vector<double> sediment_flux_kg_s;
    std::vector<double> factor_of_safety;          // <= 0 where undefined (flat, ocean, lake)
    std::vector<unsigned char> landslide;          // failed this step
    std::vector<unsigned char> unstable;           // failed and not yet restabilised (FS < 1 since)
    std::vector<double> storm_duration_s;          // running wet spell
    std::vector<double> storm_depth_m;
    std::vector<double> dry_time_s;
    std::vector<unsigned char> threshold_exceeded;
    std::vector<LandslideEvent> events;
    CatchmentTotals totals;
    double step_precipitation_m3{0.0};
    double balance_error_m3{0.0};
};

CatchmentState initialize_catchment(const CatchmentNetwork& network, const CatchmentConfig& config);

struct CatchmentForcing {
    std::vector<double> precipitation_rate_m_s;   // per cell, >= 0
    std::vector<double> air_temperature_k;        // per cell
};

// One step of the process chain described above.
CatchmentState advance_catchment(const CatchmentState& state, const CatchmentForcing& forcing,
                                 const CatchmentNetwork& network, const CatchmentConfig& config);

// Helpers exposed for tests and documentation.
double manning_velocity_m_s(double depth_m, double slope, double manning_n);
double infinite_slope_factor_of_safety(double slope_tan, double regolith_depth_m, double saturated_fraction,
                                       const CatchmentHazardConfig& hazards);
double intensity_duration_threshold_mm_h(double duration_h, const CatchmentHazardConfig& hazards);
double lake_level_for_volume(const CatchmentLake& lake, const CatchmentNetwork& network, double cell_area_m2, double volume_m3);
double total_storage_m3(const CatchmentState& state, const CatchmentConfig& config);

enum class FloodClass : unsigned char { none = 0, low = 1, medium = 2, high = 3, extreme = 4 };
FloodClass classify_flood(double depth_m, double velocity_m_s, const CatchmentHazardConfig& hazards);
const char* flood_class_name(FloodClass c);
const char* terminal_kind_name(TerminalKind k);

}  // namespace terraflux::weather
