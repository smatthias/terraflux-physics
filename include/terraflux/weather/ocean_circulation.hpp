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
    double heat_capacity_j_kg_k{3990.0};              // c_p of sea water (heat budget and surface fluxes)
    double lens_salinity_deficit_psu{0.2};            // surface lens: surface fresher than the layer by this
    double lens_memory_s{10.0 * 86400.0};             // e-folding memory of the fresh water each cell received (lens attribution)
};

// Air-sea exchange by bulk formulae (used when the forcing carries air_temperature_k):
//   shortwave  Q_sw = (1 - albedo) SW_down, absorbed with depth (two-band profile below)
//   longwave   Q_lw = -eps sigma T_s^4 (0.39 - 0.05 sqrt(e_a/hPa)) (1 - lambda C^2) - 4 eps sigma T_s^3 (T_s - T_a)
//              (Berliand & Berliand 1952, as used by Fung et al. 1984)
//   sensible   Q_h  = rho_a c_pa C_H U (T_a - T_s)
//   latent     Q_e  = -L_v(T_s) rho_a C_E U (q_s - q_a),  q_s = 0.98 q_sat(T_s, p) over sea water
//   evaporation E   = rho_a C_E U (q_s - q_a) / rho_fw   (m/s of fresh water; negative = condensation)
// with U = max(U10, minimum_wind) and L_v = 2.501e6 - 2370 (T_s - 273.15) J/kg. All fluxes are positive
// into the ocean. The surface energy balance of the top layer is solved implicitly (backward Euler with
// Newton iterations), so the fluxes reported are those at the end-of-step surface temperature and they
// sum exactly to the heat that was applied.
struct OceanAirSeaConfig {
    double air_density_kg_m3{1.22};
    double air_heat_capacity_j_kg_k{1004.0};
    double heat_transfer_coefficient{1.1e-3};      // Stanton number C_H
    double moisture_transfer_coefficient{1.2e-3};  // Dalton number C_E
    double minimum_wind_m_s{1.0};                  // gustiness floor of the bulk formulae
    double surface_albedo{0.06};
    double surface_emissivity{0.97};
    double longwave_cloud_factor{0.6};             // lambda in (1 - lambda C^2)
    double saline_vapour_factor{0.98};             // q_s = factor q_sat(T_s) over sea water
    double fresh_water_density_kg_m3{1000.0};
};

// Layered ocean temperature and salinity (a vertical column in every water cell). count = 0 keeps the
// single well-mixed slab of the 1.5-layer model. With count >= 2 the active layer of thickness h is split
// into sigma layers of fixed fractions s_k (geometrically stretched so the top layer is surface_layer_m
// thick at the initial thickness), each with its own temperature T_k and salinity S_k. Every sublayer moves
// with the layer's velocity, so the horizontal transport of sum_k s_k T_k (and S_k) is exactly the bulk
// transport and the bulk temperature and salinity stay sum_k s_k T_k and sum_k s_k S_k. Vertical processes
// per step, after the horizontal transport:
//   - sources enter where they physically do: rain, rivers and coastal runoff at the top with their own
//     temperature and salinity; upwelled deep water at the bottom; sinking removes every sublayer in
//     proportion; evaporation takes fresh water from the top layer and leaves its salt;
//   - the surface energy balance heats or cools the top layer; shortwave penetrates with the two-band
//     profile I(z)/I0 = R exp(-z/zeta1) + (1 - R) exp(-z/zeta2) (Paulson & Simpson 1977, Jerlov type I),
//     the remainder at the layer bottom is absorbed by the bottom sublayer;
//   - the column is remapped conservatively to the sigma fractions of the new thickness;
//   - convective adjustment mixes statically unstable neighbours (linear equation of state in T and S);
//   - wind mixing by the Kraus-Turner energy budget: m0 rho_0 u*^3 dt of turbulent kinetic energy per
//     step deepens the mixed layer while it pays for the potential-energy increase g sum (rho_j - rho_mix)
//     z_j dz_j (partial entrainment of the next layer; unused energy is dissipated), so a fresh rain lens
//     resists the wind as it does in the ocean;
//   - shear mixing by the bulk Richardson criterion of Price, Weller & Pinkel (1986): the wind drives a slab
//     current in the mixed layer, estimated over an inertial period as dU = u*^2 / (max(|f|, 1e-5) h); the
//     layer entrains the next one while Ri_b = g (rho_next - rho_mix) h / (rho_0 dU^2) < Ri_c. A thin fresh
//     lens under a moderate wind is therefore eroded within hours to days, in calm weather it persists;
//   - background diffusion kappa_v of heat and salt (implicit, unconditionally stable).
// The surface salinity is then the top-layer salinity (plumes, fronts, lenses and the formation criterion
// see it) and freshwater_m is diagnosed as h_mix (1 - S_top / S_layer), the freshwater content the slab
// model would need for the same surface salinity.
struct OceanThermalLayersConfig {
    std::size_t count{0};                          // 0 = slab; 2-40 = layered column
    double surface_layer_m{5.0};                   // top-layer thickness at the initial thickness
    double background_diffusivity_m2_s{1.0e-5};
    double wind_mixing_efficiency{0.5};            // m0 of the Kraus-Turner budget
    double bulk_richardson_number{0.65};           // critical Ri_b of the shear mixing (0 disables it)
    double initial_mixed_layer_depth_m{30.0};      // T(z) = T_deep + (T_s - T_deep) exp(-max(0, z - mld) / scale)
    double initial_thermocline_scale_m{150.0};
    double shortwave_red_fraction{0.58};           // R
    double shortwave_red_depth_m{0.35};            // zeta1
    double shortwave_blue_depth_m{23.0};           // zeta2
    double mld_density_threshold_kg_m3{0.03};      // mixed-layer depth: density exceeds the top by this
    double ild_temperature_threshold_k{0.2};       // isothermal-layer depth: temperature departs by this
};

