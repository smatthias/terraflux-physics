#include "terraflux/weather/ocean_circulation.hpp"

#include "terraflux/weather/pressure_wind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;

void require_finite_nonnegative(double v, const char* name) {
    if (!std::isfinite(v) || v < 0.0) throw std::invalid_argument(std::string("ocean ") + name + " must be finite and >= 0");
}
void require_finite_positive(double v, const char* name) {
    if (!std::isfinite(v) || v <= 0.0) throw std::invalid_argument(std::string("ocean ") + name + " must be finite and > 0");
}

AtmosphereConfig coriolis_settings(const OceanPhysicsConfig& p) {
    AtmosphereConfig a;
    a.coriolis_mode = p.coriolis_mode;
    a.coriolis_parameter_s = p.coriolis_parameter_s;
    return a;
}

struct Geometry {
    std::size_t nx, ny, count;
    double dx, dy, area;
    const std::vector<unsigned char>& water;
    std::vector<unsigned char> uf, vf;  // face exists (both cells are water)
    Geometry(std::size_t nx_, std::size_t ny_, double dx_, double dy_, const std::vector<unsigned char>& water_)
        : nx(nx_), ny(ny_), count(nx_ * ny_), dx(dx_), dy(dy_), area(dx_ * dy_), water(water_), uf(count, 0), vf(count, 0) {
        for (std::size_t i = 0; i < count; ++i) {
            uf[i] = ((i % nx) + 1 < nx && water[i] && water[i + 1]) ? 1 : 0;
            vf[i] = (i / nx + 1 < ny && water[i] && water[i + nx]) ? 1 : 0;
        }
    }
    bool u_face(std::size_t i) const { return i < count && uf[i]; }
    bool v_face(std::size_t i) const { return i < count && vf[i]; }
};

}  // namespace

double seawater_density_kg_m3(double t, double s, const OceanPhysicsConfig& p) {
    return p.reference_density_kg_m3 * (1.0 - p.thermal_expansion_per_k * (t - p.reference_temperature_k) + p.haline_contraction_per_psu * (s - p.reference_salinity_psu));
}

double reduced_gravity_m_s2(double t, double s, const OceanPhysicsConfig& p) {
    const double deep = seawater_density_kg_m3(p.deep_temperature_k, p.deep_salinity_psu, p);
    return p.gravity_m_s2 * (deep - seawater_density_kg_m3(t, s, p)) / p.reference_density_kg_m3;
}

double stommel_boundary_width_m(double r, double beta) {
    if (!(beta > 0.0)) return std::numeric_limits<double>::infinity();
    return r / beta;
}

double sverdrup_transport_m3_s(double tau0, double lx, double ly, double beta, double rho) {
    // curl tau = -d tau_x / dy = -tau_0 pi / L_y sin(pi y / L_y); at mid basin the interior
    // meridional transport integrated across the basin is -tau_0 pi L_x / (rho beta L_y).
    return tau0 * kPi * lx / (rho * beta * ly);
}

void wind_stress_from_wind(const std::vector<double>& ue, const std::vector<double>& un, double rho_air, double cd,
                           std::vector<double>& te, std::vector<double>& tn) {
    if (ue.size() != un.size()) throw std::invalid_argument("wind components must match");
    te.resize(ue.size());
    tn.resize(ue.size());
    for (std::size_t i = 0; i < ue.size(); ++i) {
        const double speed = std::hypot(ue[i], un[i]);
        te[i] = rho_air * cd * speed * ue[i];
        tn[i] = rho_air * cd * speed * un[i];
    }
}

