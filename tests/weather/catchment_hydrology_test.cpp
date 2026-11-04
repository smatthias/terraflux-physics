#include "terraflux/weather/catchment_hydrology.hpp"
#include "terraflux/weather/synthetic_terrain.hpp"

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

CatchmentConfig make_config(std::size_t nx, std::size_t ny, double spacing = 1000.0) {
    CatchmentConfig c;
    c.grid.nx = nx;
    c.grid.ny = ny;
    c.grid.origin_east_m = -0.5 * static_cast<double>(nx) * spacing;
    c.grid.origin_north_m = -0.5 * static_cast<double>(ny) * spacing;
    c.grid.spacing_east_m = spacing;
    c.grid.spacing_north_m = spacing;
    c.grid.geographic_anchor_longitude_deg = 10.5;
    c.grid.geographic_anchor_latitude_deg = 46.8;
    c.timestep_seconds = 600.0;
    c.sea_level_m = 0.0;
    c.open_edges = false;
    return c;
}

// A plane rising northward from an ocean strip on the southern rows.
std::vector<double> tilted_plane(const CatchmentConfig& c, double slope = 0.01, double offset = -12.0) {
    SyntheticTerrainSpec t;
    t.base_elevation_m = offset;
    t.slope_north_m_per_m = slope;
    return build_synthetic_terrain(t, c.grid);
}

CatchmentForcing uniform_forcing(std::size_t count, double rate_mm_h, double temperature_k = 285.0) {
    CatchmentForcing f;
    f.precipitation_rate_m_s.assign(count, rate_mm_h / 3.6e6);
    f.air_temperature_k.assign(count, temperature_k);
    return f;
}

double sum(const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0); }

// 1. Every drop has a path: the network drains all land to the ocean, areas accumulate and basins partition the land.
void network_drains_every_land_cell() {
    auto c = make_config(12, 10);
    const auto z = tilted_plane(c);
    const auto n = build_catchment_network(z, c);
    const std::size_t count = z.size();
    std::size_t ocean = 0, land = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (n.ocean[i]) { ++ocean; require(n.downstream[i] < 0 && n.basin_id[i] < 0, "ocean cells are not routed"); continue; }
        ++land;
        // Follow the path to the sea; it must be finite and strictly descending on the filled surface.
        std::size_t current = i, hops = 0;
        while (n.downstream[current] >= 0) {
            const auto next = static_cast<std::size_t>(n.downstream[current]);
            require(n.filled_elevation_m[next] < n.filled_elevation_m[current], "receivers lie lower on the filled surface");
            require(n.upstream_area_m2[next] > n.upstream_area_m2[current] || n.ocean[next], "upstream area grows downstream");
            if (n.ocean[next]) break;
            current = next;
            require(++hops <= count, "paths terminate");
        }
        require(n.ocean[static_cast<std::size_t>(n.downstream[current])], "closed domain: all land water reaches the ocean");
        require(n.terminal[current] == TerminalKind::ocean, "coast cells are ocean terminals");
        require(n.basin_id[i] >= 0 && static_cast<std::size_t>(n.basin_id[i]) < n.basins.size(), "every land cell belongs to a basin");
        require(n.regolith_depth_m[i] > 0.0 && n.regolith_depth_m[i] <= c.soil.regolith_depth_m, "regolith depth positive and bounded");
    }
    require(ocean == 12 && land == count - 12, "the lowest row is the sea (z <= sea level)");
    double basin_area = 0.0;
    for (const auto& b : n.basins) { basin_area += b.area_m2; require(b.terminal == TerminalKind::ocean, "basins drain to the ocean"); }
    require_near(basin_area, static_cast<double>(land) * 1.0e6, "basins partition the land", 1.0);
    require(n.lakes.empty(), "a plane has no lakes");
    // Straight downhill flow: direction 6 is south (toward the sea), area = rows above x cell area.
    const std::size_t mid = 5 * 12 + 6;
    require(n.flow_direction[mid] == 6, "flow on a north-rising plane points south");
    require_near(n.upstream_area_m2[mid], 5.0 * 1.0e6, "upstream area counts the cells above", 1.0);
    require(n.strahler_order[mid] == 1 && n.channel[1 * 12 + 6] == 1 && n.channel[8 * 12 + 6] == 0, "channels start where the area passes the initiation threshold");
    require_near(n.distance_to_outlet_m[mid], 5000.0, "distance to the sea", 1.0e-6);
    require(n.channel_target[8 * 12 + 6] == 6 * 12 + 6 && n.channel[6 * 12 + 6] == 1 && n.channel[7 * 12 + 6] == 0, "the nearest downstream channel starts at four cells of upstream area");
    // Channel width and bankfull depth follow the power laws.
    const std::size_t coast = 1 * 12 + 6;
    require_near(n.channel_width_m[coast], c.channels.width_coefficient_m * std::pow(9.0, c.channels.width_exponent), "hydraulic geometry width", 1.0e-9);
    require_near(n.bankfull_depth_m[coast], c.channels.bankfull_depth_coefficient_m * std::pow(9.0, c.channels.bankfull_depth_exponent), "hydraulic geometry depth", 1.0e-9);
    // Open edges: the same plane without an ocean drains across the edges.
    auto open = make_config(12, 10);
    open.open_edges = true;
    open.sea_level_m = -100.0;
    const auto m = build_catchment_network(z, open);
    require(std::all_of(m.ocean.begin(), m.ocean.end(), [](unsigned char v) { return v == 0; }), "no ocean below sea level");
    require(m.terminal[6] == TerminalKind::edge && m.downstream[6] < 0, "the southern edge cell leaves the domain");
    require(m.basins.front().terminal == TerminalKind::edge, "basins drain across the edge");
    auto closed = make_config(12, 10);
    closed.sea_level_m = -100.0;
    require_rejected([&] { build_catchment_network(z, closed); }, "a closed domain without ocean cannot drain");
}

