#include "terraflux/weather/ocean_circulation.hpp"

#include "terraflux/weather/moist_air.hpp"
#include "terraflux/weather/pressure_wind.hpp"
#include "terraflux/weather/sea_ice.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kStefanBoltzmann = 5.670374419e-8;  // W m-2 K-4
constexpr double kZeroCelsius = 273.15;

bool layered(const OceanConfig& c) { return c.thermal.count >= 2; }

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

AirSeaFluxes air_sea_fluxes(double ts, double ta, double qa, double p, double wind, double cloud, double sw_down, const OceanAirSeaConfig& c) {
    AirSeaFluxes out;
    const double u = std::max(wind, c.minimum_wind_m_s);
    const double e_a_hpa = vapour_pressure_pa(qa, p) / 100.0;
    const double qs = c.saline_vapour_factor * saturation_specific_humidity_kg_kg(ts, p);
    const double latent_heat = 2.501e6 - 2370.0 * (ts - kZeroCelsius);
    out.shortwave_w_m2 = (1.0 - c.surface_albedo) * sw_down;
    out.longwave_w_m2 = -c.surface_emissivity * kStefanBoltzmann * std::pow(ts, 4) * (0.39 - 0.05 * std::sqrt(std::max(0.0, e_a_hpa))) * (1.0 - c.longwave_cloud_factor * cloud * cloud)
                        - 4.0 * c.surface_emissivity * kStefanBoltzmann * std::pow(ts, 3) * (ts - ta);
    out.sensible_w_m2 = c.air_density_kg_m3 * c.air_heat_capacity_j_kg_k * c.heat_transfer_coefficient * u * (ta - ts);
    const double evaporation_kg_m2_s = c.air_density_kg_m3 * c.moisture_transfer_coefficient * u * (qs - qa);
    out.latent_w_m2 = -latent_heat * evaporation_kg_m2_s;
    out.evaporation_m_s = evaporation_kg_m2_s / c.fresh_water_density_kg_m3;
    out.net_w_m2 = out.shortwave_w_m2 + out.longwave_w_m2 + out.sensible_w_m2 + out.latent_w_m2;
    return out;
}

double shortwave_transmission(double z, const OceanThermalLayersConfig& t) {
    if (z <= 0.0) return 1.0;
    return t.shortwave_red_fraction * std::exp(-z / t.shortwave_red_depth_m) + (1.0 - t.shortwave_red_fraction) * std::exp(-z / t.shortwave_blue_depth_m);
}

double solar_cosine_zenith(double latitude_deg, double longitude_deg, double day_of_year, double utc_hour) {
    const double declination = 23.44 * kPi / 180.0 * std::sin(2.0 * kPi * (284.0 + day_of_year) / 365.0);
    const double hour_angle = (utc_hour + longitude_deg / 15.0 - 12.0) * 15.0 * kPi / 180.0;
    const double phi = latitude_deg * kPi / 180.0;
    return std::sin(phi) * std::sin(declination) + std::cos(phi) * std::cos(declination) * std::cos(hour_angle);
}

double surface_shortwave_w_m2(double cos_zenith, double vapour_pressure_pa, double cloud_fraction, double solar_constant_w_m2) {
    if (!(cos_zenith > 0.0)) return 0.0;
    const double e_hpa = std::max(0.0, vapour_pressure_pa) / 100.0;
    const double clear = solar_constant_w_m2 * cos_zenith * cos_zenith / ((cos_zenith + 2.7) * e_hpa * 1.0e-3 + 1.085 * cos_zenith + 0.10);
    return clear * std::min(1.0, std::max(0.0, 1.0 - 0.62 * cloud_fraction));
}

std::vector<double> thermal_layer_fractions(const OceanConfig& c) {
    const std::size_t n = c.thermal.count;
    if (n < 2) return {1.0};
    const double top = c.thermal.surface_layer_m / c.initial_thickness_m;
    std::vector<double> sigma(n, 1.0 / static_cast<double>(n));
    if (top * static_cast<double>(n) < 1.0) {
        // Geometric stretching: top (r^n - 1) / (r - 1) = 1 for r > 1.
        auto total = [&](double r) { return top * (std::pow(r, static_cast<double>(n)) - 1.0) / (r - 1.0); };
        double lo = 1.0 + 1.0e-12, hi = 2.0;
        while (total(hi) < 1.0) hi *= 2.0;
        for (int it = 0; it < 200; ++it) { const double mid = 0.5 * (lo + hi); (total(mid) < 1.0 ? lo : hi) = mid; }
        const double r = 0.5 * (lo + hi);
        double sum = 0.0;
        for (std::size_t k = 0; k < n; ++k) { sigma[k] = top * std::pow(r, static_cast<double>(k)); sum += sigma[k]; }
        for (double& v : sigma) v /= sum;
    }
    return sigma;
}

