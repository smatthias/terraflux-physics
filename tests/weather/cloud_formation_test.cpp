#include "terraflux/weather/cloud_formation.hpp"

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
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
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

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
}

TransportGrid2D make_grid(std::size_t nx, std::size_t ny, double spacing, double latitude = -37.8) {
    TransportGrid2D grid;
    grid.nx = nx;
    grid.ny = ny;
    grid.spacing_east_m = spacing;
    grid.spacing_north_m = spacing;
    grid.origin_east_m = -0.5 * static_cast<double>(nx) * spacing;
    grid.origin_north_m = -0.5 * static_cast<double>(ny) * spacing;
    grid.geographic_anchor_longitude_deg = 145.0;
    grid.geographic_anchor_latitude_deg = latitude;
    return grid;
}

CloudFormationConfig make_config() {
    CloudFormationConfig config;
    config.column.layer_count = 24;
    config.column.layer_thickness_m = 500.0;
    return config;
}

// Uniform state with optional per-cell overrides.
CoupledWindState uniform_state(const TransportGrid2D& grid, double temperature, double humidity) {
    CoupledWindState state;
    const std::size_t count = grid.nx * grid.ny;
    state.pressure_pa.assign(count, 100000.0);
    state.air_temperature_k.assign(count, temperature);
    state.specific_humidity_kg_kg.assign(count, humidity);
    state.wind_east_m_s.assign(count, 0.0);
    state.wind_north_m_s.assign(count, 0.0);
    return state;
}

// Baroclinic zone: cold air south, warm air north, with a tanh transition
// centred on row y_front of width `width` cells, plus a uniform northward wind
// component v0 and a convergence a (v = v0 - a (y - y_front)).
CoupledWindState front_state(const TransportGrid2D& grid, double jump_k, double width_cells, double v0, double convergence) {
    auto state = uniform_state(grid, 286.0, 0.010);
    const double y_front = 0.5 * static_cast<double>(grid.ny - 1);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        const double offset = (static_cast<double>(y) - y_front);
        for (std::size_t x = 0; x < grid.nx; ++x) {
            state.air_temperature_k[at(x, y, grid.nx)] = 286.0 + 0.5 * jump_k * std::tanh(offset / width_cells);
            state.wind_north_m_s[at(x, y, grid.nx)] = v0 - convergence * offset * grid.spacing_north_m;
        }
    }
    return state;
}

void cloud_classification_rules() {
    require(classify_cloud(800.0, 9000.0, true) == "cumulonimbus", "deep convective cloud is cumulonimbus");
    require(classify_cloud(1500.0, 7600.0, true) == "cumulonimbus", "a 6.1 km deep convective cloud is cumulonimbus even below 8 km");
    require(classify_cloud(1000.0, 4000.0, true) == "towering_cumulus", "2-6 km deep convective cloud is towering cumulus");
    require(classify_cloud(1000.0, 2000.0, true) == "cumulus", "shallow convective cloud is cumulus");
    require(classify_cloud(500.0, 4000.0, false) == "nimbostratus", "deep low layered cloud is nimbostratus");
    require(classify_cloud(500.0, 1500.0, false) == "stratus", "low layered cloud is stratus");
    require(classify_cloud(500.0, 800.0, false) == "stratocumulus", "thin low layered cloud is stratocumulus");
    require(classify_cloud(3000.0, 6000.0, false) == "altostratus", "thick mid-level layer is altostratus");
    require(classify_cloud(3000.0, 4000.0, false) == "altocumulus", "thin mid-level layer is altocumulus");
    require(classify_cloud(7000.0, 9000.0, false) == "cirrostratus", "thick high layer is cirrostratus");
    require(classify_cloud(7000.0, 7500.0, false) == "cirrus", "thin high layer is cirrus");
    require(classify_cloud(2000.0, 1000.0, false) == "none" && classify_cloud(NAN, 1000.0, true) == "none", "inverted or missing levels give no cloud");
    require(cloud_fraction_for_type("nimbostratus") == 1.0 && cloud_fraction_for_type("cumulus") == 0.3 && cloud_fraction_for_type("none") == 0.0, "fractions by type");
}

