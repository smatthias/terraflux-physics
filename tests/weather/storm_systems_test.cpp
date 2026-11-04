#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/storm_systems.hpp"

#include <algorithm>
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

TransportGrid2D make_grid(std::size_t nx, std::size_t ny, double spacing, double latitude) {
    TransportGrid2D grid;
    grid.nx = nx;
    grid.ny = ny;
    grid.spacing_east_m = spacing;
    grid.spacing_north_m = spacing;
    grid.origin_east_m = -0.5 * static_cast<double>(nx) * spacing;
    grid.origin_north_m = -0.5 * static_cast<double>(ny) * spacing;
    grid.geographic_anchor_longitude_deg = -75.0;
    grid.geographic_anchor_latitude_deg = latitude;
    return grid;
}

StormSystemsConfig make_config() {
    StormSystemsConfig config;
    config.column.layer_count = 32;
    config.column.layer_thickness_m = 500.0;
    config.column.tropopause_height_m = 15000.0;
    config.steering_factor = 0.0;
    config.beta_drift_west_m_s = 0.0;
    config.beta_drift_poleward_m_s = 0.0;
    config.genesis_gpi_threshold = 1.0e9;  // no spontaneous genesis unless a test lowers it
    return config;
}

// Warm, moist tropical state at rest.
CoupledWindState tropical_state(std::size_t count, double temperature = 300.0, double humidity = 0.018) {
    CoupledWindState state;
    state.pressure_pa.assign(count, 101000.0);
    state.air_temperature_k.assign(count, temperature);
    state.specific_humidity_kg_kg.assign(count, humidity);
    state.wind_east_m_s.assign(count, 0.0);
    state.wind_north_m_s.assign(count, 0.0);
    return state;
}

SurfaceConfig ocean(std::size_t count, double sst = 302.5) {
    SurfaceConfig surface;
    surface.land.assign(count, 0);
    surface.sea_surface_temperature_k.assign(count, sst);
    return surface;
}

void helper_references() {
    const auto config = make_config();
    // MPI by hand: k_s* - k_b over (T_s - T_o)/T_o with C_k/C_D = 0.9 and the 0.8 surface factor.
    const double sst = 302.0, ta = 301.0, p = 101000.0, to = 200.0;
    const double qa = 0.8 * saturation_specific_humidity_kg_kg(ta, p);
    const double k_sea = 1004.6 * sst + 2.501e6 * saturation_specific_humidity_kg_kg(sst, p);
    const double k_air = 1004.6 * ta + 2.501e6 * qa;
    const double expected = 0.8 * std::sqrt(0.9 * (sst - to) / to * (k_sea - k_air));
    const double mpi = potential_intensity_m_s(sst, ta, qa, p, to, config);
    require_near(mpi, expected, "MPI formula", 1.0e-9);
    require(mpi > 60.0 && mpi < 80.0, "MPI over a 29 C ocean is a strong category 4 to 5 wind");
    require(potential_intensity_m_s(299.0, 298.0, 0.015, p, to, config) == 0.0, "no MPI below the 26.5 C threshold");
    require(potential_intensity_m_s(302.0, 301.0, qa, p, to, config) > potential_intensity_m_s(300.0, 299.0, qa, p, to, config), "warmer sea gives more energy");
    require(potential_intensity_m_s(302.0, 301.0, qa, p, 220.0, config) < mpi, "a warmer outflow (lower Carnot efficiency) lowers the MPI");
    // Emanuel-Nolan GPI by hand.
    require_near(genesis_potential_index(5.0e-5, 0.7, 70.0, 5.0), std::pow(5.0, 1.5) * std::pow(70.0 / 50.0, 3.0) / (1.5 * 1.5), "GPI formula", 1.0e-9);
    require(genesis_potential_index(5.0e-5, 0.7, 70.0, 20.0) < genesis_potential_index(5.0e-5, 0.7, 70.0, 5.0), "shear suppresses genesis");
    require(genesis_potential_index(5.0e-5, 0.9, 70.0, 5.0) > genesis_potential_index(5.0e-5, 0.5, 70.0, 5.0), "moist mid-levels favour genesis");
    // Holland wind-pressure relation, surge, categories, Kaplan-DeMaria decay.
    require_near(holland_pressure_drop_pa(50.0, 1.15, 1.3), 1.15 * 2.718281828459045 * 2500.0 / 1.3, "Holland pressure drop", 1.0e-6);
    require_near(holland_pressure_drop_pa(50.0, 1.15, 1.3) / 100.0, 60.1, "a 50 m/s storm deepens by about 60 hPa", 0.2);
    require_near(surge_estimate_m(6012.0, 50.0, 1.5e-3), 6012.0 / (1025.0 * 9.80665) + 3.75, "surge = inverse barometer + wind setup", 1.0e-9);
    require(saffir_simpson_category(10.0) == "depression" && saffir_simpson_category(20.0) == "tropical_storm" &&
                saffir_simpson_category(33.0) == "category_1" && saffir_simpson_category(45.0) == "category_2" &&
                saffir_simpson_category(55.0) == "category_3" && saffir_simpson_category(65.0) == "category_4" &&
                saffir_simpson_category(75.0) == "category_5",
            "Saffir-Simpson boundaries");
    require_near(kaplan_demaria_wind_m_s(60.0, 24.0, config), 13.75 + (0.9 * 60.0 - 13.75) * std::exp(-0.095 * 24.0), "Kaplan-DeMaria decay", 1.0e-9);
    require(kaplan_demaria_wind_m_s(60.0, 24.0, config) < 20.0, "a 60 m/s storm is a depression after a day inland");
}

