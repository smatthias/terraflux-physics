// Land ice sheets, sea ice, the overturning box model and marine indicators (weather.polar_melt).
#include "terraflux/weather/ice_sheet.hpp"
#include "terraflux/weather/marine_indicators.hpp"
#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/ocean_circulation.hpp"
#include "terraflux/weather/overturning_box.hpp"
#include "terraflux/weather/sea_ice.hpp"

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
constexpr double kDay = 86400.0, kYear = 365.0 * 86400.0;

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
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

// ---- Land ice --------------------------------------------------------------------------------------

// A square of land inside a one-cell sea rim.
IceSheetConfig island(std::size_t n, double spacing, double bed = 0.0) {
    IceSheetConfig c;
    c.grid.nx = n; c.grid.ny = n;
    c.grid.origin_east_m = 0.0; c.grid.origin_north_m = 0.0;
    c.grid.spacing_east_m = spacing; c.grid.spacing_north_m = spacing;
    c.grid.geographic_anchor_longitude_deg = -40.0; c.grid.geographic_anchor_latitude_deg = 72.0;
    c.ocean.assign(n * n, 0);
    c.bed_elevation_m.assign(n * n, bed);
    for (std::size_t i = 0; i < n * n; ++i) {
        const std::size_t x = i % n, y = i / n;
        if (x == 0 || y == 0 || x + 1 == n || y + 1 == n) { c.ocean[i] = 1; c.bed_elevation_m[i] = -500.0; }
    }
    return c;
}

IceSheetForcing climate(const IceSheetConfig& c, double temperature_k, double precipitation_m_yr) {
    IceSheetForcing f;
    const std::size_t count = c.grid.nx * c.grid.ny;
    f.air_temperature_k.assign(count, temperature_k);
    f.precipitation_m_s.assign(count, precipitation_m_yr / kYear);
    return f;
}

void positive_degree_days() {
    require_near(expected_positive_degrees_k(3.0, 1.0e-9), 3.0, "no variability: the mean above zero", 1.0e-9);
    require_near(expected_positive_degrees_k(-3.0, 1.0e-9), 0.0, "no variability: nothing below zero", 1.0e-9);
    require_near(expected_positive_degrees_k(0.0, 5.0), 5.0 / std::sqrt(2.0 * kPi), "mean at zero: sigma / sqrt(2 pi)", 1.0e-12);
    // Against direct quadrature of E[max(0, T)].
    for (double mu : {-12.0, -4.0, 0.0, 2.5, 9.0}) {
        double integral = 0.0;
        const double sigma = 4.5, step = 0.001;
        for (double t = step / 2.0; t < mu + 12.0 * sigma; t += step) integral += t * std::exp(-0.5 * std::pow((t - mu) / sigma, 2.0)) / (sigma * std::sqrt(2.0 * kPi)) * step;
        require_near(expected_positive_degrees_k(mu, sigma), integral, "positive degrees match quadrature", 1.0e-5);
    }
    require(expected_positive_degrees_k(-10.0, 5.0) > 0.0, "weather variability brings melt days into a cold mean");
}

void dome_and_routing() {
    auto c = island(21, 50000.0);
    const auto h = vialov_dome(c, 525000.0, 525000.0, 400000.0, 2500.0);
    require_near(*std::max_element(h.begin(), h.end()), 2500.0 * std::pow(1.0 - std::pow(25000.0 * std::sqrt(2.0) / 400000.0 * 0.0, 4.0 / 3.0), 3.0 / 8.0), "dome centre", 1.0);
    require(h[10 * 21 + 10] > h[10 * 21 + 14] && h[10 * 21 + 14] > h[10 * 21 + 17], "thickness falls toward the margin");
    require(h[10 * 21 + 19] == 0.0 && h[0] == 0.0, "no ice beyond the radius or at sea");
    const auto sea = nearest_sea_cells(c.ocean, 21, 21);
    require(sea[0] == 0 && sea[10 * 21 + 1] == 10 * 21, "sea cells map to themselves, the coast to the adjacent sea");
    require(c.ocean[sea[10 * 21 + 10]], "every cell drains to a sea cell");
}

void surface_mass_balance() {
    auto c = island(7, 20000.0);
    c.physics.rate_factor_pa3_s = 0.0;   // no flow: the surface balance alone
    std::vector<double> h0(49, 0.0);
    for (std::size_t i = 0; i < 49; ++i) if (!c.ocean[i]) h0[i] = 1000.0;
    auto s = initialize_ice_sheet(c, h0);
    const double v0 = s.volume_m3;
    // Cold: all precipitation is snow, nothing melts (sigma tiny so no stray degree days).
    c.physics.pdd_standard_deviation_k = 0.1;
    auto cold = advance_ice_sheet(s, climate(c, 240.0, 0.5), c, kYear);
    const double land = 25.0 * 20000.0 * 20000.0;
    require_near(cold.volume_m3 - v0, 0.5 * 1000.0 / 917.0 * land, "snowfall accumulates as ice", 1.0);
    require(cold.runoff_m3_s == 0.0 && cold.melt_m3_s == 0.0 && std::abs(cold.mass_balance_error_m3) < 1.0, "no melt in the cold");
    // Warm, dry: bare ice melts at the ice degree-day factor.
    const double t_sl = 283.15;
    auto warm = advance_ice_sheet(s, climate(c, t_sl, 0.0), c, 10.0 * kDay);
    const std::size_t centre = 3 * 7 + 3;
    const double ts = t_sl - c.physics.lapse_rate_k_m * 1000.0;
    const double expected_we = c.physics.degree_day_factor_ice_m_k_s * expected_positive_degrees_k(ts - 273.15, 0.1) * 10.0 * kDay;
    require_near(warm.melt_m_s[centre] * 10.0 * kDay, expected_we * 1000.0 / 917.0, "ice melts at the ice factor", 1.0e-9);
    require_near(warm.runoff_m3_s * 10.0 * kDay, expected_we * land, "the melt runs off", 1.0e-3 * expected_we * land);
    require(std::abs(warm.mass_balance_error_m3) < 1.0e-6 * v0, "ice budget");
    double delivered = 0.0;
    for (std::size_t i = 0; i < 49; ++i) { if (!c.ocean[i]) require(warm.runoff_to_sea_m3_s[i] == 0.0, "only the sea receives runoff"); delivered += warm.runoff_to_sea_m3_s[i]; }
    require_near(delivered, warm.runoff_m3_s, "all runoff reaches the sea", 1.0e-9 * delivered);
    // Snow melts first, at the snow factor, and partly refreezes.
    auto snowy = s;
    for (std::size_t i = 0; i < 49; ++i) if (!c.ocean[i]) snowy.snow_m[i] = 0.5;
    auto thaw = advance_ice_sheet(snowy, climate(c, t_sl, 0.0), c, 2.0 * kDay);
    const double snow_potential = c.physics.degree_day_factor_snow_m_k_s * expected_positive_degrees_k(ts - 273.15, 0.1) * 2.0 * kDay * 1000.0 / 917.0;
    require_near(thaw.melt_m_s[centre] * 2.0 * kDay, snow_potential, "snow melts at the snow factor", 1.0e-9);
    require_near(thaw.refreeze_m_s[centre], 0.6 * thaw.melt_m_s[centre], "60 % of the snowmelt refreezes", 1.0e-15);
    require(std::abs(thaw.mass_balance_error_m3) < 1.0e-6 * v0, "budget with refreezing");
    // Melt-elevation feedback: a lower surface melts more at the same sea-level temperature.
    std::vector<double> uneven = h0;
    uneven[centre] = 2500.0;
    auto su = initialize_ice_sheet(c, uneven);
    auto melt = advance_ice_sheet(su, climate(c, 278.15, 0.0), c, 10.0 * kDay);
    require(melt.melt_m_s[centre] < melt.melt_m_s[centre + 1], "the high summit melts less than the lower ice around it");
    require(melt.surface_air_temperature_k[centre] < melt.surface_air_temperature_k[centre + 1] - 9.0, "lapse rate");
}

