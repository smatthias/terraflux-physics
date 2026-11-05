#include "terraflux/weather/coastal_flooding.hpp"
#include "terraflux/weather/sea_level.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace terraflux::weather;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

// A strip nx x ny of 1 km cells: the sea in the first `sea_columns` columns, land east of it at `ground`.
CoastalFloodConfig strip(std::size_t nx, std::size_t ny, std::size_t sea_columns, double ground, double sea_depth = 10.0) {
    CoastalFloodConfig c;
    c.grid.nx = nx; c.grid.ny = ny;
    c.grid.origin_east_m = 0.0; c.grid.origin_north_m = 0.0;
    c.grid.spacing_east_m = 1000.0; c.grid.spacing_north_m = 1000.0;
    c.grid.geographic_anchor_latitude_deg = 52.0; c.grid.geographic_anchor_longitude_deg = 4.0;
    const std::size_t count = nx * ny;
    c.elevation_m.assign(count, ground);
    c.sea.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) if (i % nx < sea_columns) { c.sea[i] = 1; c.elevation_m[i] = -sea_depth; }
    c.coast_type.assign(count, static_cast<unsigned char>(CoastType::rock));
    c.defence_crest_m.assign(count, -100.0);
    c.subsidence_m_yr.assign(count, 0.0);
    c.population.assign(count, 0.0);
    c.region.assign(count, -1);
    c.high_tide_m = 1.0;
    c.storm_wind_speed_m_s = 0.0;   // no setup unless a case asks for it
    c.storm_attenuation_m_per_km = 0.0;
    return c;
}

std::size_t at(const CoastalFloodConfig& c, std::size_t x, std::size_t y) { return y * c.grid.nx + x; }

// 1. The sea-level budget against closed forms.
void sea_level_budget_components() {
    std::vector<int> years;
    std::vector<double> warming, greenland;
    const double dt = 2.0;   // a step warming from the first year on
    for (int y = 1900; y <= 2400; ++y) { years.push_back(y); warming.push_back(y == 1900 ? 0.0 : dt); greenland.push_back(0.001 * (y - 1900)); }
    SeaLevelBudgetConfig c;
    c.reference_year = 1900;
    c.antarctic_base_m_yr = 0.0;
    c.land_water_m_yr = 0.0;
    const auto b = sea_level_budget(years, warming, greenland, c);
    // The first year's step warming enters as the mean of the year's ends; afterwards E = beta tau dT (1 - exp(-t/tau)).
    const std::size_t k = 300;   // 2200
    const double t = 299.5, tau = c.deep_ocean_time_scale_years;
    require_near(b.thermal_expansion_m[k], c.thermal_expansion_m_per_k_yr * tau * dt * (1.0 - std::exp(-t / tau)), "thermal expansion slows as the deep ocean warms", 0.01 * b.thermal_expansion_m[k]);
    require(b.thermal_expansion_m[500] < c.thermal_expansion_m_per_k_yr * tau * dt, "and approaches beta tau dT from below");
    // Glaciers: a finite store emptied at gamma dT (1 - G / G_max).
    require_near(b.glaciers_m[k], c.glacier_store_m * (1.0 - std::exp(-c.glacier_m_per_k_yr * dt * t / c.glacier_store_m)), "glaciers follow the store law", 0.01 * c.glacier_store_m);
    require(b.glaciers_m[500] < c.glacier_store_m, "glaciers cannot give more than their store");
    // Antarctica above its threshold: a1 (dT - T_a) per year.
    require_near(b.antarctic_m[k], c.antarctic_m_per_k_yr * (dt - c.antarctic_threshold_k) * t, "Antarctic discharge grows with the warming above its threshold", 1.0e-3);
    require_near(b.greenland_m[k], 0.3, "Greenland comes from the polar run", 1.0e-12);
    for (std::size_t j = 0; j < years.size(); ++j) {
        require_near(b.global_m[j], b.thermal_expansion_m[j] + b.glaciers_m[j] + b.antarctic_m[j] + b.greenland_m[j] + b.land_water_m[j], "the global rise is the sum of its parts", 1.0e-12);
    }
    // Counted from the reference year; fingerprints weight the ice.
    c.reference_year = 2000;
    c.greenland_fingerprint = 0.5;
    c.antarctic_fingerprint = 1.2;
    const auto r = sea_level_budget(years, warming, greenland, c);
    require(r.global_m[100] == 0.0 && r.regional_m[100] == 0.0, "zero at the reference year");
    require_near(r.regional_m[k] - r.global_m[k], -0.5 * r.greenland_m[k] + 0.2 * r.antarctic_m[k], "fingerprints: a shrinking ice sheet raises the far field and lowers the near field", 1.0e-12);
    // No warming: only the steady terms.
    SeaLevelBudgetConfig steady;
    steady.reference_year = 1900;
    const auto z = sea_level_budget(years, std::vector<double>(years.size(), 0.0), std::vector<double>(years.size(), 0.0), steady);
    require_near(z.global_m[100], 100.0 * (steady.antarctic_base_m_yr + steady.land_water_m_yr), "without warming only the steady Antarctic and land-water terms", 1.0e-9);
    require_rejected([&] { auto gap = years; gap[5] += 1; sea_level_budget(gap, warming, greenland, c); }, "years must be consecutive");
    require_rejected([&] { SeaLevelBudgetConfig bad; bad.reference_year = 1800; sea_level_budget(years, warming, greenland, bad); }, "the reference year must be in the record");
    require_rejected([&] { SeaLevelBudgetConfig bad; bad.thermal_expansion_m_per_k_yr = -1.0; validate_sea_level_budget(bad); }, "negative expansion rejected");
}

