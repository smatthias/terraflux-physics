#pragma once

#include "terraflux/weather/moist_air.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace terraflux::weather {

// Atmospheric column v1: vertical layers above one grid cell.
//
// The column is hydrostatic: pressure and density thin with height according
// to dp/dz = -rho g with rho from the moist-air relations. Layers are fixed
// in height. Two vertical exchange processes connect the ground to higher
// altitudes:
//
//   * Parcel ascent (diagnostic): a surface parcel is lifted dry-adiabatically
//     to its lifting condensation level (cloud base), then pseudo-adiabatically.
//     Comparing its virtual temperature with the environment gives the level
//     of free convection, the equilibrium level (cloud top), CAPE and CIN,
//     which are the basis of cumulus cloud and thunderstorm development.
//   * Convective adjustment (prognostic on the column): where potential
//     temperature decreases with height the layers are mixed, conserving
//     enthalpy and moisture (dry convective adjustment); where a layer is
//     supersaturated the excess vapour condenses, releasing latent heat and
//     leaving cloud water. Repeated until the column is stable.
//
// No precipitation, radiation, cloud microphysics, moist convective
// adjustment or horizontal coupling of the column is modelled.

namespace column {
constexpr double kGravityMS2 = 9.80665;
constexpr double kSpecificHeatDryAirJkgK = 1004.6;
constexpr double kLatentHeatVaporisationJkg = 2.501e6;
constexpr double kReferencePressurePa = 100000.0;
constexpr double kKappa = moist_air::kDryAirGasConstantJkgK / kSpecificHeatDryAirJkgK;
}  // namespace column

struct ColumnConfig {
    std::size_t layer_count{24};
    double layer_thickness_m{500.0};
    // Environment temperature profile used to build a column from surface
    // values: T(z) = T_s - lapse * z up to the tropopause, isothermal above.
    double environment_lapse_rate_k_m{0.0065};
    double tropopause_height_m{11000.0};
    // Environment humidity profile q(z) = q_s exp(-z / scale height).
    double humidity_scale_height_m{2500.0};
    // Vertical step of the parcel integration.
    double parcel_step_m{50.0};
};

struct AtmosphericColumn {
    // Layer k spans [interface_height_m[k], interface_height_m[k + 1]] and
    // carries values at height_m[k] (the layer midpoint). Index 0 is the
    // lowest layer.
    std::vector<double> interface_height_m;    // layer_count + 1
    std::vector<double> interface_pressure_pa; // layer_count + 1
    std::vector<double> height_m;
    std::vector<double> pressure_pa;
    std::vector<double> air_temperature_k;
    std::vector<double> specific_humidity_kg_kg;
    std::vector<double> cloud_water_kg_kg;     // condensate from adjustment
    // Diagnostics, refreshed by rediagnose_column.
    std::vector<double> air_density_kg_m3;
    std::vector<double> potential_temperature_k;
    std::vector<double> relative_humidity_fraction;
    std::vector<double> layer_mass_kg_m2;      // (p_bottom - p_top) / g
};

// Thermodynamic helpers (all SI; throw std::invalid_argument on bad input).
double potential_temperature_k(double air_temperature_k, double pressure_pa);
double dry_adiabatic_lapse_rate_k_m();  // g / c_p
// Pseudo-adiabatic (saturated) lapse rate at the given state.
double moist_adiabatic_lapse_rate_k_m(double air_temperature_k, double pressure_pa);
// Bolton (1980) equivalent potential temperature; conserved along the
// pseudo-adiabat to about 0.5 K and used to verify the moist ascent.
double equivalent_potential_temperature_k(
    double air_temperature_k, double pressure_pa, double specific_humidity_kg_kg);
double dew_point_k(double vapour_pressure_pa);

void validate_column_config(const ColumnConfig& config);
void validate_column(const AtmosphericColumn& column);

// Build a hydrostatic column from the surface state using the configured
// environment profiles. Surface values are applied at the first interface.
AtmosphericColumn build_hydrostatic_column(
    double surface_pressure_pa,
    double surface_temperature_k,
    double surface_specific_humidity_kg_kg,
    const ColumnConfig& config);

// Recompute density, potential temperature, relative humidity and layer mass
// from the current pressure, temperature and humidity.
void rediagnose_column(AtmosphericColumn& column);

struct ParcelAscent {
    // Samples along the ascent (every parcel_step_m from the lowest layer).
    std::vector<double> height_m;
    std::vector<double> pressure_pa;
    std::vector<double> temperature_k;
    std::vector<double> specific_humidity_kg_kg;
    std::vector<double> condensed_water_kg_kg;   // cumulative, pseudo-adiabatic
    std::vector<double> environment_temperature_k;
    std::vector<double> buoyancy_m_s2;           // g (Tv_parcel - Tv_env) / Tv_env
    std::optional<double> lcl_height_m;          // lifting condensation level
    std::optional<double> lfc_height_m;          // level of free convection
    std::optional<double> equilibrium_level_m;   // top of the last positive area
    double cape_j_kg{0.0};                       // positive area LFC..EL
    double cin_j_kg{0.0};                        // negative area surface..LFC (>= 0)
    // Rule-of-thumb classification by CAPE: none (< 100), weak (< 1000),
    // moderate (< 2500), strong (>= 2500 J/kg). Not a forecast.
    std::string convective_potential{"none"};
};

ParcelAscent lift_surface_parcel(const AtmosphericColumn& column, const ColumnConfig& config);

struct ConvectiveAdjustmentReport {
    std::size_t dry_mixing_events{0};
    std::size_t condensing_layers{0};
    double condensed_water_kg_m2{0.0};   // column-integrated new condensate
    double latent_heating_j_m2{0.0};
    std::size_t iterations{0};
};

// Dry convective adjustment plus condensation of supersaturated layers,
// iterated to a stable, unsaturated column. Conserves column enthalpy
// (sum m c_p T + L q) and total water (vapour + cloud water). Pressure and
// heights are held fixed.
ConvectiveAdjustmentReport adjust_column_convection(AtmosphericColumn& column);

// Per-cell column diagnostics for a horizontal field of surface states, used
// by the coupled wind CLI. Missing levels are NaN.
struct ColumnFieldDiagnostics {
    std::vector<double> lcl_height_m;
    std::vector<double> lfc_height_m;
    std::vector<double> equilibrium_level_m;
    std::vector<double> cape_j_kg;
    std::vector<double> cin_j_kg;
    std::vector<std::string> convective_potential;
    std::vector<double> condensed_water_kg_m2;   // from adjusting the environment
    std::vector<double> column_mass_kg_m2;
    std::vector<double> top_pressure_pa;
};

ColumnFieldDiagnostics diagnose_column_field(
    const std::vector<double>& surface_pressure_pa,
    const std::vector<double>& surface_temperature_k,
    const std::vector<double>& surface_specific_humidity_kg_kg,
    const ColumnConfig& config);

}  // namespace terraflux::weather
