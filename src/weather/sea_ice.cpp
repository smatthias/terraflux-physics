#include "terraflux/weather/sea_ice.hpp"

#include "terraflux/weather/moist_air.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terraflux::weather {

namespace {

constexpr double kZeroCelsius = 273.15;

void require_range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(std::string("sea ice ") + name + " is out of range");
}

// Albedo of the ice: cold dry ice, lowered to melting ice over the last kelvin below 0 degC of the air.
double ice_albedo(double air_k, const SeaIcePhysics& p) {
    const double w = std::min(1.0, std::max(0.0, air_k - (kZeroCelsius - 1.0)));
    return p.albedo_cold + (p.albedo_melting - p.albedo_cold) * w;
}

// Atmosphere-to-ice fluxes at surface temperature ts (sublimation instead of evaporation).
SeaIceSurfaceBalance fluxes_over_ice(double ts, const AirState& air, const OceanAirSeaConfig& bulk, const SeaIcePhysics& p) {
    OceanAirSeaConfig over_ice = bulk;
    over_ice.surface_albedo = ice_albedo(air.temperature_k, p);
    over_ice.saline_vapour_factor = 1.0;
    const AirSeaFluxes f = air_sea_fluxes(ts, air.temperature_k, air.specific_humidity_kg_kg, air.pressure_pa, air.wind_speed_m_s, air.cloud_fraction, air.shortwave_down_w_m2, over_ice);
    SeaIceSurfaceBalance b;
    b.surface_temperature_k = ts;
    b.albedo = over_ice.surface_albedo;
    b.shortwave_w_m2 = f.shortwave_w_m2;
    b.longwave_w_m2 = f.longwave_w_m2;
    b.sensible_w_m2 = f.sensible_w_m2;
    const double evaporation_kg = f.evaporation_m_s * bulk.fresh_water_density_kg_m3;
    const double latent_sublimation = 2.501e6 - 2370.0 * (ts - kZeroCelsius) + p.latent_heat_fusion_j_kg;
    b.latent_w_m2 = -latent_sublimation * evaporation_kg;
    b.sublimation_m_s = evaporation_kg / p.ice_density_kg_m3;
    b.atmosphere_w_m2 = b.shortwave_w_m2 + b.longwave_w_m2 + b.sensible_w_m2 + b.latent_w_m2;
    return b;
}

}  // namespace

double seawater_freezing_point_k(double salinity_psu, double depth_m) {
    return kZeroCelsius - 0.054 * std::max(0.0, salinity_psu) - 7.53e-4 * std::max(0.0, depth_m);
}

void validate_sea_ice_physics(const SeaIcePhysics& p) {
    require_range(p.ice_density_kg_m3, 800.0, 1000.0, "ice_density_kg_m3");
    require_range(p.latent_heat_fusion_j_kg, 1.0e5, 5.0e5, "latent_heat_fusion_j_kg");
    require_range(p.conductivity_w_m_k, 0.5, 5.0, "conductivity_w_m_k");
    require_range(p.ice_salinity_psu, 0.0, 20.0, "ice_salinity_psu");
    require_range(p.albedo_cold, 0.0, 1.0, "albedo_cold");
    require_range(p.albedo_melting, 0.0, 1.0, "albedo_melting");
    require_range(p.minimum_thickness_m, 0.001, 1.0, "minimum_thickness_m");
    require_range(p.new_ice_thickness_m, p.minimum_thickness_m, 5.0, "new_ice_thickness_m");
    require_range(p.heat_transfer_coefficient, 0.0, 0.1, "heat_transfer_coefficient");
    require_range(p.friction_velocity_m_s, 0.0, 0.2, "friction_velocity_m_s");
    require_range(p.maximum_concentration, 0.5, 1.0, "maximum_concentration");
}

SeaIceSurfaceBalance sea_ice_surface_balance(double h, double tf, const AirState& air, const OceanAirSeaConfig& bulk, const SeaIcePhysics& p) {
    const double thickness = std::max(h, 1.0e-4);
    // Q_atm(T_s) + k (T_f - T_s) / h = 0; both terms fall with T_s, so the root is unique (Newton, bracketed).
    auto residual = [&](double ts) { return fluxes_over_ice(ts, air, bulk, p).atmosphere_w_m2 + p.conductivity_w_m_k * (tf - ts) / thickness; };
    double lo = 180.0, hi = kZeroCelsius;
    SeaIceSurfaceBalance b;
    if (residual(hi) >= 0.0) {
        // The surface would be above freezing: it sits at 0 degC and the surplus melts the top.
        b = fluxes_over_ice(hi, air, bulk, p);
        b.conductive_w_m2 = p.conductivity_w_m_k * (tf - hi) / thickness;
        b.surface_melt_w_m2 = b.atmosphere_w_m2 + b.conductive_w_m2;
        return b;
    }
    double ts = std::min(hi - 1.0e-6, std::max(lo, air.temperature_k));
    for (int it = 0; it < 80; ++it) {
        const double r = residual(ts);
        if (r > 0.0) lo = ts; else hi = ts;
        const double slope = (residual(ts + 1.0e-3) - residual(ts - 1.0e-3)) / 2.0e-3;
        double next = slope < 0.0 ? ts - r / slope : 0.5 * (lo + hi);
        if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
        if (std::abs(next - ts) < 1.0e-9) { ts = next; break; }
        ts = next;
    }
    b = fluxes_over_ice(ts, air, bulk, p);
    b.conductive_w_m2 = p.conductivity_w_m_k * (tf - ts) / thickness;
    b.surface_melt_w_m2 = 0.0;
    return b;
}