void environment_follows_sea_land_latitude_and_rotation() {
    const auto grid = make_grid(10, 8, 50000.0, 15.0);
    const std::size_t count = 80;
    auto state = tropical_state(count);
    // Solid-body cyclonic rotation about the centre: vorticity 2 omega.
    const double omega = 1.0e-5;
    for (std::size_t i = 0; i < count; ++i) {
        const double e = grid.origin_east_m + (i % 10 + 0.5) * 50000.0, n = grid.origin_north_m + (i / 10 + 0.5) * 50000.0;
        state.wind_east_m_s[i] = -omega * n;
        state.wind_north_m_s[i] = omega * e;
    }
    auto surface = ocean(count);
    for (std::size_t i = 0; i < count; ++i) if (i % 10 < 2) surface.land[i] = 1;   // land strip to the west
    surface.sea_surface_temperature_k[9] = 298.0;                                    // one cold cell
    const auto config = make_config();
    const auto env = diagnose_storm_environment(state, grid, surface, config);
    require_near(env.relative_vorticity_s[45], 2.0 * omega, "solid rotation has vorticity 2 omega", 1.0e-12);
    require(env.potential_intensity_m_s[45] > 60.0 && env.genesis_potential_index[45] > 1.0, "warm ocean cell has MPI and genesis potential");
    require(env.potential_intensity_m_s[40] == 0.0 && env.genesis_potential_index[40] == 0.0, "land has no potential intensity");
    require(env.potential_intensity_m_s[9] == 0.0, "cold sea has no potential intensity");
    require(env.mid_level_relative_humidity[45] > 0.3 && env.mid_level_relative_humidity[45] <= 1.0 && env.outflow_temperature_k[45] < 230.0, "mid-level humidity and a cold outflow are reported");
    require(env.enthalpy_disequilibrium_j_kg[45] > 0.0, "the sea is moister and warmer than the air: energy available");
    // Near the equator nothing can spin up; in a baroclinic zone shear suppresses genesis.
    const auto equator = diagnose_storm_environment(state, make_grid(10, 8, 50000.0, 1.0), surface, config);
    require(equator.potential_intensity_m_s[45] == 0.0 && equator.genesis_potential_index[45] == 0.0, "no potential within 10 degrees of the equator");
    auto baroclinic = state;
    for (std::size_t i = 0; i < count; ++i) baroclinic.air_temperature_k[i] = 300.0 - 2.0e-5 * (grid.origin_north_m + (i / 10 + 0.5) * 50000.0);
    const auto sheared = diagnose_storm_environment(baroclinic, grid, surface, config);
    require(sheared.vertical_shear_m_s[45] > 20.0 && sheared.genesis_potential_index[45] < 0.2 * env.genesis_potential_index[45], "thermal-wind shear of a 2 K/100 km gradient suppresses genesis");
}

