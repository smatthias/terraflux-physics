#include "terraflux/weather/coastal_flooding.hpp"

#include "terraflux/weather/sea_level.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNone = -std::numeric_limits<double>::infinity();

void require_range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(std::string("coastal ") + name + " is out of range");
}

// The four neighbours of a cell (count = none) and the distance to each.
struct Neighbours {
    std::size_t cell[4];
    double distance[4];
};

Neighbours neighbours_of(std::size_t i, const TransportGrid2D& g) {
    const std::size_t count = g.nx * g.ny, x = i % g.nx, y = i / g.nx;
    return {{x + 1 < g.nx ? i + 1 : count, x > 0 ? i - 1 : count, y + 1 < g.ny ? i + g.nx : count, y > 0 ? i - g.nx : count},
            {g.spacing_east_m, g.spacing_east_m, g.spacing_north_m, g.spacing_north_m}};
}

std::vector<double> channel_depths(const CoastalFloodConfig& c) {
    std::vector<double> depth(c.elevation_m.size(), 0.0);
    for (const auto& r : c.rivers) for (std::size_t k : r.cells) depth[k] = std::max(depth[k], r.channel_depth_m);
    return depth;
}

}  // namespace

void validate_coastal_config(const CoastalFloodConfig& c) {
    validate_transport_grid(c.grid);
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (c.elevation_m.size() != count || c.sea.size() != count || c.coast_type.size() != count || c.defence_crest_m.size() != count || c.subsidence_m_yr.size() != count ||
        c.population.size() != count || c.region.size() != count) {
        throw std::invalid_argument("coastal arrays must match the grid");
    }
    std::size_t sea = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(c.elevation_m[i]) || std::abs(c.elevation_m[i]) > 10000.0) throw std::invalid_argument("coastal elevations must be finite and within +-10 km");
        if (c.sea[i] > 1) throw std::invalid_argument("coastal sea mask must be binary");
        sea += c.sea[i];
        if (c.coast_type[i] > 2) throw std::invalid_argument("coastal coast types are 0 rock, 1 sandy, 2 muddy");
        if (!std::isfinite(c.defence_crest_m[i]) || std::abs(c.defence_crest_m[i]) > 10000.0) throw std::invalid_argument("coastal defence crests must be finite");
        if (!std::isfinite(c.subsidence_m_yr[i]) || std::abs(c.subsidence_m_yr[i]) > 0.1) throw std::invalid_argument("coastal subsidence must lie within +-0.1 m/yr");
        if (!std::isfinite(c.population[i]) || c.population[i] < 0.0) throw std::invalid_argument("coastal population must be finite and non-negative");
        if (c.region[i] < -1 || c.region[i] >= static_cast<int>(c.region_names.size())) throw std::invalid_argument("coastal region index out of range");
        if (c.sea[i] && c.population[i] > 0.0) throw std::invalid_argument("coastal population lives on land");
    }
    if (sea == 0) throw std::invalid_argument("coastal domain needs a sea");
    for (const auto& r : c.rivers) {
        if (r.cells.empty()) throw std::invalid_argument("coastal river " + r.name + " has no cells");
        for (std::size_t k = 0; k < r.cells.size(); ++k) {
            const std::size_t i = r.cells[k];
            if (i >= count || c.sea[i]) throw std::invalid_argument("coastal river " + r.name + " must run over land cells of the grid");
            if (k > 0) {
                const auto n = neighbours_of(r.cells[k - 1], c.grid);
                if (std::find(std::begin(n.cell), std::end(n.cell), i) == std::end(n.cell)) throw std::invalid_argument("coastal river " + r.name + " must be a 4-connected path");
            }
        }
        const auto mouth = neighbours_of(r.cells.front(), c.grid);
        bool at_sea = false;
        for (std::size_t n : mouth.cell) at_sea = at_sea || (n < count && c.sea[n]);
        if (!at_sea) throw std::invalid_argument("coastal river " + r.name + " must start next to the sea");
        require_range(r.discharge_m3_s, 1.0e-3, 1.0e6, "river discharge");
        require_range(r.channel_depth_m, 0.1, 100.0, "river channel depth");
        require_range(r.width_m, 1.0, 50000.0, "river width");
        require_range(r.manning_n, 0.005, 0.2, "river Manning n");
    }
    require_range(c.high_tide_m, 0.0, 10.0, "high_tide_m");
    require_range(c.surge_location_m, 0.0, 10.0, "surge_location_m");
    require_range(c.surge_scale_m, 0.01, 5.0, "surge_scale_m");
    require_range(c.design_return_period_years, 1.01, 1.0e5, "design_return_period_years");
    require_range(c.storm_wind_speed_m_s, 0.0, 90.0, "storm_wind_speed_m_s");
    require_range(c.storm_wind_toward_deg, -360.0, 360.0, "storm_wind_toward_deg");
    require_range(c.air_density_kg_m3, 0.5, 2.0, "air_density_kg_m3");
    require_range(c.drag_coefficient, 0.0, 0.01, "drag_coefficient");
    require_range(c.water_density_kg_m3, 900.0, 1100.0, "water_density_kg_m3");
    require_range(c.gravity_m_s2, 1.0, 20.0, "gravity_m_s2");
    require_range(c.minimum_setup_depth_m, 0.1, 100.0, "minimum_setup_depth_m");
    require_range(c.storm_attenuation_m_per_km, 0.0, 10.0, "storm_attenuation_m_per_km");
    require_range(c.tidal_attenuation_m_per_km, 0.0, 10.0, "tidal_attenuation_m_per_km");
    require_range(c.river_attenuation_m_per_km, 0.0, 10.0, "river_attenuation_m_per_km");
    require_range(c.active_profile_width_m, 1.0, 1.0e5, "active_profile_width_m");
    require_range(c.berm_height_m, 0.0, 50.0, "berm_height_m");
    require_range(c.closure_depth_m, 0.5, 100.0, "closure_depth_m");
    require_range(c.ocean_salinity_psu, 1.0, 45.0, "ocean_salinity_psu");
    require_range(c.salt_limit_psu, 0.01, c.ocean_salinity_psu * 0.999, "salt_limit_psu");
    require_range(c.estuary_dispersion_m2_s, 0.0, 1.0e5, "estuary_dispersion_m2_s");
    require_range(c.island_max_area_km2, 0.0, 1.0e7, "island_max_area_km2");
}

