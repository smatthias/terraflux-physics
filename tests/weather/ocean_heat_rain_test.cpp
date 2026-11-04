// Rain over the sea and the coast, evaporation and the air-sea heat exchange, and the layered ocean
// temperature column (weather.ocean_heat_rain).
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/ocean_circulation.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
    }
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, message);
}

// A closed square basin with a one-cell land rim.
OceanConfig basin(std::size_t n, double spacing, const std::string& coriolis = "f_plane") {
    OceanConfig c;
    c.grid.nx = n;
    c.grid.ny = n;
    c.grid.origin_east_m = 0.0;
    c.grid.origin_north_m = 0.0;
    c.grid.spacing_east_m = spacing;
    c.grid.spacing_north_m = spacing;
    c.grid.geographic_anchor_longitude_deg = -50.0;
    c.grid.geographic_anchor_latitude_deg = 35.0;
    c.timestep_seconds = 1800.0;
    c.ocean.assign(n * n, 1);
    for (std::size_t i = 0; i < n * n; ++i) {
        const std::size_t x = i % n, y = i / n;
        if (x == 0 || y == 0 || x + 1 == n || y + 1 == n) c.ocean[i] = 0;
    }
    c.physics.coriolis_mode = coriolis;
    return c;
}

OceanForcing calm(const OceanConfig& c) {
    OceanForcing f;
    const std::size_t count = c.grid.nx * c.grid.ny;
    f.wind_stress_east_pa.assign(count, 0.0);
    f.wind_stress_north_pa.assign(count, 0.0);
    f.net_precipitation_m_s.assign(count, 0.0);
    f.target_temperature_k.assign(count, c.initial_temperature_k);
    return f;
}

void add_rain(OceanForcing& f, const OceanConfig& c, double temperature_k) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    f.rain_m_s.assign(count, 0.0);
    f.rain_temperature_k.assign(count, temperature_k);
}

void add_air(OceanForcing& f, const OceanConfig& c, double air_k, double relative_humidity, double wind_m_s, double cloud, double shortwave_w_m2) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    const double pressure = 101325.0;
    f.air_temperature_k.assign(count, air_k);
    f.air_specific_humidity_kg_kg.assign(count, relative_humidity * saturation_specific_humidity_kg_kg(air_k, pressure));
    f.surface_pressure_pa.assign(count, pressure);
    f.wind_speed_m_s.assign(count, wind_m_s);
    f.cloud_fraction.assign(count, cloud);
    f.shortwave_down_w_m2.assign(count, shortwave_w_m2);
}

OceanState run(OceanState s, const OceanForcing& f, const OceanConfig& c, double seconds) {
    const auto steps = static_cast<std::size_t>(std::llround(seconds / c.timestep_seconds));
    for (std::size_t k = 0; k < steps; ++k) s = advance_ocean(s, f, c);
    return s;
}

double water_area(const OceanConfig& c) {
    double n = 0.0;
    for (unsigned char v : c.ocean) n += v;
    return n * c.grid.spacing_east_m * c.grid.spacing_north_m;
}

// Every step closes volume, salt and heat.
void require_budgets(const OceanState& s, const std::string& label) {
    require(std::abs(s.volume_balance_error_m3) <= 1.0e-9 * s.volume_m3, label + ": volume budget");
    require(std::abs(s.salt_balance_error_kg) <= 1.0e-9 * s.salt_kg, label + ": salt budget");
    require(std::abs(s.heat_balance_error_j) <= 1.0e-11 * s.volume_m3 * 1025.0 * 3990.0 * 300.0, label + ": heat budget");
}

// 1. Bulk formulae against hand calculations.
void bulk_formulae_match_hand_calculations() {
    OceanAirSeaConfig a;
    const double p = 101325.0, ts = 300.0, ta = 298.0, u = 8.0;
    const double qa = 0.8 * saturation_specific_humidity_kg_kg(ta, p);
    const auto f = air_sea_fluxes(ts, ta, qa, p, u, 0.0, 0.0, a);
    const double qs = 0.98 * saturation_specific_humidity_kg_kg(ts, p);
    const double evaporation = 1.22 * 1.2e-3 * u * (qs - qa);
    require_near(f.evaporation_m_s, evaporation / 1000.0, "Dalton evaporation", 1.0e-15);
    require_near(f.latent_w_m2, -(2.501e6 - 2370.0 * (ts - 273.15)) * evaporation, "latent heat = L_v E", 1.0e-9);
    require_near(f.sensible_w_m2, 1.22 * 1004.0 * 1.1e-3 * u * (ta - ts), "sensible heat", 1.0e-9);
    require(f.evaporation_m_s * 86400.0 * 1000.0 > 3.0 && f.evaporation_m_s * 86400.0 * 1000.0 < 8.0, "tropical evaporation of a few mm per day");
    require(f.latent_w_m2 < -100.0 && f.latent_w_m2 > -250.0, "latent cooling of order 100-250 W/m2");
    require_near(f.net_w_m2, f.shortwave_w_m2 + f.longwave_w_m2 + f.sensible_w_m2 + f.latent_w_m2, "net is the sum", 1.0e-12);
    // Berliand longwave with equal air and sea temperatures, e_a = 10 hPa, clear and overcast.
    const double q10 = specific_humidity_kg_kg(1000.0, p);
    const auto clear = air_sea_fluxes(288.15, 288.15, q10, p, 5.0, 0.0, 0.0, a);
    const double expected = -0.97 * 5.670374419e-8 * std::pow(288.15, 4) * (0.39 - 0.05 * std::sqrt(10.0));
    require_near(clear.longwave_w_m2, expected, "Berliand clear-sky longwave", 1.0e-9);
    require(clear.longwave_w_m2 < -60.0 && clear.longwave_w_m2 > -110.0, "net longwave loss of order 90 W/m2");
    const auto overcast = air_sea_fluxes(288.15, 288.15, q10, p, 5.0, 1.0, 0.0, a);
    require_near(overcast.longwave_w_m2, expected * (1.0 - 0.6), "clouds return longwave", 1.0e-9);
    // Gustiness floor, condensation over a cold sea, and the albedo.
    require_near(air_sea_fluxes(ts, ta, qa, p, 0.0, 0.0, 0.0, a).latent_w_m2, air_sea_fluxes(ts, ta, qa, p, 1.0, 0.0, 0.0, a).latent_w_m2, "calm uses the minimum wind", 1.0e-12);
    const auto fog = air_sea_fluxes(280.0, 290.0, 0.95 * saturation_specific_humidity_kg_kg(290.0, p), p, 6.0, 0.0, 0.0, a);
    require(fog.evaporation_m_s < 0.0 && fog.latent_w_m2 > 0.0 && fog.sensible_w_m2 > 0.0, "warm moist air over a cold sea condenses and heats it");
    require_near(air_sea_fluxes(ts, ta, qa, p, u, 0.0, 500.0, a).shortwave_w_m2, 470.0, "albedo 0.06", 1.0e-9);
}

