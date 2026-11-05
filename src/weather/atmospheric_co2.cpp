#include "terraflux/weather/atmospheric_co2.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kAirMolarMass = 0.028965;   // kg/mol

double excess(const std::vector<double>& ppm, double background, double air_mol, double area) {
    long double sum = 0.0L;
    for (double x : ppm) sum += static_cast<long double>(x - background);
    return static_cast<double>(sum * 1.0e-6L * air_mol * area);
}

}  // namespace

void validate_atmospheric_co2(const AtmosphericCo2Config& c) {
    validate_transport_grid(c.grid);
    if (!std::isfinite(c.boundary_layer_height_m) || c.boundary_layer_height_m < 50.0 || c.boundary_layer_height_m > 5000.0) throw std::invalid_argument("CO2 boundary layer height must lie within 50-5000 m");
    if (!std::isfinite(c.air_density_kg_m3) || c.air_density_kg_m3 < 0.5 || c.air_density_kg_m3 > 2.0) throw std::invalid_argument("CO2 air density must lie within 0.5-2 kg/m3");
    if (!std::isfinite(c.ventilation_time_s) || c.ventilation_time_s < 600.0 || c.ventilation_time_s > 1.0e8) throw std::invalid_argument("CO2 ventilation time must lie within 10 minutes and 3 years");
    if (!std::isfinite(c.horizontal_diffusivity_m2_s) || c.horizontal_diffusivity_m2_s < 0.0 || c.horizontal_diffusivity_m2_s > 1.0e7) throw std::invalid_argument("CO2 diffusivity out of range");
}

double boundary_layer_air_mol_m2(const AtmosphericCo2Config& c) { return c.air_density_kg_m3 * c.boundary_layer_height_m / kAirMolarMass; }

AtmosphericCo2State initialize_atmospheric_co2(const AtmosphericCo2Config& c, double background) {
    validate_atmospheric_co2(c);
    if (!std::isfinite(background) || background < 100.0 || background > 5000.0) throw std::invalid_argument("CO2 background must lie within 100-5000 ppm");
    AtmosphericCo2State s;
    s.ppm.assign(c.grid.nx * c.grid.ny, background);
    return s;
}

AtmosphericCo2State advance_atmospheric_co2(const AtmosphericCo2State& previous, const AtmosphericCo2Forcing& f, const AtmosphericCo2Config& c, double dt) {
    validate_atmospheric_co2(c);
    const auto& g = c.grid;
    const std::size_t nx = g.nx, ny = g.ny, count = nx * ny;
    if (previous.ppm.size() != count) throw std::invalid_argument("CO2 state does not match the grid");
    for (const auto* v : {&f.wind_east_m_s, &f.wind_north_m_s, &f.emission_mol_m2_s, &f.land_uptake_mol_m2_s, &f.ocean_uptake_mol_m2_s}) if (v->size() != count) throw std::invalid_argument("CO2 forcing fields must match the grid");
    if (!std::isfinite(f.background_ppm) || f.background_ppm < 100.0 || f.background_ppm > 5000.0) throw std::invalid_argument("CO2 background must lie within 100-5000 ppm");
    if (!(dt > 0.0) || dt > 86400.0) throw std::invalid_argument("CO2 timestep must lie within 0-1 day");
    for (double e : f.emission_mol_m2_s) if (!std::isfinite(e) || e < 0.0) throw std::invalid_argument("CO2 emissions must be finite and non-negative");
    const double dx = g.spacing_east_m, dy = g.spacing_north_m, area = dx * dy, air = boundary_layer_air_mol_m2(c);
    AtmosphericCo2State s = previous;
    const double before = excess(s.ppm, f.background_ppm, air, area);
    s.background_shift_mol += before - previous.inventory_excess_mol;
    // Transport: upwind advection (inflow boundaries carry the background) and diffusion, in sub-steps.
    double fastest = 0.0;
    for (std::size_t i = 0; i < count; ++i) fastest = std::max(fastest, std::abs(f.wind_east_m_s[i]) / dx + std::abs(f.wind_north_m_s[i]) / dy);
    const double rate = fastest + c.horizontal_diffusivity_m2_s * (2.0 / (dx * dx) + 2.0 / (dy * dy));
    const int substeps = std::max(1, static_cast<int>(std::ceil(dt * rate / 0.5)));
    const double h = dt / substeps;
    std::vector<double> next(count);
    for (int sub = 0; sub < substeps; ++sub) {
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t x = i % nx, y = i / nx;
            const double xi = s.ppm[i], u = f.wind_east_m_s[i], v = f.wind_north_m_s[i];
            const double west = x > 0 ? s.ppm[i - 1] : f.background_ppm, east = x + 1 < nx ? s.ppm[i + 1] : f.background_ppm;
            const double south = y > 0 ? s.ppm[i - nx] : f.background_ppm, north = y + 1 < ny ? s.ppm[i + nx] : f.background_ppm;
            double tendency = u > 0.0 ? u * (west - xi) / dx : u * (xi - east) / dx;
            tendency += v > 0.0 ? v * (south - xi) / dy : v * (xi - north) / dy;
            // Diffusion with zero-gradient walls (no diffusive exchange through the boundary).
            if (x > 0) tendency += c.horizontal_diffusivity_m2_s * (s.ppm[i - 1] - xi) / (dx * dx);
            if (x + 1 < nx) tendency += c.horizontal_diffusivity_m2_s * (s.ppm[i + 1] - xi) / (dx * dx);
            if (y > 0) tendency += c.horizontal_diffusivity_m2_s * (s.ppm[i - nx] - xi) / (dy * dy);
            if (y + 1 < ny) tendency += c.horizontal_diffusivity_m2_s * (s.ppm[i + nx] - xi) / (dy * dy);
            next[i] = xi + h * tendency;
        }
        s.ppm.swap(next);
    }
    const double after_transport = excess(s.ppm, f.background_ppm, air, area);
    s.transport_mol += after_transport - before;
    // Sources and sinks, then ventilation to the background (exact relaxation).
    const double relax = 1.0 - std::exp(-dt / c.ventilation_time_s);
    for (std::size_t i = 0; i < count; ++i) {
        const double net = f.emission_mol_m2_s[i] - f.land_uptake_mol_m2_s[i] - f.ocean_uptake_mol_m2_s[i];
        s.ppm[i] += 1.0e6 * net * dt / air;
        s.emitted_mol += f.emission_mol_m2_s[i] * dt * area;
        s.land_uptake_mol += f.land_uptake_mol_m2_s[i] * dt * area;
        s.ocean_uptake_mol += f.ocean_uptake_mol_m2_s[i] * dt * area;
        const double vented = (s.ppm[i] - f.background_ppm) * relax;
        s.ppm[i] -= vented;
        s.ventilation_mol += vented * 1.0e-6 * air * area;
        if (!std::isfinite(s.ppm[i]) || s.ppm[i] < 1.0) throw std::runtime_error("atmospheric CO2 became non-physical; reduce the timestep or the uptake");
    }
    s.inventory_excess_mol = excess(s.ppm, f.background_ppm, air, area);
    s.elapsed_seconds = previous.elapsed_seconds + dt;
    return s;
}

}  // namespace terraflux::weather