double ocean_ice_heat_flux_w_m2(double t_ocean, double tf, double rho, double cp, const SeaIcePhysics& p) {
    return rho * cp * p.heat_transfer_coefficient * p.friction_velocity_m_s * (t_ocean - tf);
}

void update_ice_cover(double& a, double& h, double volume, double frozen_open, const SeaIcePhysics& p) {
    const double old_volume = a * h;
    if (!(volume > 0.0)) { a = 0.0; h = 0.0; return; }
    if (frozen_open > 0.0) {
        const double added = std::min(p.maximum_concentration - std::min(a, p.maximum_concentration), frozen_open / p.new_ice_thickness_m);
        a = std::min(p.maximum_concentration, a + std::max(0.0, added));
        h = volume / a;
        return;
    }
    if (a <= 0.0) { a = std::min(p.maximum_concentration, volume / p.new_ice_thickness_m); h = volume / a; return; }
    if (volume < old_volume) {
        const double ratio = std::sqrt(volume / old_volume);
        a *= ratio;
        h *= ratio;
    } else {
        h = volume / a;
    }
}

// Implicit thickness change of the ice-covered part over dt: rho_i L_f (h1 - h0) = -dt (Q_atm(T_s(h1)) + F_oi).
// Returns the new thickness; `surplus` is the energy (J per m2 of ice) left over when all the ice melts.
double step_ice_thickness(double h0, double tf, double f_oi, const AirState& air, const OceanAirSeaConfig& bulk, const SeaIcePhysics& p, double dt,
                          double& surplus, SeaIceSurfaceBalance& balance) {
    const double rl = p.ice_density_kg_m3 * p.latent_heat_fusion_j_kg;
    auto g = [&](double h1, SeaIceSurfaceBalance& b) {
        b = sea_ice_surface_balance(h1, tf, air, bulk, p);
        const double q = b.surface_melt_w_m2 > 0.0 ? b.atmosphere_w_m2 : -b.conductive_w_m2;   // net heat into the ice from above
        return rl * (h1 - h0) + dt * (q + f_oi);
    };
    surplus = 0.0;
    SeaIceSurfaceBalance b_lo, b_hi;
    double lo = 1.0e-6, hi = h0 + 20.0;
    const double g_lo = g(lo, b_lo);
    if (g_lo >= 0.0) {
        // Even vanishing ice gains no heat deficit: it all melts, the rest of the energy goes to the water.
        surplus = g(0.0, balance) ;
        surplus = std::max(0.0, surplus);
        return 0.0;
    }
    if (g(hi, b_hi) <= 0.0) { balance = b_hi; return hi; }
    // g rises monotonically with h1: safeguarded Newton inside the bracket [lo, hi] (bisection when a step leaves it).
    double h = std::min(hi, std::max(lo, h0));
    for (int it = 0; it < 60; ++it) {
        SeaIceSurfaceBalance b;
        const double gh = g(h, b);
        balance = b;
        if (std::abs(gh) < 1.0e-3) return h;   // J/m2: far below the energy of a micrometre of ice
        if (gh < 0.0) lo = h; else hi = h;
        const double delta = 1.0e-5 * std::max(h, 0.01);
        SeaIceSurfaceBalance unused;
        const double slope = (g(h + delta, unused) - gh) / delta;
        double next = slope > 0.0 ? h - gh / slope : 0.5 * (lo + hi);
        if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
        if (hi - lo < 1.0e-10) { h = next; break; }
        h = next;
    }
    g(h, balance);
    return h;
}

void validate_arctic_column(const ArcticColumnConfig& c) {
    require_range(c.mixed_layer_depth_m, 1.0, 1000.0, "mixed_layer_depth_m");
    require_range(c.reference_salinity_psu, 0.0, 40.0, "reference_salinity_psu");
    require_range(c.salinity_relaxation_s, 86400.0, 1.0e11, "salinity_relaxation_s");
    require_range(c.deep_ocean_heat_flux_w_m2, -50.0, 200.0, "deep_ocean_heat_flux_w_m2");
    require_range(c.reference_density_kg_m3, 900.0, 1100.0, "reference_density_kg_m3");
    require_range(c.heat_capacity_j_kg_k, 3000.0, 4300.0, "heat_capacity_j_kg_k");
    require_range(c.arctic_ocean_area_m2, 1.0e9, 1.0e14, "arctic_ocean_area_m2");
    require_range(c.ice_export_per_s, 0.0, 1.0e-6, "ice_export_per_s");
    validate_sea_ice_physics(c.ice);
}