// 2. Extreme water levels and how often they come back.
void extreme_levels_and_frequency() {
    const double mu = 0.6, sigma = 0.2;
    const double z100 = gumbel_return_level(mu, sigma, 100.0);
    require_near(z100, mu - sigma * std::log(-std::log(0.99)), "the 100-year level", 1.0e-12);
    require_near(gumbel_return_period(mu, sigma, z100), 100.0, "return period inverts the return level", 1.0e-9);
    require_near(return_period_after_rise(mu, sigma, 100.0, 0.0), 100.0, "no rise, no change", 1.0e-9);
    // Each sigma of rise makes the rare flood about e times more frequent (Hunter 2012).
    const double t1 = return_period_after_rise(mu, sigma, 1000.0, sigma);
    require_near(t1 / (1000.0 / std::exp(1.0)), 1.0, "one scale of rise: e times more frequent", 0.01);
    require(return_period_after_rise(mu, sigma, 100.0, 1.0) < 1.5, "a metre of rise makes today's 100-year flood (sigma 0.2 m) roughly annual");
    require_rejected([&] { gumbel_return_level(mu, 0.0, 100.0); }, "scale must be positive");
}

// 3. Wind setup: the exact uniform-depth solution, shallow water and the rise.
void wind_setup_over_the_shelf() {
    auto c = strip(40, 3, 40, 0.0, 8.0);   // all sea, 8 m deep
    c.high_tide_m = 0.0;
    c.storm_wind_speed_m_s = 25.0;
    c.storm_wind_toward_deg = 90.0;   // blowing east
    const double tau = c.air_density_kg_m3 * c.drag_coefficient * 625.0;
    const auto eta = wind_setup_field(c, 0.0);
    for (std::size_t x = 0; x < 40; ++x) {
        require_near(eta[at(c, x, 1)], uniform_wind_setup_m(8.0, (x + 1) * 1000.0, tau), "setup matches the exact uniform-depth solution", 1.0e-9);
    }
    require(eta[at(c, 39, 1)] > 0.5, "40 km of 8 m water under a 25 m/s wind: more than half a metre");
    const auto deeper = wind_setup_field(c, 2.0);
    require(deeper[at(c, 39, 1)] < eta[at(c, 39, 1)] * 0.85, "a higher sea is deeper and sets up less");
    c.storm_wind_toward_deg = 270.0;
    const auto west = wind_setup_field(c, 0.0);
    require_near(west[at(c, 0, 1)], eta[at(c, 39, 1)], "the setup piles up downwind whichever way the wind blows", 1.0e-9);
    // Shallow water amplifies it.
    auto shallow = strip(40, 3, 40, 0.0, 3.0);
    shallow.high_tide_m = 0.0;
    shallow.storm_wind_speed_m_s = 25.0;
    shallow.storm_wind_toward_deg = 90.0;
    require(wind_setup_field(shallow, 0.0)[at(shallow, 39, 1)] > 2.0 * eta[at(c, 39, 1)], "a shallow shelf piles up far more water");
}

