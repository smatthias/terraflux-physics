// Coupled Earth: the forecast kernel (geography, synoptic systems, surface coupling, hazards, warnings, scores) and
// the climate kernel (forcing, feedbacks, two-layer response, impacts, pattern, carbon) (weather.earth_system).
#include "terraflux/weather/earth_climate.hpp"
#include "terraflux/weather/earth_forecast.hpp"
#include "terraflux/weather/moist_air.hpp"

#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

using namespace terraflux::weather;

namespace {

int failures = 0;
constexpr double kCp = 1004.6;

void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; ++failures; }
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    require(std::abs(actual - expected) <= tolerance, message + " (got " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    require(false, "rejected: " + message);
}

TransportGrid2D grid(std::size_t nx, std::size_t ny, double spacing, double lat0, double lon0) {
    TransportGrid2D g;
    g.nx = nx;
    g.ny = ny;
    g.spacing_east_m = g.spacing_north_m = spacing;
    g.origin_east_m = -0.5 * spacing * static_cast<double>(nx);
    g.origin_north_m = -0.5 * spacing * static_cast<double>(ny);
    g.geographic_anchor_latitude_deg = lat0;
    g.geographic_anchor_longitude_deg = lon0;
    return g;
}

void geography() {
    for (double x : {-2.0e6, 0.0, 1.5e6})
        for (double y : {-1.0e6, 3.0e5, 2.5e6}) {
            const auto g = local_to_geographic(x, y, 40.0, -45.0);
            const auto back = geographic_to_local(g.latitude_deg, g.longitude_deg, 40.0, -45.0);
            require_near(back[0], x, "the projection is invertible (east)", 1e-3);
            require_near(back[1], y, "the projection is invertible (north)", 1e-3);
            // Azimuthal equidistant: the distance from the anchor is preserved.
            require_near(great_circle_km(40.0, -45.0, g.latitude_deg, g.longitude_deg), std::hypot(x, y) / 1000.0, "distances from the anchor are true", 1e-3);
        }
    require_near(local_to_geographic(0.0, 1.0e6, 40.0, -45.0).longitude_deg, -45.0, "due north stays on the meridian", 1e-9);
    require_near(great_circle_km(51.5, -0.13, 40.71, -74.0), 5570.0, "London - New York about 5570 km", 15.0);
    // Sun: the equinox (2026-03-20 12 UT is 9575 days after J2000) and the solstices.
    require(std::abs(solar_declination_deg(9575.0)) < 0.6, "declination near zero at the March equinox");
    require_near(solar_declination_deg(9668.0), 23.44, "June solstice declination", 0.1);
    require_near(solar_declination_deg(9851.0), -23.44, "December solstice declination", 0.1);
    require(solar_cos_zenith(0.0, 0.0, 9575.0) > 0.99, "the Sun stands overhead at the equator at noon on the equinox");
    require(solar_cos_zenith(0.0, 180.0, 9575.0) < -0.99, "and at the nadir on the other side of the Earth");
    require_near(daily_mean_cos_zenith(0.0, 0.0), 1.0 / 3.14159265358979323846, "equatorial daily mean at the equinox is 1/pi", 1e-9);
    require_near(daily_mean_cos_zenith(80.0, -23.44), 0.0, "polar night", 1e-12);
    require_near(daily_mean_cos_zenith(90.0, 23.44), std::sin(23.44 * 3.14159265358979323846 / 180.0), "polar day: the Sun circles at the declination", 1e-9);
    // The daily mean is the mean of the hourly values.
    double sum = 0.0;
    for (int h = 0; h < 24 * 60; ++h) sum += std::max(0.0, solar_cos_zenith(50.0, 10.0, 9668.0 + h / (24.0 * 60.0)));
    require_near(sum / (24.0 * 60.0), daily_mean_cos_zenith(50.0, solar_declination_deg(9668.0)), "daily mean of cos zenith", 2e-3);
}