// 2. Sun position and surface shortwave.
void solar_geometry_and_shortwave() {
    require_near(solar_cosine_zenith(0.0, 0.0, 80.0, 12.0), 1.0, "equinox noon at the equator: sun overhead", 1.0e-3);
    require(solar_cosine_zenith(0.0, 0.0, 80.0, 0.0) < -0.99, "midnight");
    require_near(solar_cosine_zenith(45.0, 0.0, 172.0, 12.0), std::cos((45.0 - 23.44) * kPi / 180.0), "solstice noon at 45N", 1.0e-4);
    require_near(solar_cosine_zenith(0.0, 90.0, 80.0, 6.0), solar_cosine_zenith(0.0, 0.0, 80.0, 12.0), "local noon moves with the longitude", 1.0e-12);
    require(solar_cosine_zenith(45.0, 0.0, 355.0, 12.0) < solar_cosine_zenith(45.0, 0.0, 172.0, 12.0), "winter sun is lower");
    require_near(surface_shortwave_w_m2(1.0, 2000.0, 0.0), 1361.0 / (3.7 * 20.0e-3 + 1.085 + 0.10), "Zillman clear sky", 1.0e-9);
    require_near(surface_shortwave_w_m2(1.0, 2000.0, 1.0), 0.38 * surface_shortwave_w_m2(1.0, 2000.0, 0.0), "Reed cloud reduction", 1.0e-9);
    require(surface_shortwave_w_m2(-0.2, 2000.0, 0.0) == 0.0, "no sun at night");
    double mean = 0.0;
    for (int k = 0; k < 1440; ++k) mean += surface_shortwave_w_m2(solar_cosine_zenith(0.0, 0.0, 80.0, (k + 0.5) / 60.0), 2000.0, 0.0) / 1440.0;
    require(mean > 250.0 && mean < 350.0, "daily-mean clear-sky shortwave at the equinoctial equator");
}

// 3. Layer geometry and shortwave penetration.
void layer_geometry_and_penetration() {
    auto c = basin(6, 50000.0);
    c.initial_thickness_m = 600.0;
    c.thermal.count = 12;
    c.thermal.surface_layer_m = 5.0;
    const auto sigma = thermal_layer_fractions(c);
    require(sigma.size() == 12, "twelve layers");
    require_near(std::accumulate(sigma.begin(), sigma.end(), 0.0), 1.0, "fractions sum to one", 1.0e-12);
    require_near(sigma[0] * 600.0, 5.0, "top layer thickness at the initial thickness", 1.0e-9);
    for (std::size_t k = 1; k < sigma.size(); ++k) {
        require(sigma[k] > sigma[k - 1], "layers thicken with depth");
        if (k >= 2) require_near(sigma[k] / sigma[k - 1], sigma[1] / sigma[0], "geometric stretching", 1.0e-9);
    }
    c.thermal.surface_layer_m = 100.0;   // 12 x 100 m > 600 m: uniform layers
    for (double v : thermal_layer_fractions(c)) require_near(v, 1.0 / 12.0, "uniform when the top layer would be too thick", 1.0e-15);
    OceanThermalLayersConfig t;
    require_near(shortwave_transmission(0.0, t), 1.0, "all light at the surface", 0.0);
    require_near(shortwave_transmission(10.0, t), 0.58 * std::exp(-10.0 / 0.35) + 0.42 * std::exp(-10.0 / 23.0), "two-band transmission", 1.0e-15);
    require(shortwave_transmission(1.0, t) < 0.5 && shortwave_transmission(100.0, t) < 0.02, "the red band is gone in a metre, little light below 100 m");
}

// 4. The layered column starts with a mixed layer over an exponential thermocline.
void layered_column_initialises_a_thermocline() {
    auto c = basin(6, 50000.0);
    c.initial_thickness_m = 500.0;
    c.initial_temperature_k = 298.15;
    c.thermal.count = 10;
    c.thermal.initial_mixed_layer_depth_m = 40.0;
    const auto s = initialize_ocean(c);
    const auto sigma = thermal_layer_fractions(c);
    const std::size_t i = 2 * 6 + 2;
    double bulk = 0.0;
    for (std::size_t k = 0; k < 10; ++k) bulk += sigma[k] * s.layer_temperature_k[i * 10 + k];
    require_near(s.temperature_k[i], bulk, "bulk temperature is the column mean", 1.0e-12);
    require_near(s.sst_k[i], 298.15, "surface at the initial temperature", 1.0e-12);
    for (std::size_t k = 1; k < 10; ++k) require(s.layer_temperature_k[i * 10 + k] <= s.layer_temperature_k[i * 10 + k - 1], "temperature falls with depth");
    require(s.layer_temperature_k[i * 10 + 9] < 280.0, "the bottom layer approaches the deep temperature");
    require(s.mixed_layer_depth_m[i] > 25.0 && s.mixed_layer_depth_m[i] < 70.0, "diagnosed mixed layer near the prescribed 40 m");
    require(s.temperature_k[i] < c.initial_temperature_k, "the layer mean is colder than the surface");
    // Prescribed initial surface temperatures per cell.
    c.initial_surface_temperature_k.assign(36, 290.0);
    c.initial_surface_temperature_k[i] = 300.0;
    const auto sp = initialize_ocean(c);
    require_near(sp.sst_k[i], 300.0, "per-cell initial surface temperature", 1.0e-12);
    require_near(sp.sst_k[i + 1], 290.0, "per-cell initial surface temperature elsewhere", 1.0e-12);
    // The slab reports its bulk temperature as SST and its thickness as the mixed layer.
    auto slab = basin(6, 50000.0);
    const auto ss = initialize_ocean(slab);
    require(ss.layer_temperature_k.empty() && ss.sst_k[i] == ss.temperature_k[i] && ss.mixed_layer_depth_m[i] == ss.thickness_m[i], "slab diagnostics");
}