// 2. Depressions become lakes with a spill level, capacity and outlet; they fill before they overflow.
void depressions_fill_then_spill() {
    auto c = make_config(12, 10);
    auto z = tilted_plane(c);
    const std::size_t pit = 6 * 12 + 6;
    z[pit] -= 12.0;  // a 1-cell pit: 2 m below its southern (lowest) neighbour on the 10 m/row plane
    const auto n = build_catchment_network(z, c);
    require(n.lakes.size() == 1 && n.lake_id[pit] == 0 && n.lakes[0].cells.size() == 1, "the pit is a single-cell lake");
    // The spill is the lowest neighbour: the cell directly south (z lower by 10 m per row).
    const std::size_t south = pit - 12;
    require_near(n.lakes[0].spill_elevation_m, z[south], "spill level equals the lowest neighbour", 1.0e-5);
    require_near(n.lakes[0].capacity_m3, (z[south] - z[pit]) * 1.0e6, "capacity is the filled depth times the area", 1.0);
    require(n.lakes[0].outlet_cell == static_cast<long>(south), "the lake outlet is the spill receiver");
    require(n.downstream[pit + 12] == static_cast<long>(pit), "the cell north of the pit drains into it");
    require_near(n.filled_elevation_m[pit], n.lakes[0].spill_elevation_m, "filled elevation equals the spill", 1.0e-5);
    // Start empty, rain only on the pit's upstream cells: nothing leaves until the lake is full.
    c.channels.lake_initial_fill_fraction = 0.0;
    c.soil.infiltration_max_m_s = 0.0;
    c.soil.infiltration_min_m_s = 0.0;
    c.soil.potential_evapotranspiration_m_s = 0.0;
    c.groundwater.initial_fill_fraction = 0.0;
    c.soil.detention_storage_m = 0.0;
    auto s = initialize_catchment(n, c);
    require_near(s.lake_volume_m3[0], 0.0, "lake starts empty", 1.0e-9);
    CatchmentForcing f = uniform_forcing(z.size(), 0.0);
    for (std::size_t y = 7; y < 10; ++y) f.precipitation_rate_m_s[y * 12 + 6] = 20.0 / 3.6e6;  // 20 mm/h on the three cells above the pit
    f.precipitation_rate_m_s[pit] = 20.0 / 3.6e6;
    double previous = 0.0;
    bool spilled = false;
    for (int k = 0; k < 400; ++k) {
        s = advance_catchment(s, f, n, c);
        require(s.lake_volume_m3[0] <= n.lakes[0].capacity_m3 + 1.0e-6, "a lake never exceeds its capacity");
        require(s.lake_volume_m3[0] + 1.0e-9 >= previous, "the lake fills monotonically");
        previous = s.lake_volume_m3[0];
        require(std::abs(s.balance_error_m3) < 1.0e-6, "lake budget closes");
        if (s.lake_volume_m3[0] >= n.lakes[0].capacity_m3 - 1.0e-6) spilled = true;
        if (!spilled) require(s.totals.ocean_discharge_m3 < 1.0e-9, "nothing reaches the sea before the lake spills");
    }
    require(spilled && s.totals.ocean_discharge_m3 > 0.0, "once full, the lake overflows and the water continues to the sea");
    require_near(s.lake_level_m[0], n.lakes[0].spill_elevation_m, "full lake sits at the spill level", 1.0e-6);
    require_near(s.lake_depth_m[pit], n.lakes[0].spill_elevation_m - z[pit], "lake depth at the pit", 1.0e-6);
}

