#include "terraflux/weather/ice_sheet.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kZeroCelsius = 273.15;

void require_range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(std::string("ice sheet ") + name + " is out of range");
}

void check_state(const IceSheetState& s, std::size_t count) {
    if (s.thickness_m.size() != count || s.snow_m.size() != count) throw std::invalid_argument("ice sheet state arrays must match the grid");
}

}  // namespace

void validate_ice_sheet_config(const IceSheetConfig& c) {
    validate_transport_grid(c.grid);
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (c.bed_elevation_m.size() != count || c.ocean.size() != count) throw std::invalid_argument("ice sheet bed and ocean mask must match the grid");
    std::size_t sea = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(c.bed_elevation_m[i]) || std::abs(c.bed_elevation_m[i]) > 1.0e4) throw std::invalid_argument("ice sheet bed elevations must be finite and within +-10 km");
        if (c.ocean[i] > 1) throw std::invalid_argument("ice sheet ocean mask must be binary");
        sea += c.ocean[i];
    }
    if (sea == 0) throw std::invalid_argument("ice sheet needs at least one sea cell to drain to");
    if (!c.ice_domain.empty() && c.ice_domain.size() != count) throw std::invalid_argument("ice sheet domain mask must be empty or match the grid");
    const auto& p = c.physics;
    require_range(p.ice_density_kg_m3, 800.0, 1000.0, "ice_density_kg_m3");
    require_range(p.fresh_water_density_kg_m3, 900.0, 1100.0, "fresh_water_density_kg_m3");
    require_range(p.gravity_m_s2, 1.0, 20.0, "gravity_m_s2");
    require_range(p.rate_factor_pa3_s, 0.0, 1.0e-20, "rate_factor_pa3_s");
    require_range(p.degree_day_factor_snow_m_k_s, 0.0, 1.0e-6, "degree_day_factor_snow_m_k_s");
    require_range(p.degree_day_factor_ice_m_k_s, 0.0, 1.0e-6, "degree_day_factor_ice_m_k_s");
    require_range(p.pdd_standard_deviation_k, 0.1, 15.0, "pdd_standard_deviation_k");
    require_range(p.refreezing_fraction, 0.0, 1.0, "refreezing_fraction");
    require_range(p.snow_threshold_k, 260.0, 290.0, "snow_threshold_k");
    require_range(p.lapse_rate_k_m, 0.0, 0.012, "lapse_rate_k_m");
    require_range(p.snow_to_ice_time_s, 86400.0, 1.0e10, "snow_to_ice_time_s");
    require_range(p.frontal_a, 0.0, 1.0, "frontal_a");
    require_range(p.frontal_alpha, 0.0, 2.0, "frontal_alpha");
    require_range(p.frontal_b, 0.0, 10.0, "frontal_b");
    require_range(p.frontal_beta, 0.5, 2.0, "frontal_beta");
    require_range(p.front_water_depth_m, 0.0, 2000.0, "front_water_depth_m");
    require_range(p.marine_front_fraction, 0.0, 1.0, "marine_front_fraction");
    require_range(p.global_ocean_area_m2, 1.0e10, 1.0e15, "global_ocean_area_m2");
}

double expected_positive_degrees_k(double mu, double sigma) {
    if (!(sigma > 0.0)) return std::max(0.0, mu);
    const double x = mu / sigma;
    return sigma * std::exp(-0.5 * x * x) / std::sqrt(2.0 * kPi) + mu * 0.5 * std::erfc(-x / std::sqrt(2.0));
}

double frontal_melt_rate_m_s(double tf, double qsg, const IceSheetPhysics& p) {
    if (!(tf > 0.0)) return 0.0;
    return (p.frontal_a * p.front_water_depth_m * std::pow(std::max(0.0, qsg), p.frontal_alpha) + p.frontal_b) * std::pow(tf, p.frontal_beta) / 86400.0;
}

std::vector<double> vialov_dome(const IceSheetConfig& c, double ce, double cn, double radius, double h0) {
    return elliptical_dome(c, ce, cn, radius, radius, h0);
}