// 5. Rain on the open sea: exact volume, salt conservation, a fresh and cool rain lens at the surface.
void rain_on_the_sea_freshens_and_cools() {
    const std::size_t n = 12;
    auto c = basin(n, 30000.0);
    c.initial_temperature_k = 301.15;
    c.thermal.count = 10;
    c.physics.temperature_relaxation_s = 1.0e12;   // isolate the rain
    auto f = calm(c);
    add_rain(f, c, 295.15);
    const std::size_t centre = (n / 2) * n + n / 2;
    const double rate = 30.0 / 3.6e6;              // 30 mm/h
    double rained_area = 0.0;
    for (std::size_t i = 0; i < n * n; ++i) {
        const double dxc = static_cast<double>(i % n) - static_cast<double>(n / 2), dyc = static_cast<double>(i / n) - static_cast<double>(n / 2);
        if (c.ocean[i] && dxc * dxc + dyc * dyc <= 2.0) { f.rain_m_s[i] = rate; rained_area += 30000.0 * 30000.0; }
    }
    auto s0 = initialize_ocean(c);
    const double v0 = s0.volume_m3, salt0 = s0.salt_kg, sst0 = s0.sst_k[centre];
    auto s = s0;
    const double hours = 6.0;
    for (int k = 0; k < static_cast<int>(hours * 2); ++k) { s = advance_ocean(s, f, c); require_budgets(s, "rain"); }
    const double rained = rate * rained_area * hours * 3600.0;
    require_near(s.cumulative_rain_sea_m3, rained, "rain on the sea is counted", 1.0e-6 * rained);
    require_near(s.volume_m3, v0 + rained, "the sea gains the rain", 1.0e-12 * v0);
    require_near(s.salt_kg / salt0, 1.0, "rain brings no salt", 1.0e-12);
    require(s.rain_coast_m3_s == 0.0 && s.rain_inland_m3_s == 0.0 && s.coastal_runoff_m3_s == 0.0, "no land was rained on");
    require(s.surface_salinity_psu[centre] < s.salinity_psu[centre] - 0.5, "a fresh lens sits on the surface under the rain");
    require(s.layer_salinity_psu[centre * 10] < s.layer_salinity_psu[centre * 10 + 3] - 0.5, "the rain stays in the top of the column");
    require(s.sst_k[centre] < sst0 - 0.1, "cold rain cools the surface");
    require(s.sst_k[centre] < s.sst_k[2 * n + 2], "the rained-on surface is cooler than the dry sea");
    require(s.rain_heat_w_m2[centre] < -100.0, "heavy cold rain is a strong surface cooling");
    const auto d = diagnose_ocean(s, f, c);
    require(!d.lenses.empty() && d.lenses.front().kind == "rain", "a rain lens is detected");
    require(d.lenses.front().freshest_cell == centre || std::abs(s.surface_salinity_psu[d.lenses.front().freshest_cell] - s.surface_salinity_psu[centre]) < 1.0e-9, "the lens is freshest under the rain");
    // The fresh lens is buoyant although it is colder: the column under the rain is statically stable and
    // the surface layer is lighter than the water below it.
    for (std::size_t k = 1; k < 10; ++k) {
        const double upper = seawater_density_kg_m3(s.layer_temperature_k[centre * 10 + k - 1], s.layer_salinity_psu[centre * 10 + k - 1], c.physics);
        const double lower = seawater_density_kg_m3(s.layer_temperature_k[centre * 10 + k], s.layer_salinity_psu[centre * 10 + k], c.physics);
        require(upper <= lower + 1.0e-9, "the rain lens is stable");
    }
    require(s.mixed_layer_depth_m[centre] < 0.5 * s0.mixed_layer_depth_m[centre], "the lens is a shallow mixed layer of its own");
    // A steady wind has to pay for the lens' buoyancy: mixing erodes it, spreading the fresh water down.
    auto dry = f;
    std::fill(dry.rain_m_s.begin(), dry.rain_m_s.end(), 0.0);
    auto fw = dry;
    std::fill(fw.wind_stress_east_pa.begin(), fw.wind_stress_east_pa.end(), 0.2);
    auto cw = c;
    cw.physics.coriolis_mode = "none";
    cw.physics.linear_friction_s = 1.0e-4;
    auto calm_after = run(s, dry, cw, 86400.0);
    auto windy_after = run(s, fw, cw, 86400.0);
    require(windy_after.salinity_psu[centre] - windy_after.surface_salinity_psu[centre] < 0.7 * (calm_after.salinity_psu[centre] - calm_after.surface_salinity_psu[centre]), "wind mixing erodes the rain lens");
    require(windy_after.mixed_layer_depth_m[centre] > calm_after.mixed_layer_depth_m[centre], "and deepens the mixed layer again");
    // The same rain on the slab model dilutes the whole layer instead: a much smaller surface signal.
    auto cs = c;
    cs.thermal.count = 0;
    auto slab = run(initialize_ocean(cs), f, cs, hours * 3600.0);
    require_budgets(slab, "rain on the slab");
    require(initialize_ocean(cs).sst_k[centre] - slab.sst_k[centre] < 0.2 * (sst0 - s.sst_k[centre]), "the layered column concentrates the rain cooling near the surface");
}