ArcticColumnState advance_arctic_column(const ArcticColumnState& previous, const AirState& air, const ArcticColumnConfig& c, double dt) {
    validate_arctic_column(c);
    if (!(dt > 0.0) || dt > 90.0 * 86400.0) throw std::invalid_argument("arctic column timestep must lie within (0, 90 days]");
    const auto& p = c.ice;
    const double rcp = c.reference_density_kg_m3 * c.heat_capacity_j_kg_k, depth = c.mixed_layer_depth_m;
    const double rl = p.ice_density_kg_m3 * p.latent_heat_fusion_j_kg;
    ArcticColumnState s = previous;
    s.elapsed_seconds += dt;
    const double energy_before = rcp * depth * (previous.mixed_layer_temperature_k - kZeroCelsius) - rl * previous.ice_concentration * previous.ice_thickness_m;
    double a = previous.ice_concentration, h = previous.ice_thickness_m, t = previous.mixed_layer_temperature_k;
    const double volume_before = a * h;
    const double tf = seawater_freezing_point_k(previous.mixed_layer_salinity_psu);
    // Ocean-to-ice heat, limited to the water's heat above freezing.
    double f_oi = a > 0.0 ? std::max(0.0, ocean_ice_heat_flux_w_m2(t, tf, c.reference_density_kg_m3, c.heat_capacity_j_kg_k, p)) : 0.0;
    if (a > 0.0) f_oi = std::min(f_oi, std::max(0.0, rcp * depth * (t - tf)) / (a * dt));
    double surplus = 0.0, atmosphere_over_ice = 0.0;
    if (a > 0.0) {
        SeaIceSurfaceBalance b;
        h = step_ice_thickness(h, tf, f_oi, air, c.bulk, p, dt, surplus, b);
        s.surface_temperature_k = b.surface_temperature_k;
        atmosphere_over_ice = b.surface_melt_w_m2 > 0.0 ? b.atmosphere_w_m2 : -b.conductive_w_m2;
    } else {
        s.surface_temperature_k = t;
    }
    // Open water: explicit bulk fluxes on the slab (its heat capacity makes them stable).
    const AirSeaFluxes ow = air_sea_fluxes(t, air.temperature_k, air.specific_humidity_kg_kg, air.pressure_pa, air.wind_speed_m_s, air.cloud_fraction, air.shortwave_down_w_m2, c.bulk);
    s.open_water_flux_w_m2 = ow.net_w_m2;
    s.ocean_ice_flux_w_m2 = f_oi;
    const double ocean_gain = dt * ((1.0 - a) * ow.net_w_m2 - a * f_oi + c.deep_ocean_heat_flux_w_m2) + a * surplus;
    t += ocean_gain / (rcp * depth);
    // Cooling below the freezing point makes new ice in the open water.
    double frozen_open = 0.0;
    if (t < tf) { frozen_open = rcp * depth * (tf - t) / rl; t = tf; }
    double volume = a * h + frozen_open;
    update_ice_cover(a, h, volume, frozen_open, p);
    // Export: a share of the ice leaves the basin (it takes its latent deficit with it).
    const double exported = volume * (1.0 - std::exp(-c.ice_export_per_s * dt));
    if (exported > 0.0) { volume -= exported; update_ice_cover(a, h, volume, 0.0, p); }
    s.export_m_s = exported / dt;
    // Ice thinner than the minimum melts at once with the water's heat.
    if (volume > 0.0 && h < p.minimum_thickness_m) { t -= rl * volume / (rcp * depth); volume = 0.0; a = 0.0; h = 0.0; }
    if (volume <= 0.0) { a = 0.0; h = 0.0; }
    s.ice_concentration = a;
    s.ice_thickness_m = h;
    s.mixed_layer_temperature_k = t;
    // Salt: freezing rejects brine, melt returns nearly fresh water; the surface water relaxes to its reference.
    const double dv = a * h + exported - volume_before;   // thermodynamic ice volume change per area
    s.growth_m_s = dv / dt;
    s.freshwater_m_s = -dv * p.ice_density_kg_m3 / c.reference_density_kg_m3 * (1.0 - p.ice_salinity_psu / std::max(1.0, previous.mixed_layer_salinity_psu)) / dt;
    double salinity = previous.mixed_layer_salinity_psu - previous.mixed_layer_salinity_psu * s.freshwater_m_s * dt / depth;
    salinity += (c.reference_salinity_psu - salinity) * std::min(1.0, dt / c.salinity_relaxation_s);
    s.mixed_layer_salinity_psu = std::max(0.0, salinity);
    s.heat_content_j_m2 = rcp * depth * (t - kZeroCelsius) - rl * a * h;
    const double expected = dt * ((1.0 - previous.ice_concentration) * ow.net_w_m2 + previous.ice_concentration * atmosphere_over_ice + c.deep_ocean_heat_flux_w_m2) + rl * exported;
    s.energy_error_j_m2 = (s.heat_content_j_m2 - energy_before) - expected;
    return s;
}

}  // namespace terraflux::weather
