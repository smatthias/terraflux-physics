#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/precipitation_outcomes.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <map>
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

bool has_hazard(const PrecipitationField& p, std::size_t i, const std::string& hazard) {
    return std::find(p.hazards[i].begin(), p.hazards[i].end(), hazard) != p.hazards[i].end();
}

TransportGrid2D make_grid(std::size_t nx, std::size_t ny, double spacing) {
    TransportGrid2D grid;
    grid.nx = nx;
    grid.ny = ny;
    grid.spacing_east_m = spacing;
    grid.spacing_north_m = spacing;
    grid.origin_east_m = -0.5 * static_cast<double>(nx) * spacing;
    grid.origin_north_m = -0.5 * static_cast<double>(ny) * spacing;
    grid.geographic_anchor_longitude_deg = 145.0;
    grid.geographic_anchor_latitude_deg = -37.8;
    return grid;
}

CloudFormationConfig make_cloud_config() {
    CloudFormationConfig config;
    config.column.layer_count = 24;
    config.column.layer_thickness_m = 500.0;
    return config;
}

CoupledWindState uniform_state(std::size_t count, double temperature, double humidity, double pressure = 100000.0) {
    CoupledWindState state;
    state.pressure_pa.assign(count, pressure);
    state.air_temperature_k.assign(count, temperature);
    state.specific_humidity_kg_kg.assign(count, humidity);
    state.wind_east_m_s.assign(count, 0.0);
    state.wind_north_m_s.assign(count, 0.0);
    return state;
}

// A hand-built cloud field: one genus per cell with explicit base/top/CAPE.
CloudField make_clouds(const std::vector<std::string>& types, const std::vector<double>& bases,
                       const std::vector<double>& tops, const std::vector<double>& cape, double heating = 0.0) {
    CloudField c;
    const std::size_t n = types.size();
    c.cloud_type = types;
    c.base_m = bases;
    c.top_m = tops;
    c.heated_cape_j_kg = cape;
    c.heated_cin_j_kg.assign(n, 0.0);
    c.lifting_depth_m.assign(n, 0.0);
    c.cloud_fraction.assign(n, 0.5);
    c.mechanism.assign(n, "frontal");
    c.trigger.assign(n, "frontal");
    c.thunderstorm.assign(n, 0);
    c.surface_heating_k = heating;
    return c;
}

void helper_references() {
    // Psychrometric table: 20 C, 50 % RH at 1000 hPa gives a wet-bulb of ~13.8 C.
    const double q = specific_humidity_kg_kg(0.5 * saturation_vapour_pressure_pa(293.15), 100000.0);
    require_near(wet_bulb_temperature_k(293.15, q, 100000.0) - 273.15, 13.8, "wet-bulb 20 C / 50 %", 0.15);
    require_near(wet_bulb_temperature_k(273.15, saturation_specific_humidity_kg_kg(273.15, 100000.0), 100000.0), 273.15, "saturated air has T_w = T", 1.0e-6);
    require(wet_bulb_temperature_k(283.15, 0.002, 100000.0) < 283.15, "unsaturated air has T_w < T");
    // Hail: 2 cm stones fall at about 20 m/s; 4 cm at about 28 m/s.
    require_near(hail_terminal_velocity_m_s(0.02), 19.8, "2 cm hail terminal velocity", 0.3);
    require_near(hail_terminal_velocity_m_s(0.04), 28.0, "4 cm hail terminal velocity", 0.4);
    require_near(hail_diameter_for_updraft_m(hail_terminal_velocity_m_s(0.03)), 0.03, "updraft <-> diameter inverse", 1.0e-12);
    // WMO intensity classes.
    require(precipitation_intensity_class(0.01) == "none" && precipitation_intensity_class(1.0) == "light" &&
                precipitation_intensity_class(5.0) == "moderate" && precipitation_intensity_class(20.0) == "heavy" &&
                precipitation_intensity_class(60.0) == "violent", "intensity classes");
    // Genus -> character table.
    for (const char* harmless : {"none", "cirrus", "cirrostratus", "altocumulus", "cumulus"}) {
        require(precipitation_character_for_type(harmless) == "none", std::string(harmless) + " does not precipitate");
    }
    require(precipitation_character_for_type("stratus") == "drizzle" && precipitation_character_for_type("stratocumulus") == "intermittent" &&
                precipitation_character_for_type("altostratus") == "intermittent" && precipitation_character_for_type("nimbostratus") == "continuous" &&
                precipitation_character_for_type("towering_cumulus") == "showers" && precipitation_character_for_type("cumulonimbus") == "showers",
            "precipitating genera");
}