// 6. Rain on coastal land drains to the nearest sea cell through the coastal store.
void coastal_rain_drains_to_the_sea() {
    const std::size_t n = 12;
    auto c = basin(n, 30000.0);
    for (std::size_t i = 0; i < n * n; ++i) if (i % n <= 3) c.ocean[i] = 0;   // a coastal plain on the west
    c.thermal.count = 8;          // the runoff stays near the surface, as a coastal lens
    c.coastal.band_m = 45000.0;   // 1.5 cells: only the first land column drains directly
    c.coastal.runoff_coefficient = 0.7;
    c.coastal.runoff_time_scale_s = 12.0 * 3600.0;
    auto f = calm(c);
    add_rain(f, c, 291.15);
    const auto receivers = coastal_receivers(c);
    std::size_t coastal = 0, inland = 0;
    for (std::size_t i = 0; i < n * n; ++i) {
        if (c.ocean[i]) { require(receivers[i] == n * n, "sea cells have no receiver"); continue; }
        const std::size_t x = i % n, y = i / n;
        if (receivers[i] != n * n) require(c.ocean[receivers[i]] && std::hypot(static_cast<double>(receivers[i] % n) - static_cast<double>(x), static_cast<double>(receivers[i] / n) - static_cast<double>(y)) <= 1.5, "receivers are sea cells within the band");
        if (x > 3 || y == 0 || y + 1 == n) continue;   // it rains on the western plain only
        if (x == 3) { require(receivers[i] == i + 1, "coastal land drains to the adjacent sea cell"); ++coastal; }
        else { require(receivers[i] == n * n, "land two cells from the sea is beyond the band"); ++inland; }
        f.rain_m_s[i] = 20.0 / 3.6e6;
    }
    require(coastal == n - 2 && inland == 3 * (n - 2), "a coastal strip and an inland hinterland");
    auto s = initialize_ocean(c);
    const double v0 = s.volume_m3, salt0 = s.salt_kg, area = 30000.0 * 30000.0;
    double runoff_last = 0.0;
    for (int k = 0; k < 24; ++k) { s = advance_ocean(s, f, c); require_budgets(s, "coastal rain"); require(std::abs(s.coastal_balance_error_m3) <= 1.0e-9 * std::max(1.0, s.cumulative_rain_coast_m3), "coastal store budget"); runoff_last = s.coastal_runoff_m3_s; }
    const double coast_volume = static_cast<double>(coastal) * area * 20.0 / 3.6e6 * 12.0 * 3600.0;
    require_near(s.cumulative_rain_coast_m3, coast_volume, "rain on the coastal band", 1.0e-9 * coast_volume);
    require_near(s.cumulative_rain_inland_m3, static_cast<double>(inland) * area * 20.0 / 3.6e6 * 12.0 * 3600.0, "inland rain is reported, not routed", 1.0e-3);
    require(s.cumulative_rain_sea_m3 == 0.0, "no rain on the sea");
    require_near(s.cumulative_coastal_loss_m3, 0.3 * coast_volume, "the non-runoff share leaves as a land loss", 1.0e-9 * coast_volume);
    require_near(s.cumulative_coastal_runoff_m3 + s.cumulative_coastal_loss_m3 + s.coastal_storage_m3, s.cumulative_rain_coast_m3, "rain on the coast = runoff + loss + storage", 1.0e-9 * coast_volume);
    require_near(s.volume_m3, v0 + s.cumulative_coastal_runoff_m3, "the sea gains exactly the coastal runoff", 1.0e-12 * v0);
    require_near(s.salt_kg / salt0, 1.0, "runoff brings no salt", 1.0e-12);
    require(s.coastal_inflow_m3_s[(n / 2) * n + 4] > 0.0 && s.coastal_inflow_m3_s[(n / 2) * n + 6] == 0.0, "runoff enters the nearshore cell only");
    require(s.surface_salinity_psu[(n / 2) * n + 4] < s.surface_salinity_psu[(n / 2) * n + 8], "the nearshore sea freshens");
    const auto d = diagnose_ocean(s, f, c);
    require(!d.lenses.empty() && d.lenses.front().kind == "coastal_runoff", "the nearshore lens is attributed to coastal runoff");
    // After the rain the store empties exponentially on the runoff time scale.
    add_rain(f, c, 291.15);
    s = advance_ocean(s, f, c);
    const double r1 = s.coastal_runoff_m3_s;
    s = advance_ocean(s, f, c);
    require_near(s.coastal_runoff_m3_s / r1, std::exp(-1800.0 / (12.0 * 3600.0)), "linear-reservoir recession", 1.0e-12);
    require(r1 < runoff_last, "the runoff recedes once the rain stops");
    require(s.cumulative_coastal_runoff_m3 + s.cumulative_coastal_loss_m3 + s.coastal_storage_m3 - s.cumulative_rain_coast_m3 < 1.0e-6 * coast_volume, "the land budget still closes");
}

// 7. Evaporation under dry wind: the sea loses fresh water and heat, the salt stays behind.
void evaporation_concentrates_and_cools() {
    const std::size_t n = 10;
    auto c = basin(n, 40000.0);
    c.initial_temperature_k = 300.15;
    auto f = calm(c);
    add_air(f, c, 299.15, 0.5, 10.0, 0.0, 0.0);
    auto s0 = initialize_ocean(c);
    auto s = s0;
    for (int k = 0; k < 48 * 10; ++k) { s = advance_ocean(s, f, c); require_budgets(s, "evaporation"); }
    const std::size_t i = (n / 2) * n + n / 2;
    require(s.evaporation_m_s[i] * 86400.0 * 1000.0 > 5.0, "dry wind over warm water evaporates several mm per day");
    require_near(s.latent_w_m2[i], -(2.501e6 - 2370.0 * (s.sst_k[i] - 273.15)) * 1000.0 * s.evaporation_m_s[i], "latent heat and evaporation agree at the surface temperature", 1.0e-6);
    require_near(s.volume_m3, s0.volume_m3 - s.cumulative_evaporation_m3, "the sea loses the evaporated volume", 1.0e-12 * s0.volume_m3);
    require_near(s.salt_kg / s0.salt_kg, 1.0, "evaporation leaves the salt", 1.0e-12);
    double hs = 0.0, h = 0.0;
    for (std::size_t k = 0; k < n * n; ++k) if (c.ocean[k]) { hs += s.thickness_m[k] * s.salinity_psu[k]; h += s.thickness_m[k]; }
    require_near(hs / h, c.initial_salinity_psu * s0.volume_m3 / (s0.volume_m3 - s.cumulative_evaporation_m3), "mean salinity follows the concentration law", 1.0e-9);
    require(s.surface_salinity_psu[i] > s.salinity_psu[i], "the evaporating surface is saltier than the layer");
    require(s.freshwater_m[i] < 0.0, "a negative freshwater content marks the salty surface");
    require(s.cumulative_surface_heat_j < 0.0, "the sea has lost heat to the air");
    require(s.temperature_k[i] < c.initial_temperature_k - 0.05, "latent and sensible losses cool the sea");
    require(s.net_heat_w_m2[i] < -150.0 && s.surface_heat_w < 0.0, "strong net heat loss");
    // Heat content change = surface heat + heat carried away by the evaporated water (at the surface temperature).
    require(s.heat_content_j < s0.heat_content_j, "heat content falls");
    // More humid air evaporates less.
    auto fh = f;
    add_air(fh, c, 299.15, 0.9, 10.0, 0.0, 0.0);
    auto sh = run(s0, fh, c, 86400.0);
    auto sd = run(s0, f, c, 86400.0);
    require(sh.cumulative_evaporation_m3 < 0.4 * sd.cumulative_evaporation_m3, "humid air suppresses evaporation");
}