// 3. Steady rain on a closed basin: the water budget closes and the coast discharge approaches rain x area.
void steady_rain_reaches_the_sea() {
    auto c = make_config(12, 10);
    const auto z = tilted_plane(c);
    c.soil.infiltration_max_m_s = 0.0;
    c.soil.infiltration_min_m_s = 0.0;
    c.soil.potential_evapotranspiration_m_s = 0.0;
    c.soil.detention_storage_m = 0.0;
    c.groundwater.initial_fill_fraction = 0.0;
    c.groundwater.hydraulic_conductivity_m_s = 0.0;
    const auto n = build_catchment_network(z, c);
    auto s = initialize_catchment(n, c);
    const double initial = total_storage_m3(s, c);
    const double rate = 10.0 / 3.6e6;
    const auto f = uniform_forcing(z.size(), 10.0);
    const double land_area = 12.0 * 9.0 * 1.0e6;
    for (int k = 0; k < 600; ++k) {
        s = advance_catchment(s, f, n, c);
        require(std::abs(s.balance_error_m3) < 1.0e-5, "step budget closes");
        const double stored = total_storage_m3(s, c);
        require_near(stored + s.totals.ocean_discharge_m3 + s.totals.edge_outflow_m3 + s.totals.evapotranspiration_m3,
                     initial + s.totals.precipitation_m3, "cumulative budget: stores + outflow + ET = initial + rain", 1.0e-4 * std::max(1.0, s.totals.precipitation_m3));
    }
    require_near(s.totals.precipitation_m3, rate * 600.0 * 600.0 * land_area, "rain volume counts the land only", 1.0);
    // Total discharge over the last hour approaches the steady rainfall volume on the land.
    const double before = s.totals.ocean_discharge_m3;
    for (int k = 0; k < 6; ++k) s = advance_catchment(s, f, n, c);
    const double hourly = s.totals.ocean_discharge_m3 - before;
    require_near(hourly / (rate * 3600.0 * land_area), 1.0, "steady state: outflow equals rainfall", 0.05);
    double column_q = 0.0;
    for (std::size_t x = 0; x < 12; ++x) column_q += s.discharge_m3_s[1 * 12 + x];
    // The coast reaches carry everything except the rain that falls on the coast cells themselves and runs straight into the sea.
    require(column_q / (rate * land_area) > 0.85 && column_q / (rate * land_area) <= 1.0 + 1.0e-9, "coast channels carry the basin runoff");
    require(std::all_of(s.surface_m.begin(), s.surface_m.end(), [](double v) { return v >= 0.0; }), "no negative stores");
}