void ice_flow_and_calving() {
    auto c = island(25, 40000.0, 100.0);
    auto h = vialov_dome(c, 500000.0, 500000.0, 320000.0, 2800.0);
    auto s = initialize_ice_sheet(c, h);
    const double v0 = s.volume_m3, peak0 = *std::max_element(h.begin(), h.end());
    auto f = climate(c, 230.0, 0.0);
    c.physics.pdd_standard_deviation_k = 0.1;
    for (int k = 0; k < 100; ++k) {
        s = advance_ice_sheet(s, f, c, 10.0 * kYear);
        require(std::all_of(s.thickness_m.begin(), s.thickness_m.end(), [](double v) { return v >= 0.0; }), "no negative thickness");
        require(std::abs(s.mass_balance_error_m3) < 1.0e-9 * v0, "flow conserves ice");
    }
    const double peak = *std::max_element(s.thickness_m.begin(), s.thickness_m.end());
    require(peak < peak0 - 50.0, "the dome spreads and thins without accumulation");
    require(s.area_m2 > initialize_ice_sheet(c, h).area_m2, "the ice spreads over more land");
    // Once the ice reaches the coast it calves into the sea.
    double calved = 0.0;
    for (int k = 0; k < 500; ++k) { s = advance_ice_sheet(s, f, c, 10.0 * kYear); calved += s.calving_m3_s * 10.0 * kYear; }
    require(calved > 0.0, "ice reaching the sea calves");
    require_near(v0 - s.volume_m3, calved * 1000.0 / 917.0, "the volume lost is the calved ice", 1.0e-6 * v0);
    require_near(s.cumulative_sea_level_rise_m, calved / c.physics.global_ocean_area_m2, "sea-level rise from the calved ice", 1.0e-9);
}

void frontal_ablation() {
    IceSheetPhysics p;
    require_near(frontal_melt_rate_m_s(3.0, 2.0, p) * 86400.0, (3.0e-4 * 400.0 * std::pow(2.0, 0.39) + 0.15) * std::pow(3.0, 1.18), "Rignot et al. (2016) rate", 1.0e-12);
    require(frontal_melt_rate_m_s(0.0, 5.0, p) == 0.0 && frontal_melt_rate_m_s(4.0, 5.0, p) > frontal_melt_rate_m_s(2.0, 5.0, p) && frontal_melt_rate_m_s(3.0, 10.0, p) > frontal_melt_rate_m_s(3.0, 0.0, p),
            "a warmer ocean and more subglacial discharge melt the front faster");
    auto c = island(7, 10000.0);
    c.physics.rate_factor_pa3_s = 0.0;
    c.physics.pdd_standard_deviation_k = 0.1;
    std::vector<double> h(49, 0.0);
    const std::size_t margin = 3 * 7 + 1;   // next to the western sea cell 3 * 7
    h[margin] = 500.0;
    auto s = initialize_ice_sheet(c, h);
    auto f = climate(c, 250.0, 0.0);
    f.ocean_thermal_forcing_k.assign(49, 0.0);
    auto none = advance_ice_sheet(s, f, c, kDay);
    require(none.frontal_ablation_m3_s == 0.0, "no thermal forcing, no frontal melt");
    f.ocean_thermal_forcing_k[3 * 7] = 4.0;
    auto warm = advance_ice_sheet(s, f, c, kDay);
    const double rate = frontal_melt_rate_m_s(4.0, 0.0, c.physics);
    require_near(warm.frontal_ablation_m_s[margin], rate * 500.0 / 10000.0, "the face retreats: q_m H / dx of thickness", 1.0e-15);
    require_near(warm.frontal_melt_to_sea_m3_s[3 * 7], rate * 500.0 * 10000.0 * 917.0 / 1000.0, "meltwater enters the adjacent sea cell", 1.0e-9);
    require(std::abs(warm.mass_balance_error_m3) < 1.0e-3, "budget with frontal ablation");
}

// ---- Sea ice ---------------------------------------------------------------------------------------

AirState arctic_air(double temperature_k, double shortwave, double wind = 5.0) {
    AirState a;
    a.temperature_k = temperature_k;
    a.pressure_pa = 101325.0;
    a.specific_humidity_kg_kg = 0.85 * saturation_specific_humidity_kg_kg(temperature_k, a.pressure_pa);
    a.wind_speed_m_s = wind;
    a.cloud_fraction = 0.6;
    a.shortwave_down_w_m2 = shortwave;
    return a;
}