void validate_ocean_config(const OceanConfig& c) {
    validate_transport_grid(c.grid);
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (c.ocean.size() != count) throw std::invalid_argument("ocean mask must match the grid");
    std::size_t water = 0;
    for (unsigned char v : c.ocean) { if (v > 1) throw std::invalid_argument("ocean mask must be binary"); water += v; }
    if (water == 0) throw std::invalid_argument("ocean requires at least one water cell");
    require_finite_positive(c.timestep_seconds, "timestep_seconds");
    require_finite_positive(c.initial_thickness_m, "initial_thickness_m");
    if (!std::isfinite(c.initial_salinity_psu) || c.initial_salinity_psu < 0.0 || c.initial_salinity_psu > 45.0) throw std::invalid_argument("ocean initial_salinity_psu must lie in 0-45");
    if (!std::isfinite(c.initial_temperature_k) || c.initial_temperature_k < 270.0 || c.initial_temperature_k > 310.0) throw std::invalid_argument("ocean initial_temperature_k must lie in 270-310 K");
    const auto& p = c.physics;
    require_finite_positive(p.reference_density_kg_m3, "physics.reference_density_kg_m3");
    require_finite_positive(p.gravity_m_s2, "physics.gravity_m_s2");
    require_finite_nonnegative(p.thermal_expansion_per_k, "physics.thermal_expansion_per_k");
    require_finite_nonnegative(p.haline_contraction_per_psu, "physics.haline_contraction_per_psu");
    if (!std::isfinite(p.reference_temperature_k) || !std::isfinite(p.reference_salinity_psu) || !std::isfinite(p.deep_temperature_k) || !std::isfinite(p.deep_salinity_psu) ||
        p.deep_temperature_k < 270.0 || p.deep_temperature_k > 310.0 || p.deep_salinity_psu < 0.0 || p.deep_salinity_psu > 45.0) {
        throw std::invalid_argument("ocean reference and deep-layer temperature/salinity must be finite and within 270-310 K / 0-45 psu");
    }
    require_finite_positive(p.minimum_reduced_gravity_m_s2, "physics.minimum_reduced_gravity_m_s2");
    require_finite_nonnegative(p.linear_friction_s, "physics.linear_friction_s");
    require_finite_nonnegative(p.lateral_viscosity_m2_s, "physics.lateral_viscosity_m2_s");
    require_finite_nonnegative(p.tracer_diffusivity_m2_s, "physics.tracer_diffusivity_m2_s");
    require_finite_positive(p.minimum_thickness_m, "physics.minimum_thickness_m");
    require_finite_positive(p.temperature_relaxation_s, "physics.temperature_relaxation_s");
    require_finite_nonnegative(p.formation_reduced_gravity_m_s2, "physics.formation_reduced_gravity_m_s2");
    require_finite_positive(p.convection_time_scale_s, "physics.convection_time_scale_s");
    require_finite_positive(p.sponge_time_scale_s, "physics.sponge_time_scale_s");
    require_finite_positive(p.mixed_layer_depth_m, "physics.mixed_layer_depth_m");
    require_finite_positive(p.freshwater_mixing_time_s, "physics.freshwater_mixing_time_s");
    if (!c.sponge.empty()) {
        if (c.sponge.size() != count) throw std::invalid_argument("ocean sponge must be empty or match the grid");
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(c.sponge[i]) || c.sponge[i] < 0.0 || c.sponge[i] > 1.0) throw std::invalid_argument("ocean sponge strengths must lie in [0, 1]");
            if (!c.ocean[i] && c.sponge[i] != 0.0) throw std::invalid_argument("ocean sponge must be zero on land");
        }
        if (c.timestep_seconds > c.physics.sponge_time_scale_s) throw std::invalid_argument("ocean timestep must not exceed the sponge time scale");
    }
    if (p.coriolis_mode != "none" && p.coriolis_mode != "f_plane" && p.coriolis_mode != "beta_plane" && p.coriolis_mode != "prescribed") {
        throw std::invalid_argument("ocean coriolis_mode must be none, f_plane, beta_plane or prescribed");
    }
    if (!std::isfinite(p.coriolis_parameter_s) || std::abs(p.coriolis_parameter_s) > 2.0e-4) throw std::invalid_argument("ocean coriolis_parameter_s must lie within +-2e-4 1/s");
    require_finite_positive(p.stream_speed_threshold_m_s, "physics.stream_speed_threshold_m_s");
    require_finite_positive(p.plume_salinity_deficit_psu, "physics.plume_salinity_deficit_psu");
    require_finite_positive(p.front_gradient_psu_per_100km, "physics.front_gradient_psu_per_100km");
    if (c.initial_thickness_m < p.minimum_thickness_m) throw std::invalid_argument("ocean initial thickness must not be below the minimum thickness");
    // Stability: gravity waves of the reduced-gravity layer, Coriolis, drag, viscosity and diffusion.
    const double dt = c.timestep_seconds, dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m;
    const double g_prime = std::max(p.minimum_reduced_gravity_m_s2, reduced_gravity_m_s2(c.initial_temperature_k, c.initial_salinity_psu, p));
    const double wave_speed = std::sqrt(1.5 * g_prime * c.initial_thickness_m * 1.5);
    if (dt * wave_speed * (1.0 / dx + 1.0 / dy) > 1.0) throw std::invalid_argument("ocean timestep violates the gravity-wave CFL bound dt c (1/dx + 1/dy) <= 1");
    double f_max = 0.0;
    for (double f : coriolis_parameter(c.grid, coriolis_settings(p))) f_max = std::max(f_max, std::abs(f));
    if (f_max * dt > 0.5) throw std::invalid_argument("ocean timestep must keep f dt <= 0.5");
    if (p.linear_friction_s * dt > 1.0) throw std::invalid_argument("ocean timestep must keep r dt <= 1");
    if (std::max(p.lateral_viscosity_m2_s, p.tracer_diffusivity_m2_s) * dt * (1.0 / (dx * dx) + 1.0 / (dy * dy)) > 0.25) {
        throw std::invalid_argument("ocean timestep violates the explicit viscosity/diffusion bound");
    }
}

namespace {

void check_state(const OceanState& s, std::size_t count) {
    if (s.u_m_s.size() != count || s.v_m_s.size() != count || s.thickness_m.size() != count || s.salinity_psu.size() != count || s.temperature_k.size() != count || s.freshwater_m.size() != count) {
        throw std::invalid_argument("ocean state arrays must match the grid");
    }
}

void check_forcing(const OceanForcing& f, const OceanConfig& c) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (f.wind_stress_east_pa.size() != count || f.wind_stress_north_pa.size() != count || f.net_precipitation_m_s.size() != count || f.target_temperature_k.size() != count) {
        throw std::invalid_argument("ocean forcing arrays must match the grid");
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(f.wind_stress_east_pa[i]) || !std::isfinite(f.wind_stress_north_pa[i]) || std::abs(f.wind_stress_east_pa[i]) > 10.0 || std::abs(f.wind_stress_north_pa[i]) > 10.0) {
            throw std::invalid_argument("ocean wind stress must be finite and within +-10 Pa");
        }
        if (!std::isfinite(f.net_precipitation_m_s[i]) || std::abs(f.net_precipitation_m_s[i]) > 1.0e-5) throw std::invalid_argument("ocean net precipitation must be finite and within +-1e-5 m/s");
        if (!std::isfinite(f.target_temperature_k[i]) || f.target_temperature_k[i] < 260.0 || f.target_temperature_k[i] > 320.0) throw std::invalid_argument("ocean target temperature must lie within 260-320 K");
    }
    for (const auto& r : f.rivers) {
        if (r.cell >= count || !c.ocean[r.cell]) throw std::invalid_argument("river mouths must be ocean cells");
        if (!std::isfinite(r.discharge_m3_s) || r.discharge_m3_s < 0.0 || r.discharge_m3_s > 1.0e7) throw std::invalid_argument("river discharge must lie within 0-1e7 m3/s");
        if (!std::isfinite(r.temperature_k) || r.temperature_k < 270.0 || r.temperature_k > 310.0 || !std::isfinite(r.salinity_psu) || r.salinity_psu < 0.0 || r.salinity_psu > 45.0) {
            throw std::invalid_argument("river temperature/salinity out of range");
        }
    }
}