// 8. Sunshine warms and stratifies the top of the column; wind mixes the heat down (Kraus-Turner).
void sunshine_stratifies_and_wind_mixes() {
    const std::size_t n = 6;
    auto c = basin(n, 50000.0);
    c.initial_temperature_k = 295.15;
    c.thermal.count = 12;
    c.thermal.initial_mixed_layer_depth_m = 60.0;
    c.air_sea.heat_transfer_coefficient = 0.0;
    c.air_sea.moisture_transfer_coefficient = 0.0;
    c.air_sea.surface_emissivity = 0.0;   // only the sun exchanges heat
    auto f = calm(c);
    add_air(f, c, 295.15, 0.8, 5.0, 0.0, 300.0);
    auto s0 = initialize_ocean(c);
    // One calm step without diffusion: every layer gains exactly its share of the penetrating sunshine.
    {
        auto cp = c;
        cp.thermal.background_diffusivity_m2_s = 0.0;
        const auto a = initialize_ocean(cp), b = advance_ocean(a, f, cp);
        const auto sigma = thermal_layer_fractions(cp);
        const std::size_t i = 2 * n + 2;
        double top = 0.0;
        for (std::size_t k = 0; k < 12; ++k) {
            const double dz = sigma[k] * b.thickness_m[i];
            const double share = k + 1 == 12 ? shortwave_transmission(top, cp.thermal) : shortwave_transmission(top, cp.thermal) - shortwave_transmission(top + dz, cp.thermal);
            const double gained = 1025.0 * 3990.0 * dz * (b.layer_temperature_k[i * 12 + k] - a.layer_temperature_k[i * 12 + k]);
            require_near(gained, share * 0.94 * 300.0 * 1800.0, "layer " + std::to_string(k) + " absorbs its share of the shortwave", 1.0e-6 * 0.94 * 300.0 * 1800.0);
            top += dz;
        }
    }
    const double days = 10.0;
    auto calm_run = s0;
    for (int k = 0; k < static_cast<int>(days * 48); ++k) { calm_run = advance_ocean(calm_run, f, c); require_budgets(calm_run, "sunshine"); }
    const double absorbed = 0.94 * 300.0 * water_area(c) * days * 86400.0;
    require_near(calm_run.heat_content_j - s0.heat_content_j, absorbed, "heat content grows by the absorbed sunshine", 1.0e-9 * absorbed);
    require_near(calm_run.cumulative_surface_heat_j, absorbed, "cumulative surface heat", 1.0e-9 * absorbed);
    require(calm_run.cumulative_evaporation_m3 == 0.0, "no evaporation without a moisture exchange");
    const std::size_t i = 2 * n + 2;
    require(calm_run.sst_k[i] > s0.sst_k[i] + 1.0, "the surface warms");
    require(calm_run.mixed_layer_depth_m[i] < 0.5 * s0.mixed_layer_depth_m[i], "a shallow warm layer forms under calm sunshine");
    // The same sunshine with a steady wind stress: the same heat, spread over a deeper mixed layer.
    auto fw = f;
    std::fill(fw.wind_stress_east_pa.begin(), fw.wind_stress_east_pa.end(), 0.15);
    c.physics.coriolis_mode = "none";   // a wind-driven slab current without rotation stays uniform in a closed box only briefly;
    c.physics.linear_friction_s = 1.0e-4; // strong drag keeps it small, the mixing is what matters here
    auto s0w = initialize_ocean(c);
    auto windy = s0w;
    for (int k = 0; k < static_cast<int>(days * 48); ++k) { windy = advance_ocean(windy, fw, c); require_budgets(windy, "windy sunshine"); }
    require_near(windy.heat_content_j - s0w.heat_content_j, absorbed, "wind does not change the heat input", 1.0e-9 * absorbed);
    require(windy.mixed_layer_depth_m[i] > 2.0 * calm_run.mixed_layer_depth_m[i], "wind mixing deepens the mixed layer");
    require(windy.sst_k[i] < calm_run.sst_k[i] - 0.5, "and keeps the surface cooler");
}

