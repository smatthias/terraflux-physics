#include "terraflux/weather/earth_forecast.hpp"

#include "terraflux/weather/cosmic_dust.hpp"   // the Sun's position
#include "terraflux/weather/moist_air.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kEarthRadius = 6.371e6;
constexpr double kLatent = 2.501e6;
constexpr double kCp = 1004.6;
constexpr double kRd = 287.05;
constexpr double kRv = 461.5;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

}  // namespace

// ---- Geography ----------------------------------------------------------------------------------------------------

GeoPoint local_to_geographic(double x, double y, double lat0_deg, double lon0_deg) {
    const double rho = std::hypot(x, y);
    if (rho < 1e-6) return {lat0_deg, lon0_deg};
    const double c = rho / kEarthRadius, lat0 = lat0_deg * kDeg;
    const double lat = std::asin(std::clamp(std::cos(c) * std::sin(lat0) + y * std::sin(c) * std::cos(lat0) / rho, -1.0, 1.0));
    const double lon = lon0_deg * kDeg + std::atan2(x * std::sin(c), rho * std::cos(lat0) * std::cos(c) - y * std::sin(lat0) * std::sin(c));
    double lon_deg = lon / kDeg;
    lon_deg = std::fmod(lon_deg + 540.0, 360.0) - 180.0;
    return {lat / kDeg, lon_deg};
}

std::array<double, 2> geographic_to_local(double lat_deg, double lon_deg, double lat0_deg, double lon0_deg) {
    const double lat = lat_deg * kDeg, lat0 = lat0_deg * kDeg, dl = (lon_deg - lon0_deg) * kDeg;
    const double cos_c = std::clamp(std::sin(lat0) * std::sin(lat) + std::cos(lat0) * std::cos(lat) * std::cos(dl), -1.0, 1.0);
    const double c = std::acos(cos_c);
    const double k = c < 1e-12 ? 1.0 : c / std::sin(c);
    return {kEarthRadius * k * std::cos(lat) * std::sin(dl), kEarthRadius * k * (std::cos(lat0) * std::sin(lat) - std::sin(lat0) * std::cos(lat) * std::cos(dl))};
}

double great_circle_km(double lat1, double lon1, double lat2, double lon2) {
    const double a = std::sin(0.5 * (lat2 - lat1) * kDeg), b = std::sin(0.5 * (lon2 - lon1) * kDeg);
    const double h = a * a + std::cos(lat1 * kDeg) * std::cos(lat2 * kDeg) * b * b;
    return 2.0 * kEarthRadius / 1000.0 * std::asin(std::min(1.0, std::sqrt(h)));
}

double solar_cos_zenith(double lat, double lon, double d) {
    const double lambda = solar_ecliptic_longitude_deg(d) * kDeg, eps = 23.439 * kDeg;
    const double ra = std::atan2(std::cos(eps) * std::sin(lambda), std::cos(lambda)), dec = std::asin(std::sin(eps) * std::sin(lambda));
    const double hour_angle = (greenwich_sidereal_deg(d) + lon) * kDeg - ra;
    return std::sin(lat * kDeg) * std::sin(dec) + std::cos(lat * kDeg) * std::cos(dec) * std::cos(hour_angle);
}

// ---- Synoptic systems ---------------------------------------------------------------------------------------------

double solar_declination_deg(double d) {
    const double lambda = solar_ecliptic_longitude_deg(d) * kDeg, eps = 23.439 * kDeg;
    return std::asin(std::sin(eps) * std::sin(lambda)) / kDeg;
}

double daily_mean_cos_zenith(double lat_deg, double dec_deg) {
    const double phi = lat_deg * kDeg, dec = dec_deg * kDeg;
    const double x = std::clamp(-std::tan(phi) * std::tan(dec), -1.0, 1.0);
    const double h0 = std::acos(x);
    return std::max(0.0, (h0 * std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::sin(h0)) / kPi);
}

