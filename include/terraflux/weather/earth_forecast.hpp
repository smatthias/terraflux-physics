#pragma once

#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/overturning_box.hpp"   // NormalSequence
#include "terraflux/weather/pressure_wind.hpp"
#include "terraflux/weather/storm_systems.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Coupled ensemble forecast v1: the building blocks that turn the standalone weather kernels (coupled wind, clouds,
// precipitation, severe convection, tropical cyclones) into a medium-range forecast with land, ocean and Sun coupled
// to the air, an ensemble for the uncertainty, warnings and a verification against a hidden "truth" run.
//
// 1. Synoptic systems. Highs and lows are moving Gaussian pressure anomalies: each is carried by the steering flow
//    (westerly jet, trade easterlies) at its latitude, deepens for a while, then fills. New cyclones form in a
//    genesis region at a given rhythm. The coupled layer responds to the evolving pressure every hour.
// 2. The surface. Over the sea the air relaxes to the sea-surface temperature and takes up moisture (the ocean
//    feeds hurricanes and convection); over land the Sun heats the layer by day (weaker under cloud and over wet
//    soil, which evaporates instead) and it cools at night (less under cloud); the land relaxes slowly to its
//    climate. Supersaturated air condenses: large-scale rain that warms the air (latent heat). Rain wets the soil;
//    wet soil turns more of the next rain into runoff (floods).
// 3. The ensemble. Every member draws errors of the initial positions, the speeds, the deepening, the size and the
//    genesis of the systems and of the initial temperature; the differences grow with lead time as in a real
//    ensemble, until the forecast carries no more information than the climate. A further member, drawn in the
//    same way, plays the unknown true weather: the forecasts are verified against it.
// 4. Products. Probabilities from the members; warnings where the probability of exceeding a hazard threshold is
//    high enough; connected warning areas.

// ---- Geography ----------------------------------------------------------------------------------------------------