// 8b. Shear mixing: the wind's slab current in a thin fresh lens erodes its base until the bulk Richardson
//     number reaches the critical value; a light breeze cannot.
void shear_mixing_erodes_a_lens_to_the_critical_richardson_number() {
    const std::size_t n = 6;
    auto c = basin(n, 50000.0);
    c.initial_temperature_k = 300.15;
    c.thermal.count = 12;
    c.thermal.wind_mixing_efficiency = 0.0;          // shear mixing alone
    c.thermal.background_diffusivity_m2_s = 0.0;
    c.physics.temperature_relaxation_s = 1.0e12;
    c.physics.coriolis_mode = "none";                 // no currents to muddy the column; f below is the mixing's own
    auto f = calm(c);
    add_rain(f, c, 300.15);
    for (std::size_t i = 0; i < n * n; ++i) if (c.ocean[i]) f.rain_m_s[i] = 20.0 / 3.6e6;
    auto lens = run(initialize_ocean(c), f, c, 3.0 * 3600.0);   // 60 mm of warm rain: a fresh lens in the top layer
    const std::size_t i = 2 * n + 2;
    require(lens.salinity_psu[i] - lens.surface_salinity_psu[i] > 0.2, "a fresh lens formed");
    std::fill(f.rain_m_s.begin(), f.rain_m_s.end(), 0.0);
    const auto sigma = thermal_layer_fractions(c);
    const double f_mix = 1.0e-5;                      // the floor used with coriolis_mode none
    auto base_richardson = [&](const OceanState& s, double stress, double& depth) {
        std::size_t mixed = 1;
        while (mixed < 12 && std::abs(s.layer_salinity_psu[i * 12 + mixed] - s.layer_salinity_psu[i * 12]) < 1.0e-9 && std::abs(s.layer_temperature_k[i * 12 + mixed] - s.layer_temperature_k[i * 12]) < 1.0e-9) ++mixed;
        depth = 0.0;
        for (std::size_t k = 0; k < mixed; ++k) depth += sigma[k] * s.thickness_m[i];
        if (mixed == 12) return 1.0e9;
        const double u2 = stress / 1025.0, shear = u2 / (f_mix * depth);
        const double jump = seawater_density_kg_m3(s.layer_temperature_k[i * 12 + mixed], s.layer_salinity_psu[i * 12 + mixed], c.physics) - seawater_density_kg_m3(s.layer_temperature_k[i * 12], s.layer_salinity_psu[i * 12], c.physics);
        return 9.81 * jump * depth / (1025.0 * shear * shear);
    };
    double lens_depth = 0.0;
    require(base_richardson(lens, 0.0005, lens_depth) >= 0.65, "a light breeze does not destabilise the lens");
    auto breeze = f;
    std::fill(breeze.wind_stress_east_pa.begin(), breeze.wind_stress_east_pa.end(), 0.0005);
    auto calm_step = advance_ocean(lens, breeze, c);
    double calm_depth = 0.0;
    base_richardson(calm_step, 0.0005, calm_depth);
    require_near(calm_depth, lens_depth, "the lens survives a light breeze", 0.05 * lens_depth);
    auto windy = f;
    std::fill(windy.wind_stress_east_pa.begin(), windy.wind_stress_east_pa.end(), 0.01);
    auto mixed_step = advance_ocean(lens, windy, c);
    double mixed_depth = 0.0;
    require(base_richardson(mixed_step, 0.01, mixed_depth) >= 0.65, "the eroded mixed layer sits at the critical Richardson number");
    require(mixed_depth > 2.0 * lens_depth, "the wind's shear deepens the lens");
    require(mixed_step.salinity_psu[i] - mixed_step.surface_salinity_psu[i] < 0.5 * (lens.salinity_psu[i] - lens.surface_salinity_psu[i]), "and dilutes it");
    require_budgets(mixed_step, "shear mixing");
}

// 9. Surface cooling convects: the column stays statically stable and the mixed layer deepens.
void cooling_deepens_the_mixed_layer() {
    const std::size_t n = 6;
    auto c = basin(n, 50000.0);
    c.initial_temperature_k = 293.15;
    c.thermal.count = 12;
    c.thermal.initial_mixed_layer_depth_m = 20.0;
    c.thermal.wind_mixing_efficiency = 0.0;   // convection alone
    auto f = calm(c);
    add_air(f, c, 275.15, 0.6, 12.0, 0.5, 0.0);   // a cold-air outbreak at night
    auto s0 = initialize_ocean(c);
    auto s = s0;
    for (int k = 0; k < 48 * 10; ++k) { s = advance_ocean(s, f, c); require_budgets(s, "cooling"); }
    const std::size_t i = 2 * n + 2;
    require(s.net_heat_w_m2[i] < -300.0, "a cold-air outbreak draws hundreds of W/m2");
    require(s.mixed_layer_depth_m[i] > 3.0 * s0.mixed_layer_depth_m[i], "convection deepens the mixed layer");
    const auto sigma = thermal_layer_fractions(c);
    for (std::size_t k = 1; k < 12; ++k) {
        const double upper = seawater_density_kg_m3(s.layer_temperature_k[i * 12 + k - 1], s.layer_salinity_psu[i * 12 + k - 1], c.physics);
        const double lower = seawater_density_kg_m3(s.layer_temperature_k[i * 12 + k], s.layer_salinity_psu[i * 12 + k], c.physics);
        require(upper <= lower + 1.0e-9, "no static instability remains");
    }
    require(s.sst_k[i] < s0.sst_k[i] - 1.0, "the surface cools");
    require(s.surface_salinity_psu[i] > c.initial_salinity_psu, "evaporation into the dry outbreak air makes the surface saltier");
}

// 10. Layers, bulk temperature and budgets stay consistent while a gyre carries heat around.
void layers_stay_consistent_under_currents() {
    const std::size_t n = 14;
    auto c = basin(n, 60000.0, "beta_plane");
    c.thermal.count = 8;
    c.initial_temperature_k = 297.15;
    auto f = calm(c);
    const double ly = static_cast<double>(n) * 60000.0;
    for (std::size_t i = 0; i < n * n; ++i) {
        const double y = (static_cast<double>(i / n) + 0.5) * 60000.0;
        f.wind_stress_east_pa[i] = -0.1 * std::cos(kPi * y / ly);
    }
    add_air(f, c, 294.15, 0.75, 7.0, 0.3, 0.0);
    for (std::size_t i = 0; i < n * n; ++i) { f.air_temperature_k[i] = 300.0 - 12.0 * static_cast<double>(i / n) / static_cast<double>(n); f.shortwave_down_w_m2[i] = 220.0; }
    add_rain(f, c, 293.15);
    f.rain_m_s[(n / 2) * n + n / 2] = 5.0 / 3.6e6;
    f.rivers.push_back({(n / 3) * n + 1, 1.0e4, 287.15, 0.0, "river"});
    const auto sigma = thermal_layer_fractions(c);
    auto s = initialize_ocean(c);
    for (int k = 0; k < 48 * 60; ++k) {
        s = advance_ocean(s, f, c);
        require_budgets(s, "gyre");
        if (k % 240 != 0) continue;
        for (std::size_t i = 0; i < n * n; ++i) {
            if (!c.ocean[i]) continue;
            double bulk = 0.0, bulk_s = 0.0;
            for (std::size_t j = 0; j < 8; ++j) { bulk += sigma[j] * s.layer_temperature_k[i * 8 + j]; bulk_s += sigma[j] * s.layer_salinity_psu[i * 8 + j]; }
            require_near(s.temperature_k[i], bulk, "the bulk temperature is the column mean", 1.0e-9);
            require_near(s.salinity_psu[i], bulk_s, "the bulk salinity is the column mean", 1.0e-9);
            require_near(s.surface_salinity_psu[i], s.layer_salinity_psu[i * 8], "the surface salinity is the top layer", 0.0);
        }
    }
    require(s.kinetic_energy_j > 0.0 && *std::max_element(s.speed_m_s.begin(), s.speed_m_s.end()) > 0.02, "the gyre circulates");
    // The western boundary current carries warm water north: the north-west is warmer than the north-east.
    const std::size_t row = n - 3;
    require(s.sst_k[row * n + 1] > s.sst_k[row * n + n - 2], "warm western boundary current");
}