void diurnal_heating_cycle() {
    require_near(diurnal_surface_heating_k(0.0, 6.0, 0.0), 0.0, "no heating at the start of the cycle", 1.0e-12);
    require_near(diurnal_surface_heating_k(6.0 * 3600.0, 6.0, 0.0), 6.0, "peak heating six hours in", 1.0e-9);
    require_near(diurnal_surface_heating_k(18.0 * 3600.0, 6.0, 0.0), 0.0, "no negative heating at night", 1.0e-12);
    require_near(diurnal_surface_heating_k(4.0 * 3600.0, 6.0, -2.0 * 3600.0), 6.0, "phase shifts the peak", 1.0e-9);
    require_rejected([] { (void)diurnal_surface_heating_k(0.0, -1.0, 0.0); }, "negative amplitude must be rejected");
}

// Cold and warm fronts from the direction the air masses move; stationary
// without advection; no front below the gradient threshold.
void fronts_follow_temperature_gradient_and_advection() {
    const auto grid = make_grid(6, 12, 50000.0);
    const auto config = make_config();
    // 10 K across ~3 cells (150 km): about 3 K per 100 km at the centre.
    const auto cold = analyse_fronts(front_state(grid, 10.0, 1.5, 5.0, 0.0), grid, config);
    const auto warm = analyse_fronts(front_state(grid, 10.0, 1.5, -5.0, 0.0), grid, config);
    const auto stationary = analyse_fronts(front_state(grid, 10.0, 1.5, 0.0, 0.0), grid, config);
    const auto weak = analyse_fronts(front_state(grid, 1.0, 1.5, 5.0, 0.0), grid, config);
    require(cold.front_cell_count > 0 && cold.front_cell_count == warm.front_cell_count, "the same gradient band is frontal in both cases");
    for (std::size_t i = 0; i < grid.nx * grid.ny; ++i) {
        if (cold.front_type[i] != "none") {
            require(cold.front_type[i] == "cold", "wind from the cold toward the warm side makes a cold front");
            require(warm.front_type[i] == "warm", "wind from the warm toward the cold side makes a warm front");
            require(stationary.front_type[i] == "stationary", "no advection makes a stationary front");
            require(cold.temperature_advection_k_s[i] < 0.0 && warm.temperature_advection_k_s[i] > 0.0, "advection sign");
        }
        require(weak.front_type[i] == "none", "a 1 K contrast is below the frontal threshold");
    }
    require(weak.front_cell_count == 0 && std::isnan(weak.distance_to_front_m[0]), "no front means no distance");
    // Sides and distances: the northern rows are the warm side.
    const std::size_t north = at(2, grid.ny - 1, grid.nx), south = at(2, 0, grid.nx);
    require(cold.side[north] == 1 && cold.side[south] == -1, "warm side north, cold side south");
    require(cold.distance_to_front_m[north] > 100000.0 && cold.distance_to_front_m[south] > 100000.0, "edge rows are far from the band");
    require(cold.front_type[at(2, 5, grid.nx)] != "none" || cold.front_type[at(2, 6, grid.nx)] != "none", "the band sits at the centre rows");
    // Row 5 sits 0.5 cells south of the front centre; its neighbours are at -1.5 and +0.5.
    require_near(cold.temperature_gradient_k_m[at(2, 5, grid.nx)], 5.0 / (2.0 * 50000.0) * (std::tanh(0.5 / 1.5) - std::tanh(-1.5 / 1.5)), "central gradient", 1.0e-9);
}