void freezing_point_and_surface_balance() {
    require_near(seawater_freezing_point_k(35.0), 273.15 - 1.89, "sea water freezes near -1.9 degC", 1.0e-9);
    require_near(seawater_freezing_point_k(0.0), 273.15, "fresh water at 0 degC", 0.0);
    require_near(seawater_freezing_point_k(35.0, 100.0), 273.15 - 1.89 - 0.0753, "pressure lowers the freezing point", 1.0e-9);
    SeaIcePhysics p;
    OceanAirSeaConfig bulk;
    const double tf = seawater_freezing_point_k(32.0);
    const auto thick = sea_ice_surface_balance(2.0, tf, arctic_air(243.15, 0.0), bulk, p);
    require(thick.surface_temperature_k > 243.15 - 5.0 && thick.surface_temperature_k < tf, "the ice surface sits between the air and the water");
    require_near(thick.atmosphere_w_m2 + thick.conductive_w_m2, 0.0, "surface balance closes", 1.0e-6);
    require_near(thick.conductive_w_m2, p.conductivity_w_m_k * (tf - thick.surface_temperature_k) / 2.0, "conduction through the ice", 1.0e-9);
    const auto thin = sea_ice_surface_balance(0.2, tf, arctic_air(243.15, 0.0), bulk, p);
    require(thin.surface_temperature_k > thick.surface_temperature_k && thin.conductive_w_m2 > 2.0 * thick.conductive_w_m2, "thin ice is warmer on top and loses heat faster");
    const auto summer = sea_ice_surface_balance(2.0, tf, arctic_air(275.15, 350.0), bulk, p);
    require(summer.surface_temperature_k == 273.15 && summer.surface_melt_w_m2 > 0.0, "in summer the surface melts");
    require_near(summer.albedo, p.albedo_melting, "melting ice is darker", 1.0e-12);
    require_near(thick.albedo, p.albedo_cold, "cold ice is bright", 1.0e-12);
    require(ocean_ice_heat_flux_w_m2(tf + 1.0, tf, 1025.0, 3990.0, p) > 200.0, "a kelvin of warm water under the ice is hundreds of W/m2");
}

ArcticColumnConfig column_config() {
    ArcticColumnConfig c;
    c.deep_ocean_heat_flux_w_m2 = 2.0;
    return c;
}

// A seasonal Arctic atmosphere: the air temperature and the sun of 80 N over the year.
AirState seasonal_air(double day, double mean_k, double amplitude_k) {
    const double phase = 2.0 * kPi * (day - 196.0) / 365.0;   // warmest mid-July
    const double sun = std::max(0.0, 320.0 * std::cos(2.0 * kPi * (day - 172.0) / 365.0) - 60.0);
    return arctic_air(mean_k + amplitude_k * std::cos(phase), sun, 5.0);
}

void ice_growth_and_energy() {
    auto c = column_config();
    c.deep_ocean_heat_flux_w_m2 = 0.0;
    ArcticColumnState s;
    s.mixed_layer_salinity_psu = 32.0;
    s.mixed_layer_temperature_k = seawater_freezing_point_k(32.0);
    s.ice_thickness_m = 0.3;
    s.ice_concentration = 0.99;
    std::vector<double> thickness;
    for (int k = 0; k < 120; ++k) {
        s = advance_arctic_column(s, arctic_air(243.15, 0.0), c, kDay);
        require(std::abs(s.energy_error_j_m2) < 1.0e-3 * 3.0e8, "column energy closes every day");
        thickness.push_back(s.ice_thickness_m);
    }
    require(thickness[119] > 1.2 && thickness[119] < 2.5, "a winter of -30 degC grows of order 1-2 m of ice");
    const double early = thickness[29] - thickness[0], late = thickness[119] - thickness[90];
    require(early > 1.5 * late, "growth slows as the ice thickens (conduction through thicker ice)");
    require(s.mixed_layer_salinity_psu > 32.0, "brine rejection makes the water saltier");
}

void open_water_freezes() {
    auto c = column_config();
    ArcticColumnState s;
    s.mixed_layer_salinity_psu = 32.0;
    s.mixed_layer_temperature_k = 273.15 + 1.0;
    s.ice_thickness_m = 0.0;
    s.ice_concentration = 0.0;
    bool froze = false;
    for (int k = 0; k < 120; ++k) {
        s = advance_arctic_column(s, arctic_air(248.15, 0.0, 8.0), c, kDay);
        require(s.mixed_layer_temperature_k >= seawater_freezing_point_k(s.mixed_layer_salinity_psu) - 1.0e-6 || s.ice_concentration == 0.0, "the water does not supercool");
        froze = froze || s.ice_concentration > 0.0;
    }
    require(froze && s.ice_concentration > 0.9 && s.ice_thickness_m > 0.5, "open water cools to its freezing point and freezes over");
}

void seasonal_cycle_and_warming() {
    auto c = column_config();
    auto spin = [&](double mean_k, double years, double deep_flux) {
        auto cc = c;
        cc.deep_ocean_heat_flux_w_m2 = deep_flux;
        ArcticColumnState s;
        s.mixed_layer_temperature_k = seawater_freezing_point_k(32.0);
        double march = 0.0, september = 0.0, min_area = 1.0;
        for (int day = 0; day < static_cast<int>(years * 365.0); day += 5) {
            s = advance_arctic_column(s, seasonal_air(day % 365, mean_k, 18.0), cc, 5.0 * kDay);
            require(std::abs(s.energy_error_j_m2) < 1.0e-3 * 3.0e8, "energy closes through the seasons");
            const int d = day % 365;
            if (day > (years - 1.0) * 365.0) {
                if (d >= 70 && d < 75) march = s.ice_thickness_m * s.ice_concentration;
                if (d >= 255 && d < 260) september = s.ice_thickness_m * s.ice_concentration;
                min_area = std::min(min_area, s.ice_concentration);
            }
        }
        return std::vector<double>{march, september, min_area};
    };
    const auto present = spin(258.15, 30.0, 2.0);
    require(present[0] > present[1] + 0.3, "thicker ice in March than in September");
    require(present[1] > 0.3 && present[2] > 0.5, "a perennial ice cover at -15 degC mean");
    const auto warm = spin(266.15, 30.0, 2.0);
    require(warm[1] < 0.5 * present[1], "eight kelvin warmer: much less summer ice");
    const auto hot = spin(271.15, 30.0, 2.0);
    require(hot[2] < 0.05, "thirteen kelvin warmer: ice-free summers");
    const auto atlantic = spin(258.15, 30.0, 12.0);
    require(atlantic[0] < present[0], "more Atlantic-water heat from below thins the ice");
}