// 4. Vertical column: infiltration at the wetness-dependent capacity, percolation, springs, snow.
void column_processes_follow_the_rates() {
    auto c = make_config(4, 4);
    c.open_edges = true;
    c.sea_level_m = -100.0;
    c.groundwater.hydraulic_conductivity_m_s = 0.0;
    c.groundwater.baseflow_time_scale_s = 1.0e9;
    c.soil.potential_evapotranspiration_m_s = 0.0;
    c.soil.percolation_rate_m_s = 0.0;
    c.soil.initial_saturation = 0.0;
    c.soil.regolith_depth_m = 1.0;
    c.soil.detention_storage_m = 1.0;  // nothing runs off: isolate the column
    const auto z = tilted_plane(c, 0.0, 100.0);
    const auto n = build_catchment_network(z, c);
    auto s = initialize_catchment(n, c);
    const std::size_t i = 5;
    const double capacity = c.soil.porosity * n.regolith_depth_m[i];
    // Dry soil: infiltration at f_max for one step.
    auto f = uniform_forcing(16, 50.0);
    s = advance_catchment(s, f, n, c);
    const double rain = 50.0 / 3.6e6 * 600.0;
    require_near(s.soil_m[i], c.soil.infiltration_max_m_s * 600.0, "dry soil infiltrates at the maximum capacity", 1.0e-12);
    require_near(s.surface_m[i], rain - c.soil.infiltration_max_m_s * 600.0, "the excess stays on the surface (infiltration-excess runoff)", 1.0e-12);
    // Wet soil: capacity drops linearly with saturation.
    const double saturation = s.soil_m[i] / capacity;
    const double expected_rate = c.soil.infiltration_min_m_s + (c.soil.infiltration_max_m_s - c.soil.infiltration_min_m_s) * (1.0 - saturation);
    const double soil_before = s.soil_m[i];
    s = advance_catchment(s, f, n, c);
    require_near(s.soil_m[i] - soil_before, expected_rate * 600.0, "Horton capacity falls with wetness", 1.0e-12);
    // Saturation-excess: fill the soil, then nothing more infiltrates.
    s.soil_m.assign(16, capacity);
    const double surface_before = s.surface_m[i];
    s = advance_catchment(s, f, n, c);
    require_near(s.surface_m[i], surface_before + rain, "a saturated soil rejects all rain", 1.0e-12);
    // Percolation above field capacity at the configured rate, limited by the aquifer space.
    c.soil.percolation_rate_m_s = 1.0e-6;
    c.groundwater.initial_fill_fraction = 0.0;
    auto s2 = initialize_catchment(n, c);
    s2.soil_m.assign(16, capacity);
    const auto dry = uniform_forcing(16, 0.0);
    s2 = advance_catchment(s2, dry, n, c);
    require_near(s2.groundwater_m[i], 1.0e-6 * 600.0, "full soil percolates at the percolation rate", 1.0e-9);
    require_near(s2.soil_m[i], capacity - 1.0e-6 * 600.0, "percolation leaves the soil", 1.0e-12);
    require_near(s2.totals.percolation_m3, 1.0e-6 * 600.0 * 16.0 * 1.0e6, "percolation total", 1.0e-3);
    // Springs: an over-full aquifer exfiltrates to the surface.
    auto s3 = initialize_catchment(n, c);
    const double aquifer_capacity = c.groundwater.specific_yield * c.groundwater.aquifer_thickness_m;
    s3.groundwater_m[i] = aquifer_capacity + 0.01;
    s3 = advance_catchment(s3, dry, n, c);
    require_near(s3.groundwater_m[i], aquifer_capacity, "the aquifer cannot hold more than S_y B", 1.0e-9);
    require_near(s3.surface_m[i], 0.01, "the excess emerges as a spring", 1.0e-6);
    require_near(s3.water_table_head_m[i], z[i], "a full aquifer has its water table at the surface", 1.0e-6);
    // Snow: below the threshold rain is stored as snow; above freezing it melts at the degree-day rate.
    auto s4 = initialize_catchment(n, c);
    s4 = advance_catchment(s4, uniform_forcing(16, 10.0, 270.0), n, c);
    require_near(s4.snow_m[i], 10.0 / 3.6e6 * 600.0, "cold precipitation becomes snow", 1.0e-12);
    require_near(s4.surface_m[i] + s4.soil_m[i], 0.0, "no liquid water from snowfall", 1.0e-12);
    const double snow_before = s4.snow_m[i];
    s4 = advance_catchment(s4, uniform_forcing(16, 0.0, 276.15), n, c);
    require_near(snow_before - s4.snow_m[i], c.snow.degree_day_factor_m_k_s * 3.0 * 600.0, "degree-day melt", 1.0e-12);
    require_near(s4.totals.snowmelt_m3, c.snow.degree_day_factor_m_k_s * 3.0 * 600.0 * 16.0 * 1.0e6, "melt total", 1.0e-3);
    // Evapotranspiration: surface first, then the soil under moisture stress; lakes evaporate too.
    c.soil.potential_evapotranspiration_m_s = 1.0e-7;
    auto s5 = initialize_catchment(n, c);
    s5.surface_m[i] = 1.0e-7 * 600.0 * 0.5;
    s5.soil_m[i] = capacity;  // no stress
    s5 = advance_catchment(s5, dry, n, c);
    require_near(s5.surface_m[i], 0.0, "surface water evaporates first", 1.0e-12);
    const double after_et = capacity - 1.0e-7 * 600.0 * 0.5, held = c.soil.field_capacity_fraction * capacity;
    const double percolated = 1.0e-6 * 600.0 * (after_et - held) / (capacity - held);
    require_near(s5.soil_m[i], after_et - percolated, "the remaining demand is met from the soil, then percolation scales with the drainable fraction", 1.0e-12);
}

