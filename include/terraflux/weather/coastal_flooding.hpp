#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Coastal flooding v1: what a higher sea does to a coast. For a relative sea-level rise (regional rise plus the
// land's own sinking) it maps
//   permanent inundation   land below the daily high water (high tide + rise) that the sea reaches: lost land.
//                          The bathtub with hydraulic connectivity (Kulp & Strauss 2019): low land behind higher
//                          ground or behind a flood defence stays dry, and is reported as depending on it.
//   storm flooding         the design storm (the T-year surge above high tide, Gumbel-distributed annual maxima)
//                          plus the wind setup a steady onshore storm piles up over the shallow shelf and in
//                          estuaries; the water level falls inland by an attenuation per km (Vafeidis et al. 2019)
//                          and much less along river channels, where the surge pushes up the river.
//   wind setup             d(eta)/ds = tau / (rho g (h + eta)) along the wind over the sea cells (no bottom
//                          friction: an upper estimate); shallow shelves and funnels raise it, a deeper sea lowers it.
//   rivers                 tidal reach and storm push-back: how far up the channel the high water / the storm water
//                          level stands above the river's own level (bed + Manning normal depth); salt intrusion
//                          L = D A ln(S0 / S_limit) / Q of a steady advection-dispersion balance whose cross-section
//                          A = width x (water level - bed) deepens with the rise.
//   erosion                sandy shores retreat by the Bruun rule R = S L / (B + h*); the exposed edge length of each
//                          new shoreline cell times R is lost.
//   frequency              today's T-year storm flood returns every T' = gumbel_return_period(mu + S, sigma, z_T).
//   islands and regions    land lost, people living on lost or flooded land, per named region and per island.
// Subsidence lowers the ground and the defences by rate x years. Everything is reported against today's state
// (rise 0), so intertidal land flooded today does not count as lost.

enum class CoastType : unsigned char { rock = 0, sandy = 1, muddy = 2 };   // only sandy shores follow the Bruun rule

struct CoastalRiver {
    std::string name;
    std::vector<std::size_t> cells;     // channel cells from the mouth (next to the sea) upstream, each a neighbour of the last
    double discharge_m3_s{1000.0};
    double channel_depth_m{5.0};        // bed below the banks
    double width_m{500.0};
    double manning_n{0.03};
};

struct CoastalFloodConfig {
    TransportGrid2D grid;
    std::vector<double> elevation_m;          // ground (land) or sea bed (sea), relative to today's mean sea level
    std::vector<unsigned char> sea;           // today's open sea (1); land and inland lakes 0
    std::vector<unsigned char> coast_type;    // CoastType per cell (land)
    std::vector<double> defence_crest_m;      // crest of a flood defence on the cell; <= ground means none
    std::vector<double> subsidence_m_yr;      // land motion, positive = sinking
    std::vector<double> population;           // people per cell
    std::vector<int> region;                  // index into region_names, or -1
    std::vector<std::string> region_names;
    std::vector<CoastalRiver> rivers;            // 4-connected channel paths
    // Today's water levels, m above mean sea level.
    double high_tide_m{1.0};                  // mean higher high water
    double surge_location_m{0.8};             // Gumbel location and scale of the annual maximum surge above high tide (offshore)
    double surge_scale_m{0.2};
    double design_return_period_years{100.0};
    // The design storm's steady onshore wind.
    double storm_wind_speed_m_s{25.0};
    double storm_wind_toward_deg{0.0};        // direction it blows toward, clockwise from north
    double air_density_kg_m3{1.22};
    double drag_coefficient{2.5e-3};
    double water_density_kg_m3{1025.0};
    double gravity_m_s2{9.81};
    double minimum_setup_depth_m{2.0};        // depths below this count as this (breaking waves, drying)
    // Water-level attenuation inland, m per km.
    double storm_attenuation_m_per_km{0.2};
    double tidal_attenuation_m_per_km{0.0};
    double river_attenuation_m_per_km{0.01};
    // Bruun rule for sandy shores.
    double active_profile_width_m{1000.0};
    double berm_height_m{2.0};
    double closure_depth_m{10.0};
    // Salt intrusion into the rivers.
    double ocean_salinity_psu{35.0};
    double salt_limit_psu{1.0};
    double estuary_dispersion_m2_s{300.0};
    double island_max_area_km2{1000.0};       // land masses up to this size count as islands
};

// Throws std::invalid_argument for inconsistent arrays, unknown coast types, rivers that do not start next to the
// sea or jump between cells, and out-of-range parameters.
void validate_coastal_config(const CoastalFloodConfig& config);