double manning_normal_depth_m(double q, double width, double slope, double n) {
    if (!(q >= 0.0) || !(width > 0.0) || !(slope > 0.0) || !(n > 0.0)) throw std::invalid_argument("Manning depth needs a discharge, a width, a slope and a roughness");
    return std::pow(q * n / (width * std::sqrt(slope)), 0.6);   // wide channel: hydraulic radius = depth
}

std::vector<double> wind_setup_field(const CoastalFloodConfig& c, double rise) {
    const auto& g = c.grid;
    const std::size_t count = g.nx * g.ny;
    std::vector<double> eta(count, 0.0);
    const double tau = c.air_density_kg_m3 * c.drag_coefficient * c.storm_wind_speed_m_s * c.storm_wind_speed_m_s;
    if (tau <= 0.0) return eta;
    const double theta = c.storm_wind_toward_deg * kPi / 180.0;
    const double wx = std::sin(theta), wy = std::cos(theta);
    const double ax = std::abs(wx) > 1.0e-12 ? std::abs(wx) : 0.0, ay = std::abs(wy) > 1.0e-12 ? std::abs(wy) : 0.0;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < count; ++i) if (c.sea[i]) order.push_back(i);
    auto along = [&](std::size_t i) { return (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m * wx + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m * wy; };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return along(a) < along(b); });
    const double k = 2.0 * tau / (c.water_density_kg_m3 * c.gravity_m_s2);
    for (std::size_t i : order) {
        const double depth = std::max(c.minimum_setup_depth_m, -c.elevation_m[i] + c.high_tide_m + rise);
        const std::size_t x = i % g.nx, y = i / g.nx;
        // Exact integration over each upwind step at this cell's depth: (H + eta)^2 grows by 2 tau ds / (rho g).
        auto from = [&](bool exists, std::size_t up, double step) {
            const double upstream = exists && c.sea[up] ? eta[up] : 0.0;   // open boundary or land: no setup yet
            return std::sqrt((depth + upstream) * (depth + upstream) + k * step) - depth;
        };
        double sum = 0.0, weight = 0.0;
        if (ax > 0.0) {
            const bool exists = wx > 0.0 ? x > 0 : x + 1 < g.nx;
            sum += ax * from(exists, exists ? (wx > 0.0 ? i - 1 : i + 1) : 0, g.spacing_east_m * ax);
            weight += ax;
        }
        if (ay > 0.0) {
            const bool exists = wy > 0.0 ? y > 0 : y + 1 < g.ny;
            sum += ay * from(exists, exists ? (wy > 0.0 ? i - g.nx : i + g.nx) : 0, g.spacing_north_m * ay);
            weight += ay;
        }
        eta[i] = weight > 0.0 ? sum / weight : 0.0;
    }
    return eta;
}