double ocean_volume(const OceanState& s, const std::vector<unsigned char>& water, double area) {
    long double v = 0.0L;
    for (std::size_t i = 0; i < s.thickness_m.size(); ++i) if (water[i]) v += static_cast<long double>(s.thickness_m[i]) * area;
    return static_cast<double>(v);
}

double ocean_salt(const OceanState& s, const std::vector<unsigned char>& water, double area, double rho) {
    long double v = 0.0L;
    for (std::size_t i = 0; i < s.thickness_m.size(); ++i) if (water[i]) v += static_cast<long double>(s.thickness_m[i]) * s.salinity_psu[i] * 1.0e-3 * rho * area;
    return static_cast<double>(v);
}

void diagnose_fields(OceanState& s, const OceanConfig& c) {
    const auto& p = c.physics;
    const std::size_t nx = c.grid.nx, count = nx * c.grid.ny;
    const Geometry g(nx, c.grid.ny, c.grid.spacing_east_m, c.grid.spacing_north_m, c.ocean);
    s.speed_m_s.assign(count, 0.0);
    s.u_centre_m_s.assign(count, 0.0);
    s.v_centre_m_s.assign(count, 0.0);
    s.reduced_gravity_m_s2.assign(count, 0.0);
    s.density_kg_m3.assign(count, 0.0);
    s.surface_salinity_psu.assign(count, 0.0);
    s.freshwater_fraction.assign(count, 0.0);
    s.front.assign(count, 0);
    s.stream_id.assign(count, -1);
    long double ke = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        const std::size_t x = i % nx;
        double ue = 0.0, un = 0.0;
        int ne = 0, nn = 0;
        if (g.u_face(i)) { ue += s.u_m_s[i]; ++ne; }
        if (x > 0 && g.u_face(i - 1)) { ue += s.u_m_s[i - 1]; ++ne; }
        if (g.v_face(i)) { un += s.v_m_s[i]; ++nn; }
        if (i >= nx && g.v_face(i - nx)) { un += s.v_m_s[i - nx]; ++nn; }
        s.u_centre_m_s[i] = ne ? ue / ne : 0.0;
        s.v_centre_m_s[i] = nn ? un / nn : 0.0;
        s.speed_m_s[i] = std::hypot(s.u_centre_m_s[i], s.v_centre_m_s[i]);
        s.reduced_gravity_m_s2[i] = reduced_gravity_m_s2(s.temperature_k[i], s.salinity_psu[i], p);
        s.density_kg_m3[i] = seawater_density_kg_m3(s.temperature_k[i], s.salinity_psu[i], p);
        s.surface_salinity_psu[i] = s.salinity_psu[i] * std::max(0.0, 1.0 - s.freshwater_m[i] / p.mixed_layer_depth_m);
        s.freshwater_fraction[i] = std::max(0.0, (p.reference_salinity_psu - s.surface_salinity_psu[i]) / p.reference_salinity_psu);
        ke += 0.5L * p.reference_density_kg_m3 * s.thickness_m[i] * g.area * (s.speed_m_s[i] * s.speed_m_s[i]);
    }
    s.kinetic_energy_j = static_cast<double>(ke);
    // Mixing fronts: surface-salinity gradient magnitude over water neighbours.
    const double threshold = p.front_gradient_psu_per_100km / 1.0e5;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        const std::size_t x = i % nx, y = i / nx;
        double gx = 0.0, gy = 0.0;
        const bool east = x + 1 < nx && c.ocean[i + 1], west = x > 0 && c.ocean[i - 1];
        const bool north = y + 1 < c.grid.ny && c.ocean[i + nx], south = y > 0 && c.ocean[i - nx];
        if (east && west) gx = (s.surface_salinity_psu[i + 1] - s.surface_salinity_psu[i - 1]) / (2.0 * g.dx);
        else if (east) gx = (s.surface_salinity_psu[i + 1] - s.surface_salinity_psu[i]) / g.dx;
        else if (west) gx = (s.surface_salinity_psu[i] - s.surface_salinity_psu[i - 1]) / g.dx;
        if (north && south) gy = (s.surface_salinity_psu[i + nx] - s.surface_salinity_psu[i - nx]) / (2.0 * g.dy);
        else if (north) gy = (s.surface_salinity_psu[i + nx] - s.surface_salinity_psu[i]) / g.dy;
        else if (south) gy = (s.surface_salinity_psu[i] - s.surface_salinity_psu[i - nx]) / g.dy;
        s.front[i] = std::hypot(gx, gy) > threshold ? 1 : 0;
    }
    // Streams: connected fast cells (4-neighbour).
    int next = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i] || s.stream_id[i] >= 0 || s.speed_m_s[i] < p.stream_speed_threshold_m_s) continue;
        std::vector<std::size_t> stack{i};
        s.stream_id[i] = next;
        while (!stack.empty()) {
            const std::size_t k = stack.back();
            stack.pop_back();
            const std::size_t x = k % nx, y = k / nx;
            const std::size_t neighbours[4] = {x + 1 < nx ? k + 1 : count, x > 0 ? k - 1 : count, y + 1 < c.grid.ny ? k + nx : count, y > 0 ? k - nx : count};
            for (std::size_t n : neighbours) {
                if (n >= count || !c.ocean[n] || s.stream_id[n] >= 0 || s.speed_m_s[n] < p.stream_speed_threshold_m_s) continue;
                s.stream_id[n] = next;
                stack.push_back(n);
            }
        }
        ++next;
    }
}

}  // namespace