double steering_east_m_s(double lat, const SteeringFlow& f) {
    const double jet = (lat - f.jet_latitude_deg) / f.jet_width_deg, trade = (lat - f.trade_latitude_deg) / f.trade_width_deg;
    return f.jet_speed_m_s * std::exp(-jet * jet) - f.trade_speed_m_s * std::exp(-trade * trade);
}

void validate_ensemble_spread(const EnsembleSpread& s) {
    require(within(s.position_km, 0.0, 2000.0) && within(s.speed_m_s, 0.0, 20.0) && within(s.deepening_pa_per_day, 0.0, 5000.0) && within(s.radius_fraction, 0.0, 0.5),
            "ensemble spread: position_km within 0-2000, speed_m_s within 0-20, deepening_pa_per_day within 0-5000, radius_fraction within 0-0.5");
    require(within(s.genesis_position_km, 0.0, 3000.0) && within(s.genesis_timing_hours, 0.0, 72.0) && within(s.temperature_k, 0.0, 10.0) &&
                within(s.hurricane_position_km, 0.0, 1000.0) && within(s.hurricane_wind_m_s, 0.0, 30.0),
            "ensemble spread: genesis_position_km within 0-3000, genesis_timing_hours within 0-72, temperature_k within 0-10, hurricane errors within 0-1000 km and 0-30 m/s");
}

SystemErrors draw_system_errors(NormalSequence& r, const EnsembleSpread& s, bool genesis) {
    SystemErrors e;
    const double position = (genesis ? s.genesis_position_km : s.position_km) * 1000.0;
    e.east_m = position * r.next();
    e.north_m = position * r.next();
    e.speed_east_m_s = s.speed_m_s * r.next();
    e.speed_north_m_s = s.speed_m_s * r.next();
    e.deepening_pa_per_day = s.deepening_pa_per_day * r.next();
    e.radius_factor = std::max(0.6, 1.0 + s.radius_fraction * r.next());
    e.timing_hours = genesis ? s.genesis_timing_hours * r.next() : 0.0;
    return e;
}

SynopticEvolution::SynopticEvolution(const std::vector<SynopticSystem>& systems, const std::vector<GenesisRegion>& genesis, const SteeringFlow& steering,
                                     const TransportGrid2D& grid, double horizon, NormalSequence* random, const EnsembleSpread& spread)
    : steering_(steering), grid_(grid) {
    const double lat0 = grid.geographic_anchor_latitude_deg, lon0 = grid.geographic_anchor_longitude_deg;
    for (const auto& s : systems) {
        require(s.kind == "low" || s.kind == "high", "synoptic systems are lows or highs");
        require((s.kind == "low" && s.anomaly_pa < 0.0) || (s.kind == "high" && s.anomaly_pa > 0.0), "lows need a negative, highs a positive anomaly");
        require(s.radius_m > 0.0 && std::isfinite(s.radius_m), "synoptic systems need a positive radius");
        Item item;
        item.system = s;
        item.errors = random ? draw_system_errors(*random, spread, false) : SystemErrors{};
        const auto p = geographic_to_local(s.latitude_deg, s.longitude_deg, lat0, lon0);
        item.east_m = p[0] + item.errors.east_m;
        item.north_m = p[1] + item.errors.north_m;
        items_.push_back(item);
    }
    for (const auto& g : genesis) {
        require(g.every_hours > 0.0 && g.anomaly_pa < 0.0 && g.radius_m > 0.0, "genesis regions need a positive rhythm, a negative anomaly and a positive radius");
        int k = 0;
        for (double t = g.first_hours; t <= std::min(g.last_hours, horizon); t += g.every_hours, ++k) {
            SynopticSystem s;
            s.name = g.name + " #" + std::to_string(k + 1);
            s.kind = "low";
            s.latitude_deg = g.latitude_deg;
            s.longitude_deg = g.longitude_deg;
            s.anomaly_pa = g.anomaly_pa;
            s.radius_m = g.radius_m;
            s.deepening_pa_per_day = g.deepening_pa_per_day;
            s.mature_hours = g.mature_hours;
            s.filling_pa_per_day = g.filling_pa_per_day;
            s.steering_factor = g.steering_factor;
            s.drift_north_m_s = g.drift_north_m_s;
            Item item;
            item.errors = random ? draw_system_errors(*random, spread, true) : SystemErrors{};
            s.start_hours = std::max(0.0, t + item.errors.timing_hours);
            item.system = s;
            const auto p = geographic_to_local(s.latitude_deg, s.longitude_deg, lat0, lon0);
            item.east_m = p[0] + item.errors.east_m;
            item.north_m = p[1] + item.errors.north_m;
            items_.push_back(item);
        }
    }
    for (auto& item : items_) item.born = item.system.start_hours <= 0.0;
}