// Harmless clouds deliver nothing, but their temperatures are still reported.
void harmless_clouds_deliver_nothing() {
    const auto grid = make_grid(5, 2, 50000.0);
    const auto state = uniform_state(10, 290.0, 0.010);
    const auto clouds = make_clouds({"cirrus", "cirrostratus", "altocumulus", "cumulus", "none", "none", "none", "none", "none", "none"},
                                    {8000.0, 7000.0, 3000.0, 1000.0, NAN, NAN, NAN, NAN, NAN, NAN}, {8500.0, 9000.0, 3600.0, 1800.0, NAN, NAN, NAN, NAN, NAN, NAN},
                                    std::vector<double>(10, 0.0));
    const auto p = diagnose_precipitation(state, grid, make_cloud_config(), clouds, PrecipitationConfig{});
    require(p.precipitating_cells == 0 && p.max_rate_mm_h == 0.0 && p.total_accumulation_mm == 0.0, "no precipitation");
    for (std::size_t i = 0; i < 4; ++i) {
        require(p.precipitation_type[i] == "none" && p.hazards[i].empty() && std::isfinite(p.cloud_base_temperature_k[i]) &&
                    p.cloud_top_temperature_k[i] < p.cloud_base_temperature_k[i],
                "harmless genus reports temperatures only");
    }
    require(std::isnan(p.cloud_base_temperature_k[4]) && p.precipitable_water_mm[4] > 10.0 && std::isfinite(p.freezing_level_m[4]),
            "clear cell still has column diagnostics");
}

// Nimbostratus: continuous, prolonged moderate rain with a physically sized rate.
void nimbostratus_gives_prolonged_moderate_rain() {
    const auto grid = make_grid(2, 2, 50000.0);
    const auto state = uniform_state(4, 285.0, 0.008);
    auto config = make_cloud_config();  // lifting time scale 6 h
    const auto clouds = make_clouds({"nimbostratus", "none", "none", "none"}, {800.0, NAN, NAN, NAN}, {6800.0, NAN, NAN, NAN}, {0.0, 0.0, 0.0, 0.0});
    const auto p = diagnose_precipitation(state, grid, config, clouds, PrecipitationConfig{});
    require(p.precipitation_type[0] == "rain" && p.character[0] == "continuous" && p.phase[0] == "liquid", "continuous rain");
    require_near(p.updraft_m_s[0], 6000.0 / 21600.0, "lifting through a 6 km cloud over 6 h", 1.0e-9);
    require(p.rate_mm_h[0] > 2.0 && p.rate_mm_h[0] < 8.0 && p.intensity[0] == "moderate", "moderate rate of a few mm/h");
    require_near(p.duration_fraction[0], 1.0, "continuous precipitation lasts the interval", 1.0e-12);
    require_near(p.accumulation_mm[0], p.rate_mm_h[0] * 8.0, "8 h accumulation", 1.0e-9);
    require(has_hazard(p, 0, "prolonged_rain") && !has_hazard(p, 0, "thunderstorm") && !p.lightning[0] && p.hail_max_diameter_cm[0] == 0.0,
            "long rain without lightning or hail");
    // Independent evaluation of R = rho w (q_s(base) - q_s(top)) * efficiency.
    const auto column = build_hydrostatic_column(100000.0, 285.0, 0.008, config.column);
    const double t_base = column_temperature_at_height_k(column, 800.0);
    const double p_base = column_pressure_at_height_pa(column, 800.0), p_top = column_pressure_at_height_pa(column, 6800.0);
    const double q_base = saturation_specific_humidity_kg_kg(t_base, p_base);
    const double q_top = saturation_specific_humidity_kg_kg(p.cloud_top_temperature_k[0], p_top);
    const double expected = 0.7 * moist_air_density_kg_m3(p_base, t_base, q_base) * (6000.0 / 21600.0) * (q_base - q_top) * 3600.0;
    require_near(p.rate_mm_h[0], expected, "rate formula", 1.0e-9);
}