std::vector<double> flood_levels(const CoastalFloodConfig& c, const std::vector<double>& sea_level, const std::vector<double>& ground, const std::vector<double>& crest,
                                 double attenuation, double river_attenuation) {
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (sea_level.size() != count || ground.size() != count || (!crest.empty() && crest.size() != count)) throw std::invalid_argument("flood levels need per-cell sea levels and ground");
    const std::vector<double> channel = channel_depths(c);
    std::vector<double> level(count, kNone);
    using Item = std::pair<double, std::size_t>;
    std::priority_queue<Item> open;   // highest level first: every cell gets the highest level that can reach it
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        level[i] = sea_level[i];
        open.push({level[i], i});
    }
    while (!open.empty()) {
        const auto [value, i] = open.top();
        open.pop();
        if (value < level[i]) continue;
        const auto n = neighbours_of(i, c.grid);
        for (int k = 0; k < 4; ++k) {
            const std::size_t j = n.cell[k];
            if (j >= count || c.sea[j]) continue;
            const bool along_channel = channel[j] > 0.0 && (channel[i] > 0.0 || c.sea[i]);
            const double incoming = value - (along_channel ? river_attenuation : attenuation) * n.distance[k] / 1000.0;
            // Channels carry the water along their bed and through the defence line (no sluices); land needs the
            // level above the ground and above its defence.
            double barrier = channel[j] > 0.0 ? ground[j] - channel[j] : ground[j];
            if (!crest.empty() && channel[j] <= 0.0 && crest[j] > ground[j]) barrier = std::max(barrier, crest[j]);
            if (incoming > barrier && incoming > level[j]) {
                level[j] = incoming;
                open.push({incoming, j});
            }
        }
    }
    return level;
}

namespace {

struct Surfaces {
    std::vector<double> ground, crest;
};

Surfaces sunk_surfaces(const CoastalFloodConfig& c, double years) {
    const std::size_t count = c.elevation_m.size();
    Surfaces s;
    s.ground.resize(count);
    s.crest.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double sink = c.sea[i] ? 0.0 : c.subsidence_m_yr[i] * years;
        s.ground[i] = c.elevation_m[i] - sink;
        s.crest[i] = c.defence_crest_m[i] > c.elevation_m[i] ? c.defence_crest_m[i] - sink : s.ground[i];
    }
    return s;
}

std::vector<double> sea_levels(const CoastalFloodConfig& c, double base, const std::vector<double>* setup) {
    std::vector<double> level(c.elevation_m.size(), 0.0);
    for (std::size_t i = 0; i < level.size(); ++i) if (c.sea[i]) level[i] = base + (setup ? (*setup)[i] : 0.0);
    return level;
}

double design_surge(const CoastalFloodConfig& c) { return gumbel_return_level(c.surge_location_m, c.surge_scale_m, c.design_return_period_years); }

}  // namespace

CoastalBaseline coastal_baseline(const CoastalFloodConfig& c) {
    validate_coastal_config(c);
    const std::size_t count = c.elevation_m.size();
    const Surfaces s = sunk_surfaces(c, 0.0);
    const auto permanent = flood_levels(c, sea_levels(c, c.high_tide_m, nullptr), s.ground, s.crest, c.tidal_attenuation_m_per_km, c.tidal_attenuation_m_per_km);
    const auto setup = wind_setup_field(c, 0.0);
    const auto storm = flood_levels(c, sea_levels(c, c.high_tide_m + design_surge(c), &setup), s.ground, s.crest, c.storm_attenuation_m_per_km, c.river_attenuation_m_per_km);
    CoastalBaseline b;
    b.permanent.assign(count, 0);
    b.storm.assign(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        if (c.sea[i]) continue;
        b.permanent[i] = permanent[i] > s.ground[i] ? 1 : 0;
        b.storm[i] = storm[i] > s.ground[i] ? 1 : 0;
    }
    return b;
}