double SynopticEvolution::strength(const Item& item) const {
    const auto& s = item.system;
    const double mature = std::min(item.age_hours, s.mature_hours), after = std::max(0.0, item.age_hours - s.mature_hours);
    return std::max(0.0, std::abs(s.anomaly_pa) + (s.deepening_pa_per_day + item.errors.deepening_pa_per_day) * mature / 24.0 - s.filling_pa_per_day * after / 24.0);
}

void SynopticEvolution::advance(double hours) {
    double left = hours;
    while (left > 1e-9) {
        const double dt = std::min(1.0, left);
        for (auto& item : items_) {
            if (!item.born) {
                if (hours_ + dt >= item.system.start_hours) item.born = true;
                else continue;
            }
            const auto g = local_to_geographic(item.east_m, item.north_m, grid_.geographic_anchor_latitude_deg, grid_.geographic_anchor_longitude_deg);
            // Motion errors scale with how mobile the system is: quasi-stationary highs wander less.
            const double mobility = std::max(0.25, std::min(1.0, item.system.steering_factor));
            const double u = steering_east_m_s(g.latitude_deg, steering_) * item.system.steering_factor + item.system.drift_east_m_s + item.errors.speed_east_m_s * mobility;
            const double v = item.system.drift_north_m_s + item.errors.speed_north_m_s * mobility;
            item.east_m += u * dt * 3600.0;
            item.north_m += v * dt * 3600.0;
            item.age_hours += dt;
        }
        hours_ += dt;
        left -= dt;
    }
}

std::vector<SynopticEvolution::Active> SynopticEvolution::active() const {
    std::vector<Active> out;
    const double x0 = grid_.origin_east_m, x1 = x0 + grid_.spacing_east_m * static_cast<double>(grid_.nx);
    const double y0 = grid_.origin_north_m, y1 = y0 + grid_.spacing_north_m * static_cast<double>(grid_.ny);
    for (const auto& item : items_) {
        if (!item.born) continue;
        const double a = strength(item);
        if (a < 20.0) continue;
        const double r = item.system.radius_m * item.errors.radius_factor;
        if (item.east_m < x0 - 3.0 * r || item.east_m > x1 + 3.0 * r || item.north_m < y0 - 3.0 * r || item.north_m > y1 + 3.0 * r) continue;
        out.push_back({item.system.name, item.system.kind, item.east_m, item.north_m, item.system.kind == "low" ? -a : a, r});
    }
    return out;
}

std::vector<PressureCenter> SynopticEvolution::centers() const {
    std::vector<PressureCenter> out;
    for (const auto& a : active()) out.push_back({a.kind, a.east_m, a.north_m, a.anomaly_pa, a.radius_m});
    return out;
}

// ---- Synoptic ascent, layered cloud and rain ----------------------------------------------------------------------