// 4. Flooding from the sea: connectivity, defences, attenuation and rivers.
void connectivity_defences_and_attenuation() {
    // Sea in column 0; low land 0.5 m (x 1-4), a 3 m ridge (x 5), an enclosed depression at -1 m (x 6-8), low land again.
    auto c = strip(12, 3, 1, 0.5);
    for (std::size_t y = 0; y < 3; ++y) {
        c.elevation_m[at(c, 5, y)] = 3.0;
        for (std::size_t x = 6; x <= 8; ++x) c.elevation_m[at(c, x, y)] = -1.0;
    }
    auto im = assess_coastal_impact(c, 0.0, 0.0);
    require(im.lost_km2 == 0.0, "today's intertidal land is not counted as lost");
    const auto base = coastal_baseline(c);
    require(base.permanent[at(c, 2, 1)] == 1 && base.permanent[at(c, 7, 1)] == 0, "today the tide reaches the low land, not the depression behind the ridge");
    im = assess_coastal_impact(c, 1.5, 0.0);
    require(im.lost_fraction[at(c, 7, 1)] == 0.0, "1.5 m of rise: the ridge still protects the depression (bathtub with connectivity)");
    require(im.protected_low[at(c, 7, 1)] == 0, "a natural ridge is not a defence");
    im = assess_coastal_impact(c, 2.2, 0.0);
    require(im.lost_fraction[at(c, 7, 1)] == 1.0 && im.lost_fraction[at(c, 5, 1)] == 1.0, "2.2 m: the ridge is drowned and the sea fills the depression");
    require_near(im.inundation_depth_m[at(c, 7, 1)], 1.0 + 2.2 + 1.0, "depth = high water - ground", 1.0e-12);

    // A dike on the low land keeps what is behind it dry until it is overtopped.
    auto d = strip(8, 3, 1, 0.5);
    for (std::size_t y = 0; y < 3; ++y) d.defence_crest_m[at(d, 1, y)] = 3.0;
    auto dk = assess_coastal_impact(d, 0.0, 0.0);
    require(dk.protected_low[at(d, 4, 1)] == 1 && dk.lost_fraction[at(d, 4, 1)] == 0.0, "behind the dike, below high water, kept dry");
    dk = assess_coastal_impact(d, 1.5, 0.0);
    require(dk.protected_low[at(d, 4, 1)] == 1 && dk.protected_low_km2 > 5.0, "1.5 m of rise: still protected (2.5 m < 3 m crest)");
    dk = assess_coastal_impact(d, 2.1, 0.0);
    require(dk.lost_fraction[at(d, 4, 1)] == 1.0 && dk.protected_low[at(d, 4, 1)] == 0, "2.1 m: the 3 m crest is overtopped at high water and the polder is lost");
    // Subsidence sinks the land and the dike: the same rise drowns it earlier.
    auto sinking = d;
    for (std::size_t i = 0; i < sinking.subsidence_m_yr.size(); ++i) if (!sinking.sea[i]) sinking.subsidence_m_yr[i] = 0.01;
    const auto sk = assess_coastal_impact(sinking, 1.5, 60.0);
    require(sk.lost_fraction[at(d, 4, 1)] == 1.0, "0.6 m of subsidence on top of 1.5 m of rise overtops the (sunken) dike");
    require_near(sk.relative_rise_m[at(d, 4, 1)], 2.1, "relative rise = rise + subsidence", 1.0e-12);

    // Storm floods lose height inland: 0.2 m/km over flat land at 0.5 m from a 3 m storm level reach (3 - 0.5) / 0.2 = 12.5 km.
    auto s = strip(30, 3, 1, 0.5);
    s.high_tide_m = 1.0;
    s.surge_location_m = 1.0;
    s.surge_scale_m = 0.01;
    s.design_return_period_years = 100.0;
    s.storm_attenuation_m_per_km = 0.2;
    const double level = 1.0 + gumbel_return_level(1.0, 0.01, 100.0);
    const auto st = assess_coastal_impact(s, 0.0, 0.0);
    std::size_t reach = 0;
    for (std::size_t x = 1; x < 30; ++x) if (st.storm_depth_m[at(s, x, 1)] > 0.0) reach = x;
    require(reach == static_cast<std::size_t>(std::ceil((level - 0.5) / 0.2)) - 1 || reach == static_cast<std::size_t>((level - 0.5) / 0.2), "the storm reaches inland until the attenuated level meets the ground");
    require_near(st.storm_depth_m[at(s, 1, 1)], level - 0.2 - 0.5, "one km inland the level has dropped by 0.2 m", 1.0e-9);
}

