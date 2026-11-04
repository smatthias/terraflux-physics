#include "terraflux/weather/ocean_circulation.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
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
OceanConfig basin(std::size_t n, double spacing, const std::string& coriolis = "beta_plane") {
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

OceanState run(OceanState s, const OceanForcing& f, const OceanConfig& c, double seconds) {
    const auto steps = static_cast<std::size_t>(seconds / c.timestep_seconds);
    for (std::size_t k = 0; k < steps; ++k) s = advance_ocean(s, f, c);
    return s;
}

double beta_at(const OceanConfig& c) {
    return 2.0 * 7.2921159e-5 * std::cos(c.grid.geographic_anchor_latitude_deg * kPi / 180.0) / 6371000.0;
}

// 1. Stommel: on a beta-plane the wind-driven gyre returns through a narrow western boundary current
//    carrying the Sverdrup transport; on an f-plane the circulation is symmetric.
void western_boundary_current_forms_on_the_beta_plane() {
    const std::size_t n = 24;
    const double spacing = 60000.0, tau0 = 0.1;
    auto c = basin(n, spacing);
    auto f = calm(c);
    const double ly = static_cast<double>(n) * spacing;
    for (std::size_t i = 0; i < n * n; ++i) f.wind_stress_east_pa[i] = -tau0 * std::cos(kPi * ((static_cast<double>(i / n) + 0.5) * spacing) / ly);
    auto s = run(initialize_ocean(c), f, c, 2.0 * 365.0 * 86400.0);
    const std::size_t row = n / 2;
    double northward = 0.0, southward = 0.0, west_max = 0.0, east_max = 0.0;
    std::size_t fastest = 0;
    for (std::size_t x = 1; x + 1 < n; ++x) {
        const std::size_t i = row * n + x;
        const double transport = s.v_centre_m_s[i] * s.thickness_m[i] * spacing;
        if (transport > 0.0) northward += transport; else southward -= transport;
        if (x <= 4) west_max = std::max(west_max, std::abs(s.v_centre_m_s[i]));
        if (x + 5 >= n) east_max = std::max(east_max, std::abs(s.v_centre_m_s[i]));
        if (s.speed_m_s[i] > s.speed_m_s[fastest]) fastest = i;
    }
    const double sverdrup = sverdrup_transport_m3_s(tau0, static_cast<double>(n - 2) * spacing, ly, beta_at(c), c.physics.reference_density_kg_m3);
    require(fastest % n <= 3, "the fastest flow at mid-basin hugs the western boundary");
    require(west_max > 2.5 * east_max, "western intensification: the western boundary current is much faster than the eastern side");
    require_near(northward / sverdrup, 1.0, "the western boundary current returns the Sverdrup transport", 0.3);
    require_near(southward / northward, 1.0, "the interior flow balances the boundary current", 0.15);
    // Northward on the west, southward in the interior: an anticyclonic subtropical gyre.
    require(s.v_centre_m_s[row * n + 1] > 0.0 && s.v_centre_m_s[row * n + n / 2] < 0.0, "gyre sense: north along the coast, south in the interior");
    require(std::abs(s.volume_balance_error_m3) < 1.0e-3 && std::abs(s.salt_balance_error_kg) < 1.0, "closed basin conserves volume and salt");
    // Streams: the boundary current is detected with a core streamline running north along the west.
    auto cs = c;
    cs.physics.stream_speed_threshold_m_s = 0.08;
    auto diagnosed = initialize_ocean(cs);
    diagnosed = s;
    diagnosed.elapsed_seconds = s.elapsed_seconds;
    diagnosed = advance_ocean(diagnosed, f, cs);
    const auto d = diagnose_ocean(diagnosed, f, cs);
    require(!d.streams.empty(), "a stream is detected");
    const auto& stream = d.streams.front();
    require(stream.direction == "northward" && stream.core_cell % n <= 3, "the strongest stream is the northward western boundary current");
    require(stream.transport_sv > 0.5 * sverdrup / 1.0e6 && stream.path.size() > 5, "stream transport and core streamline");
    const auto western = std::count_if(stream.path.begin(), stream.path.end(), [&](const auto& p) { return p.first < 0.2; });
    double south = 1.0, north = 0.0;
    for (const auto& p : stream.path) { south = std::min(south, p.second); north = std::max(north, p.second); }
    require(western >= 10 && north - south > 0.5, "the core streamline runs the length of the western boundary and continues around the gyre");
    require(stream.path.front().second < stream.path.back().second, "the streamline is traced from the southern inflow to the northern outflow");
    require(d.max_speed_m_s == stream.max_speed_m_s, "the fastest cell belongs to the stream");
    // f-plane: no western intensification.
    auto cf = basin(n, spacing, "f_plane");
    auto sf = run(initialize_ocean(cf), f, cf, 365.0 * 86400.0);
    double wf = 0.0, ef = 0.0;
    for (std::size_t x = 1; x <= 4; ++x) { wf = std::max(wf, std::abs(sf.v_centre_m_s[row * n + x])); ef = std::max(ef, std::abs(sf.v_centre_m_s[row * n + n - 1 - x])); }
    require(wf < 1.5 * ef && ef < 1.5 * wf, "without beta the boundary currents on both sides are alike");
    require_near(stommel_boundary_width_m(1.0e-6, 2.0e-11), 50000.0, "Stommel width r / beta", 1.0e-6);
    require_near(sverdrup_transport_m3_s(0.1, 3.0e6, 3.0e6, 2.0e-11, 1025.0) / 1.0e6, 15.32, "Sverdrup transport of the reference basin", 0.01);
}

// 2. Fresh water dilutes the sea exactly: salt is conserved, the volume grows by the river input.
void rivers_dilute_and_evaporation_concentrates() {
    const std::size_t n = 12;
    auto c = basin(n, 30000.0, "f_plane");
    auto f = calm(c);
    const double q = 3.0e4;
    f.rivers.push_back({(n / 2) * n + 1, q, 283.15, 0.0, "river"});
    auto s0 = initialize_ocean(c);
    const double v0 = s0.volume_m3, salt0 = s0.salt_kg;
    auto s = run(s0, f, c, 60.0 * 86400.0);
    const double t = s.elapsed_seconds;
    require_near(s.volume_m3, v0 + q * t, "volume grows by the river input", 1.0e-6 * v0);
    require_near(s.salt_kg / salt0, 1.0, "salt is conserved", 1.0e-9);
    require_near(s.cumulative_river_m3, q * t, "cumulative river input", 1.0);
    // Volume-weighted mean salinity follows S0 V0 / (V0 + Q t).
    double hs = 0.0, h = 0.0;
    for (std::size_t i = 0; i < n * n; ++i) if (c.ocean[i]) { hs += s.thickness_m[i] * s.salinity_psu[i]; h += s.thickness_m[i]; }
    require_near(hs / h, c.initial_salinity_psu * v0 / (v0 + q * t), "mean salinity follows the dilution law", 1.0e-9);
    require(s.salinity_psu[(n / 2) * n + 1] < s.salinity_psu[(n / 2) * n + n - 2], "the mouth is freshest");
    // The surface freshwater content makes the mouth much fresher at the surface than in the bulk.
    require(s.freshwater_m[(n / 2) * n + 1] > 0.0 && s.surface_salinity_psu[(n / 2) * n + 1] < s.salinity_psu[(n / 2) * n + 1] - 1.0, "river water floats in the mixed layer");
    require(s.freshwater_m[(n / 2) * n + 1] <= c.physics.mixed_layer_depth_m, "the freshwater content is bounded by the mixed-layer depth");
    {
        // One step in a still ocean: F = Q dt / A reduced by the mixing loss.
        auto one = initialize_ocean(c);
        one = advance_ocean(one, f, c);
        const double expected = q * c.timestep_seconds / (30000.0 * 30000.0) * (1.0 - c.timestep_seconds / c.physics.freshwater_mixing_time_s);
        require_near(one.freshwater_m[(n / 2) * n + 1], expected, "freshwater content after one step", 1.0e-12);
        require_near(one.surface_salinity_psu[(n / 2) * n + 1], one.salinity_psu[(n / 2) * n + 1] * (1.0 - expected / c.physics.mixed_layer_depth_m), "surface salinity from the content", 1.0e-9);
    }
    require(std::abs(s.salt_balance_error_kg) < 1.0e-3 * salt0 * 1.0e-9 + 10.0 && std::abs(s.volume_balance_error_m3) < 1.0, "step budgets close");
    // Evaporation leaves the salt behind: uniform E raises S to S0 h0 / (h0 - E t).
    auto ce = basin(n, 30000.0, "f_plane");
    auto fe = calm(ce);
    const double e = -1.0e-7;
    fe.net_precipitation_m_s.assign(n * n, e);
    auto se = run(initialize_ocean(ce), fe, ce, 30.0 * 86400.0);
    const double te = se.elapsed_seconds;
    const std::size_t mid = (n / 2) * n + n / 2;
    require_near(se.salinity_psu[mid], ce.initial_salinity_psu * ce.initial_thickness_m / (ce.initial_thickness_m + e * te), "evaporation concentrates the salt", 1.0e-9);
    require_near(se.thickness_m[mid], ce.initial_thickness_m + e * te, "evaporation thins the layer", 1.0e-9);
    require_near(se.cumulative_net_precipitation_m3, e * te * se.volume_m3 / (ce.initial_thickness_m + e * te), "cumulative net evaporation", 1.0);
    fe.net_precipitation_m_s.assign(n * n, -e);
    auto sp = run(initialize_ocean(ce), fe, ce, 30.0 * 86400.0);
    require(sp.salinity_psu[mid] < ce.initial_salinity_psu && sp.thickness_m[mid] > ce.initial_thickness_m, "rain freshens and thickens");
}

// 3. A river plume is a buoyant current that the Coriolis force turns to the right in the northern hemisphere.
void plumes_turn_right_in_the_northern_hemisphere() {
    const std::size_t n = 30;
    auto c = basin(n, 30000.0, "f_plane");
    c.physics.tracer_diffusivity_m2_s = 200.0;
    auto f = calm(c);
    const std::size_t mouth = (n / 2) * n + 1;
    f.rivers.push_back({mouth, 5.0e4, 283.15, 0.0, "west coast river"});
    auto s = run(initialize_ocean(c), f, c, 200.0 * 86400.0);
    const auto d = diagnose_ocean(s, f, c);
    require(d.plumes.size() == 1 && d.plumes[0].plume_area_m2 > 1.0e10 && d.plumes[0].minimum_salinity_psu < 30.0, "the plume is resolved");
    require(d.plumes[0].deflection_deg > 90.0 && d.plumes[0].deflection_deg < 180.0, "northern hemisphere: the plume centroid lies to the right (south) of the eastward outflow");
    require(s.salinity_psu[mouth - 2 * n] < s.salinity_psu[mouth + 2 * n], "the coast south of the mouth is fresher than north of it");
    require(s.freshwater_fraction[mouth] > 0.05 && s.front[mouth + 3] == 1, "freshwater fraction and a mixing front around the plume");
    // Southern hemisphere mirror.
    auto cs = basin(n, 30000.0, "prescribed");
    cs.physics.coriolis_parameter_s = -8.4e-5;
    cs.physics.tracer_diffusivity_m2_s = 200.0;
    auto ss = run(initialize_ocean(cs), f, cs, 200.0 * 86400.0);
    const auto ds = diagnose_ocean(ss, f, cs);
    require(ds.plumes[0].deflection_deg > 0.0 && ds.plumes[0].deflection_deg < 90.0, "southern hemisphere: the plume turns left (north)");
    require(ss.salinity_psu[mouth + 2 * n] < ss.salinity_psu[mouth - 2 * n], "mirror image along the coast");
    // The bearing of a plume centred on its mouth is 0, not the sign of a rounding residue (which used to flip it
    // between 0 and 180 degrees across compilers); off-centre plumes keep their bearing.
    auto plume_with = [&](const std::vector<std::size_t>& fresh) {
        auto state = initialize_ocean(c);
        for (std::size_t k : fresh) state.surface_salinity_psu[k] = 30.0;
        return diagnose_ocean(state, f, c).plumes.at(0);
    };
    const double cell = 30000.0 * 30000.0;
    const auto one = plume_with({mouth});
    require(one.plume_area_m2 == cell && one.deflection_deg == 0.0, "a one-cell plume has no direction");
    require(plume_with({mouth, mouth + n, mouth - n}).deflection_deg == 0.0, "a plume spread evenly north and south of its mouth has no direction");
    require_near(plume_with({mouth, mouth + 1}).deflection_deg, 90.0, "a plume east of its mouth bears 90 degrees", 1.0e-9);
    require_near(plume_with({mouth, mouth - n}).deflection_deg, 180.0, "a plume south of its mouth bears 180 degrees", 1.0e-9);
}

// 4. Cold, salty surface water sinks (deep-water formation); fresh water switches the sinking off.
void freshwater_weakens_the_overturning() {
    const std::size_t n = 16;
    auto c = basin(n, 50000.0, "f_plane");
    auto f = calm(c);
    for (std::size_t i = 0; i < n * n; ++i) if (i / n >= n - 4) f.target_temperature_k[i] = 275.15;  // a cold north
    auto s = run(initialize_ocean(c), f, c, 120.0 * 86400.0);
    require(s.overturning_m3_s > 0.0 && s.cumulative_sinking_m3 > 0.0, "cooling produces deep-water formation");
    for (std::size_t i = 0; i < n * n; ++i) {
        if (!c.ocean[i]) continue;
        if (i / n < n - 5) require(s.sinking_m_s[i] == 0.0, "no sinking in the warm south");
        require(s.thickness_m[i] > c.physics.minimum_thickness_m, "the layer keeps its minimum thickness");
    }
    const auto d = diagnose_ocean(s, f, c);
    require(d.formation_cells > 0 && d.formation_area_m2 == static_cast<double>(d.formation_cells) * 50000.0 * 50000.0, "formation sites are reported");
    double water = 0.0;
    for (unsigned char v : c.ocean) water += v;
    require_near(s.upwelling_m_s * water * 50000.0 * 50000.0, s.overturning_m3_s, "uniform upwelling returns the sunk volume", 1.0e-6 * s.overturning_m3_s);
    require(std::abs(s.volume_balance_error_m3) < 1.0 && std::abs(s.salt_balance_error_kg) < 1.0e3, "sinking and upwelling keep the budgets");
    // The surface branch of the overturning flows toward the formation region.
    double northward = 0.0;
    for (std::size_t x = 1; x + 1 < n; ++x) northward += s.v_centre_m_s[(n - 6) * n + x] * s.thickness_m[(n - 6) * n + x] * 50000.0;
    require(northward > 0.0, "water flows north to replace what sinks");
    // Fresh water at the formation site lowers the density and weakens the overturning.
    auto ff = f;
    ff.rivers.push_back({(n - 2) * n + n / 2, 2.0e5, 275.15, 0.0, "meltwater"});
    auto sf = run(initialize_ocean(c), ff, c, 120.0 * 86400.0);
    require(sf.overturning_m3_s < 0.7 * s.overturning_m3_s, "freshwater input weakens the overturning by more than 30 %");
    require(sf.reduced_gravity_m_s2[(n - 2) * n + n / 2] > s.reduced_gravity_m_s2[(n - 2) * n + n / 2], "the freshened water is lighter");
}

// 5. Equation of state and stress helpers.
void helpers() {
    OceanPhysicsConfig p;
    require_near(seawater_density_kg_m3(p.reference_temperature_k, p.reference_salinity_psu, p), p.reference_density_kg_m3, "reference density", 1.0e-9);
    require(seawater_density_kg_m3(283.15, 36.0, p) > seawater_density_kg_m3(283.15, 35.0, p) && seawater_density_kg_m3(293.15, 35.0, p) < seawater_density_kg_m3(283.15, 35.0, p), "salt is heavy, heat is light");
    require_near(reduced_gravity_m_s2(p.deep_temperature_k, p.deep_salinity_psu, p), 0.0, "water like the deep layer has no reduced gravity", 1.0e-12);
    require(reduced_gravity_m_s2(291.15, 35.0, p) > 0.02 && reduced_gravity_m_s2(291.15, 35.0, p) < 0.03, "typical upper-ocean reduced gravity");
    require(reduced_gravity_m_s2(275.15, 35.0, p) < 0.0 && reduced_gravity_m_s2(275.15, 33.0, p) > p.formation_reduced_gravity_m_s2, "cold salty water sinks, cold fresh water does not");
    std::vector<double> ue{10.0, 0.0}, un{0.0, -5.0}, te, tn;
    wind_stress_from_wind(ue, un, 1.2, 1.3e-3, te, tn);
    require_near(te[0], 1.2 * 1.3e-3 * 100.0, "stress from a 10 m/s wind", 1.0e-12);
    require_near(tn[1], -1.2 * 1.3e-3 * 25.0, "stress direction follows the wind", 1.0e-12);
}

// 6. Contract.
void rejections() {
    auto c = basin(10, 50000.0);
    auto bad = c;
    bad.ocean.assign(100, 0);
    require_rejected([&] { validate_ocean_config(bad); }, "no water");
    bad = c; bad.timestep_seconds = 3.0e4;
    require_rejected([&] { validate_ocean_config(bad); }, "gravity-wave CFL");
    bad = c; bad.physics.coriolis_mode = "prescribed"; bad.physics.coriolis_parameter_s = 1.0e-4; bad.timestep_seconds = 6000.0;
    require_rejected([&] { validate_ocean_config(bad); }, "f dt bound");
    bad = c; bad.physics.lateral_viscosity_m2_s = 1.0e6;
    require_rejected([&] { validate_ocean_config(bad); }, "viscous bound");
    bad = c; bad.initial_salinity_psu = 60.0;
    require_rejected([&] { validate_ocean_config(bad); }, "salinity range");
    auto s = initialize_ocean(c);
    auto f = calm(c);
    f.rivers.push_back({0, 100.0, 283.15, 0.0, "on land"});
    require_rejected([&] { advance_ocean(s, f, c); }, "river on land");
    f = calm(c);
    f.wind_stress_east_pa[55] = 50.0;
    require_rejected([&] { advance_ocean(s, f, c); }, "stress range");
    f = calm(c);
    f.wind_stress_east_pa.pop_back();
    require_rejected([&] { advance_ocean(s, f, c); }, "forcing size");
}

// 7. Regression: a small double-gyre basin with a river and a cold north, pinned.
void regression_is_pinned() {
    const std::size_t n = 20;
    auto c = basin(n, 60000.0);
    auto f = calm(c);
    const double ly = static_cast<double>(n) * 60000.0;
    for (std::size_t i = 0; i < n * n; ++i) {
        const double y = (static_cast<double>(i / n) + 0.5) * 60000.0;
        f.wind_stress_east_pa[i] = -0.1 * std::cos(2.0 * kPi * y / ly);
        f.target_temperature_k[i] = 291.15 - 14.0 * y / ly;
        f.net_precipitation_m_s[i] = -1.0e-8 + 2.0e-8 * y / ly;
    }
    f.rivers.push_back({(n / 3) * n + 1, 2.0e4, 283.15, 0.0, "river"});
    auto s = run(initialize_ocean(c), f, c, 365.0 * 86400.0);
    const auto d = diagnose_ocean(s, f, c);
    require_near(s.kinetic_energy_j, 6.13365823593e+15, "regression: kinetic energy", 1.0e7);
    require_near(d.max_speed_m_s, 0.39603594905, "regression: maximum speed", 1.0e-8);
    require_near(d.mean_salinity_psu, 34.9221178412, "regression: mean salinity", 1.0e-8);
    require_near(d.min_salinity_psu, 31.7307523079, "regression: minimum surface salinity", 1.0e-8);
    require_near(s.overturning_m3_s, 11135071.7196, "regression: overturning", 1.0e-2);
    require_near(d.plumes[0].plume_area_m2, 1.08e10, "regression: plume area", 1.0);
    require_near(s.freshwater_m[(n / 3) * n + 1], 3.55482700379, "regression: freshwater content at the mouth", 1.0e-8);
    require(d.streams.size() == 2 && d.formation_cells == 16, "regression: streams and formation sites");
    require(std::abs(s.volume_balance_error_m3) < 1.0 && std::abs(s.salt_balance_error_kg) < 1.0e3, "regression: budgets");
}

}  // namespace

int main() {
    try {
        western_boundary_current_forms_on_the_beta_plane();
        rivers_dilute_and_evaporation_concentrates();
        plumes_turn_right_in_the_northern_hemisphere();
        freshwater_weakens_the_overturning();
        helpers();
        rejections();
        regression_is_pinned();
    } catch (const std::exception& error) {
        std::cerr << "ocean circulation test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ocean circulation cases passed\n";
    return 0;
}
