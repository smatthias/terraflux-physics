#pragma once

#include "terraflux/weather/ocean_circulation.hpp"
#include "terraflux/weather/sea_ice_physics.hpp"

namespace terraflux::weather {

// Sea ice v1: thermodynamic sea ice (the polar caps) on an ocean surface layer.
//
// Zero-layer ice (Semtner 1976) of thickness h covering a fraction a of the cell:
//   surface energy balance     Q_atm(T_s) + k_i (T_f - T_s) / h = 0     (T_s <= 0 degC; the excess melts the top)
//   Q_atm                      the bulk air-sea fluxes over ice: shortwave with the ice albedo (cold dry ice
//                              -> melting ice over the last kelvin below 0 degC), Berliand longwave, sensible,
//                              and sublimation with L_s = L_v + L_f
//   base                       rho_i L_f dh/dt = F_c - F_oi,   F_c = k_i (T_f - T_s) / h,
//                              F_oi = rho_0 c_p c_h u* (T_ocean - T_f)  (McPhee 1992: c_h = 0.006, u* = 0.01 m/s)
//   open water                 cooling below the freezing point T_f = 273.15 - 0.054 S - 7.53e-4 z freezes new
//                              ice of thickness h_0 in the open fraction (Hibler 1979), raising a
//   melting                    thickness and concentration shrink together, a' = a sqrt(V'/V)
//   salt                       ice keeps S_i (5 psu): freezing rejects brine into the water, melt returns
//                              nearly fresh water
// The ice-albedo feedback follows: less ice -> more absorbed sunshine in the open water -> more basal melt.

double seawater_freezing_point_k(double salinity_psu, double depth_m = 0.0);


void validate_sea_ice_physics(const SeaIcePhysics& physics);

// The atmosphere over one cell (the same quantities as the air-sea forcing).
struct AirState {
    double temperature_k{273.15};
    double specific_humidity_kg_kg{0.003};
    double pressure_pa{101325.0};
    double wind_speed_m_s{5.0};
    double cloud_fraction{0.5};
    double shortwave_down_w_m2{0.0};
};

struct SeaIceSurfaceBalance {
    double surface_temperature_k{273.15};
    double albedo{0.65};
    double conductive_w_m2{0.0};       // heat conducted up through the ice (positive when the top is colder)
    double shortwave_w_m2{0.0};        // absorbed
    double longwave_w_m2{0.0};
    double sensible_w_m2{0.0};
    double latent_w_m2{0.0};           // sublimation (L_v + L_f)
    double atmosphere_w_m2{0.0};       // sum of the four, into the ice surface
    double surface_melt_w_m2{0.0};     // energy that melts the top (>= 0)
    double sublimation_m_s{0.0};       // ice thickness lost to sublimation (negative = deposition)
};

// Surface balance of ice of thickness h over water at its freezing point T_f.
SeaIceSurfaceBalance sea_ice_surface_balance(double thickness_m, double freezing_point_k, const AirState& air, const OceanAirSeaConfig& bulk, const SeaIcePhysics& physics);

// Ocean-to-ice heat flux (W/m2 of ice-covered area), positive when the water is above freezing.
double ocean_ice_heat_flux_w_m2(double ocean_temperature_k, double freezing_point_k, double reference_density_kg_m3, double heat_capacity_j_kg_k, const SeaIcePhysics& physics);

// Implicit thickness change of the ice-covered part over dt, rho_i L_f (h1 - h0) = -dt (Q(T_s(h1)) + F_oi), with Q
// the net heat from above (minus the conduction when the top is frozen, the atmosphere when it melts). Returns h1;
// `surplus` is the energy (J per m2 of ice) left over when the ice melts completely (it goes to the water).
double step_ice_thickness(double thickness_m, double freezing_point_k, double ocean_ice_flux_w_m2, const AirState& air, const OceanAirSeaConfig& bulk,
                          const SeaIcePhysics& physics, double dt_seconds, double& surplus_j_m2, SeaIceSurfaceBalance& balance);

// New concentration and thickness after the ice volume per cell area changes from a h to `volume_m`;
// `frozen_in_open_water_m` of the change is new ice formed in the open fraction (it raises a).
void update_ice_cover(double& concentration, double& thickness_m, double volume_m, double frozen_in_open_water_m, const SeaIcePhysics& physics);

// ---- Arctic column: a mixed layer with sea ice under a seasonal atmosphere (for century projections) ------
// The column stands for the central Arctic. Besides the thermodynamics, a fixed share of the ice volume leaves
// the basin each year (the drift through Fram Strait); without that sink thermodynamic ice would thicken far
// beyond the observed few metres. The exported ice carries its latent deficit out of the column (a budget term)
// and is fresh water for the North Atlantic.

struct ArcticColumnConfig {
    double mixed_layer_depth_m{50.0};
    double reference_salinity_psu{32.0};            // Arctic surface water
    double salinity_relaxation_s{5.0 * 365.0 * 86400.0};
    double deep_ocean_heat_flux_w_m2{2.0};          // Atlantic-water heat reaching the mixed layer
    double reference_density_kg_m3{1025.0};
    double heat_capacity_j_kg_k{3990.0};
    double arctic_ocean_area_m2{1.0e13};             // to turn the column into basin totals
    double ice_export_per_s{0.1 / (365.25 * 86400.0)};  // share of the ice volume leaving the basin (Fram Strait), per second
    SeaIcePhysics ice;
    OceanAirSeaConfig bulk;
};

struct ArcticColumnState {
    double elapsed_seconds{0.0};
    double mixed_layer_temperature_k{271.35};
    double mixed_layer_salinity_psu{32.0};
    double ice_thickness_m{2.0};
    double ice_concentration{0.95};
    // Diagnostics of the last step.
    double surface_temperature_k{250.0};
    double open_water_flux_w_m2{0.0};   // into the open water
    double ocean_ice_flux_w_m2{0.0};
    double growth_m_s{0.0};              // ice volume change rate (per cell area)
    double freshwater_m_s{0.0};          // fresh water released to the ocean by melt minus freezing (per area)
    double export_m_s{0.0};              // ice volume exported from the basin (per area); it melts downstream
    double heat_content_j_m2{0.0};       // mixed layer (relative to 0 degC) minus the latent heat in the ice
    double energy_error_j_m2{0.0};       // last step
};

void validate_arctic_column(const ArcticColumnConfig& config);
ArcticColumnState advance_arctic_column(const ArcticColumnState& state, const AirState& air, const ArcticColumnConfig& config, double dt_seconds);

}  // namespace terraflux::weather