void validate_synoptic_weather(const SynopticWeatherConfig& c) {
    require(within(c.low_ascent_m_s, 0.0, 0.5) && within(c.high_subsidence_m_s, 0.0, 0.2) && within(c.warm_sector_bias, 0.0, 1.0) && within(c.warm_sector_direction_deg, 0.0, 360.0),
            "synoptic weather: low_ascent_m_s within 0-0.5, high_subsidence_m_s within 0-0.2, warm_sector_bias within 0-1, direction within 0-360");
    require(within(c.convergence_depth_m, 0.0, 5000.0) && within(c.cloud_rh, 0.0, 1.0) && c.full_cloud_rh > c.cloud_rh && c.full_cloud_rh <= 1.0 && within(c.rain_rh, 0.0, 0.99) &&
                within(c.efficiency, 0.0, 1.0) && within(c.orographic_lift_factor, 0.0, 1.0),
            "synoptic weather: convergence depth within 0-5000 m, 0 <= cloud_rh < full_cloud_rh <= 1, rain_rh within 0-0.99, efficiency and orographic_lift_factor within 0-1");
}

std::vector<double> synoptic_ascent_m_s(const std::vector<std::array<double, 4>>& systems, const std::vector<double>& u, const std::vector<double>& v, const TransportGrid2D& g,
                                        const SynopticWeatherConfig& c) {
    const std::size_t n = g.nx * g.ny;
    require(u.size() == n && v.size() == n, "synoptic ascent needs the wind on the grid");
    std::vector<double> w(n, 0.0);
    const double dir = c.warm_sector_direction_deg * kDeg;
    for (const auto& s : systems) {
        const bool low = s[2] < 0.0;
        const double amplitude = (low ? c.low_ascent_m_s : -c.high_subsidence_m_s) * std::abs(s[2]) / 1500.0;
        for (std::size_t k = 0; k < n; ++k) {
            const double x = g.origin_east_m + (static_cast<double>(k % g.nx) + 0.5) * g.spacing_east_m - s[0];
            const double y = g.origin_north_m + (static_cast<double>(k / g.nx) + 0.5) * g.spacing_north_m - s[1];
            const double r = std::hypot(x, y) / s[3];
            if (r > 3.0) continue;
            const double angle = std::atan2(x, y);   // from north, clockwise
            const double bias = low ? 1.0 + c.warm_sector_bias * std::cos(angle - dir) : 1.0;
            w[k] += amplitude * std::exp(-r * r) * bias;
        }
    }
    // Frictional convergence of the layer.
    for (std::size_t j = 0; j < g.ny; ++j)
        for (std::size_t i = 0; i < g.nx; ++i) {
            const std::size_t k = j * g.nx + i;
            const std::size_t e = j * g.nx + std::min(i + 1, g.nx - 1), wst = j * g.nx + (i > 0 ? i - 1 : 0);
            const std::size_t nn = std::min(j + 1, g.ny - 1) * g.nx + i, ss = (j > 0 ? j - 1 : 0) * g.nx + i;
            const double div = (u[e] - u[wst]) / (2.0 * g.spacing_east_m) + (v[nn] - v[ss]) / (2.0 * g.spacing_north_m);
            w[k] += -c.convergence_depth_m * div;
        }
    return w;
}

std::vector<double> upslope_ascent_m_s(const std::vector<double>& u, const std::vector<double>& v, const std::vector<double>& h, const TransportGrid2D& g, double f) {
    const std::size_t n = g.nx * g.ny;
    require(u.size() == n && v.size() == n && h.size() == n, "upslope ascent needs the wind and the elevation on the grid");
    std::vector<double> w(n, 0.0);
    for (std::size_t j = 0; j < g.ny; ++j)
        for (std::size_t i = 0; i < g.nx; ++i) {
            const std::size_t k = j * g.nx + i;
            const std::size_t e = j * g.nx + std::min(i + 1, g.nx - 1), wst = j * g.nx + (i > 0 ? i - 1 : 0);
            const std::size_t nn = std::min(j + 1, g.ny - 1) * g.nx + i, ss = (j > 0 ? j - 1 : 0) * g.nx + i;
            const double dhdx = (h[e] - h[wst]) / (static_cast<double>((e % g.nx) - (wst % g.nx)) * g.spacing_east_m);
            const double dhdy = (h[nn] - h[ss]) / (static_cast<double>((nn / g.nx) - (ss / g.nx)) * g.spacing_north_m);
            w[k] = f * (u[k] * dhdx + v[k] * dhdy);
        }
    return w;
}