// ---- Overturning -----------------------------------------------------------------------------------

CalibratedOverturning present_amoc(double salinity_difference = 1.0) {
    OverturningCalibration cal;
    cal.salinity_difference_psu = salinity_difference;
    return calibrate_overturning(cal, OverturningBoxConfig{});
}

OverturningBoxState present_state(const CalibratedOverturning&) {
    OverturningBoxState s;
    s.north_temperature_k = 279.15;
    s.south_temperature_k = 294.15;
    s.north_salinity_psu = 35.0;
    s.south_salinity_psu = 36.0;
    return s;
}

void stommel_threshold_and_calibration() {
    // Fast thermal relaxation reproduces Stommel's closed form.
    OverturningBoxConfig c;
    c.temperature_relaxation_s = 1.0;
    double q_at = 0.0;
    const double fc = critical_freshwater_m3_s(c, 15.0, &q_at);
    require_near(fc / stommel_critical_freshwater_m3_s(c.flow_constant_m3_s, c.thermal_expansion_per_k, c.haline_contraction_per_psu, 15.0, c.reference_salinity_psu), 1.0, "Stommel threshold", 1.0e-6);
    require_near(q_at / (c.flow_constant_m3_s * c.thermal_expansion_per_k * 15.0 / 2.0), 1.0, "collapse at half the thermal strength", 1.0e-6);
    // Calibration to the present AMOC.
    const auto amoc = present_amoc();
    require_near(amoc.freshwater_sv, 17.0 * 1.0 / 35.0, "present freshwater forcing q dS / S0", 1.0e-12);
    auto s = present_state(amoc);
    require_near(overturning_m3_s(s, amoc.config) / 1.0e6, 17.0, "calibrated strength", 1.0e-9);
    require_near(heat_transport_w(s, amoc.config) / 1.0e15, 1027.0 * 3990.0 * 17.0e6 * 15.0 / 1.0e15, "northward heat transport of about a petawatt", 1.0e-9);
    for (int k = 0; k < 200; ++k) s = advance_overturning(s, amoc.config, amoc.freshwater_sv * 1.0e6, amoc.north_target_k, amoc.south_target_k, kYear / 2.0);
    require_near(overturning_m3_s(s, amoc.config) / 1.0e6, 17.0, "the present state is steady for a century", 1.0e-6);
    const double d_star = amoc.south_target_k - amoc.north_target_k;
    const double f_crit = critical_freshwater_m3_s(amoc.config, d_star) / 1.0e6;
    require(f_crit > amoc.freshwater_sv && f_crit < amoc.freshwater_sv + 0.3, "the threshold lies a fraction of a sverdrup above the present forcing");
    // A fresher north (larger salinity difference) sits closer to the threshold.
    const auto near = present_amoc(1.3);
    require(critical_freshwater_m3_s(near.config, near.south_target_k - near.north_target_k) / 1.0e6 - near.freshwater_sv < f_crit - amoc.freshwater_sv, "closer to tipping");
    require_rejected([] { present_amoc(4.0); }, "an unstable or reversed present state is rejected");
}

void equilibria_and_hysteresis() {
    const auto amoc = present_amoc();
    const double d_star = amoc.south_target_k - amoc.north_target_k;
    const double f_crit = critical_freshwater_m3_s(amoc.config, d_star);
    const auto below = overturning_equilibria(amoc.config, 0.9 * f_crit, d_star);
    require(below.size() == 3 && below[0].stable && !below[1].stable && below[2].stable && below[2].overturning_m3_s < 0.0, "bistable: strong (stable), weak (unstable), reversed (stable)");
    require(below[0].overturning_m3_s > below[1].overturning_m3_s, "the strong state is the stable one");
    const auto above = overturning_equilibria(amoc.config, 1.1 * f_crit, d_star);
    require(above.size() == 1 && above[0].overturning_m3_s < 0.0 && above[0].stable, "past the threshold only the reversed state remains");
    // Hosing: push past the threshold, then return to the present forcing: the circulation stays off.
    auto s = present_state(amoc);
    const double f0 = amoc.freshwater_sv * 1.0e6;
    for (int k = 0; k < 600; ++k) s = advance_overturning(s, amoc.config, 1.15 * f_crit, amoc.north_target_k, amoc.south_target_k, kYear / 2.0);
    require(overturning_m3_s(s, amoc.config) < 0.2 * 17.0e6, "past the threshold the overturning collapses");
    for (int k = 0; k < 600; ++k) s = advance_overturning(s, amoc.config, f0, amoc.north_target_k, amoc.south_target_k, kYear / 2.0);
    require(overturning_m3_s(s, amoc.config) < 0.2 * 17.0e6, "hysteresis: back at today's forcing it stays collapsed");
    for (int k = 0; k < 600; ++k) s = advance_overturning(s, amoc.config, -0.3e6, amoc.north_target_k, amoc.south_target_k, kYear / 2.0);
    require(overturning_m3_s(s, amoc.config) > 10.0e6, "only net evaporation over the north restarts it");
    // Below the threshold a pulse that stays within the basin of attraction recovers.
    auto r = present_state(amoc);
    for (int k = 0; k < 20; ++k) r = advance_overturning(r, amoc.config, 0.95 * f_crit, amoc.north_target_k, amoc.south_target_k, kYear / 2.0);
    for (int k = 0; k < 400; ++k) r = advance_overturning(r, amoc.config, f0, amoc.north_target_k, amoc.south_target_k, kYear / 2.0);
    require_near(overturning_m3_s(r, amoc.config) / 1.0e6, 17.0, "a sub-threshold pulse recovers", 0.05);
    // Warming with polar amplification shrinks the thermal driving and lowers the threshold.
    require(critical_freshwater_m3_s(amoc.config, d_star - 3.0) < f_crit, "polar amplification lowers the threshold");
}