// Boundary-layer convergence lifts air: w = -H div V, zero for divergence.
void convergence_sets_frontal_lifting() {
    const auto grid = make_grid(8, 4, 25000.0);
    auto config = make_config();
    config.boundary_layer_depth_m = 1000.0;
    auto state = uniform_state(grid, 288.0, 0.005);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const double east = grid.origin_east_m + (x + 0.5) * grid.spacing_east_m;
            state.wind_east_m_s[at(x, y, grid.nx)] = -2.0e-5 * east;  // div = -2e-5
        }
    }
    const auto converging = analyse_fronts(state, grid, config);
    for (std::size_t i = 0; i < grid.nx * grid.ny; ++i) {
        require_near(converging.divergence_s[i], -2.0e-5, "divergence of u = -a x", 1.0e-15);
        require_near(converging.lifting_m_s[i], 0.02, "w = -H div", 1.0e-12);
    }
    for (double& u : state.wind_east_m_s) u = -u;
    const auto diverging = analyse_fronts(state, grid, config);
    for (double w : diverging.lifting_m_s) require(w == 0.0, "divergent flow does not lift");
}

// Surface heating: the heated parcel gains CAPE and loses CIN; a warm, humid
// cell develops from cumulus to cumulonimbus as heating grows (over-
// development), a dry cell only ever gets fair-weather cumulus or nothing.
void surface_heating_drives_convective_development() {
    const auto grid = make_grid(2, 2, 50000.0);
    const auto config = make_config();
    auto state = uniform_state(grid, 294.0, 0.010);
    state.specific_humidity_kg_kg[3] = 0.002;  // dry cell
    const auto fronts = analyse_fronts(state, grid, config);
    require(fronts.front_cell_count == 0, "uniform air has no fronts");
    std::string previous_type = "none";
    double previous_cape = -1.0, previous_cin = 1.0e9;
    const std::vector<std::string> order{"none", "cumulus", "towering_cumulus", "cumulonimbus"};
    const auto rank = [&](const std::string& type) {
        for (std::size_t r = 0; r < order.size(); ++r) if (order[r] == type) return r;
        throw std::runtime_error("unexpected convective type " + type);
    };
    for (double heating : {0.0, 2.0, 4.0, 6.0, 8.0}) {
        const auto clouds = diagnose_cloud_formation(state, grid, config, heating, fronts);
        require(clouds.heated_cape_j_kg[0] >= previous_cape && clouds.heated_cin_j_kg[0] <= previous_cin, "heating raises CAPE and lowers CIN");
        previous_cape = clouds.heated_cape_j_kg[0];
        previous_cin = clouds.heated_cin_j_kg[0];
        require(rank(clouds.cloud_type[0]) >= rank(previous_type), "convective cloud only develops further with more heating");
        previous_type = clouds.cloud_type[0];
        require(clouds.cloud_type[3] == "none" || clouds.cloud_type[3] == "cumulus", "dry air gives at most fair-weather cumulus");
        if (clouds.cloud_type[0] != "none") {
            require(clouds.mechanism[0] == "convective" && (clouds.trigger[0] == "free" || clouds.trigger[0] == "thermals"), "heated convection is free or thermal");
        }
    }
    require(previous_type == "cumulonimbus" && previous_cape > 500.0, "8 K of heating over 21 C / 10 g/kg air ends in a thunderstorm");
    // The textbook afternoon sequence: none -> cumulus -> towering cumulus -> cumulonimbus.
    require(diagnose_cloud_formation(state, grid, config, 0.0, fronts).cloud_type[0] == "none" &&
                diagnose_cloud_formation(state, grid, config, 2.0, fronts).cloud_type[0] == "cumulus" &&
                diagnose_cloud_formation(state, grid, config, 4.0, fronts).cloud_type[0] == "towering_cumulus" &&
                diagnose_cloud_formation(state, grid, config, 6.0, fronts).cloud_type[0] == "cumulonimbus",
            "over-development with heating: none, cumulus, towering cumulus, cumulonimbus");
    const auto storm = diagnose_cloud_formation(state, grid, config, 8.0, fronts);
    require(storm.thunderstorm[0] == 1 && storm.thunderstorm_cells >= 3 && storm.top_m[0] > 8000.0, "cumulonimbus tops reach the upper troposphere");
    require(storm.base_m[0] > storm.base_m[0] - 1.0 && storm.base_m[0] < 3000.0, "convective base is the heated LCL");
}