double layered_cloud_fraction(double rh, double w, const SynopticWeatherConfig& c) {
    const double shift = std::clamp(w / 0.05, -1.0, 1.0) * 0.1;   // ascent: cloud at lower humidity; subsidence: higher
    return std::clamp((rh - (c.cloud_rh - shift)) / (c.full_cloud_rh - c.cloud_rh), 0.0, 1.0);
}

double layered_rain_mm_h(double rh, double w, double rho, double q, const SynopticWeatherConfig& c) {
    if (w <= 0.0 || rh <= c.rain_rh) return 0.0;
    return c.efficiency * rho * w * q * std::min(1.0, (rh - c.rain_rh) / (1.0 - c.rain_rh)) * 3600.0;
}

// ---- The surface ---------------------------------------------------------------------------------------------------

void validate_surface_coupling(const SurfaceCouplingConfig& c) {
    require(within(c.ocean_temperature_time_h, 1.0, 1000.0) && within(c.ocean_moisture_time_h, 1.0, 1000.0) && within(c.land_climate_time_h, 1.0, 10000.0),
            "surface: ocean and land time scales within 1-1000 h (land climate 1-10000 h)");
    require(within(c.sea_air_offset_k, -5.0, 5.0) && within(c.sea_relative_humidity, 0.3, 1.0), "surface: sea_air_offset_k within -5-5, sea_relative_humidity within 0.3-1");
    require(within(c.land_heating_k_per_h, 0.0, 5.0) && within(c.dry_soil_warming_k, 0.0, 20.0) && within(c.cloud_shading, 0.0, 1.0) && within(c.wet_soil_heating_reduction, 0.0, 1.0),
            "surface: land_heating_k_per_h within 0-5, dry_soil_warming_k within 0-20, cloud_shading and wet_soil_heating_reduction within 0-1");
    require(within(c.evapotranspiration_g_kg_per_h, 0.0, 2.0) && within(c.soil_capacity_mm, 10.0, 1000.0) && within(c.drainage_mm_per_day, 0.0, 50.0) &&
                within(c.boundary_layer_depth_m, 200.0, 5000.0),
            "surface: evapotranspiration within 0-2 g/kg/h, soil capacity within 10-1000 mm, drainage within 0-50 mm/day, boundary layer within 200-5000 m");
}