void synoptic() {
    const SteeringFlow flow;
    require(steering_east_m_s(flow.jet_latitude_deg, flow) > 11.0, "the jet steers eastward at its latitude");
    require(steering_east_m_s(flow.trade_latitude_deg, flow) < 0.0, "the trades steer westward");
    EnsembleSpread spread;
    validate_ensemble_spread(spread);
    require_rejected([] { EnsembleSpread s; s.position_km = -1.0; validate_ensemble_spread(s); }, "a negative position error");
    require_rejected([] { EnsembleSpread s; s.radius_fraction = 0.9; validate_ensemble_spread(s); }, "a huge radius error");
    EnsembleSpread none{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    NormalSequence random(7);
    const auto zero = draw_system_errors(random, none, true);
    require(zero.east_m == 0.0 && zero.speed_east_m_s == 0.0 && zero.radius_factor == 1.0 && zero.timing_hours == 0.0, "no spread, no errors");
    NormalSequence a(11), b(11);
    const auto ea = draw_system_errors(a, spread, false), eb = draw_system_errors(b, spread, false);
    require(ea.east_m == eb.east_m && ea.deepening_pa_per_day == eb.deepening_pa_per_day, "members are reproducible from their seed");
    require(ea.timing_hours == 0.0, "only genesis has timing errors");

    const auto g = grid(40, 30, 200000.0, 45.0, -30.0);
    SynopticSystem low;
    low.name = "L";
    low.latitude_deg = 45.0;
    low.longitude_deg = -30.0;
    low.anomaly_pa = -1500.0;
    low.radius_m = 800000.0;
    low.deepening_pa_per_day = 1000.0;
    low.mature_hours = 24.0;
    low.filling_pa_per_day = 500.0;
    low.steering_factor = 0.5;
    SynopticSystem high = low;
    high.name = "H";
    high.kind = "high";
    high.anomaly_pa = 1500.0;
    high.start_hours = 30.0;
    GenesisRegion genesis;
    genesis.name = "G";
    genesis.latitude_deg = 40.0;
    genesis.longitude_deg = -40.0;
    genesis.first_hours = 12.0;
    genesis.every_hours = 24.0;
    genesis.last_hours = 60.0;
    SynopticEvolution control({low, high}, {genesis}, flow, g, 72.0, nullptr, spread);
    require(control.active().size() == 1, "only the low exists at the start");
    control.advance(24.0);
    auto active = control.active();
    require(active.size() == 2, "the first genesis low is born after 12 h");
    require_near(active[0].anomaly_pa, -2500.0, "the low deepens for a day", 1e-6);
    require_near(active[0].east_m, steering_east_m_s(45.0, flow) * 0.5 * 86400.0, "it moves with half the steering flow", 5000.0);
    control.advance(24.0);
    active = control.active();
    require_near(active[0].anomaly_pa, -2000.0, "then it fills", 1e-6);
    require(active.size() == 4 && active[1].kind == "high", "the high started at 30 h, the second genesis low at 36 h");
    require(control.centers().size() == active.size() && control.centers()[1].pressure_anomaly_pa > 0.0, "the centres carry the anomalies");
    require_rejected([&] { SynopticSystem bad = low; bad.anomaly_pa = 100.0; SynopticEvolution({bad}, {}, flow, g, 24.0, nullptr, spread); }, "a low with a positive anomaly");
    NormalSequence member(5);
    SynopticEvolution perturbed({low}, {}, flow, g, 72.0, &member, spread);
    perturbed.advance(24.0);
    require(std::abs(perturbed.active()[0].east_m - control.active()[0].east_m) > 1.0, "ensemble members differ from the control");

    // Ascent: lows lift (most to the warm sector), highs subside; inflow converges.
    SynopticWeatherConfig weather;
    validate_synoptic_weather(weather);
    require_rejected([] { SynopticWeatherConfig c; c.full_cloud_rh = 0.5; validate_synoptic_weather(c); }, "full cloud below the cloud threshold");
    const std::size_t n = g.nx * g.ny;
    std::vector<double> calm(n, 0.0);
    const auto w = synoptic_ascent_m_s({{0.0, 0.0, -1500.0, 800000.0}}, calm, calm, g, weather);
    const std::size_t centre = 15 * g.nx + 20;
    require(w[centre] > 0.0, "a low lifts");
    const std::size_t warm = (15 + 2) * g.nx + 20 + 3, cold = (15 - 2) * g.nx + 20 - 3;   // ENE and WSW of the centre
    require(w[warm] > 1.5 * w[cold], "most ascent in the warm sector");
    const auto sink = synoptic_ascent_m_s({{0.0, 0.0, 1500.0, 800000.0}}, calm, calm, g, weather);
    require(sink[centre] < 0.0, "a high subsides");
    std::vector<double> u(n), v(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double x = g.origin_east_m + (static_cast<double>(k % g.nx) + 0.5) * g.spacing_east_m;
        const double y = g.origin_north_m + (static_cast<double>(k / g.nx) + 0.5) * g.spacing_north_m;
        u[k] = -1e-6 * x;
        v[k] = -1e-6 * y;
    }
    const auto conv = synoptic_ascent_m_s({}, u, v, g, weather);
    require_near(conv[centre], weather.convergence_depth_m * 2e-6, "frictional convergence lifts -H div V", 1e-9);
    std::vector<double> ramp(n), west(n, 10.0), still(n, 0.0);
    for (std::size_t k = 0; k < n; ++k) ramp[k] = 0.005 * (g.origin_east_m + (static_cast<double>(k % g.nx) + 0.5) * g.spacing_east_m + 4.0e6);   // rises eastward
    const auto up = upslope_ascent_m_s(west, still, ramp, g, 0.5);
    require_near(up[centre], 0.5 * 10.0 * 0.005, "a westerly rises up an eastward slope: w = f u dh/dx", 1e-12);
    require_near(up[15 * g.nx], up[centre], "one-sided differences at the edge", 1e-12);
    std::vector<double> east(n, -10.0);
    require(upslope_ascent_m_s(east, still, ramp, g, 0.5)[centre] < 0.0, "downslope wind sinks");
    require(layered_cloud_fraction(0.97, 0.0, weather) == 1.0 && layered_cloud_fraction(0.5, 0.0, weather) == 0.0, "cloud from humidity");
    require(layered_cloud_fraction(0.65, 0.05, weather) > layered_cloud_fraction(0.65, 0.0, weather), "ascent makes cloud at lower humidity");
    require(layered_rain_mm_h(0.95, -0.02, 1.2, 0.01, weather) == 0.0 && layered_rain_mm_h(0.7, 0.05, 1.2, 0.01, weather) == 0.0, "no rain in subsidence or dry air");
    require_near(layered_rain_mm_h(1.0, 0.05, 1.2, 0.01, weather), weather.efficiency * 1.2 * 0.05 * 0.01 * 3600.0, "rain = e rho w q", 1e-9);
}

void surface() {
    SurfaceCouplingConfig config;
    validate_surface_coupling(config);
    require_rejected([] { SurfaceCouplingConfig c; c.cloud_shading = 2.0; validate_surface_coupling(c); }, "cloud shading above one");
    CoupledWindState state;
    state.pressure_pa = {100000.0, 100000.0, 100000.0};
    state.air_temperature_k = {290.0, 290.0, 300.0};
    state.specific_humidity_kg_kg = {0.005, 0.005, 0.030};   // the last cell is supersaturated
    state.wind_east_m_s = state.wind_north_m_s = {0.0, 0.0, 0.0};
    SurfaceState s;
    s.land = {0, 1, 0};
    s.sea_surface_temperature_k = {300.0, 0.0, 300.5};
    s.land_climate_temperature_k = {0.0, 290.0, 0.0};
    s.soil_moisture = {1.0, 0.5, 1.0};
    s.normal_soil_moisture = {1.0, 0.5, 1.0};
    s.elevation_m = {0.0, 0.0, 0.0};
    s.cloud_fraction = {0.0, 0.0, 0.0};
    const double h0 = kCp * state.air_temperature_k[2] + 2.501e6 * state.specific_humidity_kg_kg[2];
    auto ocean_off = config;
    ocean_off.ocean_fluxes = false;
    const double sea_before = state.air_temperature_k[0];
    auto copy = state;
    auto s_copy = s;
    const auto report = couple_surface(state, s, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, config, 1.0);
    require(state.air_temperature_k[0] > sea_before, "air over a warmer sea warms");
    require_near(state.air_temperature_k[0] - sea_before, (300.0 - 0.5 - 290.0) * (1.0 - std::exp(-1.0 / 24.0)), "relaxation to SST minus the offset", 1e-9);
    require(state.specific_humidity_kg_kg[0] > 0.005, "the sea moistens the air");
    require(report.condensation_mm[2] > 0.0 && report.condensation_mm[0] == 0.0, "supersaturated air rains");
    require(state.specific_humidity_kg_kg[2] < saturation_specific_humidity_kg_kg(state.air_temperature_k[2], 100000.0), "and ends below saturation");
    couple_surface(copy, s_copy, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, ocean_off, 1.0);
    require(copy.air_temperature_k[0] == 290.0, "without ocean fluxes the sea leaves the air alone");
    const double h1 = kCp * copy.air_temperature_k[2] + 2.501e6 * copy.specific_humidity_kg_kg[2];
    require_near(h1, h0, "condensation conserves moist enthalpy", 1e-6 * h0);

    // A full day of sunshine over land: heating by day, cooling by night, no drift (land relaxation off).
    auto day = config;
    day.land_climate_time_h = 10000.0;
    day.evapotranspiration_g_kg_per_h = 0.0;
    CoupledWindState land;
    land.pressure_pa = {100000.0};
    land.air_temperature_k = {290.0};
    land.specific_humidity_kg_kg = {0.002};
    land.wind_east_m_s = land.wind_north_m_s = {0.0};
    SurfaceState ls;
    ls.land = {1};
    ls.sea_surface_temperature_k = {0.0};
    ls.land_climate_temperature_k = {290.0};
    ls.soil_moisture = ls.normal_soil_moisture = {0.3};
    ls.elevation_m = {0.0};
    ls.cloud_fraction = {0.0};
    const double mean = daily_mean_cos_zenith(45.0, solar_declination_deg(9668.0));
    double peak = 290.0;
    for (int h = 0; h < 24 * 4; ++h) {
        const double cz = solar_cos_zenith(45.0, 0.0, 9668.0 - 0.5 + (h + 0.5) / 96.0);
        couple_surface(land, ls, {cz}, {mean}, day, 0.25);
        peak = std::max(peak, land.air_temperature_k[0]);
    }
    require(peak > 292.0, "the land warms by a few kelvin by day");
    require_near(land.air_temperature_k[0], 290.0, "and cools back at night", 0.25);
    // Evapotranspiration dries the soil and moistens the air.
    ls.soil_moisture = {0.8};
    const double q0 = land.specific_humidity_kg_kg[0];
    couple_surface(land, ls, {0.9}, {mean}, config, 1.0);
    require(land.specific_humidity_kg_kg[0] > q0 && ls.soil_moisture[0] < 0.8, "evapotranspiration moves soil water into the air");
    // At night a wet soil still drains; a dry one hardly.
    auto night = config;
    night.evapotranspiration_g_kg_per_h = 0.0;
    ls.soil_moisture = {1.0};
    couple_surface(land, ls, {-0.5}, {mean}, night, 24.0);
    require_near(ls.soil_moisture[0], 1.0 - night.drainage_mm_per_day / night.soil_capacity_mm, "a saturated soil drains at the full rate", 1e-9);
    ls.soil_moisture = {0.2};
    couple_surface(land, ls, {-0.5}, {mean}, night, 24.0);
    require(ls.soil_moisture[0] > 0.199, "a dry soil keeps its water");
    require_rejected([&] { couple_surface(land, ls, {0.9, 0.1}, {mean}, config, 1.0); }, "mismatched arrays");

    // Rain on the soil: the water balance closes; wet and steep soil sheds more.
    for (double soil0 : {0.1, 0.6, 0.95})
        for (double rain : {5.0, 40.0, 150.0}) {
            double soil = soil0;
            const double runoff = add_rain_to_soil(soil, rain, 0.01, config);
            require_near(runoff + (soil - soil0) * config.soil_capacity_mm, rain, "rain = runoff + storage change", 1e-9);
            require(soil <= 1.0 && runoff >= 0.0, "soil bounded, runoff non-negative");
        }
    double dry = 0.2, wet = 0.9, flat = 0.5, steep = 0.5;
    require(add_rain_to_soil(wet, 20.0, 0.0, config) > add_rain_to_soil(dry, 20.0, 0.0, config), "wet soil sheds more");
    require(add_rain_to_soil(steep, 20.0, 0.05, config) > add_rain_to_soil(flat, 20.0, 0.0, config), "steep slopes shed more");
}

void hazards() {
    const GustConfig gusts;
    require_near(gust_m_s(10.0, false, gusts), 14.5, "gust factor", 1e-12);
    require_near(gust_m_s(10.0, true, gusts), 26.5, "convective downdraft gusts", 1e-12);
    const auto sea = surface_wind_m_s(10.0, 0.0, 0.0, 0.0, false, 0.0, gusts), plain = surface_wind_m_s(10.0, 0.0, 0.0, 0.0, true, 0.0, gusts);
    require_near(sea[0], 8.0, "the sea slows the layer wind to 0.8 at 10 m", 1e-12);
    require_near(plain[0], 6.0, "the land to 0.6", 1e-12);
    require(surface_wind_m_s(10.0, 0.0, 0.0, 0.0, true, 2000.0, gusts)[0] < plain[0], "mountains still less");
    require_near(surface_wind_m_s(10.0, 0.0, 0.0, 40.0, false, 0.0, gusts)[1], 40.0, "the hurricane vortex is already a 10 m wind", 1e-12);
    require_near(cyclone_wind_m_s(50.0, 40000.0, 40000.0, 0.6), 50.0, "maximum at the radius of maximum wind", 1e-9);
    require_near(cyclone_wind_m_s(50.0, 40000.0, 20000.0, 0.6), 25.0, "solid-body core", 1e-9);
    require_near(cyclone_wind_m_s(50.0, 40000.0, 160000.0, 0.5), 25.0, "(R/r)^x outside", 1e-9);
    require(cyclone_wind_m_s(50.0, 40000.0, 200000.0, 0.6) > 17.0, "tropical-storm winds reach 200 km");

    require_near(ground_hail_cm(5.0, 3000.0, 3500.0, 5000.0), 5.0, "low freezing level: the hail reaches the ground", 1e-12);
    require_near(ground_hail_cm(5.0, 4250.0, 3500.0, 5000.0), 2.5, "half way", 1e-12);
    require(ground_hail_cm(5.0, 5200.0, 3500.0, 5000.0) == 0.0, "tropical freezing level: no large hail");
    require_near(ground_hail_cm(5.0, std::nan(""), 3500.0, 5000.0), 5.0, "a column below freezing keeps it", 1e-12);
    require_rejected([] { ground_hail_cm(1.0, 1000.0, 4000.0, 3000.0); }, "inverted hail levels");

    const auto g = grid(20, 20, 50000.0, 25.0, -75.0);
    const std::size_t n = g.nx * g.ny;
    std::vector<unsigned char> land(n, 0);
    for (std::size_t k = 0; k < n; ++k) land[k] = (k % g.nx) >= 12;   // land in the east
    StormSystemsState storms;
    TropicalCyclone storm;
    storm.east_m = 0.0;
    storm.north_m = 0.0;
    storm.max_wind_m_s = 50.0;
    storm.radius_max_wind_m = 40000.0;
    storm.surge_estimate_m = 3.0;
    storm.core_rain_rate_mm_h = 40.0;
    storms.storms.push_back(storm);
    const CycloneHazardConfig config;
    const auto h = cyclone_hazards(storms, g, land, config);
    double peak_wind = 0.0;
    for (double x : h.wind_m_s) peak_wind = std::max(peak_wind, x);
    require_near(peak_wind, cyclone_wind_m_s(50.0, 40000.0, std::hypot(25000.0, 25000.0), 0.6), "peak wind in the cells next to the centre", 1e-9);
    for (std::size_t k = 0; k < n; ++k) {
        const bool coast = land[k] && (k % g.nx) == 12;
        if (h.surge_m[k] > 0.0) require(coast, "surge only on the coastal land");
    }
    require(h.surge_m[10 * g.nx + 12] > 0.5, "surge at the coast next to the storm");
    require(h.rain_mm_h[10 * g.nx + 10] > h.rain_mm_h[10 * g.nx + 18], "rain falls off with distance");
    std::vector<double> u(n, 0.0), v(n, 0.0);
    add_storm_vortices(u, v, storms, g, 0.6);
    const std::size_t east = 10 * g.nx + 14, north = 14 * g.nx + 10;   // 4.5 and 3.5 cells from the centre
    require(v[east] > 0.0 && u[north] < 0.0, "northern-hemisphere cyclones turn counter-clockwise");
    require_rejected([&] { cyclone_hazards(storms, g, std::vector<unsigned char>(3, 0), config); }, "a wrong land mask");
}

void warnings_and_scores() {
    require(warning_level({0.9, 0.6, 0.2}, 0.5) == 2, "orange: its probability reaches the minimum");
    require(warning_level({0.4, 0.1, 0.0}, 0.5) == 0, "no warning below the minimum");
    require(warning_level({0.9, 0.9, 0.9}, 0.5) == 3, "red");
    const auto g = grid(6, 4, 100000.0, 40.0, 0.0);
    std::vector<int> levels(24, 0);
    std::vector<double> values(24, 0.0);
    levels[0] = levels[1] = levels[6] = 1;   // a yellow blob of three cells in the corner
    values[1] = 5.0;
    levels[16] = 2;                          // a separate orange cell
    levels[17] = 1;
    values[16] = 3.0;
    const auto regions = warning_regions(levels, values, g);
    require(regions.size() == 3, "areas of one level each: the orange cell and its yellow neighbour are separate");
    require(regions[0].level == 2 && regions[0].cells.size() == 1 && regions[0].peak_cell == 16, "the orange area first");
    require(regions[1].level == 1 && regions[1].cells.size() == 3 && regions[1].peak_cell == 1, "then the larger yellow area, peaking at its largest value");
    require(regions[2].level == 1 && regions[2].cells.size() == 1 && regions[2].peak_cell == 17, "then the small one");
    require_near(regions[1].area_km2, 3.0 * 1.0e4, "areas in km2", 1e-6);

    const std::vector<double> truth{1.0, 2.0, 3.0, 4.0}, clim{2.5, 2.5, 2.5, 2.5}, zero(4, 0.0);
    const auto perfect = score_forecast(truth, zero, truth, clim, clim);
    require(perfect.rmse == 0.0 && perfect.skill_vs_climatology == 1.0 && std::abs(perfect.anomaly_correlation - 1.0) < 1e-12, "a perfect forecast");
    const auto climatology = score_forecast(clim, zero, truth, clim, truth);
    require(climatology.skill_vs_climatology == 0.0 && climatology.persistence_rmse == 0.0, "climatology has no skill");
    const std::vector<double> shifted{1.5, 2.5, 3.5, 4.5}, spread(4, 0.5);
    const auto s = score_forecast(shifted, spread, truth, clim, clim);
    require_near(s.rmse, 0.5, "a biased forecast", 1e-12);
    require_near(s.spread, 0.5, "its spread", 1e-12);
    require(s.anomaly_correlation > 0.9 && s.anomaly_correlation < 1.0, "the (uncentred) anomaly correlation counts a bias a little");
    const auto b = brier_score({1.0, 0.0, 1.0, 0.0}, {1, 0, 1, 0});
    require(b[0] == 0.0 && b[1] == 1.0, "a perfect probability forecast");
    const auto c = brier_score({0.5, 0.5, 0.5, 0.5}, {1, 0, 1, 0});
    require_near(c[0], 0.25, "the climatological frequency", 1e-12);
    require_near(c[1], 0.0, "has no skill", 1e-12);
    require_rejected([] { brier_score({0.5}, {1, 0}); }, "mismatched Brier arrays");
}

void climate() {
    require_near(methane_forcing_w_m2(1866.0), 0.50, "methane forcing of 2019 (about 0.5 W/m2, Myhre 1998)", 0.03);
    require_near(nitrous_oxide_forcing_w_m2(332.0), 0.20, "nitrous oxide forcing of 2019", 0.02);
    require_near(methane_forcing_w_m2(722.0), 0.0, "no forcing at the reference", 1e-12);
    require(ghg_band_overlap(1866.0, 332.0) > ghg_band_overlap(722.0, 270.0), "the band overlap grows with both gases");
    require_rejected([] { methane_forcing_w_m2(-1.0); }, "a negative concentration");
    const std::vector<std::array<double, 2>> points{{1850.0, 1.0}, {1950.0, 3.0}, {2000.0, 2.0}};
    require(interpolate_points(points, 1800.0) == 1.0 && interpolate_points(points, 2100.0) == 2.0, "constant beyond the ends");
    require_near(interpolate_points(points, 1900.0), 2.0, "linear in between", 1e-12);
    require_near(interpolate_points(points, 1975.0), 2.5, "on the second segment", 1e-12);

    const FeedbackSet feedbacks;
    validate_feedbacks(feedbacks);
    require_near(net_feedback_w_m2_k(feedbacks), 1.15, "AR6 central net feedback", 1e-9);
    require_rejected([] { FeedbackSet f; f.clouds_w_m2_k = 1.9; validate_feedbacks(f); }, "a runaway feedback");
    const auto shares = feedback_shares(3.0, feedbacks);
    require_near(shares.no_feedback_k + shares.water_vapour_lapse_rate_k + shares.surface_albedo_k + shares.clouds_k + shares.other_k, 3.0, "the shares add up to the warming", 1e-9);
    require(shares.water_vapour_lapse_rate_k > shares.clouds_k && shares.clouds_k > 0.0, "water vapour amplifies most");

    TwoLayerClimate climate;
    TwoLayerState state;
    for (int year = 0; year < 3000; ++year) state = step_two_layer(state, 3.71, 1.0, climate);
    require_near(state.surface_k, 3.71 / 1.15, "equilibrium F / lambda", 0.02);
    require_near(state.deep_k, state.surface_k, "the deep ocean catches up", 0.05);
    const double tcr = transient_climate_response_k(climate);
    require(tcr > 1.6 && tcr < 2.4, "TCR within the AR6 likely range");
    require(tcr < 3.71 / 1.15, "the transient response lags the equilibrium");
    require_rejected([] { step_two_layer({}, 1.0, 1.0, TwoLayerClimate{0.0, 100.0, 0.7, 1.0}); }, "zero heat capacity");

    require_near(heat_extreme_factor(0.0, 1.0, 50.0), 1.0, "no warming, no change", 1e-9);
    const double f50 = heat_extreme_factor(1.1, 0.9, 50.0), f10 = heat_extreme_factor(1.1, 0.9, 10.0);
    require(f50 > f10 && f10 > 1.0, "rarer extremes increase by a larger factor");
    require(heat_extreme_factor(2.0, 0.9, 50.0) > f50, "and more with more warming");
    require_near(heavy_rain_frequency_factor(2.0, 1.3), 1.69, "heavy rain x1.3 per K", 1e-12);
    require_near(september_sea_ice_mkm2(1.0, 4.5, 1.0, 1.5), 4.5, "the reference sea ice", 1e-12);
    require(september_sea_ice_mkm2(5.0, 4.5, 1.0, 1.5) == 0.0, "an ice-free September");
    require_near(standard_normal_cdf(0.0), 0.5, "Phi(0)", 1e-15);
    require_near(standard_normal_cdf(1.96), 0.975, "Phi(1.96)", 1e-4);

    const WarmingPattern pattern;
    require(warming_pattern_factor(80.0, false, pattern) > 2.0 * warming_pattern_factor(10.0, false, pattern), "Arctic amplification");
    require_near(warming_pattern_factor(20.0, true, pattern) / warming_pattern_factor(20.0, false, pattern), pattern.land_factor, "land warms faster", 1e-12);
    require(warming_pattern_factor(-55.0, false, pattern) < warming_pattern_factor(-30.0, false, pattern), "the Southern Ocean lags");

    const double uptake = ocean_co2_uptake_mol_m2_s(283.0, 8.0, 20.0);
    require(uptake > 0.0 && ocean_co2_uptake_mol_m2_s(283.0, 8.0, -20.0) < 0.0, "the sea takes up CO2 where the air holds more");
    const double per_year = uptake * 3.15576e7;
    require(per_year > 0.5 && per_year < 5.0, "a few mol per m2 per year for 20 uatm and 8 m/s");
    require(ocean_co2_uptake_mol_m2_s(283.0, 12.0, 20.0) > uptake, "more wind, more exchange");

    const std::size_t nx = 36, ny = 18;
    std::vector<double> source(nx * ny, 0.0);
    for (std::size_t i = 0; i < nx; ++i) source[13 * nx + i] = 1.0e-8;   // a northern mid-latitude belt
    const auto anomaly = global_steady_anomaly(source, nx, ny, 1.0e6, 3.0e7, 2000);
    double north = 0.0, south = 0.0, mean = 0.0, weight = 0.0;
    for (std::size_t j = 0; j < ny; ++j) {
        const double w = std::cos((-90.0 + (j + 0.5) * 10.0) * 3.14159265358979323846 / 180.0);
        for (std::size_t i = 0; i < nx; ++i) {
            mean += w * anomaly[j * nx + i];
            weight += w;
            (j >= ny / 2 ? north : south) += anomaly[j * nx + i];
        }
    }
    require(std::abs(mean / weight) < 1e-9, "an anomaly about the global mean");
    require(north > 0.0 && south < 0.0, "a northern source makes the north richer");
    require(std::abs(anomaly[13 * nx] - anomaly[13 * nx + 20]) < 1e-9, "a zonal source gives a zonal anomaly");
}

}  // namespace

int main() {
    geography();
    synoptic();
    surface();
    hazards();
    warnings_and_scores();
    climate();
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "earth system checks passed\n";
    return 0;
}
