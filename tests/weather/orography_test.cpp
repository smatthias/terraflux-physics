#include "terraflux/weather/atmospheric_column.hpp"
#include "terraflux/weather/coupled_wind.hpp"
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/pressure_wind.hpp"

#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

constexpr std::size_t kNx = 30, kNy = 8;
constexpr double kSpacing = 50000.0;

TransportGrid2D make_grid() {
    TransportGrid2D grid;
    grid.nx = kNx;
    grid.ny = kNy;
    grid.spacing_east_m = kSpacing;
    grid.spacing_north_m = kSpacing;
    grid.origin_east_m = -0.5 * kNx * kSpacing;
    grid.origin_north_m = -0.5 * kNy * kSpacing;
    grid.geographic_anchor_longitude_deg = 10.0;
    grid.geographic_anchor_latitude_deg = 47.0;
    return grid;
}

double east_of(std::size_t i) { return -0.5 * kNx * kSpacing + (i % kNx + 0.5) * kSpacing; }
double north_of(std::size_t i) { return -0.5 * kNy * kSpacing + (i / kNx + 0.5) * kSpacing; }

// Gaussian ridge: north-south (axis along y) when east_west is false.
std::vector<double> ridge(double height, double half_width, bool east_west) {
    std::vector<double> h(kNx * kNy);
    for (std::size_t i = 0; i < h.size(); ++i) {
        const double d = east_west ? north_of(i) : east_of(i);
        h[i] = height * std::exp(-(d / half_width) * (d / half_width));
    }
    return h;
}

// Westerly geostrophic flow of speed U over a given terrain, uniform 10 C air at 90 % RH.
CoupledWindConfig make_config(double hours, const std::vector<double>& elevation) {
    CoupledWindConfig c;
    c.time = {"2026-01-10T00:00:00Z", 300.0, static_cast<std::size_t>(hours * 12)};
    c.grid = make_grid();
    c.atmosphere.air_density_kg_m3 = 1.2;
    c.atmosphere.surface_friction_coefficient_s = 2.0e-5;
    c.atmosphere.coriolis_mode = "f_plane";
    c.diffusion = {2000.0, 2000.0, 2000.0};
    c.density_mode = "constant";
    c.orography.enabled = true;
    c.orography.elevation_m = elevation;
    c.orography.obstacle_radius_m = 250000.0;
    return c;
}

CoupledWindState westerly_state(const CoupledWindConfig& c, double speed, double temperature = 283.0, double rh = 0.9) {
    const double f = 2.0 * 7.2921159e-5 * std::sin(47.0 * M_PI / 180.0);
    CoupledWindState s;
    const std::size_t count = kNx * kNy;
    s.pressure_pa.resize(count);
    s.air_temperature_k.assign(count, temperature);
    s.specific_humidity_kg_kg.assign(count, rh * saturation_specific_humidity_kg_kg(temperature, 100000.0));
    for (std::size_t i = 0; i < count; ++i) s.pressure_pa[i] = 100000.0 - 1.2 * f * speed * north_of(i);
    PressureWindConfig wind;
    wind.time = c.time;
    wind.grid = c.grid;
    wind.atmosphere = c.atmosphere;
    const auto balanced = balanced_wind(s.pressure_pa, wind);
    s.wind_east_m_s = balanced.east_m_s;
    s.wind_north_m_s = balanced.north_m_s;
    return s;
}

std::size_t cell(std::size_t x, std::size_t y = 4) { return y * kNx + x; }

