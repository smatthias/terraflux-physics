#pragma once

#include "terraflux/weather/overturning_box.hpp"
#include "terraflux/weather/solar_cycle.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Solar irradiance v1: what the sunspot cycle does to the light, the UV, the flares and the Earth's atmosphere.
//
// Proxies (monthly, from the sunspot number SN, v2):
//   F10.7 radio flux   F = F0 + f1 SN + f2 SN^2                            (solar flux units, 10^-22 W m^-2 Hz^-1)
//   Mg II index        M = M0 + m (F - F0)                                  (core-to-wing ratio: chromospheric faculae)
//   facular index      x = (M - M0) / (M(SN_ref) - M0)                      (0 quiet Sun, 1 at the reference maximum)
//   TSI                S = S_quiet + B x - D SN                             (facular brightening minus spot darkening:
//                                                                            the Sun is brighter at maximum although
//                                                                            it carries more dark spots)
//   UV band b          E_b = E_b,quiet (1 + v_b x)                         (v_b: relative change quiet -> reference max)
// The shorter the wavelength, the larger the cycle: Lyman-alpha ~+50 %, 200-242 nm (which splits O2 and makes ozone)
// ~+6 %, UV-B ~+1 %, TSI ~+0.08 %. Measured UV variability differs by about a factor of two between instruments and
// reconstructions (SORCE/SOLSTICE and SIM vs SATIRE and NRLSSI2; Ermolli et al. 2013): the defaults are mid-range.
//
// Middle atmosphere (linear in x): total ozone +2 % and upper-stratospheric temperature +1 K at the reference maximum.
// Surface UV index at local noon under clear sky (Madronich 2007):
//   UVI = 12.5 mu0^2.42 (Omega / 300 DU)^-1.23 d(day) (1 + v_ery x),   Omega = Omega_site(day) (1 + ozone change)
// with the Earth-Sun distance factor d = 1 + 0.0334 cos(2 pi (day - 3) / 365.25). More UV at the top of the atmosphere
// also makes more ozone, which absorbs UV-B: at the surface the two nearly cancel.
//
// Flares: the rate of flares with a GOES 1-8 A peak flux >= S follows the sunspot number and a power law in S,
//   N(>=S) = r_X (SN / SN_0)^gamma [(S / S_X1)^(1 - alpha) - (S_max / S_X1)^(1 - alpha)] / [1 - (S_max / S_X1)^(1 - alpha)]
// (S_X1 = 1e-4 W m^-2; N(>=X1) = r_X at SN_0). Their radiated energy E = E_X1 (S / S_X1)^beta reaches the Earth as
// E / (4 pi AU^2): averaged over time a few 10^-5 W m^-2, far below the cycle's ~1 W m^-2 in TSI.
//
// Climate: forcing F = dS (1 - albedo) / 4 drives a two-layer energy balance (Held et al. 2010; Geoffroy et al. 2013)
//   C dT/dt = F - lambda T - gamma (T - T_d),   C_d dT_d/dt = gamma (T - T_d),
// stepped implicitly (backward Euler), T the global-mean surface temperature anomaly.

struct SolarProxyConfig {
    double f107_base_sfu{65.0};
    double f107_per_sunspot{0.62};
    double f107_per_sunspot_squared{0.0};
    double mgii_quiet{0.1500};
    double mgii_per_sfu{1.1e-4};
    double reference_sunspot_number{180.0};
    double tsi_quiet_w_m2{1360.6};
    double facular_brightening_w_m2{2.1};          // at the reference maximum
    double spot_darkening_w_m2_per_sunspot{0.0055};
};

struct SolarUvBand {
    std::string key, label;
    double from_nm{0.0}, to_nm{0.0};
    double quiet_w_m2{0.0};
    double variability{0.0};                        // relative change from the quiet Sun to the reference maximum
};
std::vector<SolarUvBand> default_solar_uv_bands();

// Throws std::invalid_argument for out-of-range coefficients, decreasing proxies or invalid bands.
void validate_solar_proxies(const SolarProxyConfig& config, const std::vector<SolarUvBand>& bands);