void storm_intensifies_toward_potential_intensity_over_the_ocean() {
    const auto grid = make_grid(12, 12, 50000.0, 15.0);
    const auto state = tropical_state(144);
    const auto surface = ocean(144);
    const auto config = make_config();
    const auto env = diagnose_storm_environment(state, grid, surface, config);
    StormSystemsState storms;
    advance_storm_systems(storms, {{0.0, 0.0, 18.0, 40000.0}}, state, grid, surface, env, config, 0.0, 0.0);
    require(storms.storms.size() == 1 && storms.events.size() == 1 && storms.events[0].kind == "seed", "seed creates one storm");
    double previous = 18.0;
    for (int hour = 0; hour < 120; hour += 6) {
        advance_storm_systems(storms, {}, state, grid, surface, env, config, hour * 3600.0, 6.0 * 3600.0);
        require(storms.storms.size() == 1 && storms.storms[0].max_wind_m_s >= previous, "intensity grows monotonically over warm water");
        previous = storms.storms[0].max_wind_m_s;
    }
    const auto& s = storms.storms[0];
    require_near(s.max_wind_m_s, s.potential_intensity_m_s, "after five time scales the storm is near its potential intensity", 0.02 * s.potential_intensity_m_s);
    require(s.category == "category_4" || s.category == "category_5", "a 29.5 C ocean supports a major hurricane");
    require(s.central_pressure_pa < 94000.0 && s.central_pressure_pa > 86000.0, "central pressure of a major hurricane (record lows are near 870 hPa)");
    require_near(s.environment_pressure_pa - s.central_pressure_pa, holland_pressure_drop_pa(s.max_wind_m_s, moist_air_density_kg_m3(101000.0, 300.0, 0.018), 1.3), "Holland relation at the centre", 1.0e-6);
    require(s.surge_estimate_m > 5.0 && s.core_rain_rate_mm_h > 20.0, "surge and core rain scale with intensity");
    require(std::abs(s.east_m) < 1.0 && std::abs(s.north_m) < 1.0, "without steering or drift the storm stays put");
}

void landfall_reduces_and_then_decays_the_storm() {
    const auto grid = make_grid(12, 8, 50000.0, 15.0);
    const auto state = tropical_state(96);
    auto surface = ocean(96);
    for (std::size_t i = 0; i < 96; ++i) if (i % 12 < 5) surface.land[i] = 1;   // coast at east = -50 km
    auto config = make_config();
    config.beta_drift_west_m_s = 2.0;  // 2 m/s westward drift -> 100 km in 14 h
    const auto env = diagnose_storm_environment(state, grid, surface, config);
    StormSystemsState storms;
    advance_storm_systems(storms, {{50000.0, 25000.0, 50.0, 40000.0}}, state, grid, surface, env, config, 0.0, 0.0);
    double wind_before_landfall = 0.0;
    for (int hour = 0; hour < 48; hour += 2) {
        const bool was_ocean = !storms.storms[0].over_land;
        if (was_ocean) wind_before_landfall = storms.storms[0].max_wind_m_s;
        advance_storm_systems(storms, {}, state, grid, surface, env, config, hour * 3600.0, 2.0 * 3600.0);
        require(storms.storms.size() == 1, "the storm survives two days");
    }
    const auto landfall = std::find_if(storms.events.begin(), storms.events.end(), [](const StormEvent& e) { return e.kind == "landfall"; });
    require(landfall != storms.events.end(), "a landfall event is recorded");
    require(landfall->east_m < -50000.0 && landfall->east_m > -100000.0 && landfall->surge_m > 3.0, "landfall at the coast with a storm surge");
    require(landfall->elapsed_seconds > 8.0 * 3600.0 && landfall->elapsed_seconds < 20.0 * 3600.0, "landfall after the 100 km drift");
    require(landfall->category == "category_3" || landfall->category == "category_4", "landfall category");
    const auto& s = storms.storms[0];
    require(s.over_land && s.time_over_land_seconds > 0.0 && s.max_wind_m_s < landfall->max_wind_m_s, "the storm decays inland");
    // Exact Kaplan-DeMaria trajectory from the reduced landfall wind.
    const double expected = 13.75 + (landfall->max_wind_m_s - 13.75) * std::exp(-0.095 / 3600.0 * s.time_over_land_seconds);
    require_near(s.max_wind_m_s, expected, "inland decay follows Kaplan-DeMaria", 1.0e-6);
    require(landfall->max_wind_m_s < wind_before_landfall, "the landfall reduction lowers the wind at the coast");
    require(s.potential_intensity_m_s == 0.0, "no potential intensity over land");
}