// Combined: a parcel with CAPE but too much CIN to rise freely is released
// by frontal lifting that carries it above its level of free convection.
void frontal_lifting_releases_inhibited_convection() {
    const auto grid = make_grid(6, 12, 50000.0);
    auto config = make_config();
    config.lifting_time_scale_s = 12.0 * 3600.0;
    // Warm front (v0 < 0) with strong convergence (w = 0.09 m/s) on a
    // 22 C / 13 g/kg warm sector; the cold sector is dry enough not to convect.
    auto state = front_state(grid, 10.0, 1.5, -3.0, 6.0e-5);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            if (y >= 6) {
                state.air_temperature_k[at(x, y, grid.nx)] = 295.0;
                state.specific_humidity_kg_kg[at(x, y, grid.nx)] = 0.013;
            } else {
                state.specific_humidity_kg_kg[at(x, y, grid.nx)] = 0.005;
            }
        }
    }
    const auto fronts = analyse_fronts(state, grid, config);
    const auto lifted = diagnose_cloud_formation(state, grid, config, 0.0, fronts);
    config.lifting_time_scale_s = 600.0;  // almost no lifting
    const auto calm = diagnose_cloud_formation(state, grid, config, 0.0, fronts);
    std::size_t released = 0;
    for (std::size_t i = 0; i < grid.nx * grid.ny; ++i) {
        if (lifted.trigger[i] == "frontal" && (lifted.mechanism[i] == "frontal_convective" || lifted.mechanism[i] == "convective")) {
            ++released;
            require(lifted.mechanism[i] == "frontal_convective", "lifting-triggered convection near a front is the combined mechanism");
            require(lifted.heated_cin_j_kg[i] > config.cin_trigger_j_kg, "released cells were inhibited");
            require(lifted.heated_cape_j_kg[i] >= config.cape_threshold_j_kg, "released cells have CAPE");
            require(calm.trigger[i] != "frontal" || calm.mechanism[i] == "frontal", "without lifting the same cells stay inhibited (layered cloud at most)");
        }
    }
    require(released > 0, "frontal lifting must release inhibited convection somewhere in the warm sector");
}

// Overrunning ahead of a warm front: cloud base rises with distance along
// the gentle wedge and the layer sequence goes nimbostratus -> altostratus.
void warm_front_overrunning_layers_rise_with_distance() {
    const auto grid = make_grid(4, 20, 40000.0);
    auto config = make_config();
    config.warm_front_reach_m = 1200000.0;
    config.warm_front_slope = 1.0 / 150.0;
    // Frontal convergence of 1.2e-4 1/s lifts at 0.18 m/s; the warm advection
    // (v0 = -10 m/s) exceeds the convergent inflow so the whole band is a warm
    // front; the cold sector is dry.
    auto state = front_state(grid, 12.0, 1.0, -10.0, 1.2e-4);
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            state.specific_humidity_kg_kg[at(x, y, grid.nx)] = y < grid.ny / 2 ? 0.004 : 0.009;
        }
    }
    const auto fronts = analyse_fronts(state, grid, config);
    const auto clouds = diagnose_cloud_formation(state, grid, config, 0.0, fronts);
    double previous_base = -1.0;
    bool seen_nimbostratus = false, seen_altostratus = false;
    std::size_t warm_fronts = 0;
    for (std::size_t y = 0; y < grid.ny; ++y) {
        const std::size_t i = at(1, y, grid.nx);
        if (fronts.front_type[i] == "warm") ++warm_fronts;
    }
    require(warm_fronts > 0, "the band is a warm front");
    // Walk away from the front on the cold (southern) side.
    for (std::size_t y = grid.ny / 2; y-- > 0;) {
        const std::size_t i = at(1, y, grid.nx);
        if (fronts.side[i] != -1 || fronts.front_type[i] != "none") continue;
        if (clouds.trigger[i] != "overrunning") continue;
        require(clouds.base_m[i] >= previous_base, "overrunning cloud base rises with distance from the warm front");
        previous_base = clouds.base_m[i];
        if (clouds.cloud_type[i] == "nimbostratus") {
            require(!seen_altostratus, "nimbostratus lies closer to the front than altostratus");
            seen_nimbostratus = true;
        }
        if (clouds.cloud_type[i] == "altostratus") seen_altostratus = true;
        require_near(clouds.base_m[i] - std::max(fronts.distance_to_front_m[i] * config.warm_front_slope, 0.0), 0.0,
                     "base follows the wedge once above the frontal LCL", clouds.base_m[i] > fronts.distance_to_front_m[i] * config.warm_front_slope + 1.0 ? 1.0e9 : 1.0e-6);
    }
    require(seen_nimbostratus && seen_altostratus, "the warm-front sequence shows both low and mid-level layers");
}