// Rain on coastal land: land within band_m of the sea drains to the nearest sea cell through a linear
// reservoir (runoff_coefficient of the rain enters a store that empties on runoff_time_scale_s; the rest
// infiltrates or evaporates on land and leaves the budget as a reported loss). Land farther inland is left
// to the rivers and the catchment model: its rain is reported but not routed. band_m = 0 disables it.
struct OceanCoastalConfig {
    double band_m{0.0};
    double runoff_coefficient{0.6};
    double runoff_time_scale_s{86400.0};
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
    OceanAirSeaConfig air_sea;
    OceanThermalLayersConfig thermal;
    OceanCoastalConfig coastal;
    // Optional initial surface temperature per cell for the layered column (empty = initial_temperature_k).
    // With layers the bulk temperature is the column mean, so initial_temperature_k is then the surface value.
    std::vector<double> initial_surface_temperature_k;
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
    // Optional rain (empty = none): falls on land and sea. On the sea it enters the surface at its own
    // temperature; on coastal land it drains to the sea through the coastal store.
    std::vector<double> rain_m_s;                // per cell, >= 0
    std::vector<double> rain_temperature_k;      // per cell (required with rain)
    // Optional air-sea exchange (empty air_temperature_k = off: the relaxation toward target_temperature_k
    // applies instead). All six arrays are required together.
    std::vector<double> air_temperature_k;
    std::vector<double> air_specific_humidity_kg_kg;
    std::vector<double> surface_pressure_pa;
    std::vector<double> wind_speed_m_s;          // 10 m wind speed for the bulk formulae
    std::vector<double> cloud_fraction;
    std::vector<double> shortwave_down_w_m2;     // incoming at the surface, before the albedo
    // Optional stress magnitude that drives the vertical mixing of the layered column (empty = |wind stress|):
    // the full wind, e.g. a storm vortex whose speed is known but whose direction the momentum forcing lacks.
    std::vector<double> mixing_stress_pa;
};