OceanState initialize_ocean(const OceanConfig& c) {
    validate_ocean_config(c);
    const std::size_t count = c.grid.nx * c.grid.ny;
    OceanState s;
    s.u_m_s.assign(count, 0.0);
    s.v_m_s.assign(count, 0.0);
    s.thickness_m.assign(count, 0.0);
    s.salinity_psu.assign(count, 0.0);
    s.temperature_k.assign(count, 0.0);
    s.freshwater_m.assign(count, 0.0);
    s.sinking_m_s.assign(count, 0.0);
    s.upwelling_field_m_s.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        s.thickness_m[i] = c.initial_thickness_m;
        s.salinity_psu[i] = c.initial_salinity_psu;
        s.temperature_k[i] = c.initial_temperature_k;
    }
    diagnose_fields(s, c);
    const double area = c.grid.spacing_east_m * c.grid.spacing_north_m;
    s.volume_m3 = ocean_volume(s, c.ocean, area);
    s.salt_kg = ocean_salt(s, c.ocean, area, c.physics.reference_density_kg_m3);
    return s;
}

OceanState advance_ocean(const OceanState& previous, const OceanForcing& f, const OceanConfig& c) {
    validate_ocean_config(c);
    check_forcing(f, c);
    const std::size_t nx = c.grid.nx, ny = c.grid.ny, count = nx * ny;
    check_state(previous, count);
    const auto& p = c.physics;
    const double dt = c.timestep_seconds, dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m, area = dx * dy;
    const Geometry g(nx, ny, dx, dy, c.ocean);
    const std::vector<double> coriolis = coriolis_parameter(c.grid, coriolis_settings(p));
    OceanState s = previous;
    s.sinking_m_s.assign(count, 0.0);
    double water_area = 0.0;
    for (std::size_t i = 0; i < count; ++i) if (c.ocean[i]) water_area += area;

    // 1. Face volume fluxes per unit width (upwind thickness) and tracer fluxes.
    std::vector<double> fx(count, 0.0), fy(count, 0.0), fsx(count, 0.0), fsy(count, 0.0), ftx(count, 0.0), fty(count, 0.0), ffx(count, 0.0), ffy(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (g.u_face(i)) {
            const double u = previous.u_m_s[i];
            const std::size_t up = u >= 0.0 ? i : i + 1;
            fx[i] = u * previous.thickness_m[up];
            const double hf = 0.5 * (previous.thickness_m[i] + previous.thickness_m[i + 1]);
            fsx[i] = fx[i] * previous.salinity_psu[up] - p.tracer_diffusivity_m2_s * hf * (previous.salinity_psu[i + 1] - previous.salinity_psu[i]) / dx;
            ftx[i] = fx[i] * previous.temperature_k[up] - p.tracer_diffusivity_m2_s * hf * (previous.temperature_k[i + 1] - previous.temperature_k[i]) / dx;
            ffx[i] = u * previous.freshwater_m[up] - p.tracer_diffusivity_m2_s * (previous.freshwater_m[i + 1] - previous.freshwater_m[i]) / dx;
        }
        if (g.v_face(i)) {
            const double v = previous.v_m_s[i];
            const std::size_t up = v >= 0.0 ? i : i + nx;
            fy[i] = v * previous.thickness_m[up];
            const double hf = 0.5 * (previous.thickness_m[i] + previous.thickness_m[i + nx]);
            fsy[i] = fy[i] * previous.salinity_psu[up] - p.tracer_diffusivity_m2_s * hf * (previous.salinity_psu[i + nx] - previous.salinity_psu[i]) / dy;
            fty[i] = fy[i] * previous.temperature_k[up] - p.tracer_diffusivity_m2_s * hf * (previous.temperature_k[i + nx] - previous.temperature_k[i]) / dy;
            ffy[i] = v * previous.freshwater_m[up] - p.tracer_diffusivity_m2_s * (previous.freshwater_m[i + nx] - previous.freshwater_m[i]) / dy;
        }
    }

    // 2. Deep-water formation (sinking) on the start-of-step state; uniform return upwelling.
    long double sink_total = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        const double surface_salinity = previous.salinity_psu[i] * std::max(0.0, 1.0 - previous.freshwater_m[i] / p.mixed_layer_depth_m);
        const double gp = reduced_gravity_m_s2(previous.temperature_k[i], surface_salinity, p);  // a fresh cap stops the sinking
        if (gp < p.formation_reduced_gravity_m_s2 && previous.thickness_m[i] > p.minimum_thickness_m) {
            const double factor = std::max(0.0, 1.0 - std::max(gp, 0.0) / p.formation_reduced_gravity_m_s2);
            s.sinking_m_s[i] = factor * (previous.thickness_m[i] - p.minimum_thickness_m) / p.convection_time_scale_s;
            sink_total += static_cast<long double>(s.sinking_m_s[i]) * area;
        }
    }
    s.upwelling_m_s = static_cast<double>(sink_total) / water_area;
    s.overturning_m3_s = static_cast<double>(sink_total);
    s.upwelling_field_m_s.assign(count, 0.0);
    double sponge_weight = 0.0;
    if (!c.sponge.empty()) for (std::size_t i = 0; i < count; ++i) if (c.ocean[i]) sponge_weight += c.sponge[i] * area;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        s.upwelling_field_m_s[i] = (p.upwelling_in_sponge && sponge_weight > 0.0) ? static_cast<double>(sink_total) * c.sponge[i] / sponge_weight : s.upwelling_m_s;
    }

    // 3. Thickness and tracer update (flux form), then sources.
    std::vector<double> hs(count, 0.0), ht(count, 0.0);
    long double river_total = 0.0L, precip_total = 0.0L, sponge_volume = 0.0L, sponge_salt = 0.0L;
    std::vector<double> river_depth(count, 0.0), river_salt(count, 0.0), river_heat(count, 0.0);
    for (const auto& r : f.rivers) {
        const double depth = r.discharge_m3_s * dt / area;
        river_depth[r.cell] += depth;
        river_salt[r.cell] += depth * r.salinity_psu;
        river_heat[r.cell] += depth * r.temperature_k;
        river_total += r.discharge_m3_s;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        const std::size_t x = i % nx, y = i / nx;
        const double div = (fx[i] - (x > 0 ? fx[i - 1] : 0.0)) / dx + (fy[i] - (y > 0 ? fy[i - nx] : 0.0)) / dy;
        const double sdiv = (fsx[i] - (x > 0 ? fsx[i - 1] : 0.0)) / dx + (fsy[i] - (y > 0 ? fsy[i - nx] : 0.0)) / dy;
        const double tdiv = (ftx[i] - (x > 0 ? ftx[i - 1] : 0.0)) / dx + (fty[i] - (y > 0 ? fty[i - nx] : 0.0)) / dy;
        const double fdiv = (ffx[i] - (x > 0 ? ffx[i - 1] : 0.0)) / dx + (ffy[i] - (y > 0 ? ffy[i - nx] : 0.0)) / dy;
        double fresh = previous.freshwater_m[i] - dt * fdiv;
        const double h0 = previous.thickness_m[i], s0 = previous.salinity_psu[i], t0 = previous.temperature_k[i];
        double h = h0 - dt * div;
        double salt = h0 * s0 - dt * sdiv;
        double heat = h0 * t0 - dt * tdiv;
        // Sinking removes local water, upwelling adds deep water.
        const double sunk = s.sinking_m_s[i] * dt, up = s.upwelling_field_m_s[i] * dt;
        h += up - sunk;
        salt += up * p.deep_salinity_psu - sunk * s0;
        heat += up * p.deep_temperature_k - sunk * t0;
        // Rivers (fresh, at their temperature) and rain feed the surface freshwater content; it mixes down in time.
        fresh += river_depth[i] * (1.0 - (river_depth[i] > 0.0 ? river_salt[i] / river_depth[i] : 0.0) / p.reference_salinity_psu);
        h += river_depth[i];
        salt += river_salt[i];
        heat += river_heat[i];
        // Rain dilutes, evaporation leaves the salt behind; both at the local temperature.
        double pe = f.net_precipitation_m_s[i] * dt;
        if (pe < 0.0) pe = -std::min(-pe, std::max(0.0, h - p.minimum_thickness_m));
        h += pe;
        heat += pe * t0;
        if (pe > 0.0) fresh += pe;
        fresh = std::max(0.0, fresh) * (1.0 - std::min(1.0, dt / p.freshwater_mixing_time_s));
        s.freshwater_m[i] = std::min(fresh, p.mixed_layer_depth_m);
        precip_total += static_cast<long double>(pe) * area / dt;
        if (!(h > 0.0)) throw std::runtime_error("ocean layer thickness became non-positive; reduce the timestep or the sinks");
        // Surface temperature relaxation.
        heat += h * (f.target_temperature_k[i] - heat / h) * std::min(1.0, dt / p.temperature_relaxation_s);
        // Sponge: relax thickness and tracers toward the initial state; the change is a budget source.
        if (!c.sponge.empty() && c.sponge[i] > 0.0) {
            const double rate = c.sponge[i] * dt / p.sponge_time_scale_s;
            const double h2 = h + (c.initial_thickness_m - h) * rate;
            const double s2 = salt / h + (c.initial_salinity_psu - salt / h) * rate;
            const double t2 = heat / h + (c.initial_temperature_k - heat / h) * rate;
            sponge_volume += static_cast<long double>(h2 - h) * area;
            sponge_salt += (static_cast<long double>(h2) * s2 - salt) * 1.0e-3 * p.reference_density_kg_m3 * area;
            h = h2;
            salt = h2 * s2;
            heat = h2 * t2;
            s.freshwater_m[i] *= 1.0 - rate;
        }
        s.thickness_m[i] = h;
        hs[i] = salt;
        ht[i] = heat;
        s.salinity_psu[i] = std::max(0.0, salt / h);
        s.temperature_k[i] = heat / h;
    }
    s.river_input_m3_s = static_cast<double>(river_total);
    s.net_precipitation_m3_s = static_cast<double>(precip_total);
    s.sponge_volume_m3_s = static_cast<double>(sponge_volume / dt);
    s.sponge_salt_kg_s = static_cast<double>(sponge_salt / dt);

    // 4. Momentum on the new thickness; forward-backward Coriolis with alternating order.
    std::vector<double> gph(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) if (c.ocean[i]) gph[i] = std::max(p.minimum_reduced_gravity_m_s2, reduced_gravity_m_s2(s.temperature_k[i], s.salinity_psu[i], p)) * s.thickness_m[i];
    const bool u_first = (static_cast<long long>(std::llround(previous.elapsed_seconds / dt)) % 2) == 0;
    auto v_at_u = [&](std::size_t i, const std::vector<double>& v) {
        double sum = 0.0; int n = 0;
        const std::size_t e = i + 1;
        if (g.v_face(i)) { sum += v[i]; ++n; }
        if (g.v_face(e)) { sum += v[e]; ++n; }
        if (i >= nx && g.v_face(i - nx)) { sum += v[i - nx]; ++n; }
        if (e >= nx && g.v_face(e - nx)) { sum += v[e - nx]; ++n; }
        return n ? sum / n : 0.0;
    };
    auto u_at_v = [&](std::size_t i, const std::vector<double>& u) {
        double sum = 0.0; int n = 0;
        const std::size_t nth = i + nx;
        if (g.u_face(i)) { sum += u[i]; ++n; }
        if (g.u_face(nth)) { sum += u[nth]; ++n; }
        if (i % nx > 0 && g.u_face(i - 1)) { sum += u[i - 1]; ++n; }
        if (nth % nx > 0 && g.u_face(nth - 1)) { sum += u[nth - 1]; ++n; }
        return n ? sum / n : 0.0;
    };
    auto laplacian = [&](std::size_t i, const std::vector<double>& w, bool is_u) {
        // Free-slip: missing neighbours take the local value.
        const std::size_t x = i % nx, y = i / nx;
        auto value = [&](std::size_t k, bool exists) { return exists ? w[k] : w[i]; };
        const bool ee = x + 1 < nx && (is_u ? g.u_face(i + 1) : g.v_face(i + 1));
        const bool ww = x > 0 && (is_u ? g.u_face(i - 1) : g.v_face(i - 1));
        const bool nn = y + 1 < ny && (is_u ? g.u_face(i + nx) : g.v_face(i + nx));
        const bool ss = y > 0 && (is_u ? g.u_face(i - nx) : g.v_face(i - nx));
        return (value(i + 1, ee) - 2.0 * w[i] + value(x > 0 ? i - 1 : i, ww)) / (dx * dx) + (value(i + nx, nn) - 2.0 * w[i] + value(y > 0 ? i - nx : i, ss)) / (dy * dy);
    };
    auto update_u = [&](const std::vector<double>& v_ref) {
        for (std::size_t i = 0; i < count; ++i) {
            if (!g.u_face(i)) { s.u_m_s[i] = 0.0; continue; }
            const double hf = std::max(p.minimum_thickness_m, 0.5 * (s.thickness_m[i] + s.thickness_m[i + 1]));
            const double ff = 0.5 * (coriolis[i] + coriolis[i + 1]);
            const double accel = -(gph[i + 1] - gph[i]) / dx + ff * v_at_u(i, v_ref) - p.linear_friction_s * previous.u_m_s[i]
                                 + 0.5 * (f.wind_stress_east_pa[i] + f.wind_stress_east_pa[i + 1]) / (p.reference_density_kg_m3 * hf)
                                 + p.lateral_viscosity_m2_s * laplacian(i, previous.u_m_s, true);
            s.u_m_s[i] = previous.u_m_s[i] + dt * accel;
        }
    };
    auto update_v = [&](const std::vector<double>& u_ref) {
        for (std::size_t i = 0; i < count; ++i) {
            if (!g.v_face(i)) { s.v_m_s[i] = 0.0; continue; }
            const double hf = std::max(p.minimum_thickness_m, 0.5 * (s.thickness_m[i] + s.thickness_m[i + nx]));
            const double ff = 0.5 * (coriolis[i] + coriolis[i + nx]);
            const double accel = -(gph[i + nx] - gph[i]) / dy - ff * u_at_v(i, u_ref) - p.linear_friction_s * previous.v_m_s[i]
                                 + 0.5 * (f.wind_stress_north_pa[i] + f.wind_stress_north_pa[i + nx]) / (p.reference_density_kg_m3 * hf)
                                 + p.lateral_viscosity_m2_s * laplacian(i, previous.v_m_s, false);
            s.v_m_s[i] = previous.v_m_s[i] + dt * accel;
        }
    };
    if (u_first) { update_u(previous.v_m_s); update_v(s.u_m_s); }
    else { update_v(previous.u_m_s); update_u(s.v_m_s); }
    if (!c.sponge.empty()) {
        for (std::size_t i = 0; i < count; ++i) {
            if (g.u_face(i)) s.u_m_s[i] *= 1.0 - std::max(c.sponge[i], c.sponge[i + 1]) * dt / p.sponge_time_scale_s;
            if (g.v_face(i)) s.v_m_s[i] *= 1.0 - std::max(c.sponge[i], c.sponge[i + nx]) * dt / p.sponge_time_scale_s;
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(s.u_m_s[i]) || !std::isfinite(s.v_m_s[i]) || std::abs(s.u_m_s[i]) > 20.0 || std::abs(s.v_m_s[i]) > 20.0) {
            throw std::runtime_error("ocean velocity became non-finite or unphysical (> 20 m/s)");
        }
    }

    // 5. Diagnostics and budgets.
    s.elapsed_seconds = previous.elapsed_seconds + dt;
    diagnose_fields(s, c);
    s.volume_m3 = ocean_volume(s, c.ocean, area);
    s.salt_kg = ocean_salt(s, c.ocean, area, p.reference_density_kg_m3);
    long double salt_source = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        salt_source += (static_cast<long double>(s.upwelling_field_m_s[i]) * dt * p.deep_salinity_psu - static_cast<long double>(s.sinking_m_s[i]) * dt * previous.salinity_psu[i] + river_salt[i]) * 1.0e-3 * p.reference_density_kg_m3 * area;
    }
    salt_source += sponge_salt;
    const double expected_volume_change = (static_cast<double>(river_total) + static_cast<double>(precip_total)) * dt + static_cast<double>(sponge_volume);  // sinking and upwelling cancel
    s.volume_balance_error_m3 = (s.volume_m3 - previous.volume_m3) - expected_volume_change;
    s.salt_balance_error_kg = (s.salt_kg - previous.salt_kg) - static_cast<double>(salt_source);
    s.cumulative_river_m3 += static_cast<double>(river_total) * dt;
    s.cumulative_net_precipitation_m3 += static_cast<double>(precip_total) * dt;
    s.cumulative_sinking_m3 += static_cast<double>(sink_total) * dt;
    s.cumulative_sponge_m3 += static_cast<double>(sponge_volume);
    return s;
}