// Cumulonimbus: heavy showers, lightning from a cold top crossing the
// freezing level, hail sized by the updraft, and shorter duration.
void cumulonimbus_gives_heavy_showers_lightning_and_hail() {
    const auto grid = make_grid(3, 2, 50000.0);
    const auto state = uniform_state(6, 296.0, 0.014);
    const auto clouds = make_clouds({"cumulonimbus", "cumulonimbus", "towering_cumulus", "none", "none", "none"},
                                    {1200.0, 1200.0, 1500.0, NAN, NAN, NAN}, {10000.0, 8200.0, 4500.0, NAN, NAN, NAN}, {2500.0, 300.0, 400.0, 0.0, 0.0, 0.0}, 4.0);
    const auto p = diagnose_precipitation(state, grid, make_cloud_config(), clouds, PrecipitationConfig{});
    require(p.precipitation_type[0] == "rain_showers" && (p.intensity[0] == "heavy" || p.intensity[0] == "violent") && p.character[0] == "showers", "strong storm is a heavy to violent shower");
    require(p.rate_mm_h[0] > 30.0 && p.rate_mm_h[0] < 80.0, "2500 J/kg gives tens of mm/h over the cell");
    require_near(p.updraft_m_s[0], 0.5 * std::sqrt(2.0 * 2500.0), "updraft from parcel theory", 1.0e-9);
    require(p.lightning[0] == 1 && has_hazard(p, 0, "thunderstorm") && (has_hazard(p, 0, "heavy_rain") || has_hazard(p, 0, "violent_rain")), "lightning and heavy rain hazards");
    require(p.hail_max_diameter_cm[0] > 3.0 && p.hail_max_diameter_cm[0] < 8.0 && has_hazard(p, 0, "hail"), "35 m/s updraft supports large hail after melting");
    const double supported_cm = 100.0 * hail_diameter_for_updraft_m(p.updraft_m_s[0]);
    require_near(p.hail_max_diameter_cm[0], supported_cm - 0.3 * p.freezing_level_m[0] / 1000.0, "hail melts 0.3 cm per km below the freezing level", 1.0e-9);
    require(p.cloud_top_temperature_k[0] < 253.15 && p.freezing_level_m[0] > 1200.0 && p.freezing_level_m[0] < 10000.0, "mixed-phase region exists");
    require_near(p.duration_fraction[0], 0.2, "showers are brief", 1.0e-12);
    require(p.accumulation_mm[0] > 15.0 && p.accumulation_mm[0] < 120.0, "storm total over 8 h");
    // Weak storm with a cold enough top: lightning but no hail (12 m/s holds only ~0.7 cm, melted away).
    require(p.cloud_top_temperature_k[1] <= 253.15 && p.lightning[1] == 1 && p.hail_max_diameter_cm[1] == 0.0 && !has_hazard(p, 1, "hail"),
            "weak updraft gives lightning without hail");
    // Towering cumulus: showers without lightning.
    require(p.precipitation_type[2] == "rain_showers" && p.lightning[2] == 0 && p.hail_max_diameter_cm[2] == 0.0 &&
                (p.intensity[2] == "light" || p.intensity[2] == "moderate"),
            "towering cumulus showers are lighter and without lightning");
    require(p.rate_mm_h[0] > p.rate_mm_h[1] && p.rate_mm_h[1] > p.rate_mm_h[2], "rate grows with convective energy");
}