// 11. Deep-water formation in the layered column under a cold-air outbreak; a rain lens on top stops it.
void formation_and_the_fresh_cap() {
    const std::size_t n = 12;
    auto c = basin(n, 50000.0);
    c.initial_temperature_k = 285.15;
    c.thermal.count = 8;
    c.physics.convection_time_scale_s = 5.0 * 86400.0;
    auto f = calm(c);
    add_air(f, c, 285.15, 0.8, 8.0, 0.6, 0.0);
    for (std::size_t i = 0; i < n * n; ++i) if (i / n >= n - 4) { f.air_temperature_k[i] = 263.15; f.air_specific_humidity_kg_kg[i] = 0.5 * saturation_specific_humidity_kg_kg(263.15, 101325.0); f.wind_speed_m_s[i] = 15.0; }
    add_rain(f, c, 280.15);
    auto s = run(initialize_ocean(c), f, c, 60.0 * 86400.0);
    require_budgets(s, "outbreak");
    require(s.overturning_m3_s > 0.0 && s.cumulative_sinking_m3 > 0.0, "the outbreak cools the surface until it sinks");
    for (std::size_t i = 0; i < n * n; ++i) if (c.ocean[i] && i / n < n - 6) require(s.sinking_m_s[i] == 0.0, "no formation away from the outbreak");
    // Steady rain over the formation region: the fresh lens floats on the cold water and switches the sinking off.
    auto fr = f;
    for (std::size_t i = 0; i < n * n; ++i) if (c.ocean[i] && i / n >= n - 4) fr.rain_m_s[i] = 4.0 / 3.6e6;
    auto sr = run(initialize_ocean(c), fr, c, 60.0 * 86400.0);
    require_budgets(sr, "outbreak with rain");
    require(sr.overturning_m3_s < 0.5 * s.overturning_m3_s, "rain on the formation site weakens the overturning");
    require(sr.surface_salinity_psu[(n - 3) * n + n / 2] < s.surface_salinity_psu[(n - 3) * n + n / 2] - 0.3, "the formation site is fresher at the surface");
}

// 12. Without the new inputs the legacy slab is unchanged and its heat budget (relaxation) closes too.
void legacy_slab_budgets() {
    const std::size_t n = 10;
    auto c = basin(n, 50000.0, "beta_plane");
    auto f = calm(c);
    for (std::size_t i = 0; i < n * n; ++i) { f.target_temperature_k[i] = 295.15 - 10.0 * static_cast<double>(i / n) / static_cast<double>(n); f.wind_stress_east_pa[i] = -0.1 * std::cos(kPi * (static_cast<double>(i / n) + 0.5) / static_cast<double>(n)); f.net_precipitation_m_s[i] = 5.0e-9; }
    f.rivers.push_back({(n / 2) * n + 1, 5.0e3, 283.15, 0.0, "river"});
    auto s = initialize_ocean(c);
    for (int k = 0; k < 48 * 20; ++k) { s = advance_ocean(s, f, c); require_budgets(s, "legacy"); }
    require(s.layer_temperature_k.empty() && s.rain_sea_m3_s == 0.0 && s.evaporation_m3_s == 0.0 && s.coastal_runoff_m3_s == 0.0, "no new processes without their inputs");
    require(std::all_of(s.evaporation_m_s.begin(), s.evaporation_m_s.end(), [](double v) { return v == 0.0; }), "no evaporation field");
    require(s.surface_heat_w != 0.0, "the relaxation is reported as surface heat");
    for (std::size_t i = 0; i < n * n; ++i) if (c.ocean[i]) require(s.surface_salinity_psu[i] <= s.salinity_psu[i] + 1.0e-12, "the legacy surface is never saltier than the layer");
}

// 13. Contract.
void rejections() {
    auto c = basin(8, 50000.0);
    auto bad = c;
    bad.thermal.count = 1;
    require_rejected([&] { validate_ocean_config(bad); }, "one layer");
    bad = c; bad.thermal.count = 41;
    require_rejected([&] { validate_ocean_config(bad); }, "too many layers");
    bad = c; bad.thermal.count = 6; bad.thermal.surface_layer_m = 600.0;
    require_rejected([&] { validate_ocean_config(bad); }, "surface layer thicker than the layer");
    bad = c; bad.thermal.count = 6; bad.thermal.background_diffusivity_m2_s = -1.0;
    require_rejected([&] { validate_ocean_config(bad); }, "negative diffusivity");
    bad = c; bad.thermal.count = 6; bad.thermal.shortwave_red_fraction = 1.5;
    require_rejected([&] { validate_ocean_config(bad); }, "shortwave fraction");
    bad = c; bad.initial_surface_temperature_k.assign(64, 290.0);
    require_rejected([&] { validate_ocean_config(bad); }, "initial surface temperatures need layers");
    bad = c; bad.thermal.count = 6; bad.initial_surface_temperature_k.assign(10, 290.0);
    require_rejected([&] { validate_ocean_config(bad); }, "initial surface temperature size");
    bad = c; bad.air_sea.moisture_transfer_coefficient = 0.5;
    require_rejected([&] { validate_ocean_config(bad); }, "transfer coefficient");
    bad = c; bad.air_sea.surface_albedo = -0.1;
    require_rejected([&] { validate_ocean_config(bad); }, "albedo");
    bad = c; bad.coastal.runoff_coefficient = 1.5;
    require_rejected([&] { validate_ocean_config(bad); }, "runoff coefficient");
    bad = c; bad.coastal.runoff_time_scale_s = 0.0;
    require_rejected([&] { validate_ocean_config(bad); }, "runoff time scale");
    bad = c; bad.physics.heat_capacity_j_kg_k = 100.0;
    require_rejected([&] { validate_ocean_config(bad); }, "heat capacity");
    const auto s = initialize_ocean(c);
    auto f = calm(c);
    add_rain(f, c, 290.0);
    f.rain_m_s[20] = -1.0e-6;
    require_rejected([&] { advance_ocean(s, f, c); }, "negative rain");
    f.rain_m_s[20] = 600.0 / 3.6e6;
    require_rejected([&] { advance_ocean(s, f, c); }, "rain above 500 mm/h");
    f = calm(c);
    f.rain_m_s.assign(64, 0.0);
    require_rejected([&] { advance_ocean(s, f, c); }, "rain without its temperature");
    f = calm(c);
    add_air(f, c, 290.0, 0.8, 5.0, 0.2, 100.0);
    f.cloud_fraction.pop_back();
    require_rejected([&] { advance_ocean(s, f, c); }, "incomplete air forcing");
    f = calm(c);
    add_air(f, c, 290.0, 0.8, 5.0, 0.2, 100.0);
    f.air_specific_humidity_kg_kg[20] = 0.2;
    require_rejected([&] { advance_ocean(s, f, c); }, "humidity range");
    f = calm(c);
    add_air(f, c, 290.0, 0.8, 5.0, 0.2, 2000.0);
    require_rejected([&] { advance_ocean(s, f, c); }, "shortwave range");
    auto cc = c;
    cc.coastal.band_m = 60000.0;
    require_rejected([&] { advance_ocean(initialize_ocean(cc), calm(cc), cc); }, "coastal runoff without rain arrays");
    auto cl = c;
    cl.thermal.count = 6;
    require_rejected([&] { advance_ocean(s, calm(cl), cl); }, "a slab state with a layered configuration");
    require_rejected([&] { advance_ocean(initialize_ocean(cl), calm(c), c); }, "a layered state with a slab configuration");
}