struct SolarIrradiance {
    double sunspot_number{0.0};
    double f107_sfu{0.0};
    double mgii{0.0};
    double facular_index{0.0};
    double facular_w_m2{0.0}, spot_w_m2{0.0};      // brightening and darkening (both >= 0)
    double tsi_w_m2{0.0};
    std::vector<double> band_w_m2, band_change;     // per band: irradiance and relative change from the quiet Sun
};
double solar_f107(double sunspot_number, const SolarProxyConfig& config);
double solar_mgii(double sunspot_number, const SolarProxyConfig& config);
double solar_facular_index(double sunspot_number, const SolarProxyConfig& config);
SolarIrradiance solar_irradiance(double sunspot_number, const SolarProxyConfig& config, const std::vector<SolarUvBand>& bands);

// ---- Middle atmosphere and surface UV --------------------------------------------------------------------------

struct SolarAtmosphereConfig {
    double ozone_change_at_reference{0.02};
    double stratosphere_warming_at_reference_k{1.0};
    double erythemal_variability{0.010};             // of the erythemally weighted irradiance at the top of the atmosphere
    double radiation_amplification_factor{1.23};     // the ozone exponent of the UV index
};
void validate_solar_atmosphere(const SolarAtmosphereConfig& config);

struct SolarUvSite {
    std::string name;
    double latitude_deg{0.0};
    double ozone_du{300.0};                           // annual mean total column
    double ozone_seasonal_amplitude_du{0.0};
    double ozone_peak_day{90.0};
};
void validate_solar_uv_site(const SolarUvSite& site);

double earth_sun_distance_factor(double day_of_year);
// Madronich (2007) clear-sky UV index for a cosine of the solar zenith angle and a total ozone column.
double clear_sky_uv_index(double cos_zenith, double ozone_du, double top_of_atmosphere_factor = 1.0, double amplification = 1.23);

struct SolarUvIndex {
    double cos_zenith{0.0};
    double ozone_du{0.0};                             // with the solar-cycle ozone change
    double uv_index{0.0};
    double quiet_uv_index{0.0};                       // the same day under the quiet Sun
    double change{0.0};                               // relative to the quiet Sun
};
// Noon UV index at a site on a day of the year for a facular index.
SolarUvIndex solar_site_uv_index(const SolarUvSite& site, double day_of_year, double facular_index, const SolarAtmosphereConfig& config);

// ---- Flares ------------------------------------------------------------------------------------------------------

constexpr double kFlareC1 = 1.0e-6, kFlareM1 = 1.0e-5, kFlareM5 = 5.0e-5, kFlareX1 = 1.0e-4, kFlareX10 = 1.0e-3, kFlareX20 = 2.0e-3;

struct SolarFlareConfig {
    double x_rate_per_year{15.0};                    // flares >= X1 per year at the reference sunspot number
    double reference_sunspot_number{100.0};
    double rate_exponent{1.3};                       // gamma
    double size_index{2.2};                          // alpha in dN/dS ~ S^-alpha: ~16 M-class flares per X-class flare
    double max_peak_flux_w_m2{4.0e-3};               // X40: the power law's upper cut-off, about the largest flares known
                                                     // (4 Nov 2003, saturated at X28, estimated X35-X45; Carrington 1859,
                                                     // estimated ~X45). A pure power law to X100 would make flares above X50
                                                     // routine over a century.
    double energy_x1_j{1.0e25};                      // radiated (bolometric) energy of an X1 flare
    double energy_exponent{0.8};                     // beta
};
void validate_solar_flares(const SolarFlareConfig& config);
double flare_rate_per_year(double sunspot_number, double threshold_w_m2, const SolarFlareConfig& config);
double flare_probability(double rate_per_year, double years);
double flare_energy_j(double peak_flux_w_m2, const SolarFlareConfig& config);
// Time-mean irradiance at the Earth of all flares >= lower_w_m2 at a sunspot number.
double mean_flare_irradiance_w_m2(double sunspot_number, double lower_w_m2, const SolarFlareConfig& config);
// Peak flux of a flare >= lower_w_m2 from a uniform number in (0, 1) (inverse of the truncated power law).
double sample_flare_peak_flux(double uniform, double lower_w_m2, const SolarFlareConfig& config);
// GOES class: "X2.3", "M1.0", "C5.6", "B8.1".
std::string flare_class_label(double peak_flux_w_m2);
// NOAA radio-blackout scale: 0 below M1, R1 >= M1, R2 >= M5, R3 >= X1, R4 >= X10, R5 >= X20.
int radio_blackout_level(double peak_flux_w_m2);

// ---- Climate -----------------------------------------------------------------------------------------------------