std::vector<double> elliptical_dome(const IceSheetConfig& c, double ce, double cn, double radius_east, double radius_north, double h0) {
    if (!(radius_east > 0.0) || !(radius_north > 0.0) || !(h0 >= 0.0)) throw std::invalid_argument("ice dome needs positive radii and a non-negative thickness");
    const std::size_t count = c.grid.nx * c.grid.ny;
    std::vector<double> h(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (c.ocean[i]) continue;
        const double e = c.grid.origin_east_m + (static_cast<double>(i % c.grid.nx) + 0.5) * c.grid.spacing_east_m - ce;
        const double n = c.grid.origin_north_m + (static_cast<double>(i / c.grid.nx) + 0.5) * c.grid.spacing_north_m - cn;
        const double r = std::hypot(e / radius_east, n / radius_north);
        if (r < 1.0) h[i] = h0 * std::pow(1.0 - std::pow(r, 4.0 / 3.0), 3.0 / 8.0);
    }
    return h;
}

std::vector<std::size_t> nearest_sea_cells(const std::vector<unsigned char>& ocean, std::size_t nx, std::size_t ny) {
    const std::size_t count = nx * ny;
    std::vector<std::size_t> target(count, count);
    std::deque<std::size_t> queue;
    for (std::size_t i = 0; i < count; ++i) if (ocean[i]) { target[i] = i; queue.push_back(i); }
    while (!queue.empty()) {
        const std::size_t k = queue.front();
        queue.pop_front();
        const std::size_t x = k % nx, y = k / nx;
        const std::size_t neighbours[4] = {x > 0 ? k - 1 : count, x + 1 < nx ? k + 1 : count, y > 0 ? k - nx : count, y + 1 < ny ? k + nx : count};
        for (std::size_t n : neighbours) {
            if (n >= count || target[n] < count) continue;
            target[n] = target[k];
            queue.push_back(n);
        }
    }
    return target;
}

IceSheetState initialize_ice_sheet(const IceSheetConfig& c, const std::vector<double>& thickness) {
    validate_ice_sheet_config(c);
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (thickness.size() != count) throw std::invalid_argument("initial ice thickness must match the grid");
    IceSheetState s;
    s.thickness_m.assign(count, 0.0);
    s.snow_m.assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(thickness[i]) || thickness[i] < 0.0 || thickness[i] > 5000.0) throw std::invalid_argument("initial ice thickness must lie within 0-5000 m");
        if (c.ocean[i] && thickness[i] > 0.0) throw std::invalid_argument("the ice sheet rests on land: no initial ice on sea cells");
        s.thickness_m[i] = thickness[i];
    }
    for (auto* v : {&s.surface_elevation_m, &s.surface_air_temperature_k, &s.accumulation_m_s, &s.melt_m_s, &s.refreeze_m_s, &s.smb_m_s, &s.frontal_ablation_m_s,
                    &s.runoff_to_sea_m3_s, &s.icebergs_to_sea_m3_s, &s.frontal_melt_to_sea_m3_s}) v->assign(count, 0.0);
    const double area = c.grid.spacing_east_m * c.grid.spacing_north_m;
    long double volume = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        s.surface_elevation_m[i] = c.ocean[i] ? 0.0 : c.bed_elevation_m[i] + s.thickness_m[i];
        volume += static_cast<long double>(s.thickness_m[i]) * area;
        if (s.thickness_m[i] > 1.0) s.area_m2 += area;
    }
    s.volume_m3 = static_cast<double>(volume);
    s.sea_level_equivalent_m = s.volume_m3 * c.physics.ice_density_kg_m3 / c.physics.fresh_water_density_kg_m3 / c.physics.global_ocean_area_m2;
    return s;
}