void storms_rotate_around_each_other_and_merge_when_cores_overlap() {
    const auto grid = make_grid(16, 16, 50000.0, 15.0);
    const auto state = tropical_state(256);
    const auto surface = ocean(256);
    auto config = make_config();
    config.induction_factor = 1.0;
    const auto env = diagnose_storm_environment(state, grid, surface, config);
    for (double latitude : {15.0, -15.0}) {
        const auto local_grid = make_grid(16, 16, 50000.0, latitude);
        const auto local_env = diagnose_storm_environment(state, local_grid, surface, config);
        StormSystemsState storms;
        advance_storm_systems(storms, {{-150000.0, 0.0, 30.0, 40000.0}, {150000.0, 0.0, 30.0, 40000.0}}, state, local_grid, surface, local_env, config, 0.0, 0.0);
        const double angle0 = std::atan2(storms.storms[0].north_m, storms.storms[0].east_m);
        advance_storm_systems(storms, {}, state, local_grid, surface, local_env, config, 0.0, 3.0 * 3600.0);
        require(storms.storms.size() == 2, "300 km apart the storms stay separate");
        const double angle1 = std::atan2(storms.storms[0].north_m, storms.storms[0].east_m);
        double turn = angle1 - angle0;
        while (turn > M_PI) turn -= 2.0 * M_PI;
        while (turn < -M_PI) turn += 2.0 * M_PI;
        require(latitude > 0.0 ? turn > 0.01 : turn < -0.01, "Fujiwhara rotation is counterclockwise in the NH and clockwise in the SH");
        const double separation = std::hypot(storms.storms[0].east_m - storms.storms[1].east_m, storms.storms[0].north_m - storms.storms[1].north_m);
        require_near(separation, 300000.0, "mutual rotation keeps the separation", 15000.0);
    }
    StormSystemsState merging;
    advance_storm_systems(merging, {{-70000.0, 0.0, 35.0, 40000.0}, {70000.0, 0.0, 25.0, 40000.0}}, state, grid, surface, env, config, 0.0, 0.0);
    advance_storm_systems(merging, {}, state, grid, surface, env, config, 0.0, 600.0);
    require(merging.storms.size() == 1, "cores 140 km apart (within 40 + 40 + 100 km) merge");
    const auto merger = std::find_if(merging.events.begin(), merging.events.end(), [](const StormEvent& e) { return e.kind == "merger"; });
    require(merger != merging.events.end() && merger->storm_id == 1 && merger->other_id == 2, "the stronger storm absorbs the weaker");
    require(merging.storms[0].max_wind_m_s >= 35.0 && std::abs(merging.storms[0].east_m) < 30000.0, "the merged storm keeps the stronger wind near the weighted centroid");
}

void genesis_occurs_only_in_favourable_environments() {
    const auto grid = make_grid(12, 12, 50000.0, 15.0);
    auto state = tropical_state(144, 300.0, 0.019);
    const double omega = 2.0e-5;   // pre-existing cyclonic disturbance
    for (std::size_t i = 0; i < 144; ++i) {
        const double e = grid.origin_east_m + (i % 12 + 0.5) * 50000.0, n = grid.origin_north_m + (i / 12 + 0.5) * 50000.0;
        state.wind_east_m_s[i] = -omega * n;
        state.wind_north_m_s[i] = omega * e;
    }
    auto config = make_config();
    config.genesis_gpi_threshold = 8.0;
    const auto warm = diagnose_storm_environment(state, grid, ocean(144, 303.0), config);
    StormSystemsState storms;
    advance_storm_systems(storms, {}, state, grid, ocean(144, 303.0), warm, config, 0.0, 600.0);
    require(storms.storms.size() == 1 && storms.events[0].kind == "genesis" && storms.storms[0].max_wind_m_s >= config.genesis_max_wind_m_s, "warm, moist, rotating air spawns a storm");
    require(std::abs(storms.events[0].east_m) <= 225000.0 && std::abs(storms.events[0].north_m) <= 225000.0, "genesis respects the 100 km edge margin");
    advance_storm_systems(storms, {}, state, grid, ocean(144, 303.0), warm, config, 600.0, 600.0);
    require(storms.storms.size() == 1, "no second genesis within the separation distance");
    const auto cold = diagnose_storm_environment(state, grid, ocean(144, 298.0), config);
    StormSystemsState none;
    advance_storm_systems(none, {}, state, grid, ocean(144, 298.0), cold, config, 0.0, 3600.0);
    require(none.storms.empty(), "a 25 C ocean spawns nothing");
}