struct CoastalRegionImpact {
    std::string name;
    double land_km2{0.0};
    double lost_km2{0.0};                     // new permanent inundation + erosion
    double inundated_km2{0.0};
    double eroded_km2{0.0};
    double storm_flooded_km2{0.0};            // land under the design storm (today's included)
    double newly_storm_flooded_km2{0.0};      // beyond today's design-storm extent
    double protected_low_km2{0.0};            // below the high water, kept dry by defences
    double population{0.0};
    double people_on_lost_land{0.0};
    double people_storm_flooded{0.0};
    double people_protected_low{0.0};
    double mean_relative_rise_m{0.0};         // at the region's coast
    double design_flood_return_years{0.0};    // how often today's design flood now comes
    double max_storm_depth_m{0.0};
};

struct CoastalRiverImpact {
    std::string name;
    double length_km{0.0};
    double tidal_reach_km{0.0};               // high water above the river's own level
    double surge_reach_km{0.0};               // design-storm level above the river's own level: the push-back
    double salt_intrusion_km{0.0};
    double mouth_storm_level_m{0.0};
    double flooded_bank_km2{0.0};             // land along the channel under the storm
};

struct CoastalIsland {
    int id{0};
    int region{-1};
    double land_km2{0.0};
    double lost_km2{0.0};
    double lost_fraction{0.0};
    double storm_flooded_fraction{0.0};
    double population{0.0};
    double highest_m{0.0};
    std::size_t top_cell{0};
};

struct CoastalImpact {
    double regional_rise_m{0.0};
    double years_since_reference{0.0};
    // Per cell.
    std::vector<double> relative_rise_m;      // regional rise + subsidence x years (land)
    std::vector<double> setup_m;              // design-storm wind setup (sea)
    std::vector<double> storm_level_m;        // design-storm water level (sea and flooded land; 0 elsewhere)
    std::vector<double> storm_depth_m;        // design-storm flood depth on land
    std::vector<unsigned char> storm_today;   // land flooded by today's design storm
    std::vector<double> inundation_depth_m;   // new permanent inundation: depth at high water
    std::vector<double> erosion_retreat_m;    // new sandy shoreline cells
    std::vector<double> lost_fraction;        // share of each land cell lost (1 inundated, partial eroded)
    std::vector<unsigned char> protected_low; // below the high water, dry thanks to a defence
    std::vector<unsigned char> river_salt;    // channel cells with salt above the limit
    std::vector<double> design_flood_return_years;   // coastal land cells: how often today's design flood comes
    // Totals.
    double land_km2{0.0}, lost_km2{0.0}, inundated_km2{0.0}, eroded_km2{0.0}, storm_flooded_km2{0.0}, newly_storm_flooded_km2{0.0}, protected_low_km2{0.0};
    double population{0.0}, people_on_lost_land{0.0}, people_storm_flooded{0.0}, people_protected_low{0.0};
    double max_setup_m{0.0}, mean_coastal_setup_m{0.0};
    std::vector<CoastalRegionImpact> regions;
    std::vector<CoastalRiverImpact> rivers;
    std::vector<CoastalIsland> islands;
};

// Today's state (rise 0): land below high water reached by the sea (intertidal), and the design-storm extent.
struct CoastalBaseline {
    std::vector<unsigned char> permanent;
    std::vector<unsigned char> storm;
};
CoastalBaseline coastal_baseline(const CoastalFloodConfig& config);

// Impacts of a regional sea-level rise `regional_rise_m` reached `years_since_reference` years after today
// (the baseline is computed when not given).
CoastalImpact assess_coastal_impact(const CoastalFloodConfig& config, double regional_rise_m, double years_since_reference);
CoastalImpact assess_coastal_impact(const CoastalFloodConfig& config, double regional_rise_m, double years_since_reference, const CoastalBaseline& baseline);

// Helpers exposed for tests.
// Wind setup of the design storm on every sea cell for still water at high tide + rise (0 on land).
std::vector<double> wind_setup_field(const CoastalFloodConfig& config, double still_water_rise_m);
// Water level reached by flooding from the sea (4-neighbour connectivity): `sea_level` on today's sea cells, then
// into every land cell whose ground (channel cells: their bed) and defence crest lie below the incoming level,
// which loses `attenuation` m per km over land and `river_attenuation` from channel cell to channel cell. An
// empty `crest` means no defences. Cells the water does not reach hold -infinity.
std::vector<double> flood_levels(const CoastalFloodConfig& config, const std::vector<double>& sea_level, const std::vector<double>& ground,
                                 const std::vector<double>& crest, double attenuation_m_per_km, double river_attenuation_m_per_km);
// Manning normal depth of a wide rectangular channel.
double manning_normal_depth_m(double discharge_m3_s, double width_m, double slope, double manning_n);

}  // namespace terraflux::weather