// 1. Adiabatic rates: a single step of unsaturated upslope flow cools at the
//    dry rate g/c_p exactly; saturated ascent cools at the moist rate with
//    condensation counted as precipitation; descent warms at the dry rate.
void slope_flow_follows_the_adiabatic_rates() {
    std::vector<double> slope_terrain(kNx * kNy);
    for (std::size_t i = 0; i < slope_terrain.size(); ++i) slope_terrain[i] = 0.002 * (east_of(i) + 0.5 * kNx * kSpacing);  // 0.2 % slope rising east
    auto c = make_config(1.0 / 12.0, slope_terrain);  // one 300 s step
    c.atmosphere.coriolis_mode = "none";
    c.atmosphere.surface_friction_coefficient_s = 0.0;
    c.orography.blocking_time_scale_s = 1.0e12;  // isolate the thermodynamics from blocking
    c.diffusion = {0.0, 0.0, 0.0};
    CoupledWindState s;
    const std::size_t count = kNx * kNy;
    s.pressure_pa.assign(count, 100000.0);
    s.air_temperature_k.assign(count, 283.0);
    s.specific_humidity_kg_kg.assign(count, 0.002);
    s.wind_east_m_s.assign(count, 10.0);
    s.wind_north_m_s.assign(count, 0.0);
    const auto dry = advance_coupled_wind(s, c);
    const double w = 10.0 * 0.002;  // 0.02 m/s upslope
    require_near(dry.air_temperature_k[cell(15)], 283.0 - dry_adiabatic_lapse_rate_k_m() * w * 300.0, "dry ascent cools at g/c_p", 1.0e-9);
    require_near(dry_adiabatic_lapse_rate_k_m() * 100.0, 0.976, "the dry rate is about 1 K per 100 m", 0.002);
    require(dry.orographic_precipitation_mm[cell(15)] == 0.0, "no condensation in dry air");

    const double p_s = 100000.0 * std::exp(-9.80665 * slope_terrain[cell(15)] / (287.05 * 283.0));
    s.specific_humidity_kg_kg.assign(count, saturation_specific_humidity_kg_kg(283.0, p_s));
    const auto moist = advance_coupled_wind(s, c);
    const double cooling = 283.0 - moist.air_temperature_k[cell(15)];
    require_near(cooling / (w * 300.0), moist_adiabatic_lapse_rate_k_m(283.0, p_s), "saturated ascent cools at the moist adiabatic rate", 0.05e-3);
    require(moist_adiabatic_lapse_rate_k_m(283.0, p_s) * 100.0 > 0.45 && moist_adiabatic_lapse_rate_k_m(283.0, p_s) * 100.0 < 0.65, "about 0.5 K per 100 m at 10 C (0.6 near 0-5 C)");
    require(moist.orographic_precipitation_mm[cell(15)] > 0.0 && moist.specific_humidity_kg_kg[cell(15)] < s.specific_humidity_kg_kg[cell(15)], "condensate precipitates and dries the air");

    s.wind_east_m_s.assign(count, -10.0);  // downslope
    s.specific_humidity_kg_kg.assign(count, 0.002);
    const auto descent = advance_coupled_wind(s, c);
    require_near(descent.air_temperature_k[cell(15)], 283.0 + dry_adiabatic_lapse_rate_k_m() * w * 300.0, "descent warms at the dry rate", 1.0e-9);
}

// 2. Foehn across a north-south ridge: precipitation on the windward slope,
//    moist-rate cooling to the crest, dry-rate warming down the lee, and a
//    lee foot several kelvin warmer and much drier than the windward foot.
void foehn_warms_and_dries_the_lee() {
    const auto c = make_config(12.0, ridge(2000.0, 80000.0, false));
    const auto s = advance_coupled_wind(westerly_state(c, 20.0), c);
    const auto d = diagnose_orography(s, c);
    const std::size_t windward = cell(9), crest = cell(14), lee_slope = cell(16), lee = cell(20);
    require(d.upslope_lifting_m_s[cell(13)] > 0.2 && d.upslope_lifting_m_s[lee_slope] < -0.2, "ascent on the windward slope, descent on the lee");
    require(s.orographic_precipitation_mm[cell(13)] > 10.0 && s.orographic_precipitation_mm[lee_slope] == 0.0, "orographic precipitation only on the windward slope");
    const double crest_cooling_per_100m = (s.air_temperature_k[windward] - s.air_temperature_k[crest]) / c.orography.elevation_m[crest] * 100.0;
    require(crest_cooling_per_100m > 0.5 && crest_cooling_per_100m < 0.75, "windward cooling near the moist rate (0.5-0.75 K per 100 m)");
    const double warming = s.air_temperature_k[lee] - s.air_temperature_k[windward];
    require(warming > 4.0 && warming < 9.0, "the lee foot is 4-9 K warmer than the windward foot (textbook foehn: about 6 K for a 2 km ridge)");
    const double rh_lee = relative_humidity_fraction(s.air_temperature_k[lee], s.specific_humidity_kg_kg[lee], d.surface_pressure_pa[lee]);
    require(rh_lee < 0.5, "foehn air is dry");
    require(d.foehn_cells > 0 && d.foehn[lee_slope] == 1 && d.foehn_warming_k[lee_slope] > 1.0, "foehn flagged on the lee slope");
    require_near(d.ridge_orientation_deg[cell(12)], 0.0, "a north-south ridge has contours along north", 1.0e-9);
    require_near(d.surface_pressure_pa[crest], s.pressure_pa[crest] * std::exp(-9.80665 * c.orography.elevation_m[crest] / (287.05 * s.air_temperature_k[crest])), "hydrostatic surface pressure at the crest", 1.0e-6);
}