struct OceanState {
    double elapsed_seconds{0.0};
    std::vector<double> u_m_s;          // C-grid: east face of each cell
    std::vector<double> v_m_s;          // C-grid: north face of each cell
    std::vector<double> thickness_m;
    std::vector<double> salinity_psu;
    std::vector<double> temperature_k;
    std::vector<double> freshwater_m;   // surface freshwater content (metres of fresh water in the mixed layer;
                                        // negative where evaporation has concentrated the surface)
    std::vector<double> layer_temperature_k;  // layered column: [cell * layers + k], k = 0 at the top (empty for the slab)
    std::vector<double> layer_salinity_psu;   // layered column, same indexing
    std::vector<double> coastal_store_m;      // water on coastal land waiting to drain to the sea
    // Decaying record of the fresh water each sea cell received (metres; e-folding lens_memory_s), used to
    // attribute surface lenses to their source. Diagnostic only.
    std::vector<double> recent_rain_m;
    std::vector<double> recent_runoff_m;
    std::vector<double> recent_river_m;
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
    std::vector<double> sst_k;                    // top layer (the bulk temperature for the slab)
    std::vector<double> mixed_layer_depth_m;      // density criterion (the layer thickness for the slab)
    std::vector<double> isothermal_layer_depth_m; // temperature criterion; ILD - MLD > 0 is a barrier layer
    std::vector<double> evaporation_m_s;          // per sea cell, negative = condensation
    std::vector<double> shortwave_w_m2;           // absorbed shortwave (net of albedo)
    std::vector<double> longwave_w_m2;
    std::vector<double> sensible_w_m2;
    std::vector<double> latent_w_m2;
    std::vector<double> rain_heat_w_m2;           // rho c_p P (T_rain - SST): cooling by rain colder than the sea
    std::vector<double> net_heat_w_m2;            // heat applied at the surface (air-sea fluxes or the relaxation)
    std::vector<double> coastal_inflow_m3_s;      // coastal runoff received by each sea cell
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
    // Rain, coastal runoff and evaporation of the last step (volume rates).
    double rain_sea_m3_s{0.0};                    // rain falling on the sea
    double rain_coastal_sea_m3_s{0.0};            // ... of which on sea cells next to land
    double rain_coast_m3_s{0.0};                  // rain on coastal land (drains to the sea)
    double rain_inland_m3_s{0.0};                 // rain on land beyond the coastal band (not routed)
    double coastal_runoff_m3_s{0.0};
    double coastal_loss_m3_s{0.0};
    double coastal_storage_m3{0.0};
    double coastal_balance_error_m3{0.0};
    double evaporation_m3_s{0.0};                 // net evaporation from the sea (condensation counts negative)
    // Heat (relative to 0 degC): content rho_0 c_p sum h (T - 273.15) A and its budget.
    double surface_heat_w{0.0};                   // area-integrated heat applied at the surface
    double heat_content_j{0.0};
    double heat_balance_error_j{0.0};
    // Cumulative totals since initialisation.
    double cumulative_river_m3{0.0};
    double cumulative_net_precipitation_m3{0.0};
    double cumulative_sinking_m3{0.0};
    double cumulative_sponge_m3{0.0};
    double cumulative_rain_sea_m3{0.0};
    double cumulative_rain_coast_m3{0.0};
    double cumulative_rain_inland_m3{0.0};
    double cumulative_coastal_runoff_m3{0.0};
    double cumulative_coastal_loss_m3{0.0};
    double cumulative_evaporation_m3{0.0};
    double cumulative_surface_heat_j{0.0};
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

// Surface lens: a connected area where the surface is fresher than the layer below by the lens deficit.
// Attributed by the fresh water its cells received recently (rain on the sea, coastal runoff, rivers):
// kind is the largest share, "other" when nothing was recorded (e.g. a lens carried away from its source
// for longer than the memory).
struct SurfaceLens {
    std::string kind;
    double rain_share{0.0};
    double coastal_runoff_share{0.0};
    double river_share{0.0};
    std::size_t freshest_cell{0};
    std::size_t cell_count{0};
    double area_m2{0.0};
    double minimum_surface_salinity_psu{0.0};
    double maximum_deficit_psu{0.0};              // layer minus surface salinity at the freshest cell
    double freshwater_m3{0.0};                    // sum F A over the lens
    double mean_sst_k{0.0};
};

struct OceanDiagnostics {
    std::vector<OceanStream> streams;
    std::vector<RiverPlume> plumes;
    std::vector<SurfaceLens> lenses;              // sorted by area, largest first
    std::size_t barrier_layer_cells{0};           // isothermal layer deeper than the mixed layer by > 1 m
    double mean_sst_k{0.0};
    double min_sst_k{0.0};
    double max_sst_k{0.0};
    double mean_mixed_layer_depth_m{0.0};
    double max_mixed_layer_depth_m{0.0};
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
// Bulk air-sea fluxes at one sea-surface temperature (W/m2, positive into the ocean). shortwave is the
// absorbed total (1 - albedo) SW_down; net is their sum; evaporation in m/s of fresh water.
struct AirSeaFluxes {
    double shortwave_w_m2{0.0};
    double longwave_w_m2{0.0};
    double sensible_w_m2{0.0};
    double latent_w_m2{0.0};
    double net_w_m2{0.0};
    double evaporation_m_s{0.0};
};
AirSeaFluxes air_sea_fluxes(double sea_surface_temperature_k, double air_temperature_k, double air_specific_humidity_kg_kg,
                            double surface_pressure_pa, double wind_speed_m_s, double cloud_fraction, double shortwave_down_w_m2,
                            const OceanAirSeaConfig& config);
// Fraction of the absorbed shortwave that reaches depth z (two-band profile of the thermal config).
double shortwave_transmission(double depth_m, const OceanThermalLayersConfig& thermal);
// Cosine of the solar zenith angle (declination by Cooper 1969, local solar time from the longitude,
// no equation of time) and the surface shortwave: clear sky by Zillman (1972)
//   Q0 = S0 cos^2 Z / ((cos Z + 2.7) e_a 1e-3 + 1.085 cos Z + 0.10),  e_a in hPa (the function takes Pa),
// reduced for cloud by Reed (1977): Q = Q0 min(1, 1 - 0.62 C).
double solar_cosine_zenith(double latitude_deg, double longitude_deg, double day_of_year, double utc_hour);
double surface_shortwave_w_m2(double cosine_zenith, double vapour_pressure_pa, double cloud_fraction);
// Sigma fractions of the layered column (top first, summing to 1).
std::vector<double> thermal_layer_fractions(const OceanConfig& config);
// Nearest sea cell of every land cell within the coastal band (count = none; sea cells map to count too).
std::vector<std::size_t> coastal_receivers(const OceanConfig& config);
// Wind stress from a 10 m wind: tau = rho_air C_d |U| U.
void wind_stress_from_wind(const std::vector<double>& wind_east_m_s, const std::vector<double>& wind_north_m_s,
                           double air_density_kg_m3, double drag_coefficient,
                           std::vector<double>& stress_east_pa, std::vector<double>& stress_north_pa);

}  // namespace terraflux::weather