void rivers_carry_the_sea_inland() {
    // Flat land at 2 m, a channel 6 m deep along row 2 from the coast (x 1) inland.
    auto c = strip(60, 5, 1, 2.0);
    c.high_tide_m = 1.0;
    c.surge_location_m = 2.0;   // a 3 m storm level
    c.surge_scale_m = 0.01;
    c.storm_attenuation_m_per_km = 0.2;
    c.river_attenuation_m_per_km = 0.01;
    for (std::size_t x = 1; x < 60; ++x) c.elevation_m[at(c, x, 2)] = 2.0 + 0.02 * static_cast<double>(x);   // gently rising valley floor
    CoastalRiver r;
    r.name = "test river";
    for (std::size_t x = 1; x < 60; ++x) r.cells.push_back(at(c, x, 2));
    r.discharge_m3_s = 300.0;
    r.channel_depth_m = 6.0;
    r.width_m = 200.0;
    c.rivers.push_back(r);
    const auto im = assess_coastal_impact(c, 0.0, 0.0);
    auto dry = c;
    dry.rivers.clear();
    const auto without = assess_coastal_impact(dry, 0.0, 0.0);
    std::size_t overland = 0, along = 0;
    for (std::size_t x = 1; x < 60; ++x) {
        if (without.storm_depth_m[at(c, x, 3)] > 0.0) overland = x;
        if (im.storm_depth_m[at(c, x, 3)] > 0.0) along = x;
    }
    require(overland > 0 && overland <= 6, "over land the 3 m storm level runs out within (3 - 2) / 0.2 = 5 km");
    require(along > 8 * overland, "the surge pushes far further up the river and floods its banks");
    require(im.rivers.size() == 1 && im.rivers[0].surge_reach_km > im.rivers[0].tidal_reach_km && im.rivers[0].tidal_reach_km > 0.0, "tidal reach inside the surge reach");
    require(im.rivers[0].flooded_bank_km2 > 0.0, "banks flood along the channel");
    const auto higher = assess_coastal_impact(c, 0.8, 0.0);
    require(higher.rivers[0].tidal_reach_km > im.rivers[0].tidal_reach_km && higher.rivers[0].surge_reach_km >= im.rivers[0].surge_reach_km, "a higher sea reaches further up the river");
    // Salt intrusion L = D A ln(S0 / S_lim) / Q grows with the depth at the mouth.
    const double depth0 = 1.0 - (2.02 - 6.0), depth1 = depth0 + 0.8;
    require_near(higher.rivers[0].salt_intrusion_km / im.rivers[0].salt_intrusion_km, depth1 / depth0, "salt intrudes further in proportion to the deeper mouth", 1.0e-9);
    require_near(im.rivers[0].salt_intrusion_km, c.estuary_dispersion_m2_s * r.width_m * depth0 * std::log(35.0 / c.salt_limit_psu) / r.discharge_m3_s / 1000.0, "salt intrusion formula", 1.0e-9);
    require(manning_normal_depth_m(300.0, 200.0, 1.0e-4, 0.03) > manning_normal_depth_m(300.0, 200.0, 1.0e-3, 0.03), "flatter rivers flow deeper");
}