SurfaceStepReport couple_surface(CoupledWindState& s, SurfaceState& surface, const std::vector<double>& cos_zenith, const std::vector<double>& mean_cos_zenith,
                                 const SurfaceCouplingConfig& c, double hours) {
    const std::size_t n = s.air_temperature_k.size();
    require(surface.land.size() == n && surface.sea_surface_temperature_k.size() == n && surface.land_climate_temperature_k.size() == n &&
                surface.soil_moisture.size() == n && surface.normal_soil_moisture.size() == n && surface.cloud_fraction.size() == n && cos_zenith.size() == n &&
                mean_cos_zenith.size() == n,
            "surface coupling arrays must match the grid");
    SurfaceStepReport report;
    report.condensation_mm.assign(n, 0.0);
    double heating = 0.0, land_cells = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double& t = s.air_temperature_k[i];
        double& q = s.specific_humidity_kg_kg[i];
        const double p = s.pressure_pa[i];
        const double air_kg_m2 = p / (kRd * t) * c.boundary_layer_depth_m;
        const double cloud = std::clamp(surface.cloud_fraction[i], 0.0, 1.0);
        if (!surface.land[i]) {
            if (c.ocean_fluxes) {
            const double sst = surface.sea_surface_temperature_k[i];
            t += (sst - c.sea_air_offset_k - t) * (1.0 - std::exp(-hours / c.ocean_temperature_time_h));
            const double target = c.sea_relative_humidity * saturation_specific_humidity_kg_kg(sst, p);
            q += (target - q) * (1.0 - std::exp(-hours / c.ocean_moisture_time_h));
            }
        } else {
            const double sun = std::max(0.0, cos_zenith[i]);
            const double soil = std::clamp(surface.soil_moisture[i], 0.0, 1.0);
            // Heating above the daily mean of the Sun by day, cooling below it at night: no net drift over a day; cloud
            // and wet soil damp the cycle.
            const double heat = c.land_heating_k_per_h * (sun - mean_cos_zenith[i]) * (1.0 - c.cloud_shading * cloud) * (1.0 - c.wet_soil_heating_reduction * soil) * hours;
            t += heat;
            // The land relaxes to its climate, warmer where the soil is drier than normal (less evaporative cooling).
            const double target = surface.land_climate_temperature_k[i] + c.dry_soil_warming_k * (surface.normal_soil_moisture[i] - soil);
            t += (target - t) * (1.0 - std::exp(-hours / c.land_climate_time_h));
            heating += std::max(0.0, heat);
            land_cells += 1.0;
            // Evapotranspiration: moisture into the air, water out of the soil.
            const double dq = c.evapotranspiration_g_kg_per_h * 1.0e-3 * sun * soil * hours;
            const double water_mm = std::min(dq * air_kg_m2, soil * c.soil_capacity_mm);
            q += water_mm / air_kg_m2;
            // Deep drainage: a wet soil percolates towards the groundwater (strongly non-linear in the wetness).
            const double drained_mm = c.drainage_mm_per_day * soil * soil * soil * hours / 24.0;
            surface.soil_moisture[i] = std::clamp(soil - (water_mm + drained_mm) / c.soil_capacity_mm, 0.0, 1.0);
        }
        q = std::max(q, 1.0e-6);
        // Large-scale condensation with its latent heat (one Newton step is enough for the small excess of an hour).
        for (int k = 0; k < 3; ++k) {
            const double qs = saturation_specific_humidity_kg_kg(t, p);
            if (q <= qs) break;
            const double dq = (q - qs) / (1.0 + kLatent * kLatent * qs / (kCp * kRv * t * t));
            q -= dq;
            t += kLatent / kCp * dq;
            report.condensation_mm[i] += dq * air_kg_m2;
        }
        // Leave a hair below saturation: the diagnostics (wet bulb) need unsaturated air.
        const double cap = 0.999 * saturation_specific_humidity_kg_kg(t, p);
        if (q > cap) {
            report.condensation_mm[i] += (q - cap) * air_kg_m2;
            t += kLatent / kCp * (q - cap);
            q = cap;
        }
    }
    report.mean_land_heating_k = land_cells > 0.0 ? heating / land_cells : 0.0;
    return report;
}

double add_rain_to_soil(double& soil, double rain_mm, double slope, const SurfaceCouplingConfig& c) {
    if (!(rain_mm > 0.0)) return 0.0;
    const double s = std::clamp(soil, 0.0, 1.0);
    const double fraction = std::clamp(s * s + 5.0 * std::max(0.0, slope), 0.0, 1.0);
    double runoff = rain_mm * fraction;
    double stored = s * c.soil_capacity_mm + rain_mm - runoff;
    if (stored > c.soil_capacity_mm) {
        runoff += stored - c.soil_capacity_mm;
        stored = c.soil_capacity_mm;
    }
    soil = stored / c.soil_capacity_mm;
    return runoff;
}

// ---- Wind ----------------------------------------------------------------------------------------------------------

double gust_m_s(double mean, bool cumulonimbus, const GustConfig& c) { return std::max(0.0, mean) * c.gust_factor + (cumulonimbus ? c.convective_gust_m_s : 0.0); }

std::array<double, 2> surface_wind_m_s(double u, double v, double vu, double vv, bool land, double elevation, const GustConfig& c) {
    const double f = land ? c.surface_factor_land * std::max(0.3, 1.0 - c.terrain_reduction_per_km * std::max(0.0, elevation) / 1000.0) : c.surface_factor_sea;
    return {f * u + vu, f * v + vv};
}

double cyclone_wind_m_s(double vmax, double r_max, double r, double x) {
    if (r <= 0.0) return 0.0;
    return r < r_max ? vmax * r / r_max : vmax * std::pow(r_max / r, x);
}