struct SolarClimateConfig {
    double albedo{0.29};
    double upper_heat_capacity{7.3};                 // W yr m^-2 K^-1 (mixed layer and atmosphere)
    double deep_heat_capacity{106.0};
    double feedback_w_m2_k{1.13};                    // lambda
    double exchange_w_m2_k{0.74};                    // gamma
};
void validate_solar_climate(const SolarClimateConfig& config);
struct SolarClimateState {
    double surface_k{0.0};
    double deep_k{0.0};
};
double solar_radiative_forcing_w_m2(double tsi_anomaly_w_m2, const SolarClimateConfig& config);
SolarClimateState step_solar_climate(const SolarClimateState& state, double forcing_w_m2, double dt_years, const SolarClimateConfig& config);

// ---- The solar disk ----------------------------------------------------------------------------------------------

// An illustrative day on the visible disk (observer in the solar equator plane): active regions drawn from the
// cycles present at that time, at the latitudes of Sporer's law, rendered with limb darkening, dark spots and
// bright faculae. Latitude of the spot zone of a cycle t years after its onset (Hathaway 2011):
//   lambda(t) = lambda_0 exp(-12 t / tau),  lambda_0 = 28 deg, tau = 90 months,  width max(w_min, f lambda).
// Each cycle contributes groups in proportion to its share of the sunspot number; areas are lognormal.
struct SolarDiskConfig {
    std::size_t cells{56};                            // grid cells across
    double extent_radii{1.08};                        // half width of the grid in solar radii
    std::size_t subsamples{6};                        // per cell and direction
    double solar_radius_m{6.957e8};
    double effective_temperature_k{5772.0};
    double limb_darkening{0.6};                       // I(mu) / I(1) = 1 - u (1 - mu)
    double groups_per_sunspot{1.0 / 17.0};
    double group_area_median_uhem{100.0};             // millionths of the hemisphere
    double group_area_log_sd{1.0};
    double umbra_fraction{0.2};
    double umbra_contrast{0.25};                      // intensity relative to the quiet photosphere
    double penumbra_contrast{0.78};
    double facular_area_ratio{23.0};                  // bright area (plage and enhanced network) per spot area: with it the
                                                      // rendered disk brightens by as much as the TSI proxy's faculae
    double facular_contrast_centre{0.005};
    double facular_contrast_limb{0.12};               // contrast = centre + (limb - centre) (1 - mu)
    double onset_latitude_deg{28.0};
    double drift_efold_months{90.0};
    double zone_width_fraction{0.25};
    double zone_min_width_deg{3.0};
    double max_longitude_deg{85.0};
    double area_per_sunspot_uhem{10.0};               // mean spot area per unit sunspot number (butterfly diagram)
};
void validate_solar_disk(const SolarDiskConfig& config);

struct SolarActiveRegion {
    double latitude_deg{0.0};
    double longitude_deg{0.0};                        // from the central meridian
    double area_uhem{0.0};
    int cycle{0};
};
double spot_zone_latitude_deg(double years_since_onset, const SolarDiskConfig& config);
double spot_zone_width_deg(double latitude_deg, const SolarDiskConfig& config);
// Poisson number of groups for the sunspot number, each from a cycle weighted by its curve value at `year`.
std::vector<SolarActiveRegion> sample_active_regions(double year, double sunspot_number, const std::vector<SolarCycleCurve>& curves,
                                                     const std::vector<int>& cycle_numbers, NormalSequence& random, const SolarDiskConfig& config);
int sample_poisson(double mean, NormalSequence& random);

struct SolarDiskImage {
    std::vector<double> brightness_k;                 // cell-mean brightness temperature; NaN off the disk
    std::vector<double> quiet_brightness_k;           // the same without regions
    double spot_change{0.0};                          // disk-integrated relative intensity change from the spots (< 0)
    double facular_change{0.0};                       // and from the faculae (> 0)
    double net_change{0.0};
};
SolarDiskImage render_solar_disk(const std::vector<SolarActiveRegion>& regions, const SolarDiskConfig& config);

// Expected spot area density (millionths of the hemisphere per degree of latitude) at `year` for each latitude.
std::vector<double> butterfly_density(double year, const std::vector<SolarCycleCurve>& curves, const std::vector<double>& latitudes_deg, const SolarDiskConfig& config);

}  // namespace terraflux::weather