// 5. Groundwater moves down the head gradient and emerges as baseflow in the channels.
void groundwater_flows_toward_lower_head() {
    auto c = make_config(6, 6);
    c.open_edges = true;
    c.sea_level_m = -100.0;
    c.groundwater.baseflow_time_scale_s = 1.0e15;  // effectively no baseflow
    c.soil.potential_evapotranspiration_m_s = 0.0;
    c.groundwater.hydraulic_conductivity_m_s = 1.0e-3;
    const auto z = tilted_plane(c, 0.0, 100.0);
    const auto n = build_catchment_network(z, c);
    auto s = initialize_catchment(n, c);
    const std::size_t centre = 3 * 6 + 3;
    const double fill = c.groundwater.initial_fill_fraction * c.groundwater.specific_yield * c.groundwater.aquifer_thickness_m;
    s.groundwater_m[centre] = fill + 0.2;
    const double total_before = sum(s.groundwater_m);
    const auto dry = uniform_forcing(36, 0.0);
    s = advance_catchment(s, dry, n, c);
    require(s.groundwater_m[centre] < fill + 0.2, "the mound drains");
    for (std::size_t k : {centre - 1, centre + 1, centre - 6, centre + 6}) require(s.groundwater_m[k] > fill, "neighbours receive groundwater");
    require_near(s.groundwater_m[centre - 7], fill, "diagonal cells are not Darcy neighbours", 1.0e-9);
    require_near(sum(s.groundwater_m), total_before, "Darcy exchange conserves water", 1.0e-9);
    // The transfer per neighbour equals K b dh/L dt / L with the donor's saturated thickness.
    const double head_difference = 0.2 / c.groundwater.specific_yield;
    const double thickness = std::min(c.groundwater.aquifer_thickness_m, (fill + 0.2) / c.groundwater.specific_yield);
    const double expected = 1.0e-3 * thickness * head_difference / 1000.0 * 600.0 / 1000.0;
    require_near(s.groundwater_m[centre + 1] - fill, expected, "explicit Darcy transfer", 1.0e-9);
    require(s.water_table_head_m[centre] > s.water_table_head_m[centre + 1], "head follows storage");
    // Baseflow: a linear reservoir delivering gw / tau per step into the nearest channel downstream.
    auto cb = make_config(12, 10);
    cb.groundwater.hydraulic_conductivity_m_s = 0.0;
    cb.groundwater.baseflow_time_scale_s = 10.0 * 86400.0;
    cb.soil.potential_evapotranspiration_m_s = 0.0;
    const auto zb = tilted_plane(cb);
    const auto nb = build_catchment_network(zb, cb);
    auto sb = initialize_catchment(nb, cb);
    const double gw0 = sb.groundwater_m[50];
    sb = advance_catchment(sb, uniform_forcing(zb.size(), 0.0), nb, cb);
    require_near(sb.groundwater_m[50], gw0 * (1.0 - 600.0 / (10.0 * 86400.0)), "linear-reservoir baseflow", 1.0e-12);
    require(sum(sb.channel_m3) > 0.0 && sb.totals.baseflow_m3 > 0.0, "baseflow feeds the channels");
    require(std::abs(sb.balance_error_m3) < 1.0e-6, "baseflow keeps the budget");
}

