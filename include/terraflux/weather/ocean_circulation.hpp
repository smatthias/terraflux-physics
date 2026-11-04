#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Ocean circulation v1: the streams, their salt, and the fresh water that
// rivers pour into them.
//
// The model is a reduced-gravity ("1.5-layer") upper ocean: an active layer
// of thickness h, velocity (u, v), salinity S and temperature T lies on a deep
// layer at rest with fixed properties (T_deep, S_deep). The interface between
// them is the thermocline; its tilt is the pressure gradient that drives the
// currents, with the reduced gravity g' = g (rho_deep - rho_1) / rho_0 from a
// linear equation of state rho = rho_0 [1 - alpha (T - T_0) + beta_S (S - S_0)].
// Fresher or warmer water is lighter, so fresh water changes g' and therefore
// the pressure field: a river plume is a buoyant current that the Coriolis
// force turns to the right (northern hemisphere) along the coast.
//
//   momentum (linear, Arakawa C-grid, forward-backward Coriolis):
//     du/dt = -d(g' h)/dx + f v - r u + tau_x / (rho_0 h) + A laplacian(u)
//     dv/dt = -d(g' h)/dy - f u - r v + tau_y / (rho_0 h) + A laplacian(v)
//   thickness (upwind flux form):
//     dh/dt = -div(h u) + (P - E) + rivers / area - sinking + upwelling
//   tracers (upwind flux form + diffusion; rivers carry S = 0 and T_river;
//     evaporation leaves the salt behind, rain dilutes):
//     d(h S)/dt = -div(h u S) + kappa laplacian(h S) + sources
//     d(h T)/dt = -div(h u T) + kappa laplacian(h T) + sources + h (T* - T) / tau_T
//
// On a beta-plane the wind-stress curl drives the Sverdrup interior flow and
// its return is squeezed into a narrow western boundary current of width
// r / beta (Stommel 1948): the Gulf Stream of the idealised basin. Where the
// surface water becomes as dense as the deep layer (cold and salty, g' below
// a threshold) it sinks at a rate (h - h_min) / tau_conv: deep-water
// formation, the downward branch of the overturning circulation. The sunk
// volume returns as uniform upwelling of deep water everywhere else, which
// closes the upper-layer volume and feeds the surface branch. Fresh water at
// the formation sites lowers the density, switches the sinking off and weakens
// the overturning: the freshwater effect on the thermohaline circulation.
//
// Fresh water first floats on the sea: river water and rain enter a surface
// mixed layer of depth h_mix as a freshwater content F (metres) that is carried
// by the currents and mixes down into the bulk layer on the mixing time scale.
// The surface salinity S_s = S (1 - F / h_mix) is what a plume looks like from
// above, where the mixing fronts are, and what caps the deep-water formation
// (fresh water on top stops the sinking even if the bulk is dense).
//
// A sponge (open-boundary proxy) can relax chosen cells toward the initial
// state and damp their currents; when present, the sunk volume returns there
// instead of uniformly, so the overturning's surface branch flows through the
// basin toward the formation sites and the deep return leaves through the
// sponge, as it does through the real South Atlantic.
//
// Diagnostics locate the streams (connected fast-flow regions with a core
// streamline, speed and transport in Sverdrups), the exchange points (river
// mouths with their plumes: area fresher than the reference by a threshold,
// minimum salinity), the mixing fronts (large salinity gradients) and the
// formation sites. The budget closes: volume and salt are conserved up to the
// explicit sources and sinks reported per step.

struct OceanPhysicsConfig {
    double reference_density_kg_m3{1025.0};
    double gravity_m_s2{9.81};
    double thermal_expansion_per_k{2.0e-4};
    double haline_contraction_per_psu{7.6e-4};
    double reference_temperature_k{283.15};
    double reference_salinity_psu{35.0};
    double deep_temperature_k{277.15};
    double deep_salinity_psu{34.9};
    double minimum_reduced_gravity_m_s2{1.0e-3};     // floor on g' for the pressure term
    double linear_friction_s{1.0e-6};                 // Rayleigh drag r (about 12 days)
    double lateral_viscosity_m2_s{500.0};             // A
    double tracer_diffusivity_m2_s{500.0};            // kappa
    double minimum_thickness_m{20.0};                 // h floor used in tau / (rho h) and sinking
    double temperature_relaxation_s{30.0 * 86400.0};  // tau_T toward the target surface temperature
    double formation_reduced_gravity_m_s2{4.0e-3};    // sinking starts below this g'
    double convection_time_scale_s{10.0 * 86400.0};   // tau_conv
    double sponge_time_scale_s{5.0 * 86400.0};        // relaxation time of sponge cells at full strength
    bool upwelling_in_sponge{true};                   // return the sunk volume in the sponge when one exists
    std::string coriolis_mode{"beta_plane"};          // "none", "f_plane", "beta_plane", "prescribed"
    double coriolis_parameter_s{0.0};
    double mixed_layer_depth_m{50.0};                 // surface layer that holds the freshwater content
    double freshwater_mixing_time_s{30.0 * 86400.0};  // mixing of the freshwater content into the bulk layer
    double stream_speed_threshold_m_s{0.15};          // cells faster than this belong to a stream (2+ connected cells)
    double plume_salinity_deficit_psu{0.5};           // plume: fresher than the reference by this
    double front_gradient_psu_per_100km{0.5};         // mixing front: |grad S| above this
};

struct OceanConfig {
    double timestep_seconds{1800.0};
    TransportGrid2D grid;
    std::vector<unsigned char> ocean;                 // 1 = water, 0 = land
    std::vector<double> sponge;                       // optional per-cell relaxation strength 0-1 (empty = none)
    double initial_thickness_m{500.0};
    double initial_salinity_psu{35.0};
    double initial_temperature_k{291.15};
    OceanPhysicsConfig physics;
};