void storm_fields_follow_the_vortex_profiles() {
    const auto grid = make_grid(12, 8, 50000.0, 15.0);
    const auto state = tropical_state(96);
    auto surface = ocean(96);
    for (std::size_t i = 0; i < 96; ++i) if (i % 12 < 4) surface.land[i] = 1;  // coast at east = -100 km
    const auto config = make_config();
    const auto env = diagnose_storm_environment(state, grid, surface, config);
    StormSystemsState storms;
    advance_storm_systems(storms, {{25000.0, 25000.0, 50.0, 50000.0}}, state, grid, surface, env, config, 0.0, 0.0);
    const auto fields = storm_fields(storms, grid, surface, config);
    const std::size_t centre = 4 * 12 + 6;  // cell containing (25 km, 25 km)
    require(fields.wind_speed_m_s[centre] < 1.0, "calm eye at the centre");
    const std::size_t ring = 4 * 12 + 7;    // 50 km east: the radius of maximum wind
    require_near(fields.wind_speed_m_s[ring], 50.0, "maximum wind at the radius of maximum wind", 1.0e-9);
    require_near(fields.wind_speed_m_s[4 * 12 + 8], 25.0, "Rankine decay: half the wind at twice the radius", 1.0e-9);
    require_near(fields.pressure_anomaly_pa[centre], -(storms.storms[0].environment_pressure_pa - storms.storms[0].central_pressure_pa) * (1.0 - std::exp(-std::pow(50000.0 / 1.0, 1.3))), "full pressure drop at the centre", 1.0);
    require(fields.rain_rate_mm_h[centre] > fields.rain_rate_mm_h[4 * 12 + 9], "rain decays away from the core");
    std::size_t surge_cells = 0;
    for (std::size_t i = 0; i < 96; ++i) {
        if (fields.surge_m[i] > 0.0) {
            ++surge_cells;
            require(surface.land[i] == 1 && i % 12 == 3, "surge only on the coastal land column");
        }
    }
    require(surge_cells > 0, "the nearby coast receives a surge");
}

void invalid_inputs_are_rejected() {
    const auto grid = make_grid(4, 4, 50000.0, 15.0);
    const auto state = tropical_state(16);
    require_rejected([&] { auto s = ocean(16); s.land[3] = 2; validate_surface(s, grid); }, "land mask values other than 0/1 must be rejected");
    require_rejected([&] { auto s = ocean(16); s.sea_surface_temperature_k.pop_back(); validate_surface(s, grid); }, "SST size mismatch must be rejected");
    require_rejected([&] { auto c = make_config(); c.landfall_reduction = 1.5; validate_storm_systems_config(c); }, "landfall reduction above 1 must be rejected");
    require_rejected([&] { auto c = make_config(); c.induction_factor = -0.1; validate_storm_systems_config(c); }, "negative induction factor must be rejected");
    require_rejected([&] { (void)genesis_potential_index(1.0e-5, 0.5, -1.0, 2.0); }, "negative potential intensity must be rejected");
    require_rejected([&] { (void)holland_pressure_drop_pa(30.0, 0.0, 1.3); }, "zero density must be rejected");
    require_rejected([&] {
        const auto config = make_config();
        const auto env = diagnose_storm_environment(state, grid, ocean(16), config);
        StormSystemsState storms;
        advance_storm_systems(storms, {{5.0e6, 0.0, 20.0, 40000.0}}, state, grid, ocean(16), env, config, 0.0, 0.0);
    }, "a seed outside the grid must be rejected");
}

}  // namespace

int main() {
    try {
        helper_references();
        environment_follows_sea_land_latitude_and_rotation();
        storm_intensifies_toward_potential_intensity_over_the_ocean();
        landfall_reduces_and_then_decays_the_storm();
        storms_rotate_around_each_other_and_merge_when_cores_overlap();
        genesis_occurs_only_in_favourable_environments();
        storm_fields_follow_the_vortex_profiles();
        invalid_inputs_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "storm systems test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "storm system cases passed\n";
    return 0;
}