// Phase at the ground follows the surface wet-bulb temperature.
void phase_follows_surface_wet_bulb_temperature() {
    const auto grid = make_grid(2, 2, 50000.0);
    const auto clouds = make_clouds({"nimbostratus", "none", "none", "none"}, {600.0, NAN, NAN, NAN}, {5600.0, NAN, NAN, NAN}, {0.0, 0.0, 0.0, 0.0});
    const auto run = [&](double temperature, double rh) {
        const double q = specific_humidity_kg_kg(rh * saturation_vapour_pressure_pa(temperature), 100000.0);
        return diagnose_precipitation(uniform_state(4, temperature, q), grid, make_cloud_config(), clouds, PrecipitationConfig{});
    };
    const auto snow = run(273.65, 0.95);  // T_w about 0.2 C
    const auto sleet = run(275.15, 0.95); // T_w about 1.6 C
    const auto rain = run(280.0, 0.9);
    require(snow.precipitation_type[0] == "snow" && snow.phase[0] == "solid" && has_hazard(snow, 0, "snow") && snow.snow_cells == 1, "snow near 0 C");
    require(sleet.precipitation_type[0] == "sleet" && sleet.phase[0] == "mixed" && has_hazard(sleet, 0, "sleet"), "sleet between 0.5 and 2 C wet-bulb");
    require(rain.precipitation_type[0] == "rain" && rain.phase[0] == "liquid", "rain in mild air");
    require(snow.surface_wet_bulb_k[0] <= 273.65 && sleet.surface_wet_bulb_k[0] > 273.65 && sleet.surface_wet_bulb_k[0] <= 275.15, "wet-bulb thresholds");
    // Dry, cool air: evaporative cooling turns 3 C air into snow at the ground.
    const auto dry = run(276.15, 0.5);
    require(dry.surface_wet_bulb_k[0] < 273.65 && dry.precipitation_type[0] == "snow", "dry 3 C air has a sub-zero wet-bulb and snows");
    // A warm shallow layer cloud gives liquid drizzle whatever the rule would say.
    const auto warm = diagnose_precipitation(uniform_state(4, 290.0, 0.011), grid, make_cloud_config(),
                                             make_clouds({"stratus", "none", "none", "none"}, {300.0, NAN, NAN, NAN}, {1300.0, NAN, NAN, NAN}, {0.0, 0.0, 0.0, 0.0}), PrecipitationConfig{});
    require(warm.precipitation_type[0] == "drizzle" && warm.rate_mm_h[0] <= 0.5 && warm.cloud_top_temperature_k[0] > 273.15 && has_hazard(warm, 0, "drizzle"),
            "warm stratus drizzles at the capped rate");
}

// Altostratus over dry air: virga, nothing reaches the ground.
void high_light_precipitation_into_dry_air_is_virga() {
    const auto grid = make_grid(2, 2, 50000.0);
    auto state = uniform_state(4, 295.0, 0.004);  // RH ~ 24 %
    state.specific_humidity_kg_kg[1] = 0.012;     // RH ~ 72 %
    const auto clouds = make_clouds({"altostratus", "altostratus", "none", "none"}, {3500.0, 3500.0, NAN, NAN}, {6000.0, 6000.0, NAN, NAN}, {0.0, 0.0, 0.0, 0.0});
    const auto p = diagnose_precipitation(state, grid, make_cloud_config(), clouds, PrecipitationConfig{});
    require(p.virga[0] == 1 && p.rate_mm_h[0] == 0.0 && p.precipitation_type[0] == "none" && has_hazard(p, 0, "virga"), "virga over dry air");
    require(p.virga[1] == 0 && p.rate_mm_h[1] > 0.0 && p.precipitation_type[1] == "rain" && p.character[1] == "intermittent" &&
                p.duration_fraction[1] == 0.5,
            "the same cloud over humid air gives intermittent light rain");
}