// 6. Hazards: helper formulas, landslides with runout, erosion/deposition mass balance, flood classes.
void hazards_follow_the_formulas() {
    CatchmentHazardConfig h;
    // Infinite slope: cohesionless dry soil has FS = tan(phi)/tan(beta).
    CatchmentHazardConfig cohesionless = h;
    cohesionless.cohesion_pa = 0.0;
    require_near(infinite_slope_factor_of_safety(std::tan(30.0 * 3.14159265358979323846 / 180.0), 1.0, 0.0, cohesionless),
                 std::tan(32.0 * 3.14159265358979323846 / 180.0) / std::tan(30.0 * 3.14159265358979323846 / 180.0), "dry cohesionless FS", 1.0e-9);
    require(infinite_slope_factor_of_safety(0.7, 1.0, 1.0, h) < infinite_slope_factor_of_safety(0.7, 1.0, 0.0, h), "saturation lowers the factor of safety");
    require(infinite_slope_factor_of_safety(0.7, 1.0, 1.0, h) < 1.0 && infinite_slope_factor_of_safety(0.7, 1.0, 0.0, h) > 1.0, "a 35 degree slope fails only when saturated");
    require(std::isinf(infinite_slope_factor_of_safety(0.0, 1.0, 1.0, h)), "flat ground cannot slide");
    require_near(intensity_duration_threshold_mm_h(1.0, h), 14.82, "Caine threshold at 1 h", 1.0e-9);
    require_near(intensity_duration_threshold_mm_h(10.0, h), 14.82 * std::pow(10.0, -0.39), "Caine threshold at 10 h", 1.0e-9);
    require_near(manning_velocity_m_s(1.0, 0.01, 0.035), 0.1 / 0.035, "Manning velocity at unit depth", 1.0e-12);
    require(classify_flood(0.05, 0.0, h) == FloodClass::none && classify_flood(0.2, 0.1, h) == FloodClass::low &&
            classify_flood(0.5, 0.1, h) == FloodClass::medium && classify_flood(0.2, 2.0, h) == FloodClass::medium &&
            classify_flood(1.5, 0.0, h) == FloodClass::high && classify_flood(0.5, 1.5, h) == FloodClass::high &&
            classify_flood(2.5, 0.0, h) == FloodClass::extreme && classify_flood(1.0, 2.0, h) == FloodClass::extreme, "depth x velocity classes");
    require(std::string(flood_class_name(FloodClass::extreme)) == "extreme", "class names");

    // A steep ridge above a plain: saturate the soil with a cloudburst and watch the slope fail.
    auto c = make_config(20, 16);
    SyntheticTerrainSpec t;
    t.base_elevation_m = -15.0;
    t.slope_north_m_per_m = 0.004;
    t.ridges.push_back({90.0, 0.0, 3000.0, 1800.0, 2200.0, 30000.0});
    const auto z = build_synthetic_terrain(t, c.grid);
    const auto n = build_catchment_network(z, c);
    double steepest = 0.0;
    for (double v : n.surface_slope_tan) steepest = std::max(steepest, v);
    require(steepest > 0.6, "the test ridge has slopes above 31 degrees");
    auto s = initialize_catchment(n, c);
    const auto dry = uniform_forcing(z.size(), 0.0);
    for (int k = 0; k < 48; ++k) s = advance_catchment(s, dry, n, c);
    const double dry_erosion = sum(s.erosion_m);
    require(s.totals.landslide_count == 0 && sum(s.surface_m) < 1.0e-6, "no landslides or surface water in dry weather; baseflow alone works the steep reaches slowly");
    const auto burst = uniform_forcing(z.size(), 60.0);
    double max_flood = 0.0, max_velocity = 0.0;
    std::size_t threshold_cells = 0;
    for (int k = 0; k < 96; ++k) {  // 16 h of 60 mm/h: the regolith wets up to its drainage equilibrium
        s = advance_catchment(s, burst, n, c);
        require(std::abs(s.balance_error_m3) < 1.0e-5 * std::max(1.0, s.totals.precipitation_m3), "budget closes with landslides and floods");
        for (std::size_t i = 0; i < z.size(); ++i) { max_flood = std::max(max_flood, s.surface_m[i]); max_velocity = std::max(max_velocity, s.overland_velocity_m_s[i]); }
        threshold_cells = std::max(threshold_cells, static_cast<std::size_t>(std::count(s.threshold_exceeded.begin(), s.threshold_exceeded.end(), 1)));
    }
    require(s.totals.landslide_count > 0 && !s.events.empty(), "a saturated steep slope fails");
    require(threshold_cells > 0, "60 mm/h exceeds the intensity-duration threshold");
    require(max_flood > c.hazards.flood_depth_threshold_m, "the cloudburst floods the plain");
    for (const auto& e : s.events) {
        require(n.surface_slope_tan[e.cell] >= c.hazards.minimum_failure_tan && e.factor_of_safety < 1.0, "events record unstable cells");
        require(e.volume_m3 > 0.0 && e.volume_m3 <= c.hazards.failure_area_fraction * n.regolith_depth_m[e.cell] * 1.0e6 + 1.0e-6, "debris volume from the mobilised share");
        require(e.deposited_in == "slope" || e.deposited_in == "channel" || e.deposited_in == "lake" || e.deposited_in == "ocean" || e.deposited_in == "edge", "runout ends somewhere");
        if (e.runout_cell >= 0 && e.deposited_in == "slope") require(n.slope[static_cast<std::size_t>(e.runout_cell)] < c.hazards.runout_tan, "debris stops on gentle ground");
        require(s.regolith_depth_m[e.cell] < n.regolith_depth_m[e.cell], "failed cells lose regolith");
    }
    const std::size_t first = s.events.front().cell;
    std::size_t repeats = 0;
    for (const auto& e : s.events) if (e.cell == first) ++repeats;
    require(repeats == 1, "an unstable cell does not fail again while it stays unstable");
    // Sediment mass balance: erosion + landslide debris = deposition + in transit + yield.
    const double rho = c.hazards.bulk_density_kg_m3, area = 1.0e6;
    const double produced = sum(s.erosion_m) * rho * area + s.totals.landslide_volume_m3 * rho;
    const double accounted = sum(s.deposition_m) * rho * area + sum(s.sediment_kg) + s.totals.sediment_yield_kg;
    require_near(accounted / produced, 1.0, "sediment mass balance", 1.0e-9);
    require(sum(s.erosion_m) > 10.0 * dry_erosion && s.totals.sediment_yield_kg > 0.0, "the flood erodes far more than the same time of baseflow and delivers sediment to the sea");
    // Erosion follows the stream power: the steep channel cells erode more than the plain.
    double steep_erosion = 0.0, plain_erosion = 0.0;
    for (std::size_t i = 0; i < z.size(); ++i) {
        if (!n.channel[i]) continue;
        if (n.slope[i] > 0.2) steep_erosion = std::max(steep_erosion, s.erosion_m[i]); else if (n.slope[i] < 0.01) plain_erosion = std::max(plain_erosion, s.erosion_m[i]);
    }
    require(steep_erosion > plain_erosion, "steep reaches erode more than flat ones");
    // Drying restabilises: with no rain the factor of safety rises and the unstable flags clear.
    for (int k = 0; k < 600; ++k) s = advance_catchment(s, dry, n, c);
    require(std::all_of(s.unstable.begin(), s.unstable.end(), [](unsigned char v) { return v == 0; }), "slopes restabilise as they dry");
}

