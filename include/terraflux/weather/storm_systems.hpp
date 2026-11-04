#pragma once

#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Storm systems v1: tropical cyclones (hurricanes) as tracked objects on the
// coupled layer, with their genesis environment, intensity bound, mutual
// interaction, merging and landfall.
//
// Energy: warm, humid air over a warm ocean. The Maximum Potential Intensity
// (Emanuel 1986/1995) follows from the enthalpy a storm can extract from the
// sea, k_s* - k_b (saturated air at the sea-surface temperature versus the
// boundary-layer air), and the Carnot efficiency between the sea surface and
// the outflow (tropopause) temperature:
//   V_pot^2 = (C_k/C_D) (T_s - T_o)/T_o (k_s* - k_b),  k = c_p T + L q,
// reduced by a surface-wind factor. No MPI over land or below the SST
// threshold.
//
// Genesis: Emanuel & Nolan (2004) genesis potential index
//   GPI = |1e5 eta|^(3/2) (RH_mid/50)^3 (V_pot/70)^3 (1 + 0.1 V_shear)^-2
// with eta = f + zeta the absolute vorticity, RH_mid the mid-level relative
// humidity (from the column at mid_level_height_m), V_shear the deep-layer
// vertical shear estimated from the thermal wind of the horizontal
// temperature gradient. A storm is seeded where GPI exceeds the threshold.
//
// Evolution per sub-step: over the ocean the intensity relaxes toward the
// shear-reduced potential intensity with the intensification time scale;
// over land it decays following Kaplan & DeMaria (1995),
//   V(t) = V_b + (R V_0 - V_b) exp(-alpha t),
// with alpha = 0.095 1/h, V_b = 13.75 m/s and landfall reduction R = 0.9.
// The centre moves with the environmental steering flow averaged over the
// steering radius, a poleward/westward beta drift, and the tangential wind
// induced by every other storm (Rankine vortex outside the core: V = V_max
// R_max / d, cyclonic), which is the Fujiwhara rotation; storms whose cores
// overlap merge. Central pressure follows Holland (1980):
//   p_env - p_c = rho e V_max^2 / B.
// Landfall records the time, position, intensity, category and a surge
// estimate (inverse barometer + wind setup). Everything is a parametrised
// diagnostic on the single-layer state; there is no feedback on the flow.

struct SurfaceConfig {
    // Per-cell land mask (1 land, 0 ocean) and sea-surface temperature (K).
    std::vector<unsigned char> land;
    std::vector<double> sea_surface_temperature_k;
};

struct StormSeed {
    double east_m{0.0};
    double north_m{0.0};
    double max_wind_m_s{18.0};
    double radius_max_wind_m{40000.0};
};

struct StormSystemsConfig {
    ColumnConfig column;
    double enthalpy_exchange_ratio{0.9};        // C_k / C_D
    double surface_wind_factor{0.8};            // gradient -> surface wind
    double sst_threshold_k{299.65};             // 26.5 C
    double minimum_coriolis_s{2.5e-5};          // about 10 degrees latitude
    double mid_level_height_m{4000.0};          // RH level for GPI (~600 hPa)
    double shear_layer_depth_m{10000.0};        // thermal-wind shear depth (850-200 hPa)
    double shear_limit_m_s{20.0};               // potential intensity vanishes at this shear
    double genesis_gpi_threshold{8.0};
    double genesis_separation_m{600000.0};      // no new storm within this distance of another
    double genesis_max_wind_m_s{17.0};
    double intensification_time_scale_s{24.0 * 3600.0};
    double decay_alpha_per_s{0.095 / 3600.0};   // Kaplan & DeMaria
    double decay_background_wind_m_s{13.75};
    double landfall_reduction{0.9};
    double dissipation_wind_m_s{10.0};
    double holland_b{1.3};
    double steering_radius_m{300000.0};
    double steering_factor{1.0};
    double beta_drift_west_m_s{1.0};
    double beta_drift_poleward_m_s{1.5};
    double merge_distance_m{100000.0};          // plus the two radii of maximum wind
    // Fraction of the other storm's Rankine wind that steers a storm (the
    // deep-layer mean circulation is weaker than the surface wind).
    double induction_factor{0.4};
    double genesis_edge_margin_m{100000.0};     // no genesis this close to the domain edge
    double surge_wind_coefficient_s2_m{1.5e-3}; // wind-setup surge = coeff * V^2
    double rain_core_factor_mm_h_per_m_s{0.4};  // core rain rate = factor * V_max
    double substep_seconds{600.0};
    std::size_t max_storms{8};
};