// Regression: the 12 x 10 baroclinic scenario diagnosed at 6 K and 0 K heating.
void regression_scenario_is_pinned() {
    CoupledWindConfig c;
    c.time = {"2026-11-01T00:00:00Z", 300.0, 96};
    c.grid = make_grid(12, 10, 50000.0);
    c.atmosphere.air_density_kg_m3 = 1.225;
    c.atmosphere.surface_friction_coefficient_s = 2.0e-5;
    c.atmosphere.coriolis_mode = "f_plane";
    c.diffusion = {5000.0, 5000.0, 5000.0};
    PressureSystemsConfig systems;
    systems.background_pressure_pa = 101300.0;
    systems.centers = {{"high", -250000.0, -80000.0, 700.0, 300000.0}, {"low", 120000.0, 60000.0, -900.0, 250000.0}};
    CoupledWindState state;
    state.pressure_pa = build_pressure_field(systems, c.grid);
    state.air_temperature_k = build_gaussian_field(289.0, {{180000.0, 150000.0, 250000.0, 6.0}, {-150000.0, -120000.0, 250000.0, -8.0}}, c.grid);
    state.specific_humidity_kg_kg = build_gaussian_field(0.008, {{180000.0, 150000.0, 250000.0, 0.006}, {-150000.0, -120000.0, 250000.0, -0.004}}, c.grid);
    state.wind_east_m_s.assign(120, 0.0);
    state.wind_north_m_s.assign(120, 0.0);
    state = advance_coupled_wind(state, c);
    const auto config = make_cloud_config();
    const auto fronts = analyse_fronts(state, c.grid, config);
    const auto count_of = [](const std::vector<std::string>& labels) {
        std::map<std::string, int> counts;
        for (const auto& label : labels) ++counts[label];
        return counts;
    };

    const auto day = diagnose_precipitation(state, c.grid, config, diagnose_cloud_formation(state, c.grid, config, 6.0, fronts), PrecipitationConfig{});
    require(count_of(day.precipitation_type) == std::map<std::string, int>{{"drizzle", 4}, {"none", 28}, {"rain", 15}, {"rain_showers", 73}}, "regression (6 K): type counts");
    require(count_of(day.intensity) == std::map<std::string, int>{{"heavy", 37}, {"light", 22}, {"moderate", 33}, {"none", 28}}, "regression (6 K): intensity counts");
    require(day.precipitating_cells == 92 && day.lightning_cells == 15 && day.hail_cells == 15 && day.snow_cells == 0, "regression (6 K): totals");
    require_near(day.max_rate_mm_h, 34.536929, "regression (6 K): max rate", 1.0e-4);
    require_near(day.total_accumulation_mm, 1691.128150, "regression (6 K): total accumulation", 1.0e-3);
    require_near(day.rate_mm_h[59], 34.536929, "regression (6 K): Cb rate at cell 59", 1.0e-4);
    require_near(day.hail_max_diameter_cm[59], 3.166024, "regression (6 K): hail at cell 59", 1.0e-4);
    require_near(day.updraft_m_s[59], 28.297109, "regression (6 K): updraft at cell 59", 1.0e-4);
    require_near(day.cloud_top_temperature_k[59], 227.627225, "regression (6 K): Cb top temperature", 1.0e-3);
    require(day.lightning[59] == 1 && day.precipitation_type[59] == "rain_showers", "regression (6 K): storm at cell 59");
    require_near(day.rate_mm_h[65], 0.5, "regression (6 K): stratus drizzle cap at cell 65", 1.0e-9);
    require_near(day.rate_mm_h[0], 2.453670, "regression (6 K): towering cumulus showers at cell 0", 1.0e-4);
    require_near(day.freezing_level_m[59], 3055.148837, "regression: freezing level at cell 59", 1.0e-3);
    require_near(day.surface_wet_bulb_k[59], 291.240704, "regression: wet-bulb at cell 59", 1.0e-4);
    require_near(day.precipitable_water_mm[59], 28.986359, "regression: precipitable water at cell 59", 1.0e-4);

    const auto night = diagnose_precipitation(state, c.grid, config, diagnose_cloud_formation(state, c.grid, config, 0.0, fronts), PrecipitationConfig{});
    require(count_of(night.precipitation_type) == std::map<std::string, int>{{"drizzle", 22}, {"none", 47}, {"rain", 38}, {"rain_showers", 13}}, "regression (0 K): type counts");
    require(count_of(night.intensity) == std::map<std::string, int>{{"heavy", 12}, {"light", 25}, {"moderate", 36}, {"none", 47}}, "regression (0 K): intensity counts");
    require(night.precipitating_cells == 73 && night.lightning_cells == 7 && night.hail_cells == 3, "regression (0 K): totals");
    require_near(night.max_rate_mm_h, 22.669414, "regression (0 K): max rate", 1.0e-4);
    require_near(night.total_accumulation_mm, 1811.962454, "regression (0 K): total accumulation", 1.0e-3);
    require(night.precipitation_type[78] == "rain" && night.character[78] == "continuous", "regression (0 K): nimbostratus rain at cell 78");
    require_near(night.rate_mm_h[78], 4.547595, "regression (0 K): Ns rate at cell 78", 1.0e-4);
    require_near(night.accumulation_mm[78], 36.380761, "regression (0 K): Ns accumulation at cell 78", 1.0e-3);
    require_near(night.rate_mm_h[104], 5.870360, "regression (0 K): Ns rate at cell 104", 1.0e-4);
    require_near(night.hail_max_diameter_cm[59], 0.682698, "regression (0 K): marginal hail at cell 59", 1.0e-4);
    require(night.precipitation_type[0] == "none" && night.rate_mm_h[0] == 0.0, "regression (0 K): clear cold sector");
}

