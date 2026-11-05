#pragma once

#include "terraflux/weather/ocean_carbon.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <vector>

namespace terraflux::weather {

// Ocean biogeochemistry and habitability v1: where the ocean takes up CO2, where it turns corrosive and where it
// runs out of oxygen (dead zones).
//
// Four tracers in umol/kg on fixed depth levels down to the seafloor (or to the deepest level in deep water):
// dissolved inorganic carbon (DIC), total alkalinity (TA), oxygen (O2) and nitrate (NO3). Each step:
//   transport        flux form: upwind advection with the ocean's current within its active layer (the deep water
//                    below is at rest) and horizontal diffusion over the thickness two neighbouring levels share.
//                    The depth-integrated transports are made consistent with the column depths by a potential-flow
//                    correction, so interior columns keep their water; open-boundary cells exchange with the ocean
//                    outside (inflow brings the reference water) and relax to it; within a column the flow over a
//                    sloping seafloor is closed by vertical fluxes. Conservative; uniform fields stay uniform
//   rivers           river water enters the surface level of the mouth with its own chemistry and pushes the same
//                    volume of sea water on toward the open boundary
//   mixing           levels above the ocean's mixed-layer depth are homogenised; implicit diffusion below
//   air-sea          CO2: F = k K0 (pCO2_air - pCO2_sea), O2 relaxes to saturation with k/dz (Wanninkhof 2014),
//                    blocked by sea ice
//   production       net community production P = e^(0.0633 (T - 20 C)) L(I) min(mu_max NO3, P_max NO3 / (NO3 + K)) in the
//                    sunlit levels (I = PAR e^(-k z)): first order in scarce nitrate, capped in eutrophic water; it takes up nitrate, carbon (C:N 117:16, plus CaCO3 at the rain
//                    ratio) and raises O2 (O2:N 170:16) and TA (+1 per nitrate; -2 per CaCO3)
//   export           the organic matter sinks with the Martin curve F(z) = F(z0) (z / z0)^-b and is respired on its
//                    way down: O2 is consumed, carbon and nitrate return. On shelves whatever reaches the seafloor
//                    is respired in the bottom water (benthic demand): with summer stratification this drains the
//                    bottom water of oxygen, as off river deltas. In deep water the rest leaves the column.
//                    When O2 runs out, nitrate is used (denitrification); beyond that the demand is recorded as
//                    sulfidic (hydrogen sulfide) and O2 stays at zero.
//   ventilation      below the ventilation depth the water relaxes to the reference profiles (the renewal by the
//                    large-scale circulation the column does not resolve)
// Habitability per level: hypoxic below 60 umol/kg (about 2 mg/L: most animals leave or die: a dead zone), anoxic
// below 5 umol/kg, corrosive where aragonite is undersaturated, metabolically stressed where the metabolic index
// (oxygen against the temperature-raised demand) is below its ecological threshold.

struct MarineTracerProfile {
    double surface{0.0};
    double deep{0.0};
    double scale_m{200.0};   // value(z) = deep + (surface - deep) exp(-z / scale)
    double at(double depth_m) const;
};

struct RiverChemistry {
    double nitrate_umol_kg{80.0};
    double dic_umol_kg{1900.0};
    double alkalinity_umol_kg{1900.0};
    double oxygen_saturation{1.0};
};

struct MarineBgcConfig {
    TransportGrid2D grid;
    std::vector<unsigned char> sea;
    std::vector<double> seafloor_depth_m;          // per cell; columns stop at the seafloor (at least one level)
    std::vector<double> level_bottoms_m;           // increasing depths of the level bottoms
    MarineTracerProfile dic{2020.0, 2250.0, 300.0};
    MarineTracerProfile alkalinity{2320.0, 2360.0, 400.0};
    MarineTracerProfile oxygen{230.0, 150.0, 300.0};
    MarineTracerProfile nitrate{0.5, 30.0, 250.0};
    bool surface_dic_from_atmosphere{true};        // initial DIC profile shifted so the surface is in equilibrium
    // Biology.
    double max_growth_per_day{0.6};                // nutrient-limited: uptake rate of the available nitrate
    double max_production_umol_n_kg_day{1.0};     // light-saturated ceiling of net production per kg of water
    double temperature_factor_per_k{0.0633};
    double nitrate_half_saturation_umol_kg{1.0};
    double light_half_saturation_w_m2{30.0};       // photosynthetically active radiation
    double par_fraction{0.43};
    double light_attenuation_per_m{0.06};
    double martin_exponent{0.858};
    double rain_ratio{0.07};                       // CaCO3 : organic carbon
    double carbon_to_nitrogen{117.0 / 16.0};
    double oxygen_to_nitrogen{170.0 / 16.0};
    double denitrification_nitrate_per_oxygen{0.8};
    // Transport and mixing.
    double horizontal_diffusivity_m2_s{500.0};
    double vertical_diffusivity_m2_s{1.0e-5};
    double ventilation_depth_m{150.0};
    double ventilation_time_s{10.0 * 365.25 * 86400.0};
    std::vector<double> open_boundary;             // per cell 0-1: cells open to the ocean outside (empty = a closed basin)
    double open_boundary_time_s{5.0 * 86400.0};    // relaxation of open cells to the reference profiles at full strength
    // Habitability thresholds.
    double hypoxia_umol_kg{60.0};
    double anoxia_umol_kg{5.0};
    double corrosive_omega{1.0};
    double dead_zone_column_fraction{0.2};         // a column is a dead zone when its bottom water or this share is hypoxic
    MetabolicIndexConfig metabolic;
};

void validate_bgc_config(const MarineBgcConfig& config);

struct MarineBgcRiver {
    std::size_t cell{0};
    double discharge_m3_s{0.0};
    RiverChemistry chemistry;
};

struct MarineBgcForcing {
    std::vector<double> temperature_k;             // cell * levels + level (sea cells)
    std::vector<double> salinity_psu;
    std::vector<double> mixed_layer_depth_m;       // per cell
    std::vector<double> current_east_m_s, current_north_m_s;
    std::vector<double> active_layer_thickness_m;  // per cell: depth the current moves (empty = the whole column); below it only diffusion
    std::vector<double> wind_speed_m_s;
    std::vector<double> shortwave_w_m2;            // at the sea surface (before the ice)
    std::vector<double> ice_fraction;              // empty = no ice
    std::vector<double> atmospheric_pco2_uatm;
    std::vector<MarineBgcRiver> rivers;
};

struct MarineBgcState {
    double elapsed_seconds{0.0};
    std::vector<std::size_t> levels;               // active levels per cell (0 on land)
    std::vector<double> dic, alkalinity, oxygen, nitrate;   // cell * L + level, umol/kg
    // Last-step diagnostics per cell.
    std::vector<double> co2_flux_mol_m2_s;         // into the ocean
    std::vector<double> o2_flux_mol_m2_s;
    std::vector<double> production_mol_c_m2_s;     // net community production (organic carbon)
    std::vector<double> export_mol_c_m2_s;         // organic carbon leaving the column bottom in deep water
    std::vector<double> benthic_mol_c_m2_s;        // organic carbon respired at the seafloor
    std::vector<double> surface_pco2_uatm;
    // Cumulative totals (mol) since the start.
    double air_sea_carbon_mol{0.0}, river_carbon_mol{0.0}, export_carbon_mol{0.0}, ventilation_carbon_mol{0.0};
    double air_sea_oxygen_mol{0.0}, river_oxygen_mol{0.0}, ventilation_oxygen_mol{0.0}, sulfidic_oxygen_demand_mol{0.0};
    double river_nitrogen_mol{0.0}, export_nitrogen_mol{0.0}, ventilation_nitrogen_mol{0.0}, denitrified_nitrogen_mol{0.0};
    double boundary_carbon_mol{0.0}, boundary_nitrogen_mol{0.0}, boundary_oxygen_mol{0.0};      // exchange with the ocean outside (open cells; the water rivers displace in a closed basin)
    double transport_carbon_mol{0.0}, transport_nitrogen_mol{0.0}, transport_oxygen_mol{0.0};   // residual of the transport (solver tolerance, clipping at zero)
    double carbon_inventory_mol{0.0}, nitrogen_inventory_mol{0.0}, oxygen_inventory_mol{0.0};
    // The reference profiles the deep levels are ventilated towards (set at initialisation).
    std::vector<double> reference_dic, reference_alkalinity, reference_oxygen, reference_nitrate;
};

MarineBgcState initialize_bgc(const MarineBgcConfig& config, const MarineBgcForcing& forcing);
MarineBgcState advance_bgc(const MarineBgcState& state, const MarineBgcForcing& forcing, const MarineBgcConfig& config, double dt_seconds);

// Habitability of each level and the column summaries.
enum class HabitatClass : int { habitable = 0, stressed = 1, hypoxic = 2, anoxic = 3 };
struct MarineHabitat {
    std::vector<int> level_class;                  // cell * L + level (-1 below the seafloor / on land)
    std::vector<double> level_ph, level_omega_aragonite, level_metabolic_index;
    std::vector<int> column_class;                 // worst class of the column (-1 on land)
    std::vector<double> habitable_fraction;        // share of the column volume that is habitable
    std::vector<double> hypoxic_thickness_m;
    std::vector<double> bottom_oxygen_umol_kg, minimum_oxygen_umol_kg, minimum_oxygen_depth_m;
    std::vector<double> surface_ph, surface_omega_aragonite, bottom_omega_aragonite, surface_pco2_uatm;
    std::vector<unsigned char> dead_zone, corrosive_bottom, corrosive_surface;
    // Totals.
    double sea_area_km2{0.0}, dead_zone_area_km2{0.0}, anoxic_area_km2{0.0}, corrosive_area_km2{0.0};
    double volume_km3{0.0}, hypoxic_volume_km3{0.0}, habitable_volume_km3{0.0};
    double mean_surface_ph{0.0}, mean_surface_omega_aragonite{0.0};
};
MarineHabitat assess_habitat(const MarineBgcState& state, const MarineBgcForcing& forcing, const MarineBgcConfig& config);

// Level geometry: top and bottom depth of level k.
double level_top_m(const MarineBgcConfig& config, std::size_t level);

}  // namespace terraflux::weather