IceSheetState advance_ice_sheet(const IceSheetState& previous, const IceSheetForcing& f, const IceSheetConfig& c, double dt) {
    validate_ice_sheet_config(c);
    const std::size_t nx = c.grid.nx, ny = c.grid.ny, count = nx * ny;
    check_state(previous, count);
    if (!(dt > 0.0) || !std::isfinite(dt) || dt > 10.0 * 365.0 * 86400.0) throw std::invalid_argument("ice sheet timestep must lie within (0, 10 years]");
    if (f.air_temperature_k.size() != count || f.precipitation_m_s.size() != count) throw std::invalid_argument("ice sheet forcing must match the grid");
    if (!f.ocean_thermal_forcing_k.empty() && f.ocean_thermal_forcing_k.size() != count) throw std::invalid_argument("ice sheet ocean thermal forcing must be empty or match the grid");
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(f.air_temperature_k[i]) || f.air_temperature_k[i] < 180.0 || f.air_temperature_k[i] > 330.0) throw std::invalid_argument("ice sheet air temperature must lie within 180-330 K");
        if (!std::isfinite(f.precipitation_m_s[i]) || f.precipitation_m_s[i] < 0.0 || f.precipitation_m_s[i] > 1.0e-5) throw std::invalid_argument("ice sheet precipitation must lie within 0-1e-5 m/s");
        if (!f.ocean_thermal_forcing_k.empty() && (!std::isfinite(f.ocean_thermal_forcing_k[i]) || f.ocean_thermal_forcing_k[i] < -5.0 || f.ocean_thermal_forcing_k[i] > 30.0)) {
            throw std::invalid_argument("ice sheet ocean thermal forcing must lie within -5..30 K");
        }
    }
    const auto& p = c.physics;
    const double dx = c.grid.spacing_east_m, dy = c.grid.spacing_north_m, area = dx * dy;
    const double ice_to_water = p.ice_density_kg_m3 / p.fresh_water_density_kg_m3;
    IceSheetState s = previous;
    s.elapsed_seconds = previous.elapsed_seconds + dt;
    for (auto* v : {&s.accumulation_m_s, &s.melt_m_s, &s.refreeze_m_s, &s.smb_m_s, &s.frontal_ablation_m_s, &s.runoff_to_sea_m3_s, &s.icebergs_to_sea_m3_s, &s.frontal_melt_to_sea_m3_s, &s.surface_air_temperature_k}) v->assign(count, 0.0);
    const std::vector<std::size_t> sea_of = nearest_sea_cells(c.ocean, nx, ny);
    long double accumulation = 0.0L, melt = 0.0L, refreeze = 0.0L, runoff = 0.0L;   // ice eq (m3) except runoff (water eq)

    // 1. Surface mass balance at the surface elevation of the start of the step.
    const double snow_decay = std::exp(-dt / p.snow_to_ice_time_s);
    for (std::size_t i = 0; i < count; ++i) {
        if (c.ocean[i]) continue;
        if (!c.ice_domain.empty() && !c.ice_domain[i] && s.thickness_m[i] <= 0.0) continue;   // no glaciers outside the domain
        const double h0 = s.thickness_m[i];
        const double surface = c.bed_elevation_m[i] + h0;
        const double ts = f.air_temperature_k[i] - p.lapse_rate_k_m * std::max(0.0, surface);
        s.surface_air_temperature_k[i] = ts;
        const double snow_fraction = std::min(1.0, std::max(0.0, (p.snow_threshold_k + 1.0 - ts) / 2.0));
        const double snowfall_ie = f.precipitation_m_s[i] * snow_fraction * dt / ice_to_water;
        const double rain_we = f.precipitation_m_s[i] * (1.0 - snow_fraction) * dt;
        double h = h0 + snowfall_ie, snow = s.snow_m[i] + snowfall_ie;
        const double degree_seconds = expected_positive_degrees_k(ts - kZeroCelsius, p.pdd_standard_deviation_k) * dt;
        // Snow melts first; the degree days it does not use melt ice.
        const double snow_potential_ie = p.degree_day_factor_snow_m_k_s * degree_seconds / ice_to_water;
        const double snow_melt = std::min(snow, snow_potential_ie);
        const double unused = snow_potential_ie > 0.0 ? 1.0 - snow_melt / snow_potential_ie : 0.0;
        const double ice_melt = std::min(std::max(0.0, h - snow), p.degree_day_factor_ice_m_k_s * degree_seconds * unused / ice_to_water);
        const double refrozen = p.refreezing_fraction * snow_melt;
        h += refrozen - snow_melt - ice_melt;
        snow = (snow - snow_melt) * snow_decay;
        s.thickness_m[i] = std::max(0.0, h);
        s.snow_m[i] = std::min(snow, s.thickness_m[i]);
        const double cell_runoff_we = ((1.0 - p.refreezing_fraction) * snow_melt + ice_melt) * ice_to_water + ((h0 > 0.0 || snowfall_ie > 0.0) ? rain_we : 0.0);
        s.accumulation_m_s[i] = snowfall_ie / dt;
        s.melt_m_s[i] = (snow_melt + ice_melt) / dt;
        s.refreeze_m_s[i] = refrozen / dt;
        s.smb_m_s[i] = (snowfall_ie + refrozen - snow_melt - ice_melt) / dt;
        accumulation += static_cast<long double>(snowfall_ie) * area;
        melt += static_cast<long double>(snow_melt + ice_melt) * area;
        refreeze += static_cast<long double>(refrozen) * area;
        runoff += static_cast<long double>(cell_runoff_we) * area;
        s.runoff_to_sea_m3_s[sea_of[i]] += cell_runoff_we * area / dt;
    }

    // 2. Ice flow (shallow-ice approximation), explicit sub-steps, donor-limited; ice reaching the sea calves.
    const double coefficient = 2.0 * p.rate_factor_pa3_s * std::pow(p.ice_density_kg_m3 * p.gravity_m_s2, 3.0) / 5.0;
    long double calved_ie = 0.0L;
    std::vector<double> surface(count), fx(count, 0.0), fy(count, 0.0), out(count);
    double remaining = dt;
    int substeps = 0;
    while (remaining > 0.0) {
        for (std::size_t i = 0; i < count; ++i) surface[i] = c.ocean[i] ? 0.0 : c.bed_elevation_m[i] + s.thickness_m[i];
        auto gradient_y = [&](std::size_t i) {
            const std::size_t y = i / nx;
            const std::size_t lo = y > 0 ? i - nx : i, hi = y + 1 < ny ? i + nx : i;
            return hi == lo ? 0.0 : (surface[hi] - surface[lo]) / (static_cast<double>((hi - lo) / nx) * dy);
        };
        auto gradient_x = [&](std::size_t i) {
            const std::size_t x = i % nx;
            const std::size_t lo = x > 0 ? i - 1 : i, hi = x + 1 < nx ? i + 1 : i;
            return hi == lo ? 0.0 : (surface[hi] - surface[lo]) / (static_cast<double>(hi - lo) * dx);
        };
        double d_max = 0.0;
        std::fill(fx.begin(), fx.end(), 0.0);
        std::fill(fy.begin(), fy.end(), 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t x = i % nx, y = i / nx;
            // East face (i, i+1) and north face (i, i+nx); no flux between two sea cells or at the domain edge.
            if (x + 1 < nx && !(c.ocean[i] && c.ocean[i + 1])) {
                const double h_face = 0.5 * (s.thickness_m[i] + s.thickness_m[i + 1]);
                if (h_face > 0.0) {
                    const double gx = (surface[i + 1] - surface[i]) / dx, gy = 0.5 * (gradient_y(i) + gradient_y(i + 1));
                    const double h2 = h_face * h_face, d = coefficient * h2 * h2 * h_face * (gx * gx + gy * gy);
                    fx[i] = -d * gx;   // m2/s, positive eastward
                    d_max = std::max(d_max, d);
                }
            }
            if (y + 1 < ny && !(c.ocean[i] && c.ocean[i + nx])) {
                const double h_face = 0.5 * (s.thickness_m[i] + s.thickness_m[i + nx]);
                if (h_face > 0.0) {
                    const double gy = (surface[i + nx] - surface[i]) / dy, gx = 0.5 * (gradient_x(i) + gradient_x(i + nx));
                    const double h2 = h_face * h_face, d = coefficient * h2 * h2 * h_face * (gx * gx + gy * gy);
                    fy[i] = -d * gy;
                    d_max = std::max(d_max, d);
                }
            }
        }
        const double limit = d_max > 0.0 ? 0.2 * std::min(dx * dx, dy * dy) / d_max : remaining;
        const double h = std::min(remaining, limit);
        if (++substeps > 100000) throw std::runtime_error("ice sheet flow needs more than 100000 sub-steps; reduce the timestep or the rate factor");
        // Donor limiting: a cell cannot export more than it holds.
        std::fill(out.begin(), out.end(), 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            if (fx[i] > 0.0) out[i] += fx[i] * dy * h; else if (fx[i] < 0.0) out[i + 1] -= fx[i] * dy * h;
            if (fy[i] > 0.0) out[i] += fy[i] * dx * h; else if (fy[i] < 0.0) out[i + nx] -= fy[i] * dx * h;
        }
        std::vector<double> scale(count, 1.0);
        for (std::size_t i = 0; i < count; ++i) if (out[i] > s.thickness_m[i] * area && out[i] > 0.0) scale[i] = s.thickness_m[i] * area / out[i];
        std::vector<double> change(count, 0.0);
        auto move = [&](std::size_t from, std::size_t to, double volume) {
            volume *= scale[from];
            change[from] -= volume;
            if (c.ocean[to]) { calved_ie += volume; s.icebergs_to_sea_m3_s[to] += volume * ice_to_water / dt; }
            else change[to] += volume;
        };
        for (std::size_t i = 0; i < count; ++i) {
            if (fx[i] > 0.0) move(i, i + 1, fx[i] * dy * h); else if (fx[i] < 0.0) move(i + 1, i, -fx[i] * dy * h);
            if (fy[i] > 0.0) move(i, i + nx, fy[i] * dx * h); else if (fy[i] < 0.0) move(i + nx, i, -fy[i] * dx * h);
        }
        for (std::size_t i = 0; i < count; ++i) if (!c.ocean[i]) s.thickness_m[i] = std::max(0.0, s.thickness_m[i] + change[i] / area);
        remaining -= h;
    }

    // 3. Frontal ablation at marine margins, driven by the ocean thermal forcing and the subglacial discharge.
    long double frontal_ie = 0.0L;
    if (!f.ocean_thermal_forcing_k.empty()) {
        const std::vector<double> arriving = s.runoff_to_sea_m3_s;   // surface runoff only
        for (std::size_t i = 0; i < count; ++i) {
            if (c.ocean[i] || s.thickness_m[i] <= 0.0) continue;
            const std::size_t x = i % nx, y = i / nx;
            const std::size_t neighbours[4] = {x > 0 ? i - 1 : count, x + 1 < nx ? i + 1 : count, y > 0 ? i - nx : count, y + 1 < ny ? i + nx : count};
            double tf = -1.0, discharge = 0.0;
            std::size_t target = count;
            for (std::size_t n : neighbours) {
                if (n >= count || !c.ocean[n]) continue;
                discharge += arriving[n];
                if (f.ocean_thermal_forcing_k[n] > tf) { tf = f.ocean_thermal_forcing_k[n]; target = n; }
            }
            if (target >= count || tf <= 0.0) continue;
            const double width = (target == i + 1 || target + 1 == i) ? dy : dx;
            const double depth = std::max(1.0, p.front_water_depth_m);
            const double qsg_m_day = p.marine_front_fraction > 0.0 ? discharge / (p.marine_front_fraction * width * depth) * 86400.0 : 0.0;
            const double rate = frontal_melt_rate_m_s(tf, qsg_m_day, p);
            const double lost = std::min(s.thickness_m[i], p.marine_front_fraction * rate * s.thickness_m[i] / (target == i + 1 || target + 1 == i ? dx : dy) * dt);
            s.thickness_m[i] -= lost;
            s.frontal_ablation_m_s[i] = lost / dt;
            frontal_ie += static_cast<long double>(lost) * area;
            s.runoff_to_sea_m3_s[target] += lost * area * ice_to_water / dt;
            s.frontal_melt_to_sea_m3_s[target] += lost * area * ice_to_water / dt;
        }
    }

    // 4. Totals and budget.
    long double volume = 0.0L;
    s.area_m2 = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        s.snow_m[i] = std::min(s.snow_m[i], s.thickness_m[i]);
        s.surface_elevation_m[i] = c.ocean[i] ? 0.0 : c.bed_elevation_m[i] + s.thickness_m[i];
        volume += static_cast<long double>(s.thickness_m[i]) * area;
        if (s.thickness_m[i] > 1.0) s.area_m2 += area;
    }
    const double previous_volume = previous.volume_m3;
    s.volume_m3 = static_cast<double>(volume);
    s.sea_level_equivalent_m = s.volume_m3 * ice_to_water / p.global_ocean_area_m2;
    s.accumulation_m3_s = static_cast<double>(accumulation) / dt;
    s.melt_m3_s = static_cast<double>(melt) / dt;
    s.refreeze_m3_s = static_cast<double>(refreeze) / dt;
    s.smb_m3_s = static_cast<double>(accumulation + refreeze - melt) / dt;
    s.runoff_m3_s = static_cast<double>(runoff) / dt;
    s.calving_m3_s = static_cast<double>(calved_ie) * ice_to_water / dt;
    s.frontal_ablation_m3_s = static_cast<double>(frontal_ie) * ice_to_water / dt;
    const double expected = static_cast<double>(accumulation + refreeze - melt - calved_ie - frontal_ie);
    s.mass_balance_error_m3 = (s.volume_m3 - previous_volume) - expected;
    s.cumulative_freshwater_m3 += (s.runoff_m3_s + s.calving_m3_s + s.frontal_ablation_m3_s) * dt;
    s.cumulative_sea_level_rise_m += -(s.volume_m3 - previous_volume) * ice_to_water / p.global_ocean_area_m2;
    return s;
}

}  // namespace terraflux::weather