void invalid_configurations_are_rejected() {
    require_rejected([] { PrecipitationConfig c; c.stratiform_efficiency = 1.5; validate_precipitation_config(c); }, "efficiency above 1 must be rejected");
    require_rejected([] { PrecipitationConfig c; c.snow_wet_bulb_k = 276.0; validate_precipitation_config(c); }, "snow threshold above sleet threshold must be rejected");
    require_rejected([] { PrecipitationConfig c; c.lightning_top_temperature_k = 280.0; validate_precipitation_config(c); }, "lightning threshold above 0 C must be rejected");
    require_rejected([] { PrecipitationConfig c; c.interval_seconds = 0.0; validate_precipitation_config(c); }, "zero interval must be rejected");
    require_rejected([] { (void)hail_terminal_velocity_m_s(-1.0); }, "negative diameter must be rejected");
    require_rejected([] { (void)precipitation_intensity_class(-1.0); }, "negative rate must be rejected");
    require_rejected([] {
        const auto grid = make_grid(2, 2, 50000.0);
        (void)diagnose_precipitation(uniform_state(4, 290.0, 0.01), grid, make_cloud_config(),
                                     make_clouds({"stratus"}, {300.0}, {1300.0}, {0.0}), PrecipitationConfig{});
    }, "cloud field size mismatch must be rejected");
}

}  // namespace

int main() {
    try {
        helper_references();
        harmless_clouds_deliver_nothing();
        nimbostratus_gives_prolonged_moderate_rain();
        cumulonimbus_gives_heavy_showers_lightning_and_hail();
        phase_follows_surface_wet_bulb_temperature();
        high_light_precipitation_into_dry_air_is_virga();
        regression_scenario_is_pinned();
        invalid_configurations_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "precipitation outcomes test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "precipitation outcome cases passed\n";
    return 0;
}