std::vector<std::size_t> coastal_receivers(const OceanConfig& c) {
    const std::size_t nx = c.grid.nx, count = nx * c.grid.ny;
    std::vector<std::size_t> receiver(count, count);
    if (!(c.coastal.band_m > 0.0)) return receiver;
    const double dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m;
    const long ny = static_cast<long>(c.grid.ny), wx = static_cast<long>(std::ceil(c.coastal.band_m / dx)), wy = static_cast<long>(std::ceil(c.coastal.band_m / dy));
    for (std::size_t i = 0; i < count; ++i) {
        if (c.ocean[i]) continue;
        const long x = static_cast<long>(i % nx), y = static_cast<long>(i / nx);
        double best = std::numeric_limits<double>::infinity();
        // Search the window that holds every cell within the band, in index order (ties go to the lowest index).
        for (long yy = std::max(0L, y - wy); yy <= std::min(ny - 1, y + wy); ++yy) {
            for (long xx = std::max(0L, x - wx); xx <= std::min(static_cast<long>(nx) - 1, x + wx); ++xx) {
                const std::size_t j = static_cast<std::size_t>(yy) * nx + static_cast<std::size_t>(xx);
                if (!c.ocean[j]) continue;
                const double d = std::hypot(static_cast<double>(xx - x) * dx, static_cast<double>(yy - y) * dy);
                if (d < best) { best = d; receiver[i] = j; }
            }
        }
        if (best > c.coastal.band_m) receiver[i] = count;
    }
    return receiver;
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
    if (!std::isfinite(p.heat_capacity_j_kg_k) || p.heat_capacity_j_kg_k < 3000.0 || p.heat_capacity_j_kg_k > 4300.0) throw std::invalid_argument("ocean physics.heat_capacity_j_kg_k must lie within 3000-4300 J/(kg K)");
    require_finite_positive(p.lens_salinity_deficit_psu, "physics.lens_salinity_deficit_psu");
    require_finite_positive(p.lens_memory_s, "physics.lens_memory_s");
    // Air-sea coefficients (used when the forcing carries the atmosphere).
    const auto& a = c.air_sea;
    if (!std::isfinite(a.air_density_kg_m3) || a.air_density_kg_m3 < 0.5 || a.air_density_kg_m3 > 2.0) throw std::invalid_argument("ocean air_sea.air_density_kg_m3 must lie within 0.5-2");
    if (!std::isfinite(a.air_heat_capacity_j_kg_k) || a.air_heat_capacity_j_kg_k < 900.0 || a.air_heat_capacity_j_kg_k > 1100.0) throw std::invalid_argument("ocean air_sea.air_heat_capacity_j_kg_k must lie within 900-1100");
    for (double v : {a.heat_transfer_coefficient, a.moisture_transfer_coefficient}) {
        if (!std::isfinite(v) || v < 0.0 || v > 0.01) throw std::invalid_argument("ocean air_sea transfer coefficients must lie within 0-0.01");
    }
    if (!std::isfinite(a.minimum_wind_m_s) || a.minimum_wind_m_s < 0.0 || a.minimum_wind_m_s > 10.0) throw std::invalid_argument("ocean air_sea.minimum_wind_m_s must lie within 0-10 m/s");
    for (double v : {a.surface_albedo, a.surface_emissivity, a.longwave_cloud_factor}) {
        if (!std::isfinite(v) || v < 0.0 || v > 1.0) throw std::invalid_argument("ocean air_sea albedo, emissivity and longwave cloud factor must lie within 0-1");
    }
    if (!std::isfinite(a.saline_vapour_factor) || a.saline_vapour_factor < 0.9 || a.saline_vapour_factor > 1.0) throw std::invalid_argument("ocean air_sea.saline_vapour_factor must lie within 0.9-1");
    if (!std::isfinite(a.fresh_water_density_kg_m3) || a.fresh_water_density_kg_m3 < 900.0 || a.fresh_water_density_kg_m3 > 1100.0) throw std::invalid_argument("ocean air_sea.fresh_water_density_kg_m3 must lie within 900-1100");
    // Layered temperature column.
    const auto& th = c.thermal;
    if (th.count == 1 || th.count > 40) throw std::invalid_argument("ocean thermal_layers.count must be 0 (slab) or 2-40");
    if (layered(c)) {
        if (!std::isfinite(th.surface_layer_m) || th.surface_layer_m <= 0.0 || th.surface_layer_m >= c.initial_thickness_m) throw std::invalid_argument("ocean thermal_layers.surface_layer_m must lie within (0, initial thickness)");
        if (!std::isfinite(th.background_diffusivity_m2_s) || th.background_diffusivity_m2_s < 0.0 || th.background_diffusivity_m2_s > 1.0) throw std::invalid_argument("ocean thermal_layers.background_diffusivity_m2_s must lie within 0-1");
        if (!std::isfinite(th.wind_mixing_efficiency) || th.wind_mixing_efficiency < 0.0 || th.wind_mixing_efficiency > 5.0) throw std::invalid_argument("ocean thermal_layers.wind_mixing_efficiency must lie within 0-5");
        if (!std::isfinite(th.bulk_richardson_number) || th.bulk_richardson_number < 0.0 || th.bulk_richardson_number > 5.0) throw std::invalid_argument("ocean thermal_layers.bulk_richardson_number must lie within 0-5");
        require_finite_nonnegative(th.initial_mixed_layer_depth_m, "thermal_layers.initial_mixed_layer_depth_m");
        require_finite_positive(th.initial_thermocline_scale_m, "thermal_layers.initial_thermocline_scale_m");
        if (!std::isfinite(th.shortwave_red_fraction) || th.shortwave_red_fraction < 0.0 || th.shortwave_red_fraction > 1.0) throw std::invalid_argument("ocean thermal_layers.shortwave_red_fraction must lie within 0-1");
        require_finite_positive(th.shortwave_red_depth_m, "thermal_layers.shortwave_red_depth_m");
        require_finite_positive(th.shortwave_blue_depth_m, "thermal_layers.shortwave_blue_depth_m");
        require_finite_positive(th.mld_density_threshold_kg_m3, "thermal_layers.mld_density_threshold_kg_m3");
        require_finite_positive(th.ild_temperature_threshold_k, "thermal_layers.ild_temperature_threshold_k");
    }
    if (!c.initial_surface_temperature_k.empty()) {
        if (!layered(c)) throw std::invalid_argument("ocean initial_surface_temperature_k requires the layered column");
        if (c.initial_surface_temperature_k.size() != count) throw std::invalid_argument("ocean initial_surface_temperature_k must match the grid");
        for (std::size_t i = 0; i < count; ++i) {
            const double v = c.initial_surface_temperature_k[i];
            if (c.ocean[i] && (!std::isfinite(v) || v < 270.0 || v > 310.0)) throw std::invalid_argument("ocean initial surface temperatures must lie within 270-310 K");
        }
    }
    // Sea ice.
    if (c.sea_ice.enabled) {
        if (!layered(c)) throw std::invalid_argument("ocean sea ice needs the layered column (thermal_layers)");
        validate_sea_ice_physics(c.sea_ice.physics);
        if (!std::isfinite(c.sea_ice.initial_thickness_m) || c.sea_ice.initial_thickness_m < 0.0 || c.sea_ice.initial_thickness_m > 20.0) throw std::invalid_argument("ocean sea_ice.initial_thickness_m must lie within 0-20 m");
        if (!std::isfinite(c.sea_ice.initial_concentration) || c.sea_ice.initial_concentration < 0.0 || c.sea_ice.initial_concentration > c.sea_ice.physics.maximum_concentration) throw std::invalid_argument("ocean sea_ice.initial_concentration must lie within 0 and the maximum concentration");
        require_finite_nonnegative(c.sea_ice.initial_band_k, "sea_ice.initial_band_k");
        if (!std::isfinite(c.sea_ice.wind_drift_factor) || c.sea_ice.wind_drift_factor < 0.0 || c.sea_ice.wind_drift_factor > 0.1) throw std::invalid_argument("ocean sea_ice.wind_drift_factor must lie within 0-0.1");
        if (!std::isfinite(c.sea_ice.wind_turning_deg) || std::abs(c.sea_ice.wind_turning_deg) > 90.0) throw std::invalid_argument("ocean sea_ice.wind_turning_deg must lie within +-90");
        if (!std::isfinite(c.sea_ice.maximum_ridge_thickness_m) || c.sea_ice.maximum_ridge_thickness_m < c.sea_ice.physics.minimum_thickness_m || c.sea_ice.maximum_ridge_thickness_m > 30.0) throw std::invalid_argument("ocean sea_ice.maximum_ridge_thickness_m must lie between the minimum thickness and 30 m");
        if (!std::isfinite(c.sea_ice.maximum_drift_m_s) || c.sea_ice.maximum_drift_m_s <= 0.0 || c.sea_ice.maximum_drift_m_s > 5.0) throw std::invalid_argument("ocean sea_ice.maximum_drift_m_s must lie within 0-5 m/s");
    }
    // Coastal runoff.
    require_finite_nonnegative(c.coastal.band_m, "coastal.band_m");
    if (!std::isfinite(c.coastal.runoff_coefficient) || c.coastal.runoff_coefficient < 0.0 || c.coastal.runoff_coefficient > 1.0) throw std::invalid_argument("ocean coastal.runoff_coefficient must lie within 0-1");
    require_finite_positive(c.coastal.runoff_time_scale_s, "coastal.runoff_time_scale_s");
    // Stability: gravity waves of the reduced-gravity layer, Coriolis, drag, viscosity and diffusion.
    const double dt = c.timestep_seconds, dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m;
    double warmest = c.initial_temperature_k;
    for (std::size_t i = 0; i < c.initial_surface_temperature_k.size(); ++i) if (c.ocean[i]) warmest = std::max(warmest, c.initial_surface_temperature_k[i]);
    const double g_prime = std::max(p.minimum_reduced_gravity_m_s2, reduced_gravity_m_s2(warmest, c.initial_salinity_psu, p));
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

void check_layers(const OceanState& s, const OceanConfig& c) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (layered(c) && (s.layer_temperature_k.size() != count * c.thermal.count || s.layer_salinity_psu.size() != count * c.thermal.count)) throw std::invalid_argument("ocean state lacks the layered column; initialise it with the same thermal configuration");
    if (!layered(c) && (!s.layer_temperature_k.empty() || !s.layer_salinity_psu.empty())) throw std::invalid_argument("ocean state carries a layered column but the configuration has none");
    if (!s.coastal_store_m.empty() && s.coastal_store_m.size() != count) throw std::invalid_argument("ocean coastal store must be empty or match the grid");
    for (const auto* v : {&s.recent_rain_m, &s.recent_runoff_m, &s.recent_river_m, &s.recent_glacial_m}) if (!v->empty() && v->size() != count) throw std::invalid_argument("ocean freshwater records must be empty or match the grid");
    if (c.sea_ice.enabled && (s.ice_thickness_m.size() != count || s.ice_concentration.size() != count)) throw std::invalid_argument("ocean state lacks the sea ice; initialise it with sea ice enabled");
    if (!c.sea_ice.enabled && (!s.ice_thickness_m.empty() || !s.ice_concentration.empty())) throw std::invalid_argument("ocean state carries sea ice but the configuration has none");
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
    if (!f.rain_m_s.empty() || !f.rain_temperature_k.empty()) {
        if (f.rain_m_s.size() != count || f.rain_temperature_k.size() != count) throw std::invalid_argument("ocean rain and rain temperature arrays must both match the grid");
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(f.rain_m_s[i]) || f.rain_m_s[i] < 0.0 || f.rain_m_s[i] > 500.0 / 3.6e6) throw std::invalid_argument("ocean rain must be finite and within 0-500 mm/h");
            if (!std::isfinite(f.rain_temperature_k[i]) || f.rain_temperature_k[i] < 250.0 || f.rain_temperature_k[i] > 320.0) throw std::invalid_argument("ocean rain temperature must lie within 250-320 K");
        }
    }
    if (c.coastal.band_m > 0.0 && f.rain_m_s.empty()) throw std::invalid_argument("ocean coastal runoff needs the rain arrays (zeros when dry)");
    const bool any_air = !f.air_temperature_k.empty() || !f.air_specific_humidity_kg_kg.empty() || !f.surface_pressure_pa.empty() || !f.wind_speed_m_s.empty() || !f.cloud_fraction.empty() || !f.shortwave_down_w_m2.empty();
    if (any_air) {
        if (f.air_temperature_k.size() != count || f.air_specific_humidity_kg_kg.size() != count || f.surface_pressure_pa.size() != count || f.wind_speed_m_s.size() != count || f.cloud_fraction.size() != count || f.shortwave_down_w_m2.size() != count) {
            throw std::invalid_argument("ocean air-sea forcing needs air temperature, humidity, pressure, wind speed, cloud fraction and shortwave, each matching the grid");
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(f.air_temperature_k[i]) || f.air_temperature_k[i] < 200.0 || f.air_temperature_k[i] > 330.0) throw std::invalid_argument("ocean air temperature must lie within 200-330 K");
            if (!std::isfinite(f.surface_pressure_pa[i]) || f.surface_pressure_pa[i] < 50000.0 || f.surface_pressure_pa[i] > 110000.0) throw std::invalid_argument("ocean surface pressure must lie within 50-110 kPa");
            if (!std::isfinite(f.air_specific_humidity_kg_kg[i]) || f.air_specific_humidity_kg_kg[i] < 0.0 || f.air_specific_humidity_kg_kg[i] > 0.05) throw std::invalid_argument("ocean air specific humidity must lie within 0-0.05 kg/kg");
            if (!std::isfinite(f.wind_speed_m_s[i]) || f.wind_speed_m_s[i] < 0.0 || f.wind_speed_m_s[i] > 80.0) throw std::invalid_argument("ocean wind speed must lie within 0-80 m/s");
            if (!std::isfinite(f.cloud_fraction[i]) || f.cloud_fraction[i] < 0.0 || f.cloud_fraction[i] > 1.0) throw std::invalid_argument("ocean cloud fraction must lie within 0-1");
            if (!std::isfinite(f.shortwave_down_w_m2[i]) || f.shortwave_down_w_m2[i] < 0.0 || f.shortwave_down_w_m2[i] > 1400.0) throw std::invalid_argument("ocean shortwave must lie within 0-1400 W/m2");
        }
    }
    for (const auto* glacial : {&f.glacial_meltwater_m3_s, &f.glacial_ice_m3_s}) {
        if (glacial->empty()) continue;
        if (glacial->size() != count) throw std::invalid_argument("ocean glacial fresh water must be empty or match the grid");
        for (std::size_t i = 0; i < count; ++i) {
            const double v = (*glacial)[i];
            if (!std::isfinite(v) || v < 0.0 || v > 1.0e7) throw std::invalid_argument("ocean glacial fresh water must lie within 0-1e7 m3/s");
            if (v > 0.0 && !c.ocean[i]) throw std::invalid_argument("ocean glacial fresh water must enter sea cells");
        }
    }
    if (c.sea_ice.enabled && f.air_temperature_k.empty()) throw std::invalid_argument("ocean sea ice needs the air-sea forcing");
    if (!f.mixing_stress_pa.empty()) {
        if (f.mixing_stress_pa.size() != count) throw std::invalid_argument("ocean mixing stress must be empty or match the grid");
        for (double v : f.mixing_stress_pa) if (!std::isfinite(v) || v < 0.0 || v > 20.0) throw std::invalid_argument("ocean mixing stress must lie within 0-20 Pa");
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

// ---- Layered column -------------------------------------------------------------------------

// Initial profile T(z) = T_deep + (T_s - T_deep) exp(-max(0, z - mld) / scale) at the layer midpoints.
void initial_column(const OceanConfig& c, const std::vector<double>& sigma, std::size_t cell, double* out) {
    const double ts = c.initial_surface_temperature_k.empty() ? c.initial_temperature_k : c.initial_surface_temperature_k[cell];
    const double deep = c.physics.deep_temperature_k;
    double top = 0.0;
    for (std::size_t k = 0; k < sigma.size(); ++k) {
        const double z = (top + 0.5 * sigma[k]) * c.initial_thickness_m;
        out[k] = deep + (ts - deep) * std::exp(-std::max(0.0, z - c.thermal.initial_mixed_layer_depth_m) / c.thermal.initial_thermocline_scale_m);
        top += sigma[k];
    }
}

// Conservative piecewise-constant remap of (thickness, temperature) onto new thicknesses with the same total.
void remap_column(const std::vector<double>& dz_src, const std::vector<double>& t_src, const std::vector<double>& dz_dst, std::vector<double>& t_dst) {
    const std::size_t n = dz_src.size(), m = dz_dst.size();
    t_dst.assign(m, 0.0);
    std::size_t k = 0;
    double src_top = 0.0, src_bottom = dz_src[0];
    double dst_top = 0.0;
    for (std::size_t j = 0; j < m; ++j) {
        const double dst_bottom = (j + 1 == m) ? std::numeric_limits<double>::infinity() : dst_top + dz_dst[j];
        long double heat = 0.0L;
        double covered = 0.0;
        while (k < n) {
            const double lo = std::max(src_top, dst_top), hi = std::min(src_bottom, dst_bottom);
            if (hi > lo) { heat += static_cast<long double>(t_src[k]) * (hi - lo); covered += hi - lo; }
            if (src_bottom <= dst_bottom) {
                ++k;
                if (k < n) { src_top = src_bottom; src_bottom = src_top + dz_src[k]; }
            } else {
                break;
            }
        }
        t_dst[j] = covered > 0.0 ? static_cast<double>(heat / covered) : (j > 0 ? t_dst[j - 1] : t_src[0]);
        dst_top = dst_bottom;
    }
}

// Convective adjustment: merge neighbouring groups (temperature and salinity together) while the upper is denser.
void convective_adjustment(std::vector<double>& t, std::vector<double>& sal, const std::vector<double>& dz, const OceanPhysicsConfig& p) {
    struct Group { std::size_t first, last; double thickness, temperature, salinity; };
    thread_local std::vector<Group> stack;
    stack.clear();
    for (std::size_t k = 0; k < t.size(); ++k) {
        stack.push_back({k, k, dz[k], t[k], sal[k]});
        while (stack.size() >= 2) {
            const Group& lower = stack[stack.size() - 1];
            const Group& upper = stack[stack.size() - 2];
            if (!(seawater_density_kg_m3(upper.temperature, upper.salinity, p) > seawater_density_kg_m3(lower.temperature, lower.salinity, p) + 1.0e-10)) break;
            const double total = upper.thickness + lower.thickness;
            Group merged{upper.first, lower.last, total, (upper.temperature * upper.thickness + lower.temperature * lower.thickness) / total,
                         (upper.salinity * upper.thickness + lower.salinity * lower.thickness) / total};
            stack.pop_back();
            stack.back() = merged;
        }
    }
    for (const auto& g : stack) for (std::size_t k = g.first; k <= g.last; ++k) { t[k] = g.temperature; sal[k] = g.salinity; }
}

// Kraus-Turner wind mixing: the turbulent kinetic energy m0 rho_0 u*^3 dt deepens the mixed layer while it
// pays for the potential-energy increase g sum (rho_j - rho_mix) z_j dz_j (linear equation of state); the
// next layer is entrained partially when the energy runs out. Returns the energy used (J/m2).
double wind_mixing(std::vector<double>& t, std::vector<double>& sal, const std::vector<double>& dz, double energy, const OceanPhysicsConfig& p) {
    const std::size_t n = t.size();
    if (!(energy > 0.0) || n < 2) return 0.0;
    const double available = energy;
    auto same = [](double a, double b) { return std::abs(a - b) <= 1.0e-12 * std::max(1.0, std::abs(a)); };
    // Existing mixed layer: leading layers with the top temperature and salinity.
    std::size_t mixed = 1;
    while (mixed < n && same(t[mixed], t[0]) && same(sal[mixed], sal[0])) ++mixed;
    double depth = 0.0, first_moment = 0.0, heat = 0.0, salt = 0.0;   // depth, sum z_j dz_j, sum T_j dz_j, sum S_j dz_j
    for (std::size_t j = 0; j < mixed; ++j) { first_moment += (depth + 0.5 * dz[j]) * dz[j]; heat += t[j] * dz[j]; salt += sal[j] * dz[j]; depth += dz[j]; }
    for (std::size_t k = mixed; k < n; ++k) {
        const double tu = heat / depth, su = salt / depth;   // the mixed layer is uniform
        const double rho_u = seawater_density_kg_m3(tu, su, p), rho_k = seawater_density_kg_m3(t[k], sal[k], p);
        auto mix = [&](double phi, double& tm, double& sm) {
            const double part = phi * dz[k];
            tm = (tu * depth + t[k] * part) / (depth + part);
            sm = (su * depth + sal[k] * part) / (depth + part);
        };
        auto cost = [&](double phi) {
            double tm, sm;
            mix(phi, tm, sm);
            const double rho_m = seawater_density_kg_m3(tm, sm, p), part = phi * dz[k];
            return p.gravity_m_s2 * ((rho_u - rho_m) * first_moment + (rho_k - rho_m) * (depth + 0.5 * part) * part);
        };
        const double full = cost(1.0);
        if (full <= energy) {
            energy -= std::max(0.0, full);
            first_moment += (depth + 0.5 * dz[k]) * dz[k];
            heat += t[k] * dz[k];
            salt += sal[k] * dz[k];
            depth += dz[k];
            for (std::size_t j = 0; j <= k; ++j) { t[j] = heat / depth; sal[j] = salt / depth; }
            continue;
        }
        double lo = 0.0, hi = 1.0;
        for (int it = 0; it < 60; ++it) { const double mid = 0.5 * (lo + hi); (cost(mid) <= energy ? lo : hi) = mid; }
        const double phi = lo;
        if (phi > 0.0) {
            double tm, sm;
            mix(phi, tm, sm);
            energy -= std::max(0.0, cost(phi));
            for (std::size_t j = 0; j < k; ++j) { t[j] = tm; sal[j] = sm; }
            t[k] = phi * tm + (1.0 - phi) * t[k];     // the partly entrained layer is averaged (numerical mixing)
            sal[k] = phi * sm + (1.0 - phi) * sal[k];
        }
        break;
    }
    return available - energy;
}

// Shear mixing (Price, Weller & Pinkel 1986 bulk criterion) with the inertial slab-current estimate
// dU = u*^2 / (|f| h): entrain the next layer while Ri_b = g (rho_next - rho_mix) h / (rho_0 dU^2) < Ri_c.
void shear_mixing(std::vector<double>& t, std::vector<double>& sal, const std::vector<double>& dz, double friction_velocity, double coriolis, double critical, const OceanPhysicsConfig& p) {
    const std::size_t n = t.size();
    if (!(critical > 0.0) || !(friction_velocity > 0.0) || n < 2) return;
    auto same = [](double a, double b) { return std::abs(a - b) <= 1.0e-12 * std::max(1.0, std::abs(a)); };
    std::size_t mixed = 1;
    while (mixed < n && same(t[mixed], t[0]) && same(sal[mixed], sal[0])) ++mixed;
    double depth = 0.0, heat = 0.0, salt = 0.0;
    for (std::size_t j = 0; j < mixed; ++j) { depth += dz[j]; heat += t[j] * dz[j]; salt += sal[j] * dz[j]; }
    const double f = std::max(std::abs(coriolis), 1.0e-5);
    while (mixed < n) {
        const double shear = friction_velocity * friction_velocity / (f * depth);
        const double jump = seawater_density_kg_m3(t[mixed], sal[mixed], p) - seawater_density_kg_m3(heat / depth, salt / depth, p);
        if (p.gravity_m_s2 * jump * depth / (p.reference_density_kg_m3 * shear * shear) >= critical) break;
        depth += dz[mixed];
        heat += t[mixed] * dz[mixed];
        salt += sal[mixed] * dz[mixed];
        ++mixed;
        for (std::size_t j = 0; j < mixed; ++j) { t[j] = heat / depth; sal[j] = salt / depth; }
    }
}

// Implicit vertical diffusion with no flux through the surface and the layer bottom (Thomas algorithm).
void vertical_diffusion(std::vector<double>& t, const std::vector<double>& dz, double kappa, double dt) {
    const std::size_t n = t.size();
    if (!(kappa > 0.0) || n < 2) return;
    thread_local std::vector<double> a, b, cc, d;   // reused work space (called per cell per step)
    a.assign(n, 0.0); b.assign(n, 0.0); cc.assign(n, 0.0); d.assign(n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        const double up = k > 0 ? kappa * dt / (0.5 * (dz[k - 1] + dz[k])) : 0.0;
        const double down = k + 1 < n ? kappa * dt / (0.5 * (dz[k] + dz[k + 1])) : 0.0;
        a[k] = -up;
        cc[k] = -down;
        b[k] = dz[k] + up + down;
        d[k] = dz[k] * t[k];
    }
    for (std::size_t k = 1; k < n; ++k) {
        const double w = a[k] / b[k - 1];
        b[k] -= w * cc[k - 1];
        d[k] -= w * d[k - 1];
    }
    t[n - 1] = d[n - 1] / b[n - 1];
    for (std::size_t k = n - 1; k-- > 0;) t[k] = (d[k] - cc[k] * t[k + 1]) / b[k];
}

// Depth where a profile first departs from the top-layer value by the threshold (linear between midpoints).
double departure_depth(const std::vector<double>& value, const std::vector<double>& dz, double threshold, bool absolute) {
    double top = 0.0, previous_mid = 0.5 * dz[0];
    for (std::size_t k = 1; k < value.size(); ++k) {
        top += dz[k - 1];
        const double mid = top + 0.5 * dz[k];
        const double dk = absolute ? std::abs(value[k] - value[0]) : value[k] - value[0];
        if (dk >= threshold) {
            const double dp = absolute ? std::abs(value[k - 1] - value[0]) : value[k - 1] - value[0];
            const double w = dk > dp ? (threshold - dp) / (dk - dp) : 1.0;
            return std::max(dz[0], previous_mid + std::max(0.0, std::min(1.0, w)) * (mid - previous_mid));
        }
        previous_mid = mid;
    }
    return top + dz.back();
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
    s.sst_k.assign(count, 0.0);
    s.mixed_layer_depth_m.assign(count, 0.0);
    s.isothermal_layer_depth_m.assign(count, 0.0);
    const std::size_t layers = layered(c) ? c.thermal.count : 0;
    const std::vector<double> sigma = layers ? thermal_layer_fractions(c) : std::vector<double>{};
    std::vector<double> dz(layers), column(layers), rho(layers);
    long double heat_content = 0.0L;
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
        s.surface_salinity_psu[i] = layers ? s.layer_salinity_psu[i * layers] : s.salinity_psu[i] * std::max(0.0, 1.0 - s.freshwater_m[i] / p.mixed_layer_depth_m);
        s.freshwater_fraction[i] = std::max(0.0, (p.reference_salinity_psu - s.surface_salinity_psu[i]) / p.reference_salinity_psu);
        ke += 0.5L * p.reference_density_kg_m3 * s.thickness_m[i] * g.area * (s.speed_m_s[i] * s.speed_m_s[i]);
        heat_content += static_cast<long double>(s.thickness_m[i]) * (s.temperature_k[i] - kZeroCelsius) * g.area;
        if (layers) {
            for (std::size_t k = 0; k < layers; ++k) { dz[k] = sigma[k] * s.thickness_m[i]; column[k] = s.layer_temperature_k[i * layers + k]; }
            for (std::size_t k = 0; k < layers; ++k) rho[k] = seawater_density_kg_m3(column[k], s.layer_salinity_psu[i * layers + k], p);
            s.sst_k[i] = column[0];
            s.mixed_layer_depth_m[i] = departure_depth(rho, dz, c.thermal.mld_density_threshold_kg_m3, false);
            s.isothermal_layer_depth_m[i] = departure_depth(column, dz, c.thermal.ild_temperature_threshold_k, true);
        } else {
            s.sst_k[i] = s.temperature_k[i];
            s.mixed_layer_depth_m[i] = s.thickness_m[i];
            s.isothermal_layer_depth_m[i] = s.thickness_m[i];
        }
    }
    s.kinetic_energy_j = static_cast<double>(ke);
    s.heat_content_j = static_cast<double>(heat_content) * p.reference_density_kg_m3 * p.heat_capacity_j_kg_k;
    long double store = 0.0L;
    for (std::size_t i = 0; i < s.coastal_store_m.size(); ++i) store += static_cast<long double>(s.coastal_store_m[i]) * g.area;
    s.coastal_storage_m3 = static_cast<double>(store);
    long double ice_volume = 0.0L, ice_area = 0.0L;
    for (std::size_t i = 0; i < s.ice_thickness_m.size(); ++i) { ice_volume += static_cast<long double>(s.ice_concentration[i]) * s.ice_thickness_m[i] * g.area; ice_area += static_cast<long double>(s.ice_concentration[i]) * g.area; }
    s.ice_volume_m3 = static_cast<double>(ice_volume);
    s.ice_area_m2 = static_cast<double>(ice_area);
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

// Free drift of the sea ice: velocity = upper-ocean current + Nansen wind drift, capped; upwind (donor-cell)
// transport of the area a, the volume a h and the surface temperature (weighted by area) between sea cells, in
// sub-steps of Courant number <= 0.25; ridging caps the area at the maximum concentration.
void drift_sea_ice(OceanState& s, const OceanForcing& f, const OceanConfig& c, const Geometry& g, const std::vector<double>& coriolis) {
    const std::size_t nx = g.nx, count = g.count;
    const auto& si = c.sea_ice;
    const double turn = si.wind_turning_deg * 3.14159265358979323846 / 180.0;
    std::vector<double>& ue = s.ice_drift_east_m_s;
    std::vector<double>& un = s.ice_drift_north_m_s;
    bool any = false;
    double fastest = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        ue[i] = un[i] = 0.0;
        if (!c.ocean[i] || s.ice_concentration[i] <= 0.0) continue;
        any = true;
        // The current at the cell centre: the mean of its existing faces.
        double cu = 0.0, cv = 0.0;
        int nu = 0, nv = 0;
        if (g.u_face(i)) { cu += s.u_m_s[i]; ++nu; }
        if (i % nx > 0 && g.u_face(i - 1)) { cu += s.u_m_s[i - 1]; ++nu; }
        if (g.v_face(i)) { cv += s.v_m_s[i]; ++nv; }
        if (i >= nx && g.v_face(i - nx)) { cv += s.v_m_s[i - nx]; ++nv; }
        cu = nu ? cu / nu : 0.0;
        cv = nv ? cv / nv : 0.0;
        // The wind: its direction from the stress, its speed from the air-sea forcing; turned right (NH) or left (SH).
        const double tx = f.wind_stress_east_pa[i], ty = f.wind_stress_north_pa[i], tau = std::hypot(tx, ty);
        double wu = 0.0, wv = 0.0;
        if (tau > 0.0 && !f.wind_speed_m_s.empty()) {
            const double speed = si.wind_drift_factor * f.wind_speed_m_s[i];
            const double angle = coriolis[i] > 0.0 ? -turn : coriolis[i] < 0.0 ? turn : 0.0;   // clockwise in the north, none without rotation
            const double ex = tx / tau, ey = ty / tau;
            wu = speed * (ex * std::cos(angle) - ey * std::sin(angle));
            wv = speed * (ex * std::sin(angle) + ey * std::cos(angle));
        }
        double u = cu + wu, v = cv + wv;
        const double sp = std::hypot(u, v);
        if (sp > si.maximum_drift_m_s) { u *= si.maximum_drift_m_s / sp; v *= si.maximum_drift_m_s / sp; }
        ue[i] = u;
        un[i] = v;
        fastest = std::max(fastest, std::max(std::abs(u) / g.dx, std::abs(v) / g.dy));
    }
    if (!any || fastest == 0.0) return;
    const double dt = c.timestep_seconds;
    const int substeps = std::max(1, static_cast<int>(std::ceil(fastest * dt / 0.25)));
    const double h = dt / substeps;
    std::vector<double> area(count), volume(count), heat(count);
    for (std::size_t i = 0; i < count; ++i) {
        area[i] = s.ice_concentration[i];
        volume[i] = s.ice_concentration[i] * s.ice_thickness_m[i];
        heat[i] = s.ice_concentration[i] * s.ice_surface_temperature_k[i];
    }
    std::vector<double> fa(count), fv(count), ft(count), ga(count), gv(count), gt(count);
    const double full = si.physics.maximum_concentration * (1.0 - 1.0e-9);
    // A packed cell (at the maximum concentration and the ridge thickness) takes no more ice.
    auto packed = [&](std::size_t k) { return area[k] >= full && volume[k] >= area[k] * si.maximum_ridge_thickness_m; };
    for (int step = 0; step < substeps; ++step) {
        for (std::size_t i = 0; i < count; ++i) {
            fa[i] = fv[i] = ft[i] = ga[i] = gv[i] = gt[i] = 0.0;
            if (g.u_face(i)) {
                const double u = 0.5 * (ue[i] + ue[i + 1]);
                const std::size_t up = u >= 0.0 ? i : i + 1, down = u >= 0.0 ? i + 1 : i;
                const double w = packed(down) ? 0.0 : u * h / g.dx;
                fa[i] = w * area[up]; fv[i] = w * volume[up]; ft[i] = w * heat[up];
            }
            if (g.v_face(i)) {
                const double v = 0.5 * (un[i] + un[i + nx]);
                const std::size_t up = v >= 0.0 ? i : i + nx, down = v >= 0.0 ? i + nx : i;
                const double w = packed(down) ? 0.0 : v * h / g.dy;
                ga[i] = w * area[up]; gv[i] = w * volume[up]; gt[i] = w * heat[up];
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!c.ocean[i]) continue;
            const std::size_t x = i % nx;
            const double west_a = x > 0 ? fa[i - 1] : 0.0, west_v = x > 0 ? fv[i - 1] : 0.0, west_t = x > 0 ? ft[i - 1] : 0.0;
            const double south_a = i >= nx ? ga[i - nx] : 0.0, south_v = i >= nx ? gv[i - nx] : 0.0, south_t = i >= nx ? gt[i - nx] : 0.0;
            area[i] = std::max(0.0, area[i] + west_a - fa[i] + south_a - ga[i]);
            volume[i] = std::max(0.0, volume[i] + west_v - fv[i] + south_v - gv[i]);
            heat[i] = std::max(0.0, heat[i] + west_t - ft[i] + south_t - gt[i]);
        }
    }
    const double cap = si.physics.maximum_concentration;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        if (volume[i] <= 0.0 || area[i] <= 0.0) {
            // Rounding leftovers of ice that drifted away entirely.
            s.ice_concentration[i] = 0.0; s.ice_thickness_m[i] = 0.0;
            continue;
        }
        const double a = std::min(cap, area[i]);   // ridging: the area is capped, the volume kept
        s.ice_surface_temperature_k[i] = heat[i] / area[i];
        s.ice_concentration[i] = a;
        s.ice_thickness_m[i] = volume[i] / a;
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
    s.coastal_store_m.assign(count, 0.0);
    for (auto* v : {&s.recent_rain_m, &s.recent_runoff_m, &s.recent_river_m, &s.recent_glacial_m}) v->assign(count, 0.0);
    for (auto* v : {&s.evaporation_m_s, &s.shortwave_w_m2, &s.longwave_w_m2, &s.sensible_w_m2, &s.latent_w_m2, &s.rain_heat_w_m2, &s.net_heat_w_m2, &s.coastal_inflow_m3_s, &s.ice_growth_m_s, &s.ocean_to_ice_heat_w_m2, &s.ice_drift_east_m_s, &s.ice_drift_north_m_s}) v->assign(count, 0.0);
    s.ice_surface_temperature_k.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        s.thickness_m[i] = c.initial_thickness_m;
        s.salinity_psu[i] = c.initial_salinity_psu;
        s.temperature_k[i] = c.initial_temperature_k;
    }
    if (layered(c)) {
        const std::size_t layers = c.thermal.count;
        const std::vector<double> sigma = thermal_layer_fractions(c);
        s.layer_temperature_k.assign(count * layers, 0.0);
        s.layer_salinity_psu.assign(count * layers, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            if (!c.ocean[i]) continue;
            initial_column(c, sigma, i, &s.layer_temperature_k[i * layers]);
            for (std::size_t k = 0; k < layers; ++k) s.layer_salinity_psu[i * layers + k] = c.initial_salinity_psu;
            double bulk = 0.0;
            // The water cannot start below its freezing point.
            for (std::size_t k = 0; k < layers; ++k) {
                double& tk = s.layer_temperature_k[i * layers + k];
                tk = std::max(tk, seawater_freezing_point_k(c.initial_salinity_psu));
                bulk += sigma[k] * tk;
            }
            s.temperature_k[i] = bulk;
        }
        if (c.sea_ice.enabled) {
            s.ice_thickness_m.assign(count, 0.0);
            s.ice_concentration.assign(count, 0.0);
            const double tf = seawater_freezing_point_k(c.initial_salinity_psu);
            for (std::size_t i = 0; i < count; ++i) {
                if (!c.ocean[i] || s.layer_temperature_k[i * layers] > tf + c.sea_ice.initial_band_k || c.sea_ice.initial_thickness_m <= 0.0 || c.sea_ice.initial_concentration <= 0.0) continue;
                s.ice_thickness_m[i] = c.sea_ice.initial_thickness_m;
                s.ice_concentration[i] = c.sea_ice.initial_concentration;
                s.ice_surface_temperature_k[i] = tf;   // until the first surface balance
            }
        }
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
    check_layers(previous, c);
    const auto& p = c.physics;
    const double dt = c.timestep_seconds, dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m, area = dx * dy;
    const Geometry g(nx, ny, dx, dy, c.ocean);
    const std::vector<double> coriolis = coriolis_parameter(c.grid, coriolis_settings(p));
    const bool air_sea = !f.air_temperature_k.empty();
    const bool has_rain = !f.rain_m_s.empty();
    const std::size_t layers = layered(c) ? c.thermal.count : 0;
    const std::vector<double> sigma = layers ? thermal_layer_fractions(c) : std::vector<double>{};
    const double rho_cp = p.reference_density_kg_m3 * p.heat_capacity_j_kg_k;
    OceanState s = previous;
    s.sinking_m_s.assign(count, 0.0);
    if (s.coastal_store_m.empty()) s.coastal_store_m.assign(count, 0.0);
    const double memory = std::exp(-dt / p.lens_memory_s);
    for (auto* v : {&s.recent_rain_m, &s.recent_runoff_m, &s.recent_river_m, &s.recent_glacial_m}) {
        if (v->empty()) v->assign(count, 0.0);
        for (double& x : *v) x *= memory;
    }
    for (auto* v : {&s.evaporation_m_s, &s.shortwave_w_m2, &s.longwave_w_m2, &s.sensible_w_m2, &s.latent_w_m2, &s.rain_heat_w_m2, &s.net_heat_w_m2, &s.coastal_inflow_m3_s, &s.ice_growth_m_s, &s.ocean_to_ice_heat_w_m2, &s.ice_drift_east_m_s, &s.ice_drift_north_m_s}) v->assign(count, 0.0);
    s.ice_surface_temperature_k.assign(count, 0.0);
    const bool ice = c.sea_ice.enabled;
    const SeaIcePhysics& ip = c.sea_ice.physics;
    const double ice_to_water = ip.ice_density_kg_m3 / p.reference_density_kg_m3;   // water (m) per m of ice
    const double latent_per_ice = ip.ice_density_kg_m3 * ip.latent_heat_fusion_j_kg / rho_cp;   // K m per m of ice
    long double ice_water_total = 0.0L, ice_salt_total = 0.0L, glacial_liquid_total = 0.0L, glacial_ice_total = 0.0L;
    double water_area = 0.0;
    for (std::size_t i = 0; i < count; ++i) if (c.ocean[i]) water_area += area;

    // 1. Face volume fluxes per unit width (upwind thickness) and tracer fluxes.
    std::vector<double> fx(count, 0.0), fy(count, 0.0), fsx(count, 0.0), fsy(count, 0.0), ftx(count, 0.0), fty(count, 0.0), ffx(count, 0.0), ffy(count, 0.0);
    // Layered column: the temperature and salt fluxes of every sublayer per unit sigma (the layer moves as a slab).
    std::vector<double> ftxk(count * layers, 0.0), ftyk(count * layers, 0.0), fsxk(count * layers, 0.0), fsyk(count * layers, 0.0);
    const std::vector<double>& lt0 = previous.layer_temperature_k;
    const std::vector<double>& ls0 = previous.layer_salinity_psu;
    for (std::size_t i = 0; i < count; ++i) {
        if (g.u_face(i)) {
            const double u = previous.u_m_s[i];
            const std::size_t up = u >= 0.0 ? i : i + 1;
            fx[i] = u * previous.thickness_m[up];
            const double hf = 0.5 * (previous.thickness_m[i] + previous.thickness_m[i + 1]);
            fsx[i] = fx[i] * previous.salinity_psu[up] - p.tracer_diffusivity_m2_s * hf * (previous.salinity_psu[i + 1] - previous.salinity_psu[i]) / dx;
            ftx[i] = fx[i] * previous.temperature_k[up] - p.tracer_diffusivity_m2_s * hf * (previous.temperature_k[i + 1] - previous.temperature_k[i]) / dx;
            ffx[i] = u * previous.freshwater_m[up] - p.tracer_diffusivity_m2_s * (previous.freshwater_m[i + 1] - previous.freshwater_m[i]) / dx;
            for (std::size_t k = 0; k < layers; ++k) {
                ftxk[i * layers + k] = fx[i] * lt0[up * layers + k] - p.tracer_diffusivity_m2_s * hf * (lt0[(i + 1) * layers + k] - lt0[i * layers + k]) / dx;
                fsxk[i * layers + k] = fx[i] * ls0[up * layers + k] - p.tracer_diffusivity_m2_s * hf * (ls0[(i + 1) * layers + k] - ls0[i * layers + k]) / dx;
            }
        }
        if (g.v_face(i)) {
            const double v = previous.v_m_s[i];
            const std::size_t up = v >= 0.0 ? i : i + nx;
            fy[i] = v * previous.thickness_m[up];
            const double hf = 0.5 * (previous.thickness_m[i] + previous.thickness_m[i + nx]);
            fsy[i] = fy[i] * previous.salinity_psu[up] - p.tracer_diffusivity_m2_s * hf * (previous.salinity_psu[i + nx] - previous.salinity_psu[i]) / dy;
            fty[i] = fy[i] * previous.temperature_k[up] - p.tracer_diffusivity_m2_s * hf * (previous.temperature_k[i + nx] - previous.temperature_k[i]) / dy;
            ffy[i] = v * previous.freshwater_m[up] - p.tracer_diffusivity_m2_s * (previous.freshwater_m[i + nx] - previous.freshwater_m[i]) / dy;
            for (std::size_t k = 0; k < layers; ++k) {
                ftyk[i * layers + k] = fy[i] * lt0[up * layers + k] - p.tracer_diffusivity_m2_s * hf * (lt0[(i + nx) * layers + k] - lt0[i * layers + k]) / dy;
                fsyk[i * layers + k] = fy[i] * ls0[up * layers + k] - p.tracer_diffusivity_m2_s * hf * (ls0[(i + nx) * layers + k] - ls0[i * layers + k]) / dy;
            }
        }
    }

    // 2. Deep-water formation (sinking) on the start-of-step state; uniform return upwelling.
    long double sink_total = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        const double surface_salinity = layers ? ls0[i * layers] : previous.salinity_psu[i] * std::max(0.0, 1.0 - previous.freshwater_m[i] / p.mixed_layer_depth_m);
        const double surface_temperature = layers ? lt0[i * layers] : previous.temperature_k[i];  // the column sinks when its surface is dense
        const double gp = reduced_gravity_m_s2(surface_temperature, surface_salinity, p);  // a fresh cap stops the sinking
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

    // 2b. Rain on land: coastal land fills its store and drains to the nearest sea cell (exact linear
    //     reservoir over the step), land beyond the band is left to the rivers. Rain on the sea is counted here.
    std::vector<double> coastal_depth(count, 0.0), coastal_heat(count, 0.0);
    long double rain_sea = 0.0L, rain_coastal_sea = 0.0L, rain_coast = 0.0L, rain_inland = 0.0L, runoff_total = 0.0L, loss_total = 0.0L;
    const double store_before = s.coastal_storage_m3;
    if (has_rain || c.coastal.band_m > 0.0) {
        const std::vector<std::size_t> receiver = coastal_receivers(c);
        const double decay = std::exp(-dt / c.coastal.runoff_time_scale_s), tau = c.coastal.runoff_time_scale_s, cr = c.coastal.runoff_coefficient;
        for (std::size_t i = 0; i < count; ++i) {
            const double rain = has_rain ? f.rain_m_s[i] : 0.0;
            if (c.ocean[i]) {
                rain_sea += static_cast<long double>(rain) * area;
                const std::size_t x = i % nx, y = i / nx;
                const bool coast = (x + 1 < nx && !c.ocean[i + 1]) || (x > 0 && !c.ocean[i - 1]) || (y + 1 < ny && !c.ocean[i + nx]) || (y > 0 && !c.ocean[i - nx]);
                if (coast) rain_coastal_sea += static_cast<long double>(rain) * area;
                continue;
            }
            if (receiver[i] >= count) { rain_inland += static_cast<long double>(rain) * area; continue; }
            rain_coast += static_cast<long double>(rain) * area;
            const double w0 = s.coastal_store_m[i];
            const double w1 = w0 * decay + cr * rain * tau * (1.0 - decay);
            const double runoff = std::max(0.0, w0 + cr * rain * dt - w1);
            s.coastal_store_m[i] = w0 + cr * rain * dt - runoff;
            runoff_total += static_cast<long double>(runoff) * area / dt;
            loss_total += static_cast<long double>((1.0 - cr) * rain) * area;
            coastal_depth[receiver[i]] += runoff;   // equal cell areas: a depth on land is the same depth at sea
            coastal_heat[receiver[i]] += runoff * f.rain_temperature_k[i];
        }
        for (std::size_t i = 0; i < count; ++i) if (c.ocean[i]) s.coastal_inflow_m3_s[i] = coastal_depth[i] * area / dt;
    }

    // 3. Thickness and tracer update (flux form), then sources.
    std::vector<double> hs(count, 0.0), ht(count, 0.0);
    long double river_total = 0.0L, precip_total = 0.0L, sponge_volume = 0.0L, sponge_salt = 0.0L;
    long double evaporation_total = 0.0L, surface_heat = 0.0L, heat_source = 0.0L, sponge_heat = 0.0L;   // heat in K m3
    std::vector<double> river_depth(count, 0.0), river_salt(count, 0.0), river_heat(count, 0.0);
    for (const auto& r : f.rivers) {
        const double depth = r.discharge_m3_s * dt / area;
        river_depth[r.cell] += depth;
        river_salt[r.cell] += depth * r.salinity_psu;
        river_heat[r.cell] += depth * r.temperature_k;
        river_total += r.discharge_m3_s;
    }
    std::vector<double> vol(layers), lh(layers), ls(layers), dz(layers), column(layers), column_s(layers), remapped(layers), remapped_s(layers), initial(layers), absorbed;
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
        if (layers) {
            // Sublayer volumes and heat after the horizontal transport; sinking takes every sublayer in
            // proportion, upwelled deep water enters the bottom one.
            double sunk_heat = 0.0;
            for (std::size_t k = 0; k < layers; ++k) {
                const std::size_t ik = i * layers + k;
                const double divk = (ftxk[ik] - (x > 0 ? ftxk[ik - layers] : 0.0)) / dx + (ftyk[ik] - (y > 0 ? ftyk[ik - nx * layers] : 0.0)) / dy;
                const double sdivk = (fsxk[ik] - (x > 0 ? fsxk[ik - layers] : 0.0)) / dx + (fsyk[ik] - (y > 0 ? fsyk[ik - nx * layers] : 0.0)) / dy;
                vol[k] = sigma[k] * (h0 - dt * div) - sigma[k] * sunk;
                lh[k] = sigma[k] * (h0 * lt0[ik] - dt * divk) - sigma[k] * sunk * lt0[ik];
                ls[k] = sigma[k] * (h0 * ls0[ik] - dt * sdivk) - sigma[k] * sunk * ls0[ik];
                sunk_heat += sigma[k] * sunk * lt0[ik];
            }
            vol[layers - 1] += up;
            lh[layers - 1] += up * p.deep_temperature_k;
            ls[layers - 1] += up * p.deep_salinity_psu;
            heat_source += up * p.deep_temperature_k - sunk_heat;
        } else {
            heat_source += up * p.deep_temperature_k - sunk * t0;
        }
        const double surface_before = layers ? lt0[i * layers] : t0;
        // Rivers (fresh, at their temperature) and rain feed the surface freshwater content; it mixes down in time.
        fresh += river_depth[i] * (1.0 - (river_depth[i] > 0.0 ? river_salt[i] / river_depth[i] : 0.0) / p.reference_salinity_psu);
        h += river_depth[i];
        salt += river_salt[i];
        heat += river_heat[i];
        heat_source += river_heat[i];
        if (layers) { vol[0] += river_depth[i]; lh[0] += river_heat[i]; ls[0] += river_salt[i]; }
        // Coastal runoff and rain on the sea: fresh water at its own temperature, into the top of the column.
        const double rain = has_rain ? f.rain_m_s[i] * dt : 0.0;
        s.recent_rain_m[i] += rain;
        s.recent_runoff_m[i] += coastal_depth[i];
        s.recent_river_m[i] += river_depth[i];
        if (rain > 0.0 || coastal_depth[i] > 0.0) {
            const double added = rain + coastal_depth[i], added_heat = (has_rain ? rain * f.rain_temperature_k[i] : 0.0) + coastal_heat[i];
            fresh += added;
            h += added;
            heat += added_heat;
            heat_source += added_heat;
            if (layers) { vol[0] += added; lh[0] += added_heat; }
            if (has_rain) s.rain_heat_w_m2[i] = rho_cp * f.rain_m_s[i] * (f.rain_temperature_k[i] - surface_before);
        }
        // Glacial fresh water: meltwater arrives liquid at 0 degC; ice melts in the top layer with the water's heat.
        const double glacial_liquid = f.glacial_meltwater_m3_s.empty() ? 0.0 : f.glacial_meltwater_m3_s[i] * dt / area;
        const double glacial_ice = f.glacial_ice_m3_s.empty() ? 0.0 : f.glacial_ice_m3_s[i] * dt / area;
        if (glacial_liquid > 0.0 || glacial_ice > 0.0) {
            const double top_salinity = layers ? ls[0] / vol[0] : salt / h;
            const double melt_temperature = seawater_freezing_point_k(top_salinity);
            const double latent = glacial_ice * 1000.0 * ip.latent_heat_fusion_j_kg / rho_cp;   // ice of fresh-water mass
            const double added_heat = glacial_liquid * kZeroCelsius + glacial_ice * melt_temperature - latent;
            fresh += glacial_liquid + glacial_ice;
            h += glacial_liquid + glacial_ice;
            heat += added_heat;
            heat_source += added_heat;
            if (layers) { vol[0] += glacial_liquid + glacial_ice; lh[0] += added_heat; }
            glacial_liquid_total += static_cast<long double>(glacial_liquid) * area / dt;
            glacial_ice_total += static_cast<long double>(glacial_ice) * area / dt;
            s.recent_glacial_m[i] += glacial_liquid + glacial_ice;
        }
        // Prescribed net precipitation: rain dilutes, evaporation leaves the salt behind; both at the local temperature.
        double pe = f.net_precipitation_m_s[i] * dt;
        if (pe < 0.0) pe = -std::min(-pe, std::max(0.0, h - p.minimum_thickness_m));
        if (layers && pe < 0.0) pe = -std::min(-pe, 0.5 * vol[0]);
        h += pe;
        if (layers) {
            const double top_t = lh[0] / vol[0];
            vol[0] += pe;
            lh[0] += pe * top_t;
            heat_source += pe * top_t;
        } else {
            heat += pe * t0;
            heat_source += pe * t0;
        }
        if (pe > 0.0) fresh += pe;
        // Air-sea exchange: the surface energy balance of the top layer (the slab without layers), solved
        // implicitly for the end-of-step surface temperature; shortwave penetrates the column. Evaporation
        // at that temperature removes fresh water from the top.
        if (air_sea) {
            const double top_volume = layers ? vol[0] : h;
            double top_heat = layers ? lh[0] : heat;
            const double t_start = top_heat / top_volume;
            absorbed.assign(std::max<std::size_t>(layers, 1), 0.0);
            if (layers) {
                double depth = 0.0;
                for (std::size_t k = 0; k < layers; ++k) {
                    const double above = shortwave_transmission(depth, c.thermal);
                    depth += vol[k];
                    absorbed[k] = k + 1 == layers ? above : above - shortwave_transmission(depth, c.thermal);
                }
            } else {
                absorbed[0] = 1.0;
            }
            const double ta = f.air_temperature_k[i], qa = f.air_specific_humidity_kg_kg[i], pa = f.surface_pressure_pa[i];
            const double wind = f.wind_speed_m_s[i], cloud = f.cloud_fraction[i], sw = f.shortwave_down_w_m2[i];
            const double per_flux = dt / rho_cp;   // K m per W/m2
            const double qsw = (1.0 - c.air_sea.surface_albedo) * sw;
            auto non_solar = [&](double ts) { return air_sea_fluxes(ts, ta, qa, pa, wind, cloud, 0.0, c.air_sea).net_w_m2; };
            // Sea ice: the covered fraction a takes heat F_oi from the water and evolves with its own surface balance;
            // the open fraction exchanges with the air. Without ice open = 1 and the ice terms vanish exactly.
            const double a_ice = ice ? previous.ice_concentration[i] : 0.0, h_ice = ice ? previous.ice_thickness_m[i] : 0.0;
            const double open = 1.0 - a_ice;
            const double freezing = layers ? seawater_freezing_point_k(ls[0] / vol[0]) : 0.0;
            double f_oi = 0.0, ice_surplus = 0.0, h_ice_new = h_ice;
            if (ice && a_ice > 0.0) {
                f_oi = std::max(0.0, ocean_ice_heat_flux_w_m2(t_start, freezing, p.reference_density_kg_m3, p.heat_capacity_j_kg_k, ip));
                f_oi = std::min(f_oi, std::max(0.0, top_volume * rho_cp * (t_start - freezing)) / (a_ice * dt));
                AirState air;
                air.temperature_k = ta; air.specific_humidity_kg_kg = qa; air.pressure_pa = pa; air.wind_speed_m_s = wind; air.cloud_fraction = cloud; air.shortwave_down_w_m2 = sw;
                SeaIceSurfaceBalance balance;
                h_ice_new = step_ice_thickness(h_ice, freezing, f_oi, air, c.air_sea, ip, dt, ice_surplus, balance);
                s.ice_surface_temperature_k[i] = balance.surface_temperature_k;
            }
            const double ice_heat = per_flux * a_ice * f_oi - a_ice * ice_surplus / rho_cp;   // K m the water gives the ice
            double t1 = t_start;
            for (int it = 0; it < 20; ++it) {
                const double residual = top_volume * (t1 - t_start) - per_flux * (open * (absorbed[0] * qsw + non_solar(t1))) + ice_heat;
                const double slope = top_volume - per_flux * open * (non_solar(t1 + 1.0e-3) - non_solar(t1 - 1.0e-3)) / 2.0e-3;
                const double step = residual / slope;
                t1 -= step;
                if (std::abs(step) < 1.0e-11) break;
            }
            const AirSeaFluxes flux = air_sea_fluxes(t1, ta, qa, pa, wind, cloud, sw, c.air_sea);
            top_heat += per_flux * (open * (absorbed[0] * flux.shortwave_w_m2 + flux.longwave_w_m2 + flux.sensible_w_m2 + flux.latent_w_m2)) - ice_heat;
            if (layers) {
                lh[0] = top_heat;
                for (std::size_t k = 1; k < layers; ++k) lh[k] += per_flux * absorbed[k] * (open * flux.shortwave_w_m2);   // keeps the slab-free rounding when open = 1
            } else {
                heat = top_heat;
            }
            // The ocean's surface heat: the open-water exchange with the air and the heat given to the ice.
            const double surface_w_m2 = open * flux.net_w_m2 - ice_heat / per_flux;
            heat_source += per_flux * (open * flux.net_w_m2) - ice_heat;
            surface_heat += static_cast<long double>(surface_w_m2) * area * dt / rho_cp;
            s.shortwave_w_m2[i] = open * flux.shortwave_w_m2;
            s.longwave_w_m2[i] = open * flux.longwave_w_m2;
            s.sensible_w_m2[i] = open * flux.sensible_w_m2;
            s.latent_w_m2[i] = open * flux.latent_w_m2;
            s.net_heat_w_m2[i] = surface_w_m2;
            s.ocean_to_ice_heat_w_m2[i] = ice_heat / per_flux;
            if (!ice || a_ice <= 0.0) s.ice_surface_temperature_k[i] = t1;
            double evaporated = open * flux.evaporation_m_s * dt;
            if (evaporated > 0.0) evaporated = std::min(evaporated, std::max(0.0, h - p.minimum_thickness_m));
            if (layers && evaporated > 0.0) evaporated = std::min(evaporated, 0.5 * vol[0]);
            const double t_top = layers ? lh[0] / vol[0] : heat / h;
            h -= evaporated;
            if (layers) { vol[0] -= evaporated; lh[0] -= evaporated * t_top; } else { heat -= evaporated * t_top; }
            heat_source -= evaporated * t_top;
            fresh -= evaporated;
            s.evaporation_m_s[i] = evaporated / dt;
            evaporation_total += static_cast<long double>(evaporated) * area / dt;
            if (ice) {
                // Melt or basal growth of the covered part exchanges water at the freezing point (ice keeps S_i).
                auto exchange = [&](double water, double temperature) {   // water > 0 into the ocean
                    vol[0] += water; lh[0] += water * temperature; ls[0] += water * ip.ice_salinity_psu; h += water;
                    heat_source += water * temperature;
                    ice_water_total += static_cast<long double>(water) * area / dt;
                    ice_salt_total += static_cast<long double>(water) * ip.ice_salinity_psu * 1.0e-3 * p.reference_density_kg_m3 * area / dt;
                };
                const double covered_volume = a_ice * h_ice_new;   // per cell area
                exchange(-(covered_volume - a_ice * h_ice) * ice_to_water, freezing);
                // Open water cooled below its freezing point freezes: the latent heat warms it back to freezing.
                double frazil = 0.0;
                const double top_t = lh[0] / vol[0], top_freezing = seawater_freezing_point_k(ls[0] / vol[0]);
                if (top_t < top_freezing) {
                    const double deficit = vol[0] * (top_freezing - top_t);   // K m
                    frazil = deficit / latent_per_ice;
                    lh[0] += deficit;
                    heat_source += deficit;
                    exchange(-frazil * ice_to_water, top_freezing);
                }
                double a_new = a_ice, h_new = h_ice_new, total = covered_volume + frazil;
                update_ice_cover(a_new, h_new, total, frazil, ip);
                if (total > 0.0 && h_new < ip.minimum_thickness_m) {
                    // Too thin to last: it melts with the water's heat.
                    const double latent = total * latent_per_ice;
                    lh[0] -= latent;
                    heat_source -= latent;
                    exchange(total * ice_to_water, seawater_freezing_point_k(ls[0] / vol[0]));
                    total = 0.0;
                }
                if (total <= 0.0) { a_new = 0.0; h_new = 0.0; }
                s.ice_concentration[i] = a_new;
                s.ice_thickness_m[i] = h_new;
                s.ice_growth_m_s[i] = (a_new * h_new - a_ice * h_ice) / dt;
            }
        }
        const double lowest = air_sea ? -0.5 * p.mixed_layer_depth_m : 0.0;   // evaporation can leave a salty surface
        fresh = std::max(lowest, fresh) * (1.0 - std::min(1.0, dt / p.freshwater_mixing_time_s));
        s.freshwater_m[i] = std::min(fresh, p.mixed_layer_depth_m);
        precip_total += static_cast<long double>(pe) * area / dt;
        if (!(h > 0.0)) throw std::runtime_error("ocean layer thickness became non-positive; reduce the timestep or the sinks");
        if (!air_sea) {
            if (!layers) {
                // Surface temperature relaxation.
                const double before = heat;
                heat += h * (f.target_temperature_k[i] - heat / h) * std::min(1.0, dt / p.temperature_relaxation_s);
                heat_source += heat - before;
                surface_heat += static_cast<long double>(heat - before) * area;
                s.net_heat_w_m2[i] = rho_cp * (heat - before) / dt;
            } else {
                // The same restoring as a surface heat flux rho c_p h_mix (T* - SST) / tau_T into the top layer, implicit.
                const double top_t = lh[0] / vol[0];
                const double a = p.mixed_layer_depth_m * dt / (p.temperature_relaxation_s * vol[0]);
                const double applied = vol[0] * ((top_t + a * f.target_temperature_k[i]) / (1.0 + a) - top_t);
                lh[0] += applied;
                heat_source += applied;
                surface_heat += static_cast<long double>(applied) * area;
                s.net_heat_w_m2[i] = rho_cp * applied / dt;
            }
        }
        if (layers) {
            // Remap the column to the sigma fractions of the new thickness, then the vertical physics.
            double total = 0.0;
            for (std::size_t k = 0; k < layers; ++k) { total += vol[k]; column[k] = lh[k] / vol[k]; column_s[k] = std::max(0.0, ls[k] / vol[k]); }
            h = total;
            for (std::size_t k = 0; k < layers; ++k) dz[k] = sigma[k] * total;
            remap_column(vol, column, dz, remapped);
            remap_column(vol, column_s, dz, remapped_s);
            convective_adjustment(remapped, remapped_s, dz, p);
            const double open_water = ice ? 1.0 - s.ice_concentration[i] : 1.0;   // the ice shelters the water from the wind
            const double stress = open_water * (f.mixing_stress_pa.empty() ? std::hypot(f.wind_stress_east_pa[i], f.wind_stress_north_pa[i]) : f.mixing_stress_pa[i]);
            const double friction_velocity = std::sqrt(stress / p.reference_density_kg_m3);
            wind_mixing(remapped, remapped_s, dz, c.thermal.wind_mixing_efficiency * p.reference_density_kg_m3 * std::pow(friction_velocity, 3) * dt, p);
            shear_mixing(remapped, remapped_s, dz, friction_velocity, coriolis[i], c.thermal.bulk_richardson_number, p);
            vertical_diffusion(remapped, dz, c.thermal.background_diffusivity_m2_s, dt);
            vertical_diffusion(remapped_s, dz, c.thermal.background_diffusivity_m2_s, dt);
            long double column_heat = 0.0L, column_salt = 0.0L;
            for (std::size_t k = 0; k < layers; ++k) {
                s.layer_temperature_k[i * layers + k] = remapped[k];
                s.layer_salinity_psu[i * layers + k] = remapped_s[k];
                column_heat += static_cast<long double>(remapped[k]) * dz[k];
                column_salt += static_cast<long double>(remapped_s[k]) * dz[k];
            }
            heat = static_cast<double>(column_heat);
            salt = static_cast<double>(column_salt);
        }
        // Sponge: relax thickness and tracers toward the initial state; the change is a budget source.
        if (!c.sponge.empty() && c.sponge[i] > 0.0) {
            const double rate = c.sponge[i] * dt / p.sponge_time_scale_s;
            const double h2 = h + (c.initial_thickness_m - h) * rate;
            const double s2 = salt / h + (c.initial_salinity_psu - salt / h) * rate;
            double t2 = heat / h + (c.initial_temperature_k - heat / h) * rate;
            double s2_layers = s2;
            if (layers) {
                initial_column(c, sigma, i, initial.data());
                long double mean = 0.0L, mean_s = 0.0L;
                for (std::size_t k = 0; k < layers; ++k) {
                    double& tk = s.layer_temperature_k[i * layers + k];
                    double& sk = s.layer_salinity_psu[i * layers + k];
                    tk += (initial[k] - tk) * rate;
                    sk += (c.initial_salinity_psu - sk) * rate;
                    mean += static_cast<long double>(sigma[k]) * tk;
                    mean_s += static_cast<long double>(sigma[k]) * sk;
                }
                t2 = static_cast<double>(mean);
                s2_layers = static_cast<double>(mean_s);
            }
            sponge_volume += static_cast<long double>(h2 - h) * area;
            sponge_salt += (static_cast<long double>(h2) * s2_layers - salt) * 1.0e-3 * p.reference_density_kg_m3 * area;
            sponge_heat += (static_cast<long double>(h2) * t2 - heat) * area;
            h = h2;
            salt = h2 * s2_layers;
            heat = h2 * t2;
            s.freshwater_m[i] *= 1.0 - rate;
        }
        // The layered column carries its own surface salinity; report the equivalent slab freshwater content.
        if (layers) s.freshwater_m[i] = p.mixed_layer_depth_m * (1.0 - s.layer_salinity_psu[i * layers] / std::max(1.0e-9, salt / h));
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
    s.rain_sea_m3_s = static_cast<double>(rain_sea);
    s.rain_coastal_sea_m3_s = static_cast<double>(rain_coastal_sea);
    s.rain_coast_m3_s = static_cast<double>(rain_coast);
    s.rain_inland_m3_s = static_cast<double>(rain_inland);
    s.coastal_runoff_m3_s = static_cast<double>(runoff_total);
    s.coastal_loss_m3_s = static_cast<double>(loss_total);
    s.evaporation_m3_s = static_cast<double>(evaporation_total);
    s.surface_heat_w = static_cast<double>(surface_heat * rho_cp / dt);
    s.ice_water_m3_s = static_cast<double>(ice_water_total);
    s.ice_salt_kg_s = static_cast<double>(ice_salt_total);
    s.glacial_meltwater_m3_s = static_cast<double>(glacial_liquid_total);
    s.glacial_ice_m3_s = static_cast<double>(glacial_ice_total);

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

    // 4b. Sea-ice drift (after the thermodynamics, with the new current).
    if (ice && c.sea_ice.drift) drift_sea_ice(s, f, c, g, coriolis);

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
    salt_source += static_cast<long double>(s.ice_salt_kg_s) * dt;
    double expected_volume_change = (static_cast<double>(river_total) + static_cast<double>(precip_total)) * dt + static_cast<double>(sponge_volume);  // sinking and upwelling cancel
    const double fresh_exchange = s.rain_sea_m3_s + s.coastal_runoff_m3_s - s.evaporation_m3_s + s.ice_water_m3_s + s.glacial_meltwater_m3_s + s.glacial_ice_m3_s;
    if (fresh_exchange != 0.0) expected_volume_change += fresh_exchange * dt;
    s.volume_balance_error_m3 = (s.volume_m3 - previous.volume_m3) - expected_volume_change;
    s.salt_balance_error_kg = (s.salt_kg - previous.salt_kg) - static_cast<double>(salt_source);
    // Heat: every source enters with its temperature; horizontal transport, remapping and mixing conserve.
    long double heat_before = 0.0L, heat_after = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.ocean[i]) continue;
        heat_before += static_cast<long double>(previous.thickness_m[i]) * previous.temperature_k[i] * area;
        heat_after += static_cast<long double>(s.thickness_m[i]) * s.temperature_k[i] * area;
    }
    s.heat_balance_error_j = static_cast<double>(((heat_after - heat_before) - (heat_source * area + sponge_heat)) * rho_cp);
    s.coastal_balance_error_m3 = (s.coastal_storage_m3 - store_before) - (s.rain_coast_m3_s - s.coastal_runoff_m3_s - s.coastal_loss_m3_s) * dt;
    s.cumulative_river_m3 += static_cast<double>(river_total) * dt;
    s.cumulative_net_precipitation_m3 += static_cast<double>(precip_total) * dt;
    s.cumulative_sinking_m3 += static_cast<double>(sink_total) * dt;
    s.cumulative_sponge_m3 += static_cast<double>(sponge_volume);
    s.cumulative_rain_sea_m3 += s.rain_sea_m3_s * dt;
    s.cumulative_rain_coast_m3 += s.rain_coast_m3_s * dt;
    s.cumulative_rain_inland_m3 += s.rain_inland_m3_s * dt;
    s.cumulative_coastal_runoff_m3 += s.coastal_runoff_m3_s * dt;
    s.cumulative_coastal_loss_m3 += s.coastal_loss_m3_s * dt;
    s.cumulative_evaporation_m3 += s.evaporation_m3_s * dt;
    s.cumulative_surface_heat_j += s.surface_heat_w * dt;
    s.cumulative_ice_water_m3 += s.ice_water_m3_s * dt;
    s.cumulative_glacial_m3 += (s.glacial_meltwater_m3_s + s.glacial_ice_m3_s) * dt;
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
            cxs += fresh * (kx - mx); cys += fresh * (ky - my); weight += fresh;   // offsets from the mouth: exactly 0 for the mouth itself
            const std::size_t x = k % nx, y = k / nx;
            const std::size_t neighbours[4] = {x + 1 < nx ? k + 1 : count, x > 0 ? k - 1 : count, y + 1 < ny ? k + nx : count, y > 0 ? k - nx : count};
            for (std::size_t n : neighbours) {
                if (n >= count || !c.ocean[n] || seen[n] || s.surface_salinity_psu[n] >= plume_limit) continue;
                seen[n] = 1;
                stack.push_back(n);
            }
        }
        if (weight > 0.0) {
            const double ox = cxs / weight, oy = cys / weight;   // centroid offset in cells
            // A plume centred on its mouth (within a millionth of a cell: rounding) has no direction; without the
            // tolerance the sign of the rounding residue would flip the bearing between 0 and 180 degrees across
            // compilers and platforms.
            if (std::hypot(ox, oy) > 1.0e-6) {
                double bearing = std::atan2(ox * dx, oy * dy) * 180.0 / kPi;
                if (bearing < 0.0) bearing += 360.0;
                pl.deflection_deg = bearing;
            }
        }
        d.plumes.push_back(std::move(pl));
    }

    // Surface temperature and mixed-layer statistics; barrier layers where the isothermal layer reaches deeper.
    if (s.sst_k.size() == count && s.mixed_layer_depth_m.size() == count && s.isothermal_layer_depth_m.size() == count) {
        d.min_sst_k = std::numeric_limits<double>::infinity();
        d.max_sst_k = -std::numeric_limits<double>::infinity();
        long double sst_sum = 0.0L, mld_sum = 0.0L;
        for (std::size_t i = 0; i < count; ++i) {
            if (!c.ocean[i]) continue;
            sst_sum += s.sst_k[i];
            mld_sum += s.mixed_layer_depth_m[i];
            d.min_sst_k = std::min(d.min_sst_k, s.sst_k[i]);
            d.max_sst_k = std::max(d.max_sst_k, s.sst_k[i]);
            d.max_mixed_layer_depth_m = std::max(d.max_mixed_layer_depth_m, s.mixed_layer_depth_m[i]);
            if (s.isothermal_layer_depth_m[i] > s.mixed_layer_depth_m[i] + 1.0) ++d.barrier_layer_cells;
        }
        d.mean_sst_k = water ? static_cast<double>(sst_sum / water) : 0.0;
        d.mean_mixed_layer_depth_m = water ? static_cast<double>(mld_sum / water) : 0.0;
    }

    // Surface lenses: connected water fresher at the surface than in the layer by the lens deficit.
    std::vector<unsigned char> lens_seen(count, 0);
    auto record = [&](const std::vector<double>& v, std::size_t k) { return v.size() == count ? v[k] : 0.0; };
    for (std::size_t start = 0; start < count; ++start) {
        if (!c.ocean[start] || lens_seen[start] || s.salinity_psu[start] - s.surface_salinity_psu[start] < p.lens_salinity_deficit_psu) continue;
        SurfaceLens lens;
        lens.freshest_cell = start;
        lens.minimum_surface_salinity_psu = s.surface_salinity_psu[start];
        long double sst_sum = 0.0L, rain_sum = 0.0L, runoff_sum = 0.0L, river_sum = 0.0L, glacial_sum = 0.0L;
        std::vector<std::size_t> stack{start};
        lens_seen[start] = 1;
        while (!stack.empty()) {
            const std::size_t k = stack.back();
            stack.pop_back();
            ++lens.cell_count;
            lens.area_m2 += area;
            lens.freshwater_m3 += s.freshwater_m[k] * area;
            sst_sum += s.sst_k.size() == count ? s.sst_k[k] : s.temperature_k[k];
            rain_sum += record(s.recent_rain_m, k);
            runoff_sum += record(s.recent_runoff_m, k);
            river_sum += record(s.recent_river_m, k);
            glacial_sum += record(s.recent_glacial_m, k);
            if (s.surface_salinity_psu[k] < lens.minimum_surface_salinity_psu) { lens.minimum_surface_salinity_psu = s.surface_salinity_psu[k]; lens.freshest_cell = k; }
            const std::size_t x = k % nx, y = k / nx;
            const std::size_t neighbours[4] = {x + 1 < nx ? k + 1 : count, x > 0 ? k - 1 : count, y + 1 < ny ? k + nx : count, y > 0 ? k - nx : count};
            for (std::size_t n : neighbours) {
                if (n >= count || !c.ocean[n] || lens_seen[n] || s.salinity_psu[n] - s.surface_salinity_psu[n] < p.lens_salinity_deficit_psu) continue;
                lens_seen[n] = 1;
                stack.push_back(n);
            }
        }
        const long double total = rain_sum + runoff_sum + river_sum + glacial_sum;
        if (total > 0.0L) {
            lens.rain_share = static_cast<double>(rain_sum / total);
            lens.coastal_runoff_share = static_cast<double>(runoff_sum / total);
            lens.river_share = static_cast<double>(river_sum / total);
            lens.glacial_share = static_cast<double>(glacial_sum / total);
            const long double largest = std::max({rain_sum, runoff_sum, river_sum, glacial_sum});
            lens.kind = largest == glacial_sum ? "glacial" : largest == river_sum ? "river" : largest == runoff_sum ? "coastal_runoff" : "rain";
        } else {
            lens.kind = "other";
        }
        lens.maximum_deficit_psu = s.salinity_psu[lens.freshest_cell] - s.surface_salinity_psu[lens.freshest_cell];
        lens.mean_sst_k = static_cast<double>(sst_sum / lens.cell_count);
        d.lenses.push_back(std::move(lens));
    }
    std::stable_sort(d.lenses.begin(), d.lenses.end(), [](const SurfaceLens& a, const SurfaceLens& b) { return a.area_m2 > b.area_m2; });
    return d;
}

}  // namespace terraflux::weather