CoastalImpact assess_coastal_impact(const CoastalFloodConfig& c, double rise, double years) {
    return assess_coastal_impact(c, rise, years, coastal_baseline(c));
}

CoastalImpact assess_coastal_impact(const CoastalFloodConfig& c, double rise, double years, const CoastalBaseline& baseline) {
    validate_coastal_config(c);
    if (!std::isfinite(rise) || std::abs(rise) > 100.0 || !std::isfinite(years) || years < 0.0 || years > 10000.0) throw std::invalid_argument("coastal rise or time out of range");
    const auto& g = c.grid;
    const std::size_t count = g.nx * g.ny;
    if (baseline.permanent.size() != count || baseline.storm.size() != count) throw std::invalid_argument("coastal baseline does not match the grid");
    const double area = g.spacing_east_m * g.spacing_north_m, km2 = area / 1.0e6;
    const Surfaces s = sunk_surfaces(c, years);
    const std::vector<double> channel = channel_depths(c);

    CoastalImpact out;
    out.regional_rise_m = rise;
    out.years_since_reference = years;
    out.relative_rise_m.assign(count, rise);
    for (std::size_t i = 0; i < count; ++i) if (!c.sea[i]) out.relative_rise_m[i] = rise + c.subsidence_m_yr[i] * years;

    // Permanent inundation at high water, with and without the defences.
    const auto high = sea_levels(c, c.high_tide_m + rise, nullptr);
    const auto permanent = flood_levels(c, high, s.ground, s.crest, c.tidal_attenuation_m_per_km, c.tidal_attenuation_m_per_km);
    const auto undefended = flood_levels(c, high, s.ground, {}, c.tidal_attenuation_m_per_km, c.tidal_attenuation_m_per_km);
    // The design storm: surge + wind setup on the sea, attenuated inland, pushed up the rivers.
    out.setup_m = wind_setup_field(c, rise);
    const auto storm = flood_levels(c, sea_levels(c, c.high_tide_m + rise + design_surge(c), &out.setup_m), s.ground, s.crest, c.storm_attenuation_m_per_km, c.river_attenuation_m_per_km);

    out.storm_level_m.assign(count, 0.0);
    out.storm_depth_m.assign(count, 0.0);
    out.storm_today.assign(count, 0);
    out.inundation_depth_m.assign(count, 0.0);
    out.erosion_retreat_m.assign(count, 0.0);
    out.lost_fraction.assign(count, 0.0);
    out.protected_low.assign(count, 0);
    out.river_salt.assign(count, 0);
    out.design_flood_return_years.assign(count, 0.0);
    std::vector<unsigned char> wet(count, 0);   // water at high tide: the sea and permanently flooded land
    for (std::size_t i = 0; i < count; ++i) {
        if (c.sea[i]) { wet[i] = 1; out.storm_level_m[i] = storm[i]; continue; }
        out.storm_today[i] = baseline.storm[i];
        if (permanent[i] > s.ground[i]) {
            wet[i] = 1;
            if (!baseline.permanent[i]) { out.inundation_depth_m[i] = permanent[i] - s.ground[i]; out.lost_fraction[i] = 1.0; }
        } else if (undefended[i] > s.ground[i]) {
            out.protected_low[i] = 1;
        }
        if (storm[i] > s.ground[i]) { out.storm_level_m[i] = storm[i]; out.storm_depth_m[i] = storm[i] - s.ground[i]; }
    }
    // Sandy shores retreat (Bruun) along the new shoreline; the exposed edges times the retreat are lost.
    double coastal_setup = 0.0;
    std::size_t coastal_sea = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto n = neighbours_of(i, g);
        if (c.sea[i]) {
            bool shore = false;
            for (std::size_t j : n.cell) shore = shore || (j < count && !c.sea[j]);
            if (shore) { coastal_setup += out.setup_m[i]; ++coastal_sea; }
            out.max_setup_m = std::max(out.max_setup_m, out.setup_m[i]);
            continue;
        }
        bool coastal = false;
        for (std::size_t j : n.cell) coastal = coastal || (j < count && c.sea[j]);
        if (coastal) out.design_flood_return_years[i] = return_period_after_rise(c.surge_location_m, c.surge_scale_m, c.design_return_period_years, out.relative_rise_m[i]);
        if (wet[i] || c.coast_type[i] != static_cast<unsigned char>(CoastType::sandy) || out.relative_rise_m[i] <= 0.0) continue;
        double exposed = 0.0;
        for (int k = 0; k < 4; ++k) {
            const std::size_t j = n.cell[k];
            if (j < count && wet[j]) exposed += k < 2 ? g.spacing_north_m : g.spacing_east_m;   // east/west faces are dy long
        }
        if (exposed <= 0.0) continue;
        const double retreat = bruun_retreat_m(out.relative_rise_m[i], c.active_profile_width_m, c.berm_height_m, c.closure_depth_m);
        out.erosion_retreat_m[i] = retreat;
        out.lost_fraction[i] = std::min(1.0, retreat * exposed / area);
    }
    out.mean_coastal_setup_m = coastal_sea ? coastal_setup / static_cast<double>(coastal_sea) : 0.0;

    // Regions.
    out.regions.resize(c.region_names.size());
    std::vector<double> region_rise(c.region_names.size(), 0.0);
    std::vector<std::size_t> region_coast(c.region_names.size(), 0);
    for (std::size_t r = 0; r < out.regions.size(); ++r) out.regions[r].name = c.region_names[r];
    for (std::size_t i = 0; i < count; ++i) {
        if (c.sea[i]) continue;
        const double lost = out.lost_fraction[i] * km2, pop = c.population[i];
        const bool inundated = out.inundation_depth_m[i] > 0.0, eroded = !inundated && out.lost_fraction[i] > 0.0;
        const bool flooded = out.storm_depth_m[i] > 0.0;
        out.land_km2 += km2;
        out.lost_km2 += lost;
        out.inundated_km2 += inundated ? km2 : 0.0;
        out.eroded_km2 += eroded ? lost : 0.0;
        out.storm_flooded_km2 += flooded ? km2 : 0.0;
        out.newly_storm_flooded_km2 += flooded && !baseline.storm[i] ? km2 : 0.0;
        out.protected_low_km2 += out.protected_low[i] ? km2 : 0.0;
        out.population += pop;
        out.people_on_lost_land += pop * out.lost_fraction[i];
        out.people_storm_flooded += flooded ? pop : 0.0;
        out.people_protected_low += out.protected_low[i] ? pop : 0.0;
        const int r = c.region[i];
        if (r < 0) continue;
        auto& R = out.regions[static_cast<std::size_t>(r)];
        R.land_km2 += km2;
        R.lost_km2 += lost;
        R.inundated_km2 += inundated ? km2 : 0.0;
        R.eroded_km2 += eroded ? lost : 0.0;
        R.storm_flooded_km2 += flooded ? km2 : 0.0;
        R.newly_storm_flooded_km2 += flooded && !baseline.storm[i] ? km2 : 0.0;
        R.protected_low_km2 += out.protected_low[i] ? km2 : 0.0;
        R.population += pop;
        R.people_on_lost_land += pop * out.lost_fraction[i];
        R.people_storm_flooded += flooded ? pop : 0.0;
        R.people_protected_low += out.protected_low[i] ? pop : 0.0;
        R.max_storm_depth_m = std::max(R.max_storm_depth_m, out.storm_depth_m[i]);
        if (out.design_flood_return_years[i] > 0.0) { region_rise[static_cast<std::size_t>(r)] += out.relative_rise_m[i]; ++region_coast[static_cast<std::size_t>(r)]; }
    }
    for (std::size_t r = 0; r < out.regions.size(); ++r) {
        auto& R = out.regions[r];
        R.mean_relative_rise_m = region_coast[r] ? region_rise[r] / static_cast<double>(region_coast[r]) : rise;
        R.design_flood_return_years = return_period_after_rise(c.surge_location_m, c.surge_scale_m, c.design_return_period_years, R.mean_relative_rise_m);
    }

    // Rivers: the sea's reach up each channel against the river's own level, salt intrusion, flooded banks.
    for (const auto& river : c.rivers) {
        CoastalRiverImpact R;
        R.name = river.name;
        const std::size_t n = river.cells.size();
        std::vector<double> x(n, 0.0);
        for (std::size_t k = 1; k < n; ++k) {
            const std::size_t a = river.cells[k - 1], b = river.cells[k];
            x[k] = x[k - 1] + ((a % g.nx) != (b % g.nx) ? g.spacing_east_m : g.spacing_north_m);
        }
        const double length = n > 1 ? x[n - 1] : g.spacing_east_m;
        R.length_km = length / 1000.0;
        // The river's own level: bed + Manning normal depth on the local bed slope (over +-3 cells, at least 1e-5).
        auto local_slope = [&](std::size_t k) {
            const std::size_t a = k >= 3 ? k - 3 : 0, b = std::min(n - 1, k + 3);
            return b > a ? std::max(1.0e-5, (s.ground[river.cells[b]] - s.ground[river.cells[a]]) / (x[b] - x[a])) : 1.0e-5;
        };
        double salt_stretch = 0.0;
        bool salt_open = true;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t i = river.cells[k];
            const double bed = s.ground[i] - river.channel_depth_m, own = bed + manning_normal_depth_m(river.discharge_m3_s, river.width_m, local_slope(k), river.manning_n);
            if (permanent[i] > own) R.tidal_reach_km = x[k] / 1000.0;
            if (storm[i] > own) R.surge_reach_km = x[k] / 1000.0;
            if (salt_open && permanent[i] > bed) salt_stretch = x[k]; else salt_open = false;
        }
        const std::size_t mouth = river.cells.front();
        R.mouth_storm_level_m = storm[mouth];
        const double mouth_depth = std::max(0.0, c.high_tide_m + rise - (s.ground[mouth] - river.channel_depth_m));
        const double intrusion = c.estuary_dispersion_m2_s * river.width_m * mouth_depth * std::log(c.ocean_salinity_psu / c.salt_limit_psu) / river.discharge_m3_s;
        R.salt_intrusion_km = std::min(intrusion, salt_stretch) / 1000.0;   // salt only where the bed lies below the high water
        for (std::size_t k = 0; k < n; ++k) if (x[k] <= R.salt_intrusion_km * 1000.0) out.river_salt[river.cells[k]] = 1;
        // Land along the channel under the storm.
        std::vector<unsigned char> counted(count, 0);
        for (std::size_t i : river.cells) {
            const auto nb = neighbours_of(i, g);
            std::size_t cand[5] = {i, nb.cell[0], nb.cell[1], nb.cell[2], nb.cell[3]};
            for (std::size_t j : cand) {
                if (j >= count || c.sea[j] || counted[j]) continue;
                counted[j] = 1;
                if (out.storm_depth_m[j] > 0.0) R.flooded_bank_km2 += km2;
            }
        }
        out.rivers.push_back(R);
    }

    // Islands: land masses (8-connected) up to island_max_area_km2.
    std::vector<int> label(count, -1);
    int next = 0;
    for (std::size_t start = 0; start < count; ++start) {
        if (c.sea[start] || label[start] >= 0) continue;
        std::vector<std::size_t> stack{start}, members;
        label[start] = next;
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            members.push_back(i);
            const std::size_t x = i % g.nx, y = i / g.nx;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || (dx < 0 && x == 0) || (dy < 0 && y == 0) || (dx > 0 && x + 1 >= g.nx) || (dy > 0 && y + 1 >= g.ny)) continue;
                const std::size_t j = (y + dy) * g.nx + (x + dx);
                if (c.sea[j] || label[j] >= 0) continue;
                label[j] = next;
                stack.push_back(j);
            }
        }
        const double land = static_cast<double>(members.size()) * km2;
        if (land <= c.island_max_area_km2) {
            CoastalIsland isl;
            isl.id = next;
            isl.land_km2 = land;
            std::vector<int> votes(c.region_names.size(), 0);
            isl.highest_m = -1.0e9;
            double flooded = 0.0;
            for (std::size_t i : members) {
                isl.lost_km2 += out.lost_fraction[i] * km2;
                flooded += out.storm_depth_m[i] > 0.0 ? km2 : 0.0;
                isl.population += c.population[i];
                if (c.elevation_m[i] > isl.highest_m) { isl.highest_m = c.elevation_m[i]; isl.top_cell = i; }
                if (c.region[i] >= 0) ++votes[static_cast<std::size_t>(c.region[i])];
            }
            isl.lost_fraction = isl.lost_km2 / land;
            isl.storm_flooded_fraction = flooded / land;
            if (!votes.empty()) {
                const auto best = std::max_element(votes.begin(), votes.end());
                if (*best > 0) isl.region = static_cast<int>(best - votes.begin());
            }
            out.islands.push_back(isl);
        }
        ++next;
    }
    return out;
}

}  // namespace terraflux::weather