// 7. Contract: configuration and forcing bounds.
void rejections() {
    auto c = make_config(6, 6);
    const auto z = tilted_plane(c);
    auto bad = c; bad.timestep_seconds = 7200.0;
    require_rejected([&] { validate_catchment_config(bad); }, "timestep above one hour");
    bad = c; bad.timestep_seconds = 3000.0; bad.grid.spacing_east_m = 1000.0; bad.grid.spacing_north_m = 1000.0;
    require_rejected([&] { validate_catchment_config(bad); }, "timestep above twice the spacing");
    bad = c; bad.groundwater.hydraulic_conductivity_m_s = 10.0;
    require_rejected([&] { validate_catchment_config(bad); }, "Darcy stability bound");
    bad = c; bad.soil.porosity = 1.5;
    require_rejected([&] { validate_catchment_config(bad); }, "porosity above one");
    bad = c; bad.soil.infiltration_min_m_s = 1.0;
    require_rejected([&] { validate_catchment_config(bad); }, "minimum infiltration above the maximum");
    bad = c; bad.hazards.friction_angle_deg = 90.0;
    require_rejected([&] { validate_catchment_config(bad); }, "friction angle");
    bad = c; bad.hazards.failure_area_fraction = 0.0;
    require_rejected([&] { validate_catchment_config(bad); }, "failure share");
    require_rejected([&] { build_catchment_network(std::vector<double>(10, 1.0), c); }, "elevation size");
    const auto n = build_catchment_network(z, c);
    auto s = initialize_catchment(n, c);
    auto f = uniform_forcing(36, 10.0);
    f.precipitation_rate_m_s[0] = -1.0;
    require_rejected([&] { advance_catchment(s, f, n, c); }, "negative rain");
    f = uniform_forcing(36, 10.0, 100.0);
    require_rejected([&] { advance_catchment(s, f, n, c); }, "temperature range");
    f = uniform_forcing(35, 10.0);
    require_rejected([&] { advance_catchment(s, f, n, c); }, "forcing size");
}