// Regression scenario: a fixed 12 x 10 baroclinic case run for 8 hours through
// the coupled model, then diagnosed with 6 K of heating. Counts and selected
// cells are pinned; changing the physics must change these on purpose.
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
    const auto config = make_config();
    const auto fronts = analyse_fronts(state, c.grid, config);
    const auto count_of = [](const std::vector<std::string>& labels) {
        std::map<std::string, int> counts;
        for (const auto& label : labels) ++counts[label];
        return counts;
    };
    require(count_of(fronts.front_type) == std::map<std::string, int>{{"cold", 13}, {"none", 78}, {"stationary", 5}, {"warm", 24}},
            "regression: front type counts changed");
    require(fronts.front_cell_count == 42 && fronts.front_type[66] == "warm" && fronts.side[103] == -1, "regression: front cells");
    require_near(fronts.lifting_m_s[66], 0.3023, "regression: lifting at cell 66", 1.0e-3);
    require_near(fronts.temperature_advection_k_s[66], 5.532959e-4, "regression: warm advection at cell 66", 1.0e-9);

    // Afternoon (6 K heating): convection dominates, combined inside the frontal band.
    const auto heated = diagnose_cloud_formation(state, c.grid, config, 6.0, fronts);
    require(count_of(heated.cloud_type) == std::map<std::string, int>{{"altostratus", 5}, {"cumulonimbus", 15}, {"cumulus", 28}, {"nimbostratus", 10}, {"stratus", 4}, {"towering_cumulus", 58}},
            "regression (6 K): cloud type counts changed");
    require(count_of(heated.mechanism) == std::map<std::string, int>{{"convective", 34}, {"frontal", 19}, {"frontal_convective", 67}},
            "regression (6 K): mechanism counts changed");
    require(count_of(heated.trigger) == std::map<std::string, int>{{"free", 73}, {"frontal", 1}, {"overrunning", 18}, {"thermals", 28}},
            "regression (6 K): trigger counts changed");
    require(heated.cloudy_cells == 120 && heated.thunderstorm_cells == 15, "regression (6 K): totals changed");
    require(heated.cloud_type[0] == "towering_cumulus" && heated.mechanism[0] == "convective" && heated.trigger[0] == "free", "regression (6 K): cell 0");
    require_near(heated.base_m[0], 1715.140, "regression (6 K): base at cell 0", 0.01);
    require_near(heated.top_m[0], 4300.0, "regression (6 K): top at cell 0", 0.01);
    require_near(heated.heated_cape_j_kg[0], 100.807, "regression (6 K): CAPE at cell 0", 0.01);
    require(heated.cloud_type[66] == "towering_cumulus" && heated.mechanism[66] == "frontal_convective", "regression (6 K): cell 66 combined");
    require(heated.cloud_type[45] == "cumulonimbus" && heated.mechanism[45] == "frontal_convective", "regression (6 K): frontal thunderstorm at cell 45");
    require_near(heated.base_m[45], 1513.023, "regression (6 K): Cb base at cell 45", 0.01);
    require_near(heated.top_m[45], 7900.0, "regression (6 K): Cb top at cell 45", 0.01);

    // Night (no heating): frontal lifting and overrunning dominate.
    const auto night = diagnose_cloud_formation(state, c.grid, config, 0.0, fronts);
    require(count_of(night.cloud_type) == std::map<std::string, int>{{"altostratus", 5}, {"cumulonimbus", 7}, {"nimbostratus", 33}, {"none", 44}, {"stratocumulus", 3}, {"stratus", 22}, {"towering_cumulus", 6}},
            "regression (0 K): cloud type counts changed");
    require(count_of(night.mechanism) == std::map<std::string, int>{{"frontal", 63}, {"frontal_convective", 13}, {"none", 44}},
            "regression (0 K): mechanism counts changed");
    require(count_of(night.trigger) == std::map<std::string, int>{{"free", 8}, {"frontal", 37}, {"none", 44}, {"overrunning", 31}},
            "regression (0 K): trigger counts changed");
    require(night.cloudy_cells == 76 && night.thunderstorm_cells == 7, "regression (0 K): totals changed");
    require(night.cloud_type[0] == "none" && std::isnan(night.base_m[0]), "regression (0 K): clear cold sector at cell 0");
    require(night.cloud_type[66] == "nimbostratus" && night.trigger[66] == "frontal", "regression (0 K): cell 66 frontal");
    require_near(night.base_m[66], 920.413, "regression (0 K): base at cell 66", 0.01);
    require_near(night.top_m[66], 6779.265, "regression (0 K): top at cell 66", 0.01);
    require(night.trigger[103] == "overrunning" && night.cloud_type[103] == "nimbostratus", "regression (0 K): overrunning at cell 103");
    require_near(night.base_m[103], 924.246, "regression (0 K): base at cell 103", 0.01);
    require_near(night.top_m[103], 8350.959, "regression (0 K): top at cell 103", 0.01);
    require(night.cloud_type[57] == "cumulonimbus" && night.mechanism[57] == "frontal_convective" && night.trigger[57] == "free", "regression (0 K): thunderstorm in the frontal band at cell 57");
    require_near(night.base_m[57], 672.616, "regression (0 K): Cb base at cell 57", 0.01);
    require_near(night.top_m[57], 7400.0, "regression (0 K): Cb top at cell 57", 0.01);
}