// 3. Orientation: the same westerlies are blocked and deflected into a
//    barrier jet by a north-south range (Fr < 1) but flow untouched along an
//    east-west range; a fast flow (Fr >= 1) crosses a lower ridge unblocked.
void ridge_orientation_controls_blocking() {
    const auto blocked_config = make_config(12.0, ridge(2500.0, 80000.0, false));
    const auto blocked = advance_coupled_wind(westerly_state(blocked_config, 10.0), blocked_config);
    const auto db = diagnose_orography(blocked, blocked_config);
    const std::size_t foot = cell(10);
    require(db.froude_number[foot] < 1.0 && db.blocked_fraction[foot] > 0.2, "10 m/s against 2.5 km is a blocked regime");
    require(blocked.wind_east_m_s[foot] < 0.85 * 9.7, "the cross-barrier wind at the windward foot is reduced");
    require(db.along_barrier_wind_m_s[foot] > 8.0, "the blocked air is deflected along the range (barrier jet)");
    require_near(db.ridge_orientation_deg[foot], 0.0, "north-south range", 1.0e-9);

    const auto parallel_config = make_config(12.0, ridge(2500.0, 80000.0, true));
    const auto parallel = advance_coupled_wind(westerly_state(parallel_config, 10.0), parallel_config);
    const auto dp = diagnose_orography(parallel, parallel_config);
    require_near(parallel.wind_east_m_s[foot], 9.7, "westerlies run along an east-west range unhindered", 0.3);
    require(std::abs(dp.cross_barrier_wind_m_s[cell(15, 2)]) < 3.0 && dp.blocked_fraction[cell(15, 2)] < 0.05, "no blocking along the range");
    require_near(dp.ridge_orientation_deg[cell(15, 2)], 90.0, "east-west range", 1.0e-9);
    require(parallel.orographic_precipitation_mm[cell(15, 4)] < 1.0, "no orographic precipitation without cross-barrier flow");

    const auto fast_config = make_config(6.0, ridge(1500.0, 80000.0, false));
    const auto fast = advance_coupled_wind(westerly_state(fast_config, 20.0), fast_config);
    const auto df = diagnose_orography(fast, fast_config);
    require(df.froude_number[foot] >= 1.0 && df.blocked_fraction[foot] == 0.0 && fast.wind_east_m_s[foot] > 18.0, "Fr >= 1: the flow goes over the ridge");
}

// 4. Cold-air pooling: calm clear night, a basin cools faster than the
//    surrounding plateau and is flagged; the next day's heating erodes it.
void cold_air_pools_in_valleys_at_night() {
    std::vector<double> basin(kNx * kNy, 800.0);
    for (std::size_t i = 0; i < basin.size(); ++i) {
        const double r = std::hypot(east_of(i) - 25000.0, north_of(i) - 25000.0);
        basin[i] = 800.0 - 500.0 * std::exp(-(r / 90000.0) * (r / 90000.0));
    }
    auto c = make_config(10.0, basin);
    c.orography.nocturnal_cooling_k_per_s = 0.5 / 3600.0;
    c.orography.daytime_heating_k_per_s = 0.8 / 3600.0;
    c.orography.diurnal_phase_seconds = 12.0 * 3600.0;  // sunrise after the 10 h night... 12 h later
    c.orography.valley_pooling_per_km = 1.5;
    CoupledWindState s;
    const std::size_t count = kNx * kNy;
    s.pressure_pa.assign(count, 100000.0);
    s.air_temperature_k.assign(count, 280.0);
    s.specific_humidity_kg_kg.assign(count, 0.003);
    s.wind_east_m_s.assign(count, 0.0);
    s.wind_north_m_s.assign(count, 0.0);
    const auto night = advance_coupled_wind(s, c);
    const auto d = diagnose_orography(night, c);
    const std::size_t floor = cell(15, 4), plateau = cell(3, 1);
    require(d.valley_depth_m[floor] > 150.0 && d.valley_depth_m[plateau] < 50.0, "the basin floor lies below its surroundings");
    require(night.air_temperature_k[floor] < night.air_temperature_k[plateau] - 1.0, "the basin cools more than the plateau");
    require(d.cold_pool[floor] == 1 && d.cold_pool_k[floor] > 1.0 && d.cold_pool[plateau] == 0 && d.cold_pool_cells > 0, "a cold pool is flagged in the basin only");
    // Plateau cooling = integral of 0.5 K/h * sin(pi t / 12 h) over the 10 h run.
    require_near(night.air_temperature_k[plateau], 280.0 - 0.5 * (12.0 / M_PI) * (1.0 - std::cos(10.0 * M_PI / 12.0)), "plateau cooling follows the half-sine night", 0.1);
    c.time.steps = 12 * 12;
    c.orography.diurnal_phase_seconds = night.elapsed_seconds;  // sunrise now: a 12 h day
    const auto day = advance_coupled_wind(night, c);
    const auto dd = diagnose_orography(day, c);
    require(dd.cold_pool_k[floor] < d.cold_pool_k[floor], "daytime heating erodes the cold pool");
}