void critical_slowing_down() {
    const auto amoc = present_amoc();
    const double d_star = amoc.south_target_k - amoc.north_target_k;
    const double f_crit = critical_freshwater_m3_s(amoc.config, d_star);
    // The linearised recovery time against a perturbation experiment at 97 % of the threshold.
    std::vector<double> times;
    for (double fraction : {0.815, 0.9, 0.97, 0.995}) times.push_back(equilibrium_recovery_time_s(amoc.config, fraction * f_crit, d_star) / kYear);
    for (std::size_t k = 1; k < times.size(); ++k) require(times[k] > times[k - 1], "recovery slows toward the threshold");
    require(times[0] > 20.0 && times[0] < 60.0, "today the overturning recovers in decades");
    require(times[3] > 4.0 * times[0], "near the threshold it takes centuries");
    require(std::isinf(equilibrium_recovery_time_s(amoc.config, 1.01 * f_crit, d_star)), "past the threshold there is nothing to recover to");
    {
        const double f = 0.97 * f_crit;
        const auto e = overturning_equilibria(amoc.config, f, d_star)[0];
        OverturningBoxState s;
        s.north_temperature_k = 279.15; s.south_temperature_k = 279.15 + e.temperature_difference_k;
        s.north_salinity_psu = 35.0; s.south_salinity_psu = 35.0 + e.salinity_difference_psu;
        for (int k = 0; k < 3000; ++k) s = advance_overturning(s, amoc.config, f, amoc.north_target_k, amoc.south_target_k, 30.0 * kDay);
        const double q0 = overturning_m3_s(s, amoc.config);
        // A small kick excites the fast thermal mode and the slow haline mode; once the fast one has died
        // away the deviation decays at the slowest rate, which is the recovery time.
        s.north_salinity_psu -= 0.01;
        auto deviation_after = [&](int years) {
            for (int y = 0; y < years; ++y) for (int m = 0; m < 12; ++m) s = advance_overturning(s, amoc.config, f, amoc.north_target_k, amoc.south_target_k, kYear / 12.0);
            return std::abs(overturning_m3_s(s, amoc.config) - q0);
        };
        const double early = deviation_after(50), late = deviation_after(100);
        require_near(100.0 / std::log(early / late) / times[2], 1.0, "the linearised recovery time matches the decay of a perturbation", 0.05);
    }
    // The statistical indicators on a synthetic AR(1) record whose memory grows: variance and lag-1
    // autocorrelation rise, a constant memory shows no trend.
    auto record = [](bool growing) {
        NormalSequence n(11);
        std::vector<double> x{0.0};
        for (int k = 1; k < 3000; ++k) { const double phi = growing ? 0.2 + 0.75 * k / 3000.0 : 0.5; x.push_back(phi * x.back() + n.next()); }
        return early_warning_indicators(x, 300);
    };
    const auto rising = record(true), flat = record(false);
    auto tail = [](const std::vector<double>& v) { return std::vector<double>(v.begin() + 300, v.end()); };
    require(kendall_tau(tail(rising.autocorrelation)) > 0.8 && kendall_tau(tail(rising.variance)) > 0.8, "rising memory raises autocorrelation and variance");
    require(std::abs(kendall_tau(tail(flat.autocorrelation))) < 0.5 && std::abs(kendall_tau(tail(flat.variance))) < 0.5, "constant memory: no trend");
    require_near(rising.autocorrelation.back(), 0.95, "the estimator finds the final memory", 0.06);
    require_near(kendall_tau({1.0, 2.0, 3.0, 4.0}), 1.0, "Kendall tau of a rising series", 0.0);
    NormalSequence n(7);
    double sum = 0.0, sq = 0.0;
    for (int k = 0; k < 20000; ++k) { const double v = n.next(); sum += v; sq += v * v; }
    require(std::abs(sum / 20000.0) < 0.03 && std::abs(sq / 20000.0 - 1.0) < 0.04, "standard normal numbers");
}

void oxygen_and_rejections() {
    require_near(oxygen_solubility_umol_kg(293.15, 35.0), 225.0, "oxygen solubility at 20 degC, 35 psu", 2.5);
    require_near(oxygen_solubility_umol_kg(273.15, 35.0), 349.0, "oxygen solubility at 0 degC, 35 psu", 3.5);
    require(oxygen_solubility_umol_kg(298.15, 35.0) < oxygen_solubility_umol_kg(288.15, 35.0) && oxygen_solubility_umol_kg(288.15, 36.0) < oxygen_solubility_umol_kg(288.15, 30.0), "warm and salty water hold less oxygen");
    require_rejected([] { oxygen_solubility_umol_kg(330.0, 35.0); }, "oxygen range");
    auto c = island(5, 10000.0);
    auto bad = c;
    bad.ocean.assign(25, 0);
    require_rejected([&] { validate_ice_sheet_config(bad); }, "an ice sheet needs a sea");
    bad = c; bad.physics.refreezing_fraction = 1.5;
    require_rejected([&] { validate_ice_sheet_config(bad); }, "refreezing fraction");
    std::vector<double> on_sea(25, 0.0);
    on_sea[0] = 100.0;
    require_rejected([&] { initialize_ice_sheet(c, on_sea); }, "no ice on sea cells");
    auto s = initialize_ice_sheet(c, std::vector<double>(25, 0.0));
    auto f = climate(c, 250.0, 0.3);
    f.precipitation_m_s[3] = -1.0;
    require_rejected([&] { advance_ice_sheet(s, f, c, kDay); }, "negative precipitation");
    SeaIcePhysics p;
    p.albedo_cold = 1.5;
    require_rejected([&] { validate_sea_ice_physics(p); }, "albedo");
    auto col = column_config();
    require_rejected([&] { advance_arctic_column(ArcticColumnState{}, arctic_air(250.0, 0.0), col, 200.0 * kDay); }, "column timestep");
    OverturningBoxConfig o;
    o.north_volume_m3 = -1.0;
    require_rejected([&] { validate_overturning(o); }, "box volume");
    OverturningCalibration cal;
    cal.temperature_difference_k = 2.0;   // too little thermal driving for the salinity difference
    require_rejected([&] { calibrate_overturning(cal, OverturningBoxConfig{}); }, "a haline present state is rejected");
}


