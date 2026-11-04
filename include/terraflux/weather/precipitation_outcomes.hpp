#pragma once

#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/cloud_formation.hpp"
#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Precipitation outcomes v1: what each cloud formation delivers at the ground.
//
// Genus -> character (WMO usage):
//   cirrus, cirrostratus, altocumulus, cumulus : no precipitation (ice
//     crystals or shallow droplets that never reach the ground; cirrostratus
//     often precedes a warm front).
//   stratus                 : continuous drizzle (or snow grains).
//   stratocumulus           : at most intermittent light drizzle.
//   altostratus             : intermittent light rain/snow, often virga.
//   nimbostratus            : continuous, prolonged moderate rain or snow.
//   towering cumulus        : showers.
//   cumulonimbus            : heavy showers, thunderstorm (lightning) and
//                             hail when the updraft and the ice region allow.
//
// Rate (cell mean) from condensation in lifted saturated air:
//   R = sigma * epsilon * rho_base * w * (q_s(base) - q_s(top)) * 3600  [mm/h]
// with w the lifting through the cloud (stratiform: cloud depth / lifting
// time scale; convective: updraft_parcel_factor * sqrt(2 CAPE)), sigma the
// area fraction of updrafts (1 for layered cloud, convective_updraft_fraction
// for convection), epsilon the precipitation efficiency and q_s along the
// moist adiabat from the cloud base to the cloud top.
//
// Phase at the ground from the surface wet-bulb temperature: snow at or
// below snow_wet_bulb_k, sleet (mixed) up to sleet_wet_bulb_k, else liquid;
// clouds entirely warmer than 0 °C give liquid only. Lightning requires a
// cumulonimbus whose top is colder than lightning_top_temperature_k and which
// crosses the freezing level (mixed-phase charge separation). Hail: the
// largest stone the updraft can hold has terminal velocity equal to the
// updraft, v_t = sqrt(4 g rho_ice D / (3 C_d rho_air)); it loses
// hail_melting_cm_per_km on the way down from the freezing level.
//
// Everything is a cell-mean rule-of-thumb diagnostic; there is no
// microphysics, no precipitation transport and no feedback on the air.

struct PrecipitationConfig {
    double stratiform_efficiency{0.7};
    double convective_efficiency{0.4};
    double convective_updraft_fraction{0.08};
    double updraft_parcel_factor{0.5};
    // Snapshot interval over which accumulation is estimated.
    double interval_seconds{8.0 * 3600.0};
    double snow_wet_bulb_k{273.65};       // 0.5 °C
    double sleet_wet_bulb_k{275.15};      // 2.0 °C
    double lightning_top_temperature_k{253.15};   // -20 °C
    double hail_min_diameter_cm{0.5};
    double hail_melting_cm_per_km{0.3};
    double hail_max_diameter_cm{10.0};
    double drizzle_max_rate_mm_h{0.5};
    double virga_surface_rh{0.4};
    double virga_max_rate_mm_h{1.0};
    double virga_min_base_m{2000.0};
    double minimum_rate_mm_h{0.05};
};

struct PrecipitationField {
    // none, drizzle, rain, sleet, snow, rain_showers, sleet_showers, snow_showers
    std::vector<std::string> precipitation_type;
    std::vector<std::string> phase;       // none, liquid, mixed, solid
    std::vector<std::string> character;   // none, drizzle, continuous, intermittent, showers
    std::vector<std::string> intensity;   // none, light, moderate, heavy, violent (WMO rate classes)
    std::vector<double> rate_mm_h;        // at the ground while precipitating
    std::vector<double> duration_fraction;   // of the interval
    std::vector<double> accumulation_mm;     // rate * duration fraction * interval
    std::vector<double> hail_max_diameter_cm;   // 0 when none
    std::vector<unsigned char> lightning;
    std::vector<unsigned char> virga;     // precipitation evaporating before the ground
    std::vector<double> updraft_m_s;      // lifting used for the rate
    std::vector<double> freezing_level_m; // NaN when the column never crosses 0 C
    std::vector<double> surface_wet_bulb_k;
    std::vector<double> precipitable_water_mm;
    std::vector<double> cloud_base_temperature_k;   // NaN when clear
    std::vector<double> cloud_top_temperature_k;    // NaN when clear
    std::vector<std::vector<std::string>> hazards;  // thunderstorm, hail, heavy_rain, violent_rain,
                                                    // prolonged_rain, snow, sleet, drizzle, virga
    std::size_t precipitating_cells{0};
    std::size_t lightning_cells{0};
    std::size_t hail_cells{0};
    std::size_t snow_cells{0};
    double max_rate_mm_h{0.0};
    double total_accumulation_mm{0.0};   // sum over cells
};

void validate_precipitation_config(const PrecipitationConfig& config);

// Helpers exposed for tests and documentation.
std::string precipitation_character_for_type(const std::string& cloud_type);
double hail_terminal_velocity_m_s(double diameter_m);
double hail_diameter_for_updraft_m(double updraft_m_s);
std::string precipitation_intensity_class(double rate_mm_h);

PrecipitationField diagnose_precipitation(
    const CoupledWindState& state,
    const TransportGrid2D& grid,
    const CloudFormationConfig& cloud_config,
    const CloudField& clouds,
    const PrecipitationConfig& config);

}  // namespace terraflux::weather