CycloneHazards cyclone_hazards(const StormSystemsState& storms, const TransportGrid2D& g, const std::vector<unsigned char>& land, const CycloneHazardConfig& c) {
    const std::size_t n = g.nx * g.ny;
    require(land.size() == n, "cyclone hazards need the land mask");
    CycloneHazards h{std::vector<double>(n, 0.0), std::vector<double>(n, 0.0), std::vector<double>(n, 0.0)};
    const double cell = std::min(g.spacing_east_m, g.spacing_north_m);
    auto coastal = [&](std::size_t k) {
        if (!land[k]) return false;
        const std::size_t x = k % g.nx, y = k / g.nx;
        return (x > 0 && !land[k - 1]) || (x + 1 < g.nx && !land[k + 1]) || (y > 0 && !land[k - g.nx]) || (y + 1 < g.ny && !land[k + g.nx]);
    };
    for (const auto& s : storms.storms) {
        const double r_max = std::max(s.radius_max_wind_m, 0.5 * cell);
        for (std::size_t k = 0; k < n; ++k) {
            const double x = g.origin_east_m + (static_cast<double>(k % g.nx) + 0.5) * g.spacing_east_m - s.east_m;
            const double y = g.origin_north_m + (static_cast<double>(k / g.nx) + 0.5) * g.spacing_north_m - s.north_m;
            const double r = std::hypot(x, y);
            h.wind_m_s[k] = std::max(h.wind_m_s[k], r < 0.5 * cell ? s.max_wind_m_s : cyclone_wind_m_s(s.max_wind_m_s, r_max, r, c.decay_exponent));
            h.rain_mm_h[k] = std::max(h.rain_mm_h[k], s.core_rain_rate_mm_h * std::exp(-r / std::max(c.rain_scale_m, cell)));
            if (coastal(k) && r <= std::max(c.surge_reach_m, 1.5 * cell))
                h.surge_m[k] = std::max(h.surge_m[k], s.surge_estimate_m * std::exp(-r / std::max(0.5 * c.surge_reach_m, cell)));
        }
    }
    return h;
}

void add_storm_vortices(std::vector<double>& u, std::vector<double>& v, const StormSystemsState& storms, const TransportGrid2D& g, double x_decay) {
    const double sign = g.geographic_anchor_latitude_deg >= 0.0 ? 1.0 : -1.0;   // cyclonic: counter-clockwise in the north
    for (const auto& storm : storms.storms) {
        const double r_max = std::max(storm.radius_max_wind_m, 0.5 * std::min(g.spacing_east_m, g.spacing_north_m));
        for (std::size_t j = 0; j < g.ny; ++j)
            for (std::size_t i = 0; i < g.nx; ++i) {
                const double x = g.origin_east_m + (static_cast<double>(i) + 0.5) * g.spacing_east_m - storm.east_m;
                const double y = g.origin_north_m + (static_cast<double>(j) + 0.5) * g.spacing_north_m - storm.north_m;
                const double r = std::hypot(x, y);
                if (r < 1.0 || r > 1.5e6) continue;
                const double vt = cyclone_wind_m_s(storm.max_wind_m_s, r_max, r, x_decay);
                const std::size_t k = j * g.nx + i;
                u[k] += -sign * vt * y / r;
                v[k] += sign * vt * x / r;
            }
    }
}

double ground_hail_cm(double hail, double freezing_level_m, double full_m, double none_m) {
    require(none_m > full_m, "hail melting levels: none_m must lie above full_m");
    if (!(hail > 0.0) || !std::isfinite(freezing_level_m)) return std::max(0.0, hail);
    return hail * std::clamp((none_m - freezing_level_m) / (none_m - full_m), 0.0, 1.0);
}

// ---- Warnings ------------------------------------------------------------------------------------------------------

int warning_level(const std::array<double, 3>& p, double min_probability) {
    for (int level = 3; level >= 1; --level)
        if (p[static_cast<std::size_t>(level - 1)] >= min_probability) return level;
    return 0;
}

