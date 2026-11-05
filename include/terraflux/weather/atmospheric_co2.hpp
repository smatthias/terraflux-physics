#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <vector>

namespace terraflux::weather {

// Atmospheric CO2 v1: the CO2 of the boundary-layer air over a region, in ppm.
//
// The background mixing ratio X_bg (the global concentration of a scenario) enters at the inflow boundaries and is
// what the boundary layer relaxes to by exchange with the free troposphere (ventilation time tau). Within the
// region the wind carries local anomalies (upwind advection, advective form) and diffusion spreads them; sources
// and sinks change the column of n = rho_air H / M_air moles of air per m2:
//   dX/dt = -u.grad X + K lap X + 1e6 (E - U_land - F_ocean) / n - (X - X_bg) / tau
// with emissions E (cities, industry), land uptake U_land (net ecosystem exchange: photosynthesis in sunlight minus
// respiration) and the ocean's uptake F_ocean (from the ocean carbon model; negative = outgassing).

struct AtmosphericCo2Config {
    TransportGrid2D grid;
    double boundary_layer_height_m{1000.0};
    double air_density_kg_m3{1.2};
    double ventilation_time_s{2.0 * 86400.0};
    double horizontal_diffusivity_m2_s{5.0e3};
};

void validate_atmospheric_co2(const AtmosphericCo2Config& config);

struct AtmosphericCo2Forcing {
    double background_ppm{420.0};
    std::vector<double> wind_east_m_s, wind_north_m_s;
    std::vector<double> emission_mol_m2_s;       // >= 0
    std::vector<double> land_uptake_mol_m2_s;    // net uptake by vegetation (may be negative: respiration)
    std::vector<double> ocean_uptake_mol_m2_s;   // into the ocean (negative = outgassing)
};

struct AtmosphericCo2State {
    double elapsed_seconds{0.0};
    std::vector<double> ppm;
    // Cumulative mol.
    double emitted_mol{0.0}, land_uptake_mol{0.0}, ocean_uptake_mol{0.0}, ventilation_mol{0.0}, transport_mol{0.0};
    double background_shift_mol{0.0};            // change of the excess because the background itself rose (negative) or fell
    double inventory_excess_mol{0.0};            // moles above the background in the region's boundary layer
    // excess(t) - excess(0) = emitted - land - ocean - ventilation + transport + background_shift
};

double boundary_layer_air_mol_m2(const AtmosphericCo2Config& config);
AtmosphericCo2State initialize_atmospheric_co2(const AtmosphericCo2Config& config, double background_ppm);
AtmosphericCo2State advance_atmospheric_co2(const AtmosphericCo2State& state, const AtmosphericCo2Forcing& forcing, const AtmosphericCo2Config& config, double dt_seconds);

}  // namespace terraflux::weather