OceanDiagnostics diagnose_ocean(const OceanState& s, const OceanForcing& f, const OceanConfig& c) {
    validate_ocean_config(c);
    const std::size_t nx = c.grid.nx, ny = c.grid.ny, count = nx * ny;
    check_state(s, count);
    if (s.speed_m_s.size() != count || s.stream_id.size() != count || s.sinking_m_s.size() != count) throw std::invalid_argument("ocean state lacks diagnostics; advance or initialise it first");
    const auto& p = c.physics;
    const double dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m, area = dx * dy;
    OceanDiagnostics d;
    d.min_salinity_psu = std::numeric_limits<double>::infinity();
    long double salinity_sum = 0.0L;
    std::size_t water = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        ++water;
        salinity_sum += s.salinity_psu[i];
        d.min_salinity_psu = std::min(d.min_salinity_psu, s.surface_salinity_psu[i]);
        d.max_speed_m_s = std::max(d.max_speed_m_s, s.speed_m_s[i]);
        if (s.sinking_m_s[i] > 0.0) { ++d.formation_cells; d.formation_area_m2 += area; }
    }
    d.mean_salinity_psu = water ? static_cast<double>(salinity_sum / water) : 0.0;
    if (!std::isfinite(d.min_salinity_psu)) d.min_salinity_psu = 0.0;

    // Streams.
    int stream_count = 0;
    for (int id : s.stream_id) stream_count = std::max(stream_count, id + 1);
    auto interpolate = [&](double gx, double gy, double& u, double& v) {
        // gx, gy in cell units (centre of cell (x, y) at x + 0.5, y + 0.5); bilinear over water cells.
        const double px = gx - 0.5, py = gy - 0.5;
        const long x0 = static_cast<long>(std::floor(px)), y0 = static_cast<long>(std::floor(py));
        const double tx = px - static_cast<double>(x0), ty = py - static_cast<double>(y0);
        double su = 0.0, sv = 0.0, sw = 0.0;
        for (int k = 0; k < 4; ++k) {
            const long xx = x0 + (k & 1), yy = y0 + (k >> 1);
            if (xx < 0 || yy < 0 || xx >= static_cast<long>(nx) || yy >= static_cast<long>(ny)) continue;
            const std::size_t i = static_cast<std::size_t>(yy) * nx + static_cast<std::size_t>(xx);
            if (!c.ocean[i]) continue;
            const double w = ((k & 1) ? tx : 1.0 - tx) * ((k >> 1) ? ty : 1.0 - ty);
            su += w * s.u_centre_m_s[i]; sv += w * s.v_centre_m_s[i]; sw += w;
        }
        if (sw <= 0.0) { u = v = 0.0; return false; }
        u = su / sw; v = sv / sw;
        return true;
    };
    for (int id = 0; id < stream_count; ++id) {
        OceanStream st;
        st.id = id;
        double sum_speed = 0.0, sum_s = 0.0, sum_t = 0.0, mu = 0.0, mv = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (s.stream_id[i] != id) continue;
            ++st.cell_count;
            sum_speed += s.speed_m_s[i]; sum_s += s.salinity_psu[i]; sum_t += s.temperature_k[i];
            mu += s.u_centre_m_s[i]; mv += s.v_centre_m_s[i];
            if (s.speed_m_s[i] > st.max_speed_m_s) { st.max_speed_m_s = s.speed_m_s[i]; st.core_cell = i; }
        }
        st.mean_speed_m_s = sum_speed / static_cast<double>(st.cell_count);
        st.mean_salinity_psu = sum_s / static_cast<double>(st.cell_count);
        st.mean_temperature_k = sum_t / static_cast<double>(st.cell_count);
        st.direction = std::abs(mu) >= std::abs(mv) ? (mu >= 0.0 ? "eastward" : "westward") : (mv >= 0.0 ? "northward" : "southward");
        // Transport across the core section: walk perpendicular to the core velocity while the flow stays in the stream.
        const double cu = s.u_centre_m_s[st.core_cell], cv = s.v_centre_m_s[st.core_cell], cs = std::hypot(cu, cv);
        const double ex = cu / cs, ey = cv / cs;   // along-stream unit vector
        const bool zonal_section = std::abs(ey) >= std::abs(ex);  // stream mostly meridional -> section runs east-west
        long double transport = 0.0L;
        const long cx = static_cast<long>(st.core_cell % nx), cy = static_cast<long>(st.core_cell / nx);
        transport += static_cast<long double>(cs) * s.thickness_m[st.core_cell] * (zonal_section ? dx : dy);
        for (int dir = -1; dir <= 1; dir += 2) {
            for (long k = 1;; ++k) {
                const long xx = zonal_section ? cx + dir * k : cx, yy = zonal_section ? cy : cy + dir * k;
                if (xx < 0 || yy < 0 || xx >= static_cast<long>(nx) || yy >= static_cast<long>(ny)) break;
                const std::size_t i = static_cast<std::size_t>(yy) * nx + static_cast<std::size_t>(xx);
                if (!c.ocean[i]) break;
                const double along = s.u_centre_m_s[i] * ex + s.v_centre_m_s[i] * ey;
                if (s.stream_id[i] != id || along <= 0.0) break;
                transport += static_cast<long double>(along) * s.thickness_m[i] * (zonal_section ? dx : dy);
            }
        }
        st.transport_sv = static_cast<double>(transport) / 1.0e6;
        // Core streamline: midpoint integration forward and backward.
        auto trace = [&](double sign) {
            std::vector<std::pair<double, double>> pts;
            double gx = static_cast<double>(cx) + 0.5, gy = static_cast<double>(cy) + 0.5;
            for (int step = 0; step < 600; ++step) {
                double u, v;
                if (!interpolate(gx, gy, u, v)) break;
                const double speed = std::hypot(u, v);
                if (speed < p.stream_speed_threshold_m_s * 0.3) break;
                const double hx = gx + sign * 0.25 * u / speed, hy = gy + sign * 0.25 * v / speed;
                double um, vm;
                if (!interpolate(hx, hy, um, vm)) break;
                const double sm = std::hypot(um, vm);
                if (sm < p.stream_speed_threshold_m_s * 0.3) break;
                gx += sign * 0.5 * um / sm;
                gy += sign * 0.5 * vm / sm;
                if (gx < 0.0 || gy < 0.0 || gx > static_cast<double>(nx) || gy > static_cast<double>(ny)) break;
                const std::size_t cell = static_cast<std::size_t>(std::min(static_cast<double>(ny) - 1.0, gy)) * nx + static_cast<std::size_t>(std::min(static_cast<double>(nx) - 1.0, gx));
                if (!c.ocean[cell]) break;
                pts.emplace_back(gx / static_cast<double>(nx), gy / static_cast<double>(ny));
                if (pts.size() > 2 && std::hypot(gx - (static_cast<double>(cx) + 0.5), gy - (static_cast<double>(cy) + 0.5)) < 0.5 && step > 10) break;  // closed loop
            }
            return pts;
        };
        auto back = trace(-1.0);
        std::reverse(back.begin(), back.end());
        st.path = std::move(back);
        st.path.emplace_back((static_cast<double>(cx) + 0.5) / static_cast<double>(nx), (static_cast<double>(cy) + 0.5) / static_cast<double>(ny));
        auto forward = trace(1.0);
        st.path.insert(st.path.end(), forward.begin(), forward.end());
        if (st.cell_count >= 2) d.streams.push_back(std::move(st));  // single fast cells are noise, not streams
    }
    std::stable_sort(d.streams.begin(), d.streams.end(), [](const OceanStream& a, const OceanStream& b) { return a.transport_sv > b.transport_sv; });

    // River plumes: surface water fresher than the reference by the deficit, connected to the mouth.
    const double plume_limit = p.reference_salinity_psu - p.plume_salinity_deficit_psu;
    for (const auto& r : f.rivers) {
        RiverPlume pl;
        pl.mouth_cell = r.cell;
        pl.name = r.name;
        pl.discharge_m3_s = r.discharge_m3_s;
        pl.mouth_salinity_psu = s.surface_salinity_psu[r.cell];
        pl.minimum_salinity_psu = s.surface_salinity_psu[r.cell];
        std::vector<unsigned char> seen(count, 0);
        std::vector<std::size_t> stack;
        if (s.surface_salinity_psu[r.cell] < plume_limit) { stack.push_back(r.cell); seen[r.cell] = 1; }
        double cxs = 0.0, cys = 0.0, weight = 0.0;
        const double mx = static_cast<double>(r.cell % nx), my = static_cast<double>(r.cell / nx);
        while (!stack.empty()) {
            const std::size_t k = stack.back();
            stack.pop_back();
            pl.plume_area_m2 += area;
            const double fresh = std::max(0.0, (p.reference_salinity_psu - s.surface_salinity_psu[k]) / p.reference_salinity_psu);
            pl.plume_freshwater_m3 += fresh * s.thickness_m[k] * area;
            pl.minimum_salinity_psu = std::min(pl.minimum_salinity_psu, s.surface_salinity_psu[k]);
            const double kx = static_cast<double>(k % nx), ky = static_cast<double>(k / nx);
            pl.plume_extent_km = std::max(pl.plume_extent_km, std::hypot((kx - mx) * dx, (ky - my) * dy) / 1000.0);
            cxs += fresh * kx; cys += fresh * ky; weight += fresh;
            const std::size_t x = k % nx, y = k / nx;
            const std::size_t neighbours[4] = {x + 1 < nx ? k + 1 : count, x > 0 ? k - 1 : count, y + 1 < ny ? k + nx : count, y > 0 ? k - nx : count};
            for (std::size_t n : neighbours) {
                if (n >= count || !c.ocean[n] || seen[n] || s.surface_salinity_psu[n] >= plume_limit) continue;
                seen[n] = 1;
                stack.push_back(n);
            }
        }
        if (weight > 0.0) {
            const double ddx = (cxs / weight - mx) * dx, ddy = (cys / weight - my) * dy;
            double bearing = std::atan2(ddx, ddy) * 180.0 / kPi;
            if (bearing < 0.0) bearing += 360.0;
            pl.deflection_deg = (ddx == 0.0 && ddy == 0.0) ? 0.0 : bearing;
        }
        d.plumes.push_back(std::move(pl));
    }
    return d;
}

}  // namespace terraflux::weather