struct StormEnvironment {
    std::vector<double> potential_intensity_m_s;   // 0 over land / cold sea
    std::vector<double> genesis_potential_index;
    std::vector<double> vertical_shear_m_s;
    std::vector<double> relative_vorticity_s;
    std::vector<double> absolute_vorticity_s;
    std::vector<double> mid_level_relative_humidity;
    std::vector<double> outflow_temperature_k;
    std::vector<double> enthalpy_disequilibrium_j_kg;   // k_s* - k_b
};

struct StormEvent {
    std::string kind;       // genesis, landfall, merger, dissipation, seed
    double elapsed_seconds{0.0};
    std::size_t storm_id{0};
    std::size_t other_id{0};  // merger partner
    double east_m{0.0};
    double north_m{0.0};
    double max_wind_m_s{0.0};
    std::string category;
    double surge_m{0.0};
};

struct TropicalCyclone {
    std::size_t id{0};
    double east_m{0.0};
    double north_m{0.0};
    double max_wind_m_s{0.0};
    double radius_max_wind_m{40000.0};
    double central_pressure_pa{0.0};
    double environment_pressure_pa{0.0};
    std::string category;      // depression, tropical_storm, category_1 .. category_5
    bool over_land{false};
    double age_seconds{0.0};
    double time_over_land_seconds{0.0};
    double potential_intensity_m_s{0.0};
    double surge_estimate_m{0.0};
    double core_rain_rate_mm_h{0.0};
    std::vector<std::pair<double, double>> track;   // (east, north) per sub-step record
};

struct StormSystemsState {
    std::vector<TropicalCyclone> storms;
    std::vector<StormEvent> events;
    std::size_t next_id{1};
};

struct StormFields {
    std::vector<double> wind_speed_m_s;       // Rankine vortex envelope of all storms
    std::vector<double> pressure_anomaly_pa;  // Holland profile, negative in the core
    std::vector<double> rain_rate_mm_h;
    std::vector<double> surge_m;              // coastal land cells near a storm
};

void validate_storm_systems_config(const StormSystemsConfig& config);
void validate_surface(const SurfaceConfig& surface, const TransportGrid2D& grid);

// Helpers exposed for tests and documentation.
double potential_intensity_m_s(
    double sea_surface_temperature_k, double air_temperature_k, double specific_humidity_kg_kg,
    double pressure_pa, double outflow_temperature_k, const StormSystemsConfig& config);
double genesis_potential_index(
    double absolute_vorticity_s, double mid_level_relative_humidity, double potential_intensity_m_s,
    double vertical_shear_m_s);
double holland_pressure_drop_pa(double max_wind_m_s, double air_density_kg_m3, double holland_b);
double surge_estimate_m(double pressure_drop_pa, double max_wind_m_s, double wind_coefficient_s2_m);
std::string saffir_simpson_category(double max_wind_m_s);
double kaplan_demaria_wind_m_s(double landfall_wind_m_s, double hours_over_land, const StormSystemsConfig& config);

StormEnvironment diagnose_storm_environment(
    const CoupledWindState& state, const TransportGrid2D& grid, const SurfaceConfig& surface,
    const StormSystemsConfig& config);

// Advance the storms over duration_seconds in sub-steps against a fixed
// environment (the coupled state at the start of the interval). Seeds are
// inserted before the first sub-step. elapsed_seconds is the time at the
// start of the interval, used for event time stamps.
void advance_storm_systems(
    StormSystemsState& storms, const std::vector<StormSeed>& seeds,
    const CoupledWindState& state, const TransportGrid2D& grid, const SurfaceConfig& surface,
    const StormEnvironment& environment, const StormSystemsConfig& config,
    double elapsed_seconds, double duration_seconds);

StormFields storm_fields(
    const StormSystemsState& storms, const TransportGrid2D& grid, const SurfaceConfig& surface,
    const StormSystemsConfig& config);

}  // namespace terraflux::weather