// 14. Regression: a small double-gyre basin with the layered column, air-sea exchange, a moving rain band,
//     coastal rain and a river, pinned after 60 days.
void regression_is_pinned() {
    const std::size_t n = 16;
    auto c = basin(n, 60000.0, "beta_plane");
    for (std::size_t i = 0; i < n * n; ++i) if (i % n <= 2) c.ocean[i] = 0;
    c.thermal.count = 8;
    c.initial_temperature_k = 296.15;
    c.coastal.band_m = 70000.0;
    auto f = calm(c);
    const double ly = static_cast<double>(n) * 60000.0;
    add_air(f, c, 293.15, 0.75, 7.0, 0.4, 0.0);
    add_rain(f, c, 291.15);
    for (std::size_t i = 0; i < n * n; ++i) {
        const double y = (static_cast<double>(i / n) + 0.5) * 60000.0;
        f.wind_stress_east_pa[i] = -0.1 * std::cos(2.0 * kPi * y / ly);
        f.air_temperature_k[i] = 298.15 - 14.0 * y / ly;
        f.air_specific_humidity_kg_kg[i] = 0.82 * saturation_specific_humidity_kg_kg(f.air_temperature_k[i], 101325.0);
        f.wind_speed_m_s[i] = std::sqrt(std::abs(f.wind_stress_east_pa[i]) / (1.2 * 1.3e-3));
    }
    f.rivers.push_back({(n / 3) * n + 3, 8.0e3, 287.15, 0.0, "river"});
    auto s = initialize_ocean(c);
    const double start_doy = 172.0;
    for (int k = 0; k < 48 * 60; ++k) {
        const double t = s.elapsed_seconds + 900.0, doy = start_doy + t / 86400.0, hour = std::fmod(t / 3600.0, 24.0);
        for (std::size_t i = 0; i < n * n; ++i) {
            const double lat = 35.0 + ((static_cast<double>(i / n) + 0.5) * 60000.0) / 111195.0, lon = -50.0 + ((static_cast<double>(i % n) + 0.5) * 60000.0) / (111195.0 * std::cos(35.0 * kPi / 180.0));
            f.shortwave_down_w_m2[i] = surface_shortwave_w_m2(solar_cosine_zenith(lat, lon, doy, hour), vapour_pressure_pa(f.air_specific_humidity_kg_kg[i], 101325.0), f.cloud_fraction[i]);
            // A rain band moving east across the basin and the coast every ten days.
            const double band_x = std::fmod(t / (10.0 * 86400.0), 1.0) * static_cast<double>(n);
            const double dxb = static_cast<double>(i % n) - band_x;
            f.rain_m_s[i] = 3.0 / 3.6e6 * std::exp(-dxb * dxb / 4.0);
        }
        s = advance_ocean(s, f, c);
        require_budgets(s, "regression");
    }
    const auto d = diagnose_ocean(s, f, c);
    auto pinned = [](double actual, double expected, const std::string& name) { require_near(actual, expected, "regression: " + name, 1.0e-8 * std::abs(expected)); };
    pinned(s.kinetic_energy_j, 1.51568478376e+14, "kinetic energy");
    pinned(s.heat_content_j, 1.30038637729e+22, "heat content");
    pinned(d.mean_sst_k, 295.146686848, "mean SST");
    pinned(d.min_sst_k, 292.112414896, "minimum SST");
    pinned(d.max_sst_k, 298.120650158, "maximum SST");
    pinned(d.mean_mixed_layer_depth_m, 26.8805643088, "mean mixed-layer depth");
    pinned(d.max_mixed_layer_depth_m, 44.3819871877, "deepest mixed layer");
    pinned(s.cumulative_evaporation_m3, 155094649227.0, "cumulative evaporation");
    pinned(s.cumulative_rain_sea_m3, 573194305023.0, "cumulative rain on the sea");
    pinned(s.cumulative_coastal_runoff_m3, 95639655676.1, "cumulative coastal runoff");
    pinned(d.min_salinity_psu, 27.0919110122, "minimum surface salinity");
    pinned(s.cumulative_surface_heat_j, 4.94982566963e+18, "cumulative surface heat");
    require(d.lenses.size() == 1 && d.barrier_layer_cells == 142 && s.overturning_m3_s == 0.0, "regression: lenses, barrier layers, no summer formation");
}

}  // namespace

int main() {
    try {
        bulk_formulae_match_hand_calculations();
        solar_geometry_and_shortwave();
        layer_geometry_and_penetration();
        layered_column_initialises_a_thermocline();
        rain_on_the_sea_freshens_and_cools();
        coastal_rain_drains_to_the_sea();
        evaporation_concentrates_and_cools();
        sunshine_stratifies_and_wind_mixes();
        shear_mixing_erodes_a_lens_to_the_critical_richardson_number();
        cooling_deepens_the_mixed_layer();
        layers_stay_consistent_under_currents();
        formation_and_the_fresh_cap();
        legacy_slab_budgets();
        rejections();
        regression_is_pinned();
    } catch (const std::exception& error) {
        std::cerr << "ocean heat and rain test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ocean heat and rain cases passed\n";
    return 0;
}