struct GeoPoint {
    double latitude_deg{0.0};
    double longitude_deg{0.0};
};
// Spherical azimuthal equidistant projection about an anchor (the grid's local frame), and its inverse.
GeoPoint local_to_geographic(double east_m, double north_m, double anchor_latitude_deg, double anchor_longitude_deg);
std::array<double, 2> geographic_to_local(double latitude_deg, double longitude_deg, double anchor_latitude_deg, double anchor_longitude_deg);
double great_circle_km(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
// Cosine of the solar zenith angle (negative at night); days since 2000-01-01 12:00 UT.
double solar_cos_zenith(double latitude_deg, double longitude_deg, double days_since_j2000);

// ---- Synoptic systems ---------------------------------------------------------------------------------------------

struct SteeringFlow {
    double jet_latitude_deg{45.0};
    double jet_speed_m_s{12.0};
    double jet_width_deg{12.0};
    double trade_latitude_deg{15.0};
    double trade_speed_m_s{6.0};
    double trade_width_deg{8.0};
};
// Eastward steering (m/s) at a latitude: the westerly jet minus the trade easterlies.
double steering_east_m_s(double latitude_deg, const SteeringFlow& flow);

struct SynopticSystem {
    std::string name;
    std::string kind{"low"};                 // low or high
    double latitude_deg{45.0}, longitude_deg{-40.0};
    double anomaly_pa{-1500.0};              // < 0 for lows, > 0 for highs
    double radius_m{800000.0};
    double deepening_pa_per_day{0.0};        // change of |anomaly| while maturing (negative: weakening)
    double mature_hours{48.0};               // after this the system fills
    double filling_pa_per_day{500.0};        // decay of |anomaly| after maturity
    double steering_factor{0.8};             // share of the steering flow that moves it
    double drift_east_m_s{0.0}, drift_north_m_s{0.0};
    double start_hours{0.0};
};
struct GenesisRegion {
    std::string name;
    double latitude_deg{38.0}, longitude_deg{-72.0};
    double first_hours{24.0}, every_hours{48.0}, last_hours{240.0};
    double anomaly_pa{-1000.0}, radius_m{700000.0};
    double deepening_pa_per_day{1000.0}, mature_hours{48.0}, filling_pa_per_day{600.0};
    double steering_factor{0.9}, drift_north_m_s{1.5};
};
struct EnsembleSpread {
    double position_km{80.0};                // initial position error of each system
    double speed_m_s{1.5};                   // error of the motion (scaled by the system's steering factor, at least a quarter)
    double deepening_pa_per_day{250.0};
    double radius_fraction{0.08};
    double genesis_position_km{300.0};       // where new systems form
    double genesis_timing_hours{8.0};
    double temperature_k{0.6};               // smooth initial temperature error
    double hurricane_position_km{40.0};
    double hurricane_wind_m_s{3.0};
};
void validate_ensemble_spread(const EnsembleSpread& spread);

struct SystemErrors {
    double east_m{0.0}, north_m{0.0}, speed_east_m_s{0.0}, speed_north_m_s{0.0}, deepening_pa_per_day{0.0}, radius_factor{1.0}, timing_hours{0.0};
};
// Draw one member's errors (zero errors for an unperturbed control).
SystemErrors draw_system_errors(NormalSequence& random, const EnsembleSpread& spread, bool genesis);

// The evolving systems of one member on a grid (positions in the grid's local metres).
class SynopticEvolution {
public:
    SynopticEvolution(const std::vector<SynopticSystem>& systems, const std::vector<GenesisRegion>& genesis, const SteeringFlow& steering,
                      const TransportGrid2D& grid, double horizon_hours, NormalSequence* random, const EnsembleSpread& spread);
    void advance(double hours);
    double hours() const { return hours_; }
    std::vector<PressureCenter> centers() const;
    struct Active {
        std::string name, kind;
        double east_m, north_m, anomaly_pa, radius_m;
    };
    std::vector<Active> active() const;
private:
    struct Item {
        SynopticSystem system;
        SystemErrors errors;
        double east_m{0.0}, north_m{0.0}, age_hours{0.0};
        bool born{false};
    };
    std::vector<Item> items_;
    SteeringFlow steering_;
    TransportGrid2D grid_;
    double hours_{0.0};
    double strength(const Item& item) const;
};

// ---- Synoptic ascent, layered cloud and rain ----------------------------------------------------------------------
//
// The boundary layer cannot lift itself: the widespread cloud and rain of a cyclone come from the ascent of the warm
// conveyor belt ahead of it (quasi-geostrophic lifting), the fair weather of a high from its subsidence. The ascent
// of each system is w = w0 (|anomaly| / 15 hPa) exp(-(r/R)^2) (1 + b cos(angle to the warm sector)), positive for lows
// (strongest towards the east-northeast), negative for highs; the frictional convergence of the layer, -H div V, is
// added. Layered cloud covers cf = clamp((RH - RH_c) / (RH_full - RH_c)) (more with ascent, less with subsidence); it
// rains at R = e rho w q (RH - RH_r)/(1 - RH_r) where the air rises and is humid enough.
struct SynopticWeatherConfig {
    double low_ascent_m_s{0.05};             // w0 for a 15 hPa low
    double high_subsidence_m_s{0.015};
    double warm_sector_bias{0.6};            // b
    double warm_sector_direction_deg{60.0};  // direction of the strongest ascent from the centre (from north, clockwise)
    double convergence_depth_m{1500.0};      // H of the frictional convergence
    double cloud_rh{0.6};                    // RH_c
    double full_cloud_rh{0.95};
    double rain_rh{0.75};                    // RH_r
    double efficiency{0.6};                  // e
    double orographic_lift_factor{0.5};      // share of the upslope wind that rises over the terrain (the rest flows around)
};
void validate_synoptic_weather(const SynopticWeatherConfig& config);
std::vector<double> synoptic_ascent_m_s(const std::vector<std::array<double, 4>>& systems,   // east_m, north_m, anomaly_pa, radius_m
                                        const std::vector<double>& wind_east_m_s, const std::vector<double>& wind_north_m_s,
                                        const TransportGrid2D& grid, const SynopticWeatherConfig& config);
// Forced ascent over terrain: w = f (u dh/dx + v dh/dy) (central differences; positive where the wind blows upslope).
std::vector<double> upslope_ascent_m_s(const std::vector<double>& wind_east_m_s, const std::vector<double>& wind_north_m_s, const std::vector<double>& elevation_m,
                                       const TransportGrid2D& grid, double lift_factor);
double layered_cloud_fraction(double relative_humidity, double ascent_m_s, const SynopticWeatherConfig& config);
double layered_rain_mm_h(double relative_humidity, double ascent_m_s, double air_density_kg_m3, double specific_humidity_kg_kg, const SynopticWeatherConfig& config);

// ---- The surface ---------------------------------------------------------------------------------------------------

struct SurfaceCouplingConfig {
    double ocean_temperature_time_h{24.0};   // the air over the sea relaxes to SST - offset
    double sea_air_offset_k{0.5};
    double ocean_moisture_time_h{36.0};
    double sea_relative_humidity{0.8};
    double land_heating_k_per_h{1.6};        // diurnal heating per unit of cos(zenith) above its daily mean (clear, dry soil)
    double dry_soil_warming_k{4.0};          // the land air warms by this much per unit of soil moisture below its normal
    double cloud_shading{0.6};               // share of heating and cooling removed under full cloud
    double wet_soil_heating_reduction{0.5};  // share of heating going into evaporation over saturated soil
    double evapotranspiration_g_kg_per_h{0.25};
    double land_climate_time_h{72.0};        // relaxation of the land air to its climate (plus the soil-moisture anomaly)
    double soil_capacity_mm{100.0};
    double drainage_mm_per_day{8.0};         // deep percolation of a saturated soil (x soil moisture^3: none when dry)
    double boundary_layer_depth_m{1500.0};   // air mass for condensation and moistening
    bool ocean_fluxes{true};                 // false: the air over the sea keeps its heat and moisture (sensitivity runs)
};
void validate_surface_coupling(const SurfaceCouplingConfig& config);

struct SurfaceState {
    std::vector<unsigned char> land;
    std::vector<double> sea_surface_temperature_k;   // over land: unused
    std::vector<double> land_climate_temperature_k;  // over sea: unused
    std::vector<double> soil_moisture;                // 0-1, over sea: 1
    std::vector<double> normal_soil_moisture;         // the climatological soil moisture (initial state)
    std::vector<double> elevation_m;
    std::vector<double> cloud_fraction;               // from the latest cloud diagnosis
};

struct SurfaceStepReport {
    std::vector<double> condensation_mm;              // large-scale rain of the step
    double mean_land_heating_k{0.0};
};
// One step of the surface coupling on the coupled layer (in place): the Sun (cos zenith per cell and its daily mean:
// the land heats by day and cools by night with no net drift over a day), the sea, the land and large-scale
// condensation.
SurfaceStepReport couple_surface(CoupledWindState& state, SurfaceState& surface, const std::vector<double>& cos_zenith,
                                 const std::vector<double>& daily_mean_cos_zenith, const SurfaceCouplingConfig& config, double hours);
// Daily mean of max(0, cos zenith) at a latitude for a solar declination (degrees).
double daily_mean_cos_zenith(double latitude_deg, double declination_deg);
double solar_declination_deg(double days_since_j2000);
// Rain onto the soil: the runoff (mm) that the saturated fraction sheds (saturation excess, steeper slopes shed more)
// and the soil moisture update.
double add_rain_to_soil(double& soil_moisture, double rain_mm, double slope, const SurfaceCouplingConfig& config);

// ---- Wind ----------------------------------------------------------------------------------------------------------

struct GustConfig {
    double gust_factor{1.45};                // gust / mean wind over open terrain
    double convective_gust_m_s{12.0};        // downdraft gusts under cumulonimbus
    double terrain_reduction_per_km{0.1};    // rougher, more sheltered terrain: mean wind reduced per km of elevation
    double surface_factor_land{0.6};         // 10 m wind / wind of the coupled layer over land (surface friction)
    double surface_factor_sea{0.8};          // ... and over the sea
};
double gust_m_s(double mean_wind_m_s, bool cumulonimbus, const GustConfig& config);
// The wind at 10 m: the wind of the coupled layer slowed by surface friction (about 0.6 over land, 0.8 over the sea,
// less on high, rough terrain) plus the vortex of a tropical cyclone (whose wind is already a 10 m wind).
std::array<double, 2> surface_wind_m_s(double layer_east_m_s, double layer_north_m_s, double vortex_east_m_s, double vortex_north_m_s, bool land, double elevation_m,
                                       const GustConfig& config);
// Tropical cyclones on a coarse grid: the modified Rankine vortex V = V_max (r/R) inside the radius of maximum wind
// and V_max (R/r)^x outside (x about 0.5-0.6 for real hurricanes: tropical-storm winds reach 150-300 km), the rain
// shield and the storm surge on coastal land with widths of at least a grid cell.
struct CycloneHazardConfig {
    double decay_exponent{0.6};
    double rain_scale_m{250000.0};
    double surge_reach_m{300000.0};
};
struct CycloneHazards {
    std::vector<double> wind_m_s, rain_mm_h, surge_m;
};
CycloneHazards cyclone_hazards(const StormSystemsState& storms, const TransportGrid2D& grid, const std::vector<unsigned char>& land, const CycloneHazardConfig& config);
double cyclone_wind_m_s(double max_wind_m_s, double radius_max_wind_m, double distance_m, double decay_exponent);
// Adds the cyclonic vortex of every tropical cyclone to a wind field (m/s, in place).
void add_storm_vortices(std::vector<double>& east_m_s, std::vector<double>& north_m_s, const StormSystemsState& storms, const TransportGrid2D& grid, double decay_exponent);

// Large hail at the ground needs a low enough melting level: the updraft diagnostic of the precipitation kernel gives
// the largest stone the updraft can hold, which the deep, warm tropical columns overstate (their buoyancy is spread
// over a tall, "skinny" profile and the stones fall through a warm layer several kilometres deep). Hail is kept in full
// below full_m, removed above none_m, linear in between (large hail becomes rare where the freezing level lies above
// about 4 km). A NaN freezing level (the column never crosses 0 C) keeps the hail.
double ground_hail_cm(double hail_cm, double freezing_level_m, double full_m, double none_m);

// ---- Warnings ------------------------------------------------------------------------------------------------------

// Level 0-3 (none, yellow, orange, red): the highest level whose probability reaches min_probability.
int warning_level(const std::array<double, 3>& probabilities, double min_probability);

struct WarningRegion {
    int level{0};
    std::vector<std::size_t> cells;
    std::size_t peak_cell{0};
    double peak_value{0.0};
    double area_km2{0.0};
};
// Connected areas (4-neighbour) of cells with the same level >= 1 (a red core inside an orange area is its own area),
// each with its peak cell (the largest value); the highest level first, the largest first within a level.
std::vector<WarningRegion> warning_regions(const std::vector<int>& levels, const std::vector<double>& values, const TransportGrid2D& grid);

// ---- Verification --------------------------------------------------------------------------------------------------

struct SkillScores {
    double rmse{0.0};
    double spread{0.0};
    double climatology_rmse{0.0};
    double persistence_rmse{0.0};
    double skill_vs_climatology{0.0};        // 1 - MSE / MSE_clim
    double anomaly_correlation{0.0};
};
// Ensemble mean and spread against the truth, with the climatology and persistence references (cell weights 1).
SkillScores score_forecast(const std::vector<double>& mean, const std::vector<double>& spread, const std::vector<double>& truth,
                           const std::vector<double>& climatology, const std::vector<double>& persistence);
// Brier score of probabilities against binary outcomes, and its skill against the climatological frequency.
std::array<double, 2> brier_score(const std::vector<double>& probability, const std::vector<unsigned char>& outcome);

}  // namespace terraflux::weather