// Throws std::invalid_argument for invalid ranges, a mask that does not match
// the grid, no water at all, or a timestep violating the gravity-wave CFL
// bound, f dt <= 0.5 and the explicit viscous/diffusive bounds.
void validate_ocean_config(const OceanConfig& config);

struct RiverSource {
    std::size_t cell{0};                  // ocean cell receiving the water (the mouth)
    double discharge_m3_s{0.0};
    double temperature_k{283.15};
    double salinity_psu{0.0};
    std::string name;
};

struct OceanForcing {
    std::vector<double> wind_stress_east_pa;     // per cell
    std::vector<double> wind_stress_north_pa;
    std::vector<double> net_precipitation_m_s;   // P - E per cell (negative = net evaporation)
    std::vector<double> target_temperature_k;    // T* per cell for the surface relaxation
    std::vector<RiverSource> rivers;
};

struct OceanState {
    double elapsed_seconds{0.0};
    std::vector<double> u_m_s;          // C-grid: east face of each cell
    std::vector<double> v_m_s;          // C-grid: north face of each cell
    std::vector<double> thickness_m;
    std::vector<double> salinity_psu;
    std::vector<double> temperature_k;
    std::vector<double> freshwater_m;   // surface freshwater content (metres of fresh water in the mixed layer)
    // Diagnostics of the last step (cell centres).
    std::vector<double> speed_m_s;
    std::vector<double> u_centre_m_s;
    std::vector<double> v_centre_m_s;
    std::vector<double> reduced_gravity_m_s2;
    std::vector<double> density_kg_m3;
    std::vector<double> sinking_m_s;              // deep-water formation rate
    std::vector<double> upwelling_field_m_s;      // where the sunk volume returns
    std::vector<double> surface_salinity_psu;     // S (1 - F / h_mix), what plumes and fronts are diagnosed on
    std::vector<double> freshwater_fraction;      // (S_ref - S_surface) / S_ref, >= 0
    std::vector<unsigned char> front;             // mixing front flag
    std::vector<int> stream_id;                   // -1 outside streams
    double upwelling_m_s{0.0};                    // basin-mean upwelling rate
    double overturning_m3_s{0.0};                 // total sinking
    double river_input_m3_s{0.0};
    double net_precipitation_m3_s{0.0};
    double sponge_volume_m3_s{0.0};               // volume supplied (+) or absorbed (-) by the sponge relaxation
    double sponge_salt_kg_s{0.0};
    double volume_m3{0.0};
    double salt_kg{0.0};                          // sum rho_0 h S A (S in kg/kg = psu / 1000)
    double kinetic_energy_j{0.0};
    double volume_balance_error_m3{0.0};
    double salt_balance_error_kg{0.0};
    // Cumulative totals since initialisation.
    double cumulative_river_m3{0.0};
    double cumulative_net_precipitation_m3{0.0};
    double cumulative_sinking_m3{0.0};
    double cumulative_sponge_m3{0.0};
};

OceanState initialize_ocean(const OceanConfig& config);
OceanState advance_ocean(const OceanState& state, const OceanForcing& forcing, const OceanConfig& config);

struct OceanStream {
    int id{0};
    std::size_t core_cell{0};
    std::size_t cell_count{0};
    double max_speed_m_s{0.0};
    double mean_speed_m_s{0.0};
    double transport_sv{0.0};                     // across the core section, 1 Sv = 1e6 m3/s
    double mean_salinity_psu{0.0};
    double mean_temperature_k{0.0};
    std::string direction;                        // "northward", "southward", "eastward", "westward"
    std::vector<std::pair<double, double>> path;  // streamline through the core in grid fractions [0, 1]
};

struct RiverPlume {
    std::size_t mouth_cell{0};
    std::string name;
    double discharge_m3_s{0.0};
    double mouth_salinity_psu{0.0};
    double minimum_salinity_psu{0.0};             // surface salinity
    double plume_area_m2{0.0};
    double plume_freshwater_m3{0.0};              // freshwater volume held in the plume
    double plume_extent_km{0.0};                  // farthest plume cell from the mouth
    double deflection_deg{0.0};                   // bearing of the plume centroid from the mouth, clockwise from north
};

struct OceanDiagnostics {
    std::vector<OceanStream> streams;
    std::vector<RiverPlume> plumes;
    std::size_t formation_cells{0};
    double formation_area_m2{0.0};
    double mean_salinity_psu{0.0};
    double min_salinity_psu{0.0};
    double max_speed_m_s{0.0};
};

OceanDiagnostics diagnose_ocean(const OceanState& state, const OceanForcing& forcing, const OceanConfig& config);

// Helpers exposed for tests and documentation.
double seawater_density_kg_m3(double temperature_k, double salinity_psu, const OceanPhysicsConfig& physics);
double reduced_gravity_m_s2(double temperature_k, double salinity_psu, const OceanPhysicsConfig& physics);
// Stommel western-boundary-layer width r / beta and the Sverdrup transport
// of a basin of zonal width L_x under tau_x = -tau_0 cos(pi y / L_y).
double stommel_boundary_width_m(double friction_s, double beta_per_m_s);
double sverdrup_transport_m3_s(double wind_stress_amplitude_pa, double zonal_width_m, double meridional_width_m, double beta_per_m_s, double density_kg_m3);
// Wind stress from a 10 m wind: tau = rho_air C_d |U| U.
void wind_stress_from_wind(const std::vector<double>& wind_east_m_s, const std::vector<double>& wind_north_m_s,
                           double air_density_kg_m3, double drag_coefficient,
                           std::vector<double>& stress_east_pa, std::vector<double>& stress_north_pa);

}  // namespace terraflux::weather