// ---- Sea ice and glacial water on the layered ocean --------------------------------------------------

OceanConfig polar_basin(double initial_k, bool ice_enabled = true) {
    OceanConfig c;
    const std::size_t n = 7;
    c.grid.nx = n; c.grid.ny = n;
    c.grid.origin_east_m = 0.0; c.grid.origin_north_m = 0.0;
    c.grid.spacing_east_m = 60000.0; c.grid.spacing_north_m = 60000.0;
    c.grid.geographic_anchor_longitude_deg = -30.0; c.grid.geographic_anchor_latitude_deg = 75.0;
    c.timestep_seconds = 3600.0;
    c.ocean.assign(n * n, 1);
    for (std::size_t i = 0; i < n * n; ++i) if (i % n == 0 || i / n == 0 || i % n + 1 == n || i / n + 1 == n) c.ocean[i] = 0;
    c.physics.coriolis_mode = "none";
    c.physics.temperature_relaxation_s = 1.0e12;
    c.physics.deep_temperature_k = 271.6;   // Arctic deep water under a fresher surface (the halocline)
    c.physics.deep_salinity_psu = 34.9;
    c.initial_thickness_m = 300.0;
    c.initial_salinity_psu = 34.0;
    c.initial_temperature_k = initial_k;
    c.thermal.count = 8;
    c.thermal.surface_layer_m = 5.0;
    c.thermal.initial_mixed_layer_depth_m = 30.0;
    c.thermal.initial_thermocline_scale_m = 1.0e6;   // an isothermal column
    c.sea_ice.enabled = ice_enabled;
    c.sea_ice.initial_thickness_m = 0.0;
    return c;
}

OceanForcing polar_air(const OceanConfig& c, double air_k, double shortwave) {
    OceanForcing f;
    const std::size_t count = c.grid.nx * c.grid.ny;
    f.wind_stress_east_pa.assign(count, 0.0);
    f.wind_stress_north_pa.assign(count, 0.0);
    f.net_precipitation_m_s.assign(count, 0.0);
    f.target_temperature_k.assign(count, c.initial_temperature_k);
    f.air_temperature_k.assign(count, air_k);
    f.air_specific_humidity_kg_kg.assign(count, 0.8 * saturation_specific_humidity_kg_kg(air_k, 101325.0));
    f.surface_pressure_pa.assign(count, 101325.0);
    f.wind_speed_m_s.assign(count, 6.0);
    f.cloud_fraction.assign(count, 0.5);
    f.shortwave_down_w_m2.assign(count, shortwave);
    return f;
}

double ocean_plus_ice_salt(const OceanState& s, const OceanConfig& c) {
    return s.salt_kg + s.ice_volume_m3 * c.sea_ice.physics.ice_density_kg_m3 * c.sea_ice.physics.ice_salinity_psu * 1.0e-3;
}

void ocean_sea_ice_freezes_and_rejects_brine() {
    auto c = polar_basin(seawater_freezing_point_k(34.0) + 0.3);
    auto f = polar_air(c, 248.15, 0.0);
    auto s = initialize_ocean(c);
    require(s.ice_volume_m3 == 0.0, "open water at the start");
    const double water0 = s.volume_m3, salt0 = ocean_plus_ice_salt(s, c), ratio = 917.0 / 1025.0;
    const std::size_t centre = 3 * 7 + 3;
    double open_loss = 0.0;
    for (int k = 0; k < 24 * 60; ++k) {
        s = advance_ocean(s, f, c);
        if (k == 2) open_loss = -s.net_heat_w_m2[centre];
        require(std::abs(s.volume_balance_error_m3) < 1.0e-9 * s.volume_m3 && std::abs(s.salt_balance_error_kg) < 1.0e-9 * s.salt_kg, "ocean volume and salt budgets with sea ice");
        require(std::abs(s.heat_balance_error_j) < 1.0e-11 * s.volume_m3 * 1025.0 * 3990.0 * 300.0, "ocean heat budget with sea ice");
        require(s.layer_temperature_k[centre * 8] > seawater_freezing_point_k(s.layer_salinity_psu[centre * 8]) - 1.0e-6, "the water does not supercool");
    }
    require(s.ice_concentration[centre] > 0.9 && s.ice_thickness_m[centre] > 0.5, "two months at -25 degC freeze the sea over");
    require_near(s.volume_m3, water0 + s.cumulative_ice_water_m3 - s.cumulative_evaporation_m3, "ocean volume changes by the ice exchange and the evaporation", 1.0e-9 * water0);
    require_near(s.volume_m3 + s.ice_volume_m3 * ratio, water0 - s.cumulative_evaporation_m3, "water in the ocean and the ice is conserved", 1.0e-9 * water0);
    require_near(ocean_plus_ice_salt(s, c), salt0, "salt in the ocean and the ice is conserved", 1.0e-9 * salt0);
    require(s.layer_salinity_psu[centre * 8] > 34.05, "brine rejection makes the surface saltier");
    require(s.ice_surface_temperature_k[centre] < 265.0, "the ice surface is far colder than the water");
    require(-s.net_heat_w_m2[centre] < 0.3 * open_loss, "the ice insulates the water: the ocean loses far less heat");
}

void ocean_sea_ice_melts_in_summer() {
    auto c = polar_basin(seawater_freezing_point_k(34.0));
    c.sea_ice.initial_thickness_m = 1.0;
    c.sea_ice.initial_concentration = 0.95;
    c.sea_ice.initial_band_k = 1.0;
    auto f = polar_air(c, 276.15, 300.0);
    auto s = initialize_ocean(c);
    const std::size_t centre = 3 * 7 + 3;
    require_near(s.ice_thickness_m[centre], 1.0, "initial ice where the water is at freezing", 0.0);
    const double salt0 = ocean_plus_ice_salt(s, c);
    bool gone = false;
    double sst_when_gone = 0.0;
    for (int k = 0; k < 24 * 120 && !gone; ++k) {
        s = advance_ocean(s, f, c);
        require(std::abs(s.heat_balance_error_j) < 1.0e-11 * s.volume_m3 * 1025.0 * 3990.0 * 300.0, "heat budget while melting");
        if (s.ice_volume_m3 == 0.0) { gone = true; sst_when_gone = s.sst_k[centre]; }
    }
    require(gone, "summer air and sun melt a metre of ice");
    require(s.cumulative_ice_water_m3 > 0.0, "the melt returns water to the ocean");
    require_near(ocean_plus_ice_salt(s, c), salt0, "salt conserved through the melt", 1.0e-9 * salt0);
    // Once the ice is gone the dark open water absorbs the sun and warms (the ice-albedo feedback).
    for (int k = 0; k < 24 * 20; ++k) s = advance_ocean(s, f, c);
    require(s.sst_k[centre] > sst_when_gone + 0.5, "open water warms once the ice is gone");
}