// 5. Diurnal tendency helper and rejections.
void helpers_and_rejections() {
    OrographyConfig o;
    o.daytime_heating_k_per_s = 1.0e-4;
    o.nocturnal_cooling_k_per_s = 2.0e-4;
    require_near(diurnal_surface_tendency_k_s(6.0 * 3600.0, o), 1.0e-4, "peak heating at noon", 1.0e-12);
    require_near(diurnal_surface_tendency_k_s(18.0 * 3600.0, o), -2.0e-4, "peak cooling at midnight", 1.0e-12);
    require_near(diurnal_surface_tendency_k_s(0.0, o), 0.0, "zero at sunrise", 1.0e-12);
    const auto elevation = ridge(1000.0, 80000.0, false);
    require_rejected([&] { auto c = make_config(1.0, elevation); c.orography.elevation_m.pop_back(); validate_coupled_wind_config(c); }, "elevation size mismatch must be rejected");
    require_rejected([&] { auto c = make_config(1.0, elevation); c.orography.blocking_time_scale_s = 100.0; validate_coupled_wind_config(c); }, "timestep above the blocking time scale must be rejected");
    require_rejected([&] { auto c = make_config(1.0, elevation); c.orography.brunt_vaisala_s = 0.0; validate_coupled_wind_config(c); }, "zero stability frequency must be rejected");
    require_rejected([&] { auto c = make_config(1.0, elevation); c.orography.valley_pooling_per_km = -1.0; validate_coupled_wind_config(c); }, "negative pooling must be rejected");
    require_rejected([&] { auto c = make_config(1.0, elevation); c.orography.enabled = false; (void)diagnose_orography(westerly_state(c, 5.0), c); }, "diagnostics need orography enabled");
}

// 6. Regression: the foehn scenario pinned at a few cells.
void regression_scenario_is_pinned() {
    const auto c = make_config(12.0, ridge(2000.0, 80000.0, false));
    const auto s = advance_coupled_wind(westerly_state(c, 20.0), c);
    const auto d = diagnose_orography(s, c);
    require_near(s.air_temperature_k[cell(9)], 282.996173, "regression: windward foot temperature", 1.0e-3);
    require_near(s.air_temperature_k[cell(14)], 271.626474, "regression: crest temperature", 1.0e-3);
    require_near(s.air_temperature_k[cell(20)], 289.392461, "regression: lee foot temperature", 1.0e-3);
    require_near(s.orographic_precipitation_mm[cell(13)], 37.521162, "regression: windward precipitation", 1.0e-3);
    require_near(d.upslope_lifting_m_s[cell(13)], 0.316829, "regression: upslope lifting", 1.0e-5);
    require_near(relative_humidity_fraction(s.air_temperature_k[cell(20)], s.specific_humidity_kg_kg[cell(20)], d.surface_pressure_pa[cell(20)]), 0.374144, "regression: lee humidity", 1.0e-5);
}

}  // namespace

int main() {
    try {
        slope_flow_follows_the_adiabatic_rates();
        foehn_warms_and_dries_the_lee();
        ridge_orientation_controls_blocking();
        cold_air_pools_in_valleys_at_night();
        helpers_and_rejections();
        regression_scenario_is_pinned();
    } catch (const std::exception& error) {
        std::cerr << "orography test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "orography cases passed\n";
    return 0;
}