// 8. Regression: the cloudburst scenario pinned.
void regression_is_pinned() {
    auto c = make_config(20, 16);
    c.open_edges = true;  // the plain north of the ridge drains across the edge instead of forming a giant lake
    SyntheticTerrainSpec t;
    t.base_elevation_m = -15.0;
    t.slope_north_m_per_m = 0.004;
    t.ridges.push_back({90.0, 0.0, 3000.0, 1800.0, 2200.0, 30000.0});
    t.roughness_m = 5.0;
    t.roughness_seed = 3;
    const auto z = build_synthetic_terrain(t, c.grid);
    const auto n = build_catchment_network(z, c);
    auto s = initialize_catchment(n, c);
    const auto burst = uniform_forcing(z.size(), 40.0);
    const auto dry = uniform_forcing(z.size(), 0.0);
    for (int k = 0; k < 36; ++k) s = advance_catchment(s, burst, n, c);
    for (int k = 0; k < 36; ++k) s = advance_catchment(s, dry, n, c);
    require(n.lakes.empty() && std::count(n.channel.begin(), n.channel.end(), 1) == 112, "regression: network");
    require_near(s.totals.ocean_discharge_m3, 14385715.9223, "regression: ocean discharge", 1.0);
    require_near(s.totals.edge_outflow_m3, 10417785.0381, "regression: edge outflow", 1.0);
    require_near(s.totals.sediment_yield_kg, 4884355.74619, "regression: sediment yield", 1.0);
    require_near(*std::max_element(s.discharge_m3_s.begin(), s.discharge_m3_s.end()), 1.79977850912, "regression: peak discharge at the end of the recession", 1.0e-6);
    require_near(sum(s.erosion_m), 0.011783881729, "regression: total erosion", 1.0e-9);
    require_near(sum(s.groundwater_m), 126.794320804, "regression: groundwater", 1.0e-6);
    require_near(sum(s.soil_m), 53.3182606834, "regression: soil water", 1.0e-6);
    require_near(s.totals.precipitation_m3, 40.0 / 3.6e6 * 600.0 * 36.0 * static_cast<double>(std::count(n.ocean.begin(), n.ocean.end(), 0)) * 1.0e6, "regression: rain volume", 1.0);
    require(std::abs(s.balance_error_m3) < 1.0e-9 * std::max(1.0, total_storage_m3(s, c)), "regression: budget");
}

}  // namespace

int main() {
    try {
        network_drains_every_land_cell();
        depressions_fill_then_spill();
        steady_rain_reaches_the_sea();
        column_processes_follow_the_rates();
        groundwater_flows_toward_lower_head();
        hazards_follow_the_formulas();
        rejections();
        regression_is_pinned();
    } catch (const std::exception& error) {
        std::cerr << "catchment hydrology test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "catchment hydrology cases passed\n";
    return 0;
}