void warm_water_melts_ice_from_below() {
    auto c = polar_basin(seawater_freezing_point_k(34.0) + 2.0);
    c.sea_ice.initial_thickness_m = 1.0;
    c.sea_ice.initial_concentration = 0.9;
    c.sea_ice.initial_band_k = 2.5;
    auto f = polar_air(c, 263.15, 0.0);
    auto cold = polar_basin(seawater_freezing_point_k(34.0));
    cold.sea_ice = c.sea_ice;
    auto s_warm = initialize_ocean(c), s_cold = initialize_ocean(cold);
    for (int k = 0; k < 24 * 10; ++k) { s_warm = advance_ocean(s_warm, f, c); s_cold = advance_ocean(s_cold, f, cold); }
    require(s_warm.ice_volume_m3 < 0.7 * s_cold.ice_volume_m3, "two kelvin of warm water under the ice melt it from below");
}

void glacial_meltwater_and_icebergs() {
    auto c = polar_basin(277.15, false);
    auto f = polar_air(c, 277.15, 0.0);
    c.air_sea.heat_transfer_coefficient = 0.0;
    c.air_sea.moisture_transfer_coefficient = 0.0;
    c.air_sea.surface_emissivity = 0.0;
    const std::size_t mouth = 3 * 7 + 1;
    const double q = 2.0e4;
    auto s0 = initialize_ocean(c);
    auto liquid = f, solid = f;
    liquid.glacial_meltwater_m3_s.assign(49, 0.0);
    liquid.glacial_meltwater_m3_s[mouth] = q;
    solid.glacial_ice_m3_s.assign(49, 0.0);
    solid.glacial_ice_m3_s[mouth] = q;
    auto a = s0, b = s0;
    for (int k = 0; k < 24 * 5; ++k) {
        a = advance_ocean(a, liquid, c);
        b = advance_ocean(b, solid, c);
        require(std::abs(a.heat_balance_error_j) < 1.0e-11 * a.volume_m3 * 4.1e6 * 300.0 && std::abs(b.heat_balance_error_j) < 1.0e-11 * b.volume_m3 * 4.1e6 * 300.0, "heat budgets with glacial water");
        require(std::abs(a.volume_balance_error_m3) < 1.0e-9 * a.volume_m3 && std::abs(b.volume_balance_error_m3) < 1.0e-9 * b.volume_m3, "volume budgets with glacial water");
    }
    const double added = q * 5.0 * kDay;
    require_near(a.volume_m3 - s0.volume_m3, added, "meltwater adds its volume", 1.0e-9 * s0.volume_m3);
    require_near(a.cumulative_glacial_m3, added, "cumulative glacial water", 1.0);
    require_near(a.salt_kg / s0.salt_kg, 1.0, "meltwater brings no salt", 1.0e-12);
    require(a.surface_salinity_psu[mouth] < 33.0, "a fresh meltwater plume at the glacier front");
    // Icebergs take the latent heat of melting from the sea: 334 kJ/kg against 4 kJ/kg/K.
    const double heat_difference = a.heat_content_j - b.heat_content_j;
    require_near(heat_difference, added * 1000.0 * 3.34e5 - added * 1025.0 * 3990.0 * (273.15 - seawater_freezing_point_k(a.salinity_psu[mouth])) * 0.0, "melting ice cools the sea by its latent heat", 0.1 * added * 1000.0 * 3.34e5);
    require(b.sst_k[mouth] < a.sst_k[mouth] - 0.5, "iceberg melt chills the surface");
    const auto d = diagnose_ocean(a, liquid, c);
    require(!d.lenses.empty() && d.lenses.front().kind == "glacial" && d.lenses.front().glacial_share > 0.99, "the lens is attributed to the glacier");
}