std::vector<WarningRegion> warning_regions(const std::vector<int>& levels, const std::vector<double>& values, const TransportGrid2D& g) {
    const std::size_t n = g.nx * g.ny;
    require(levels.size() == n && values.size() == n, "warning regions need one level and value per cell");
    std::vector<WarningRegion> out;
    std::vector<unsigned char> seen(n, 0);
    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < n; ++start) {
        if (seen[start] || levels[start] < 1) continue;
        WarningRegion r;
        stack.assign(1, start);
        seen[start] = 1;
        while (!stack.empty()) {
            const std::size_t k = stack.back();
            stack.pop_back();
            r.cells.push_back(k);
            r.level = std::max(r.level, levels[k]);
            const std::size_t x = k % g.nx, y = k / g.nx;
            const std::size_t neighbours[4] = {x > 0 ? k - 1 : n, x + 1 < g.nx ? k + 1 : n, y > 0 ? k - g.nx : n, y + 1 < g.ny ? k + g.nx : n};
            for (std::size_t m : neighbours)
                if (m < n && !seen[m] && levels[m] == levels[start]) { seen[m] = 1; stack.push_back(m); }
        }
        std::sort(r.cells.begin(), r.cells.end());
        r.peak_cell = r.cells.front();
        for (std::size_t k : r.cells)
            if (values[k] > values[r.peak_cell]) r.peak_cell = k;
        r.peak_value = values[r.peak_cell];
        r.area_km2 = static_cast<double>(r.cells.size()) * g.spacing_east_m * g.spacing_north_m / 1.0e6;
        out.push_back(r);
    }
    std::stable_sort(out.begin(), out.end(), [](const WarningRegion& a, const WarningRegion& b) { return a.level != b.level ? a.level > b.level : a.area_km2 > b.area_km2; });
    return out;
}

// ---- Verification --------------------------------------------------------------------------------------------------

SkillScores score_forecast(const std::vector<double>& mean, const std::vector<double>& spread, const std::vector<double>& truth,
                           const std::vector<double>& clim, const std::vector<double>& persistence) {
    const std::size_t n = truth.size();
    require(n > 0 && mean.size() == n && spread.size() == n && clim.size() == n && persistence.size() == n, "forecast scores need equal, non-empty arrays");
    double se = 0.0, ss = 0.0, sc = 0.0, sp = 0.0, num = 0.0, da = 0.0, db = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        se += (mean[i] - truth[i]) * (mean[i] - truth[i]);
        ss += spread[i] * spread[i];
        sc += (clim[i] - truth[i]) * (clim[i] - truth[i]);
        sp += (persistence[i] - truth[i]) * (persistence[i] - truth[i]);
        const double a = mean[i] - clim[i], b = truth[i] - clim[i];
        num += a * b;
        da += a * a;
        db += b * b;
    }
    SkillScores s;
    const double m = static_cast<double>(n);
    s.rmse = std::sqrt(se / m);
    s.spread = std::sqrt(ss / m);
    s.climatology_rmse = std::sqrt(sc / m);
    s.persistence_rmse = std::sqrt(sp / m);
    s.skill_vs_climatology = sc > 0.0 ? 1.0 - se / sc : 0.0;
    s.anomaly_correlation = da > 0.0 && db > 0.0 ? num / std::sqrt(da * db) : 0.0;
    return s;
}

std::array<double, 2> brier_score(const std::vector<double>& p, const std::vector<unsigned char>& o) {
    require(!p.empty() && p.size() == o.size(), "Brier scores need equal, non-empty arrays");
    double bs = 0.0, f = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double x = o[i] ? 1.0 : 0.0;
        bs += (p[i] - x) * (p[i] - x);
        f += x;
    }
    const double m = static_cast<double>(p.size());
    bs /= m;
    f /= m;
    const double reference = f * (1.0 - f);
    return {bs, reference > 0.0 ? 1.0 - bs / reference : 0.0};
}

}  // namespace terraflux::weather