void invalid_configurations_are_rejected() {
    const auto grid = make_grid(3, 3, 50000.0);
    const auto state = uniform_state(grid, 290.0, 0.005);
    require_rejected([&] { auto c = make_config(); c.warm_front_slope = 1.0 / 50.0; validate_cloud_formation_config(c); }, "a warm front steeper than a cold front must be rejected");
    require_rejected([&] { auto c = make_config(); c.boundary_layer_depth_m = 0.0; validate_cloud_formation_config(c); }, "zero boundary layer must be rejected");
    require_rejected([&] { auto c = make_config(); c.frontal_gradient_threshold_k_m = -1.0; validate_cloud_formation_config(c); }, "negative threshold must be rejected");
    require_rejected([&] { auto c = make_config(); c.column.layer_count = 1; validate_cloud_formation_config(c); }, "column configuration is validated too");
    require_rejected([&] { auto s = state; s.wind_east_m_s.pop_back(); (void)analyse_fronts(s, grid, make_config()); }, "array mismatch must be rejected");
    require_rejected([&] { (void)diagnose_cloud_formation(state, grid, make_config(), -1.0, analyse_fronts(state, grid, make_config())); }, "negative heating must be rejected");
}

}  // namespace

int main() {
    try {
        cloud_classification_rules();
        diurnal_heating_cycle();
        fronts_follow_temperature_gradient_and_advection();
        convergence_sets_frontal_lifting();
        surface_heating_drives_convective_development();
        frontal_lifting_releases_inhibited_convection();
        warm_front_overrunning_layers_rise_with_distance();
        regression_scenario_is_pinned();
        invalid_configurations_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << "cloud formation test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "cloud formation cases passed\n";
    return 0;
}