void sea_ice_drifts_with_wind_and_current() {
    // Half-covered sea at its freezing point under air at the freezing point: little thermodynamics, an eastward wind.
    auto c = polar_basin(seawater_freezing_point_k(34.0));
    c.sea_ice.initial_thickness_m = 1.0;
    c.sea_ice.initial_concentration = 0.5;
    c.sea_ice.initial_band_k = 1.0;
    auto f = polar_air(c, seawater_freezing_point_k(34.0), 0.0);
    f.wind_speed_m_s.assign(49, 10.0);
    f.wind_stress_east_pa.assign(49, 0.1);
    // One step from rest: the ice moves at 2 % of the wind, turned 30 degrees to the right in the north, to the left
    // in the south and not at all without rotation (the current after one step is a millimetre per second).
    const std::size_t centre = 3 * 7 + 3;
    for (double f0 : {1.3e-4, -1.3e-4, 0.0}) {
        auto rot = c;
        rot.physics.coriolis_mode = "prescribed";
        rot.physics.coriolis_parameter_s = f0;
        const auto s1 = advance_ocean(initialize_ocean(rot), f, rot);
        const double expected_north = f0 > 0.0 ? -0.1 : f0 < 0.0 ? 0.1 : 0.0;
        const double expected_east = f0 == 0.0 ? 0.2 : 0.2 * std::cos(kPi / 6.0);
        require_near(s1.ice_drift_east_m_s[centre], expected_east, "Nansen drift: 2 % of the wind", 0.005);
        require_near(s1.ice_drift_north_m_s[centre], expected_north, "turned right of the wind in the north, left in the south", 0.005);
    }
    // A week of wind: the ice piles up on the eastern coast and leaves open water in the west; the drift conserves
    // the ice (the volume changes only by the thermodynamic growth).
    auto s = initialize_ocean(c);
    auto fixed = c;
    fixed.sea_ice.drift = false;
    auto still = initialize_ocean(fixed);
    const double area = 60000.0 * 60000.0;
    double grown = 0.0;
    for (int k = 0; k < 24 * 7; ++k) {
        const double before = s.ice_volume_m3;
        s = advance_ocean(s, f, c);
        still = advance_ocean(still, f, fixed);
        double growth = 0.0;
        for (double g : s.ice_growth_m_s) growth += g * 3600.0 * area;
        grown += growth;
        require_near(s.ice_volume_m3 - before, growth, "drift neither creates nor destroys ice", 1.0e-9 * before + 1.0);
        require(std::abs(s.volume_balance_error_m3) < 1.0e-9 * s.volume_m3 && std::abs(s.heat_balance_error_j) < 1.0e-11 * s.volume_m3 * 1025.0 * 3990.0 * 300.0, "ocean budgets with drifting ice");
        for (std::size_t i = 0; i < 49; ++i) require(s.ice_concentration[i] <= c.sea_ice.physics.maximum_concentration + 1.0e-12 && (c.ocean[i] || s.ice_concentration[i] == 0.0), "concentration capped, no ice on land");
    }
    const std::size_t west = 3 * 7 + 1, east = 3 * 7 + 5;
    require(s.ice_concentration[east] > 0.95 && s.ice_concentration[west] < 0.25, "the wind packs the ice against the eastern coast and opens the west");
    require(s.ice_thickness_m[east] > 1.2 && s.ice_thickness_m[east] <= c.sea_ice.maximum_ridge_thickness_m + 0.2, "converging ice ridges, up to the ridge thickness");
    require(std::abs(still.ice_concentration[east] - still.ice_concentration[west]) < 0.01, "without drift the cover stays uniform");
    require_near(s.ice_volume_m3 - initialize_ocean(c).ice_volume_m3, grown, "over the week the ice changed only by growth and melt", 1.0e-6 * s.ice_volume_m3);
    // Without internal stress free drift would pile all the ice onto the coast; packed cells take no more.
    auto strong = c;
    strong.sea_ice.maximum_ridge_thickness_m = 1.5;
    auto sp = initialize_ocean(strong);
    for (int k = 0; k < 24 * 30; ++k) sp = advance_ocean(sp, f, strong);
    require(sp.ice_thickness_m[east] < 1.5 + 0.3, "ridging stops at the ridge thickness (plus thermodynamic growth)");
    require(sp.ice_concentration[east - 1] > 0.9, "the ice stops behind the packed coastal cell");
    // Ice drifting onto warm water melts there (the export of fresh water to where the ice melts).
    auto warm = c;
    warm.initial_temperature_k = seawater_freezing_point_k(34.0);
    warm.initial_surface_temperature_k.assign(49, seawater_freezing_point_k(34.0));
    for (std::size_t i = 0; i < 49; ++i) if (i % 7 >= 4) warm.initial_surface_temperature_k[i] = 276.15;   // warm eastern half, ice-free at the start
    auto sw = initialize_ocean(warm);
    require(sw.ice_concentration[east] == 0.0 && sw.ice_concentration[west] > 0.0, "ice only over the cold western half");
    double melt_east = 0.0;
    for (int k = 0; k < 24 * 5; ++k) {
        sw = advance_ocean(sw, f, warm);
        for (std::size_t i = 0; i < 49; ++i) if (i % 7 >= 4) melt_east += -std::min(0.0, sw.ice_growth_m_s[i]) * 3600.0 * area;
    }
    require(melt_east > 0.0, "the drifting ice melts over the warm water");
}

void ocean_sea_ice_contract() {
    auto c = polar_basin(272.0);
    auto bad = c;
    bad.thermal.count = 0;
    require_rejected([&] { validate_ocean_config(bad); }, "sea ice needs the layered column");
    bad = c; bad.sea_ice.physics.conductivity_w_m_k = -1.0;
    require_rejected([&] { validate_ocean_config(bad); }, "sea-ice physics");
    bad = c; bad.sea_ice.initial_concentration = 1.5;
    require_rejected([&] { validate_ocean_config(bad); }, "initial concentration");
    bad = c; bad.sea_ice.wind_drift_factor = 0.5;
    require_rejected([&] { validate_ocean_config(bad); }, "wind drift factor");
    bad = c; bad.sea_ice.maximum_ridge_thickness_m = 0.0;
    require_rejected([&] { validate_ocean_config(bad); }, "ridge thickness");
    const auto s = initialize_ocean(c);
    auto f = polar_air(c, 260.0, 0.0);
    auto no_air = f;
    no_air.air_temperature_k.clear(); no_air.air_specific_humidity_kg_kg.clear(); no_air.surface_pressure_pa.clear(); no_air.wind_speed_m_s.clear(); no_air.cloud_fraction.clear(); no_air.shortwave_down_w_m2.clear();
    require_rejected([&] { advance_ocean(s, no_air, c); }, "sea ice needs the air-sea forcing");
    auto on_land = f;
    on_land.glacial_meltwater_m3_s.assign(49, 0.0);
    on_land.glacial_meltwater_m3_s[0] = 100.0;
    require_rejected([&] { advance_ocean(s, on_land, c); }, "glacial water enters the sea");
    auto no_ice = c;
    no_ice.sea_ice.enabled = false;
    require_rejected([&] { advance_ocean(s, f, no_ice); }, "an iced state with an ice-free configuration");
}

}  // namespace

int main() {
    try {
        positive_degree_days();
        dome_and_routing();
        surface_mass_balance();
        ice_flow_and_calving();
        frontal_ablation();
        freezing_point_and_surface_balance();
        ice_growth_and_energy();
        open_water_freezes();
        seasonal_cycle_and_warming();
        stommel_threshold_and_calibration();
        equilibria_and_hysteresis();
        critical_slowing_down();
        oxygen_and_rejections();
        ocean_sea_ice_freezes_and_rejects_brine();
        ocean_sea_ice_melts_in_summer();
        warm_water_melts_ice_from_below();
        glacial_meltwater_and_icebergs();
        sea_ice_drifts_with_wind_and_current();
        ocean_sea_ice_contract();
    } catch (const std::exception& error) {
        std::cerr << "polar melt test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "polar melt cases passed\n";
    return 0;
}