// 5. Sandy shores retreat, islands drown, regions and people add up.
void erosion_islands_and_people() {
    require_near(bruun_retreat_m(1.0, 1000.0, 2.0, 10.0), 1000.0 / 12.0, "Bruun: about 80 m of retreat per metre of rise", 1.0e-12);
    auto c = strip(10, 3, 1, 3.0);
    for (std::size_t i = 0; i < c.coast_type.size(); ++i) c.coast_type[i] = static_cast<unsigned char>(CoastType::sandy);
    const auto im = assess_coastal_impact(c, 0.6, 0.0);
    const double retreat = bruun_retreat_m(0.6, c.active_profile_width_m, c.berm_height_m, c.closure_depth_m);
    require_near(im.erosion_retreat_m[at(c, 1, 1)], retreat, "the new shoreline retreats by the Bruun rule", 1.0e-12);
    require_near(im.lost_fraction[at(c, 1, 1)], retreat * 1000.0 / 1.0e6, "the exposed edge times the retreat is lost", 1.0e-12);
    require(im.lost_fraction[at(c, 2, 1)] == 0.0, "inland cells do not erode");
    auto rock = c;
    for (auto& t : rock.coast_type) t = static_cast<unsigned char>(CoastType::rock);
    require(assess_coastal_impact(rock, 0.6, 0.0).lost_km2 == 0.0, "rocky coasts do not retreat");

    // A low island (peak 2.6 m, shore 1.5 m) in a sea with high tide 1 m; people live on it.
    auto s = strip(15, 15, 15, 0.0, 20.0);
    s.region_names = {"Isle"};
    for (std::size_t y = 4; y <= 10; ++y) for (std::size_t x = 4; x <= 10; ++x) {
        const std::size_t i = at(s, x, y);
        const double r = std::hypot(static_cast<double>(x) - 7.0, static_cast<double>(y) - 7.0);
        if (r > 3.2) continue;
        s.sea[i] = 0;
        s.elevation_m[i] = 2.6 - 1.1 * r / 3.2;
        s.population[i] = 100.0;
        s.region[i] = 0;
    }
    const auto today = assess_coastal_impact(s, 0.0, 0.0);
    require(today.islands.size() == 1 && today.islands[0].lost_fraction == 0.0 && today.islands[0].region == 0, "one island, nothing lost today");
    double previous = 0.0;
    for (double rise : {0.3, 0.6, 0.9, 1.2, 1.7}) {
        const auto im2 = assess_coastal_impact(s, rise, 0.0);
        require(im2.islands[0].lost_fraction >= previous, "an island loses ever more land as the sea rises");
        previous = im2.islands[0].lost_fraction;
        double people = 0.0;
        for (std::size_t i = 0; i < s.population.size(); ++i) people += s.population[i] * im2.lost_fraction[i];
        require_near(im2.people_on_lost_land, people, "people on lost land add up from the cells", 1.0e-9);
        require_near(im2.regions[0].people_on_lost_land, people, "and all of them live in the island's region", 1.0e-9);
        require_near(im2.regions[0].lost_km2, im2.islands[0].lost_km2, "region and island agree", 1.0e-9);
    }
    require(previous == 1.0, "1.7 m of rise drowns a 2.6 m island at a 1 m high tide");
    // The region's design flood comes more often with its mean coastal rise.
    const auto im3 = assess_coastal_impact(s, 0.5, 0.0);
    require_near(im3.regions[0].design_flood_return_years, return_period_after_rise(s.surge_location_m, s.surge_scale_m, s.design_return_period_years, 0.5), "frequency of today's design flood", 1.0e-9);
}

void contract() {
    auto c = strip(6, 3, 1, 1.0);
    require_rejected([&] { auto bad = c; bad.population.pop_back(); validate_coastal_config(bad); }, "arrays must match the grid");
    require_rejected([&] { auto bad = c; bad.coast_type[3] = 7; validate_coastal_config(bad); }, "unknown coast type");
    require_rejected([&] { auto bad = c; bad.population[0] = 5.0; validate_coastal_config(bad); }, "nobody lives at sea");
    require_rejected([&] { auto bad = c; bad.sea.assign(18, 0); validate_coastal_config(bad); }, "a coast needs a sea");
    require_rejected([&] { auto bad = c; bad.region[2] = 3; validate_coastal_config(bad); }, "region index out of range");
    require_rejected([&] { auto bad = c; bad.surge_scale_m = 0.0; validate_coastal_config(bad); }, "surge scale must be positive");
    CoastalRiver jump;
    jump.name = "jumping";
    jump.cells = {at(c, 1, 1), at(c, 3, 1)};
    require_rejected([&] { auto bad = c; bad.rivers.push_back(jump); validate_coastal_config(bad); }, "rivers must be 4-connected");
    CoastalRiver inland;
    inland.name = "inland";
    inland.cells = {at(c, 3, 1), at(c, 4, 1)};
    require_rejected([&] { auto bad = c; bad.rivers.push_back(inland); validate_coastal_config(bad); }, "rivers must start next to the sea");
    require_rejected([&] { assess_coastal_impact(c, std::numeric_limits<double>::quiet_NaN(), 0.0); }, "rise must be finite");
}

}  // namespace

int main() {
    try {
        sea_level_budget_components();
        extreme_levels_and_frequency();
        wind_setup_over_the_shelf();
        connectivity_defences_and_attenuation();
        rivers_carry_the_sea_inland();
        erosion_islands_and_people();
        contract();
    } catch (const std::exception& error) {
        std::cerr << "coastal flooding test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "coastal flooding cases passed\n";
    return 0;
}
