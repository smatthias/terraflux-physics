// terraflux-run-coastal-flooding: where a rising sea takes land, floods coasts and pushes up the rivers.
//
// Reads a terraflux.coastal.flooding-run/v1 request: the sea-level scenarios (the warming and Greenland of a
// polar-melt timeline, or explicit curves) with the sea-level budget, a coast (per-cell values or a synthetic
// coast built from delta, estuary, barrier, cliff and island features), flood defences, land subsidence,
// population, named regions and rivers, and the design storm. For every year from today to the end it assesses
// the impacts (coastal_flooding.hpp) and writes a viewer timeline: maps of the map scenario at the output years
// as frames, and a `coast` block with the sea-level budget, per region, river and island series for every
// scenario, the events (when today's design flood becomes annual, when a tenth of the land is lost, when the
// defences are overtopped, when an island loses half its land) and per-cell "year lost" maps.
#include "terraflux/weather/coastal_flooding.hpp"
#include "terraflux/weather/sea_level.hpp"
#include "terraflux/weather/synthetic_terrain.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace terraflux::weather;

const Json& required(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) throw std::invalid_argument(std::string("run request is missing field: ") + key);
    return object.at(key);
}

double required_number(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_number()) throw std::invalid_argument(std::string("run request field must be numeric: ") + key);
    const double result = value.get<double>();
    if (!std::isfinite(result)) throw std::invalid_argument(std::string("run request field must be finite: ") + key);
    return result;
}

double optional_number(const Json& object, const char* key, double fallback) {
    if (!object.is_object() || !object.contains(key)) return fallback;
    return required_number(object, key);
}

std::string required_string(const Json& object, const char* key) {
    const Json& value = required(object, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) throw std::invalid_argument(std::string("run request field must be a non-empty string: ") + key);
    return value.get<std::string>();
}

int required_int(const Json& object, const char* key, int lo, int hi) {
    const Json& value = required(object, key);
    if (!value.is_number_integer() || value.get<long long>() < lo || value.get<long long>() > hi) throw std::invalid_argument(std::string("run request field is not an integer within range: ") + key);
    return static_cast<int>(value.get<long long>());
}

template <typename T>
void read_into(const Json& section, const char* key, T& target) { target = static_cast<T>(optional_number(section, key, static_cast<double>(target))); }

double tidy(double v, int digits = 5) {
    if (v == 0.0 || !std::isfinite(v)) return v;
    const int exponent = static_cast<int>(std::floor(std::log10(std::abs(v)))) - (digits - 1);
    if (exponent < 0) { const double inverse = std::pow(10.0, -exponent); return std::round(v * inverse) / inverse; }
    const double scale = std::pow(10.0, exponent);
    return std::round(v / scale) * scale;
}

Json tidy_array(const std::vector<double>& values, int digits = 5) {
    Json out = Json::array();
    for (double v : values) out.push_back(std::isfinite(v) ? Json(tidy(v, digits)) : Json(nullptr));
    return out;
}

// Piecewise-linear curve in calendar years, held constant beyond its ends.
struct Curve {
    std::vector<double> years, values;
    double at(double year) const {
        if (year <= years.front()) return values.front();
        if (year >= years.back()) return values.back();
        std::size_t k = 1;
        while (years[k] < year) ++k;
        const double w = (year - years[k - 1]) / (years[k] - years[k - 1]);
        return values[k - 1] + (values[k] - values[k - 1]) * w;
    }
};

Curve parse_curve(const Json& j, const char* what) {
    if (!j.is_array() || j.empty()) throw std::invalid_argument(std::string(what) + " must be a non-empty array of [year, value]");
    Curve c;
    for (const Json& p : j) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) throw std::invalid_argument(std::string(what) + " points must be [year, value]");
        const double y = p[0].get<double>(), v = p[1].get<double>();
        if (!std::isfinite(y) || !std::isfinite(v) || (!c.years.empty() && y <= c.years.back())) throw std::invalid_argument(std::string(what) + " years must increase");
        c.years.push_back(y);
        c.values.push_back(v);
    }
    return c;
}

struct SeaLevelScenario {
    std::string name;
    Curve warming, greenland;
};

// ---- The coast ---------------------------------------------------------------------------------------

struct Coast {
    CoastalFloodConfig config;
    Json provenance;
};

std::size_t cell_of(const TransportGrid2D& g, double east, double north) {
    const double fx = (east - g.origin_east_m) / g.spacing_east_m, fy = (north - g.origin_north_m) / g.spacing_north_m;
    if (fx < 0.0 || fy < 0.0 || fx >= static_cast<double>(g.nx) || fy >= static_cast<double>(g.ny)) return g.nx * g.ny;
    return static_cast<std::size_t>(fy) * g.nx + static_cast<std::size_t>(fx);
}

// The sea: cells below 0 m connected (4-neighbour) to the grid edge; enclosed depressions stay land.
std::vector<unsigned char> connected_sea(const std::vector<double>& z, std::size_t nx, std::size_t ny) {
    const std::size_t count = nx * ny;
    std::vector<unsigned char> sea(count, 0);
    std::vector<std::size_t> stack;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t x = i % nx, y = i / nx;
        if ((x == 0 || y == 0 || x + 1 == nx || y + 1 == ny) && z[i] < 0.0) { sea[i] = 1; stack.push_back(i); }
    }
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        const std::size_t x = i % nx, y = i / nx;
        const std::size_t n[4] = {x + 1 < nx ? i + 1 : count, x > 0 ? i - 1 : count, y + 1 < ny ? i + nx : count, y > 0 ? i - nx : count};
        for (std::size_t j : n) if (j < count && !sea[j] && z[j] < 0.0) { sea[j] = 1; stack.push_back(j); }
    }
    return sea;
}

int region_index(CoastalFloodConfig& c, const std::string& name) {
    const auto it = std::find(c.region_names.begin(), c.region_names.end(), name);
    if (it != c.region_names.end()) return static_cast<int>(it - c.region_names.begin());
    c.region_names.push_back(name);
    return static_cast<int>(c.region_names.size()) - 1;
}

unsigned char parse_coast_type(const std::string& s) {
    if (s == "rock") return 0;
    if (s == "sandy") return 1;
    if (s == "muddy") return 2;
    throw std::invalid_argument("coast_type must be rock, sandy or muddy");
}

// A synthetic coast: a straight east-west shoreline with land to the north and a shelf to the south, reshaped by
// features (each a named region): a protruding delta with polders and dikes, a funnel estuary with a city on
// its banks, a barrier island chain with a lagoon, cliffs, and low islands offshore.
void build_synthetic_coast(const Json& j, CoastalFloodConfig& c, std::vector<double>& density, Json& provenance) {
    const auto& g = c.grid;
    const std::size_t count = g.nx * g.ny;
    const double coast_y = required_number(j, "coastline_north_m");
    const Json& shelf = required(j, "shelf");
    const double d0 = optional_number(shelf, "depth_at_coast_m", 3.0), shelf_slope = optional_number(shelf, "slope", 4.0e-4), dmax = optional_number(shelf, "max_depth_m", 100.0);
    const Json& land = required(j, "land");
    const double z0 = optional_number(land, "elevation_at_coast_m", 3.0), land_slope = optional_number(land, "slope", 1.0e-3), zmax = optional_number(land, "max_elevation_m", 300.0);
    const double roughness = optional_number(j, "roughness_m", 0.2);
    const auto seed = static_cast<std::uint32_t>(optional_number(j, "seed", 1.0));
    const double rural = optional_number(j, "rural_population_per_km2", 30.0);
    if (d0 < 0.5 || shelf_slope < 0.0 || dmax < d0 || z0 < 0.5 || land_slope < 0.0 || zmax < z0 || roughness < 0.0 || roughness > 5.0 || rural < 0.0) throw std::invalid_argument("synthetic_coast shelf, land or roughness out of range");
    const double cell_km2 = g.spacing_east_m * g.spacing_north_m / 1.0e6;
    auto east = [&](std::size_t i) { return g.origin_east_m + (static_cast<double>(i % g.nx) + 0.5) * g.spacing_east_m; };
    auto north = [&](std::size_t i) { return g.origin_north_m + (static_cast<double>(i / g.nx) + 0.5) * g.spacing_north_m; };
    std::vector<unsigned char> sea(count, 0);
    std::vector<double> z(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double y = north(i);
        if (y < coast_y) { sea[i] = 1; z[i] = -std::min(dmax, d0 + shelf_slope * (coast_y - y)); }
        else { z[i] = std::min(zmax, z0 + land_slope * (y - coast_y)) + roughness * terrain_noise(i, seed); density[i] = rural; }
    }
    auto neighbours_sea = [&](std::size_t i) {
        const std::size_t x = i % g.nx, y = i / g.nx;
        const std::size_t n[4] = {x + 1 < g.nx ? i + 1 : count, x > 0 ? i - 1 : count, y + 1 < g.ny ? i + g.nx : count, y > 0 ? i - g.nx : count};
        for (std::size_t k : n) if (k < count && sea[k]) return true;
        return false;
    };
    Json features = Json::array();
    for (const Json& f : required(j, "features")) {
        const std::string kind = required_string(f, "kind"), name = required_string(f, "name");
        const int region = region_index(c, name);
        const double density_f = optional_number(f, "population_per_km2", rural);
        const double subsidence = optional_number(f, "subsidence_mm_yr", 0.0) / 1000.0;
        const unsigned char type = parse_coast_type(f.value("coast_type", kind == "cliffs" ? std::string("rock") : kind == "delta" || kind == "estuary" ? std::string("muddy") : std::string("sandy")));
        if (density_f < 0.0 || std::abs(subsidence) > 0.1) throw std::invalid_argument("feature " + name + ": population or subsidence out of range");
        auto claim = [&](std::size_t i, double elevation) {
            sea[i] = 0;
            z[i] = elevation;
            c.region[i] = region;
            c.coast_type[i] = type;
            c.subsidence_m_yr[i] = subsidence;
            density[i] = density_f;
        };
        if (kind == "delta") {
            const double x0 = required_number(f, "west_m"), x1 = required_number(f, "east_m"), protrusion = required_number(f, "protrusion_m");
            const double inland = optional_number(f, "inland_m", 40000.0), zd = optional_number(f, "elevation_m", 0.8), gradient = optional_number(f, "gradient_m_per_km", 0.05);
            const double front = optional_number(f, "front_depth_factor", 0.4), sea_dike = optional_number(f, "sea_dike_crest_m", 0.0);
            if (!(x1 > x0) || protrusion < 0.0 || inland <= 0.0 || front <= 0.0 || front > 1.0) throw std::invalid_argument("delta " + name + ": geometry out of range");
            const double xm = 0.5 * (x0 + x1), half = 0.5 * (x1 - x0);
            for (std::size_t i = 0; i < count; ++i) {
                const double x = east(i), y = north(i);
                if (x < x0 || x > x1) continue;
                const double u = (x - xm) / half, shore = coast_y - protrusion * std::sqrt(std::max(0.0, 1.0 - u * u));
                if (y >= shore && y <= coast_y + inland) {
                    // The plain blends into the surrounding land over the last quarter of its inland extent.
                    const double plain = zd + gradient * (y - shore) / 1000.0 + 0.3 * roughness * terrain_noise(i, seed + 17);
                    const double w = std::clamp((y - (coast_y + 0.75 * inland)) / (0.25 * inland), 0.0, 1.0);
                    claim(i, (1.0 - w) * plain + w * std::max(plain, z[i]));
                }
                else if (y < shore && sea[i]) z[i] = -std::min(dmax, front * (d0 + shelf_slope * (shore - y)));   // the shallow delta front
            }
            for (const Json& p : f.value("polders", Json::array())) {
                const double pe = required_number(p, "center_east_m"), pn = required_number(p, "center_north_m"), pr = required_number(p, "radius_m"), level = required_number(p, "level_m");
                const double crest = optional_number(p, "dike_crest_m", 0.0);
                for (std::size_t i = 0; i < count; ++i) {
                    if (sea[i] || c.region[i] != region) continue;
                    const double r = std::hypot(east(i) - pe, north(i) - pn);
                    if (r > pr) continue;
                    z[i] = level + 0.1 * roughness * terrain_noise(i, seed + 23);
                    if (r > pr - std::max(g.spacing_east_m, g.spacing_north_m)) c.defence_crest_m[i] = std::max(c.defence_crest_m[i], crest);
                }
            }
            if (sea_dike > 0.0) for (std::size_t i = 0; i < count; ++i) if (!sea[i] && c.region[i] == region && neighbours_sea(i)) c.defence_crest_m[i] = std::max(c.defence_crest_m[i], sea_dike);
            features.push_back({{"kind", kind}, {"name", name}, {"west_m", x0}, {"east_m", x1}, {"protrusion_m", protrusion}});
        } else if (kind == "estuary") {
            const double xc = required_number(f, "center_east_m"), length = required_number(f, "length_m"), mouth = required_number(f, "mouth_width_m");
            const double convergence = optional_number(f, "convergence_length_m", 25000.0), depth = optional_number(f, "depth_m", 12.0);
            const double bank_z = optional_number(f, "bank_elevation_m", 2.5), bank_w = optional_number(f, "bank_width_m", 12000.0), wall = optional_number(f, "wall_crest_m", 0.0);
            if (length <= 0.0 || mouth <= 0.0 || convergence <= 0.0 || depth <= 0.0 || bank_w < 0.0) throw std::invalid_argument("estuary " + name + ": geometry out of range");
            std::vector<unsigned char> channel(count, 0);
            for (std::size_t i = 0; i < count; ++i) {
                const double x = east(i), y = north(i);
                if (y < coast_y || y > coast_y + length) continue;
                const double w = mouth * std::exp(-(y - coast_y) / convergence);
                const double off = std::abs(x - xc);
                if (off <= 0.5 * std::max(w, g.spacing_east_m)) {
                    sea[i] = 1; c.region[i] = -1; density[i] = 0.0;
                    z[i] = -std::max(2.0, depth * std::sqrt(w / mouth));
                    channel[i] = 1;
                } else if (off <= 0.5 * w + bank_w) {
                    // Low banks that rise into the surrounding land over the outer half of the bank width.
                    const double bank = bank_z + 0.02 * (y - coast_y) / 1000.0 + 0.0002 * (off - 0.5 * w) + 0.3 * roughness * terrain_noise(i, seed + 29);
                    const double t = std::clamp((off - 0.5 * w - 0.5 * bank_w) / (0.5 * bank_w), 0.0, 1.0);
                    claim(i, (1.0 - t) * bank + t * std::max(bank, z[i]));
                }
            }
            if (wall > 0.0) for (std::size_t i = 0; i < count; ++i) if (!sea[i] && c.region[i] == region && neighbours_sea(i) && north(i) > coast_y + 0.3 * length) c.defence_crest_m[i] = std::max(c.defence_crest_m[i], wall);
            features.push_back({{"kind", kind}, {"name", name}, {"center_east_m", xc}, {"length_m", length}, {"mouth_width_m", mouth}});
        } else if (kind == "barrier") {
            const double x0 = required_number(f, "west_m"), x1 = required_number(f, "east_m"), offshore = required_number(f, "offshore_m");
            const double zb = optional_number(f, "barrier_elevation_m", 2.5), lagoon = optional_number(f, "lagoon_depth_m", 2.0), inlet = optional_number(f, "inlet_spacing_m", 30000.0);
            const double mainland = optional_number(f, "mainland_elevation_m", 1.6), mainland_w = optional_number(f, "mainland_width_m", 15000.0);
            if (!(x1 > x0) || offshore <= 0.0 || lagoon <= 0.0 || inlet <= 0.0) throw std::invalid_argument("barrier " + name + ": geometry out of range");
            for (std::size_t i = 0; i < count; ++i) {
                const double x = east(i), y = north(i);
                if (x < x0 || x > x1) continue;
                const double barrier_y = coast_y - offshore;
                const bool gap = std::fmod(x - x0, inlet) < g.spacing_east_m;   // one-cell tidal inlets
                if (std::abs(y - barrier_y) < 0.5 * g.spacing_north_m && !gap) claim(i, zb + 0.3 * roughness * terrain_noise(i, seed + 31));
                else if (y > barrier_y && y < coast_y) { sea[i] = 1; c.region[i] = -1; density[i] = 0.0; z[i] = -lagoon; }
                else if (y >= coast_y && y < coast_y + mainland_w) claim(i, mainland + land_slope * (y - coast_y) + 0.3 * roughness * terrain_noise(i, seed + 37));
            }
            features.push_back({{"kind", kind}, {"name", name}, {"west_m", x0}, {"east_m", x1}, {"offshore_m", offshore}});
        } else if (kind == "cliffs") {
            const double x0 = required_number(f, "west_m"), x1 = required_number(f, "east_m"), height = optional_number(f, "cliff_height_m", 25.0), inland = optional_number(f, "inland_m", 30000.0);
            if (!(x1 > x0) || height <= 0.0) throw std::invalid_argument("cliffs " + name + ": geometry out of range");
            for (std::size_t i = 0; i < count; ++i) {
                const double x = east(i), y = north(i);
                if (x < x0 || x > x1) continue;
                if (!sea[i] && y < coast_y + inland) claim(i, std::max(z[i], height + 2.0 * land_slope * (y - coast_y)) + roughness * terrain_noise(i, seed + 41));
                else if (sea[i] && y > coast_y - 20000.0) z[i] -= 15.0;   // steep nearshore
            }
            features.push_back({{"kind", kind}, {"name", name}, {"west_m", x0}, {"east_m", x1}});
        } else if (kind == "islands") {
            Json list = Json::array();
            const double shore = optional_number(f, "shore_elevation_m", 1.5);
            for (const Json& isl : required(f, "islands")) {
                const double ie = required_number(isl, "center_east_m"), in = required_number(isl, "center_north_m"), ir = required_number(isl, "radius_m"), ih = required_number(isl, "height_m");
                if (ir <= 0.0 || ih < shore) throw std::invalid_argument("island of " + name + ": the radius must be positive and the height at least the shore elevation");
                for (std::size_t i = 0; i < count; ++i) {
                    const double r = std::hypot(east(i) - ie, north(i) - in);
                    if (r > ir) continue;
                    claim(i, shore + (ih - shore) * std::pow(1.0 - r / ir, 0.7) + 0.05 * roughness * terrain_noise(i, seed + 43));
                }
                list.push_back({{"center_east_m", ie}, {"center_north_m", in}, {"radius_m", ir}, {"height_m", ih}});
            }
            features.push_back({{"kind", kind}, {"name", name}, {"islands", list}});
        } else {
            throw std::invalid_argument("unknown synthetic_coast feature kind: " + kind);
        }
    }
    // Cities: Gaussian population peaks on land.
    Json cities = Json::array();
    for (const Json& city : j.value("cities", Json::array())) {
        const double ce = required_number(city, "center_east_m"), cn = required_number(city, "center_north_m"), cr = required_number(city, "radius_m"), peak = required_number(city, "peak_per_km2");
        if (cr <= 0.0 || peak < 0.0) throw std::invalid_argument("city radius must be positive and its density non-negative");
        for (std::size_t i = 0; i < count; ++i) {
            const double r = std::hypot(east(i) - ce, north(i) - cn);
            density[i] += peak * std::exp(-(r * r) / (cr * cr));
        }
        cities.push_back({{"name", city.value("name", std::string("city"))}, {"center_east_m", ce}, {"center_north_m", cn}, {"radius_m", cr}, {"peak_per_km2", peak}});
    }
    c.elevation_m = z;
    c.sea = sea;
    for (std::size_t i = 0; i < count; ++i) c.population[i] = sea[i] ? 0.0 : density[i] * cell_km2;
    provenance = {{"kind", "synthetic_coast"}, {"coastline_north_m", coast_y}, {"features", features}, {"cities", cities}, {"rural_population_per_km2", rural}, {"roughness_m", roughness}, {"seed", seed}};
}

// River paths: a polyline from the mouth upstream, rasterised into 4-connected cells and trimmed to start at the
// first land cell next to the sea.
std::vector<std::size_t> rasterise_river(const Json& path, const CoastalFloodConfig& c, const std::string& name) {
    const auto& g = c.grid;
    const std::size_t count = g.nx * g.ny;
    if (!path.is_array() || path.size() < 2) throw std::invalid_argument("river " + name + ": path needs at least two points");
    std::vector<std::size_t> cells;
    auto add = [&](std::size_t i) {
        if (i >= count) throw std::invalid_argument("river " + name + ": path leaves the grid");
        if (!cells.empty() && cells.back() == i) return;
        if (!cells.empty()) {
            const std::size_t a = cells.back();
            const long ax = static_cast<long>(a % g.nx), ay = static_cast<long>(a / g.nx), bx = static_cast<long>(i % g.nx), by = static_cast<long>(i / g.nx);
            if (std::abs(ax - bx) + std::abs(ay - by) > 1) cells.push_back(static_cast<std::size_t>(ay) * g.nx + static_cast<std::size_t>(bx));   // keep the path 4-connected
        }
        if (std::find(cells.begin(), cells.end(), i) == cells.end()) cells.push_back(i);
    };
    for (std::size_t k = 0; k + 1 < path.size(); ++k) {
        const double e0 = path[k].at(0).get<double>(), n0 = path[k].at(1).get<double>(), e1 = path[k + 1].at(0).get<double>(), n1 = path[k + 1].at(1).get<double>();
        const double length = std::hypot(e1 - e0, n1 - n0);
        const int steps = std::max(1, static_cast<int>(std::ceil(4.0 * length / std::min(g.spacing_east_m, g.spacing_north_m))));
        for (int s = 0; s <= steps; ++s) add(cell_of(g, e0 + (e1 - e0) * s / steps, n0 + (n1 - n0) * s / steps));
    }
    auto at_sea = [&](std::size_t i) {
        const std::size_t x = i % g.nx, y = i / g.nx;
        const std::size_t n[4] = {x + 1 < g.nx ? i + 1 : count, x > 0 ? i - 1 : count, y + 1 < g.ny ? i + g.nx : count, y > 0 ? i - g.nx : count};
        for (std::size_t j : n) if (j < count && c.sea[j]) return true;
        return false;
    };
    // The mouth: the last land cell next to the sea before the path stays on land.
    std::size_t start = cells.size();
    for (std::size_t k = 0; k < cells.size(); ++k) if (!c.sea[cells[k]] && at_sea(cells[k])) start = k;
    if (start == cells.size()) throw std::invalid_argument("river " + name + ": its path never reaches the sea");
    std::vector<std::size_t> out(cells.begin() + static_cast<long>(start), cells.end());
    for (std::size_t i : out) if (c.sea[i]) throw std::invalid_argument("river " + name + ": the path crosses the sea upstream of its mouth");
    return out;
}

Coast parse_coast(const Json& request) {
    Coast out;
    auto& c = out.config;
    const Json& grid = required(request, "grid");
    c.grid.nx = static_cast<std::size_t>(required_int(grid, "nx", 2, 1000));
    c.grid.ny = static_cast<std::size_t>(required_int(grid, "ny", 2, 1000));
    c.grid.spacing_east_m = c.grid.spacing_north_m = required_number(grid, "spacing_m");
    c.grid.origin_east_m = optional_number(grid, "origin_east_m", 0.0);
    c.grid.origin_north_m = optional_number(grid, "origin_north_m", 0.0);
    c.grid.geographic_anchor_latitude_deg = optional_number(grid, "anchor_latitude_deg", 52.0);
    c.grid.geographic_anchor_longitude_deg = optional_number(grid, "anchor_longitude_deg", 4.0);
    if (c.grid.spacing_east_m < 10.0 || c.grid.spacing_east_m > 100000.0) throw std::invalid_argument("grid spacing must lie within 10 m - 100 km");
    const std::size_t count = c.grid.nx * c.grid.ny;
    c.coast_type.assign(count, 0);
    c.defence_crest_m.assign(count, -1.0e4);
    c.subsidence_m_yr.assign(count, 0.0);
    c.population.assign(count, 0.0);
    c.region.assign(count, -1);
    std::vector<double> density(count, 0.0);
    if (request.contains("synthetic_coast")) {
        build_synthetic_coast(required(request, "synthetic_coast"), c, density, out.provenance);
    } else {
        // Per-cell data, e.g. a digital elevation model resampled to the grid.
        const Json& cells = required(request, "cells");
        auto array = [&](const char* key, bool needed) {
            std::vector<double> v;
            if (!cells.contains(key)) { if (needed) throw std::invalid_argument(std::string("cells.") + key + " is required"); return v; }
            for (const Json& x : required(cells, key)) { if (!x.is_number()) throw std::invalid_argument(std::string("cells.") + key + " must hold numbers"); v.push_back(x.get<double>()); }
            if (v.size() != count) throw std::invalid_argument(std::string("cells.") + key + " must match the grid");
            return v;
        };
        c.elevation_m = array("elevation_m", true);
        const auto types = array("coast_type", false), crest = array("defence_crest_m", false), sub = array("subsidence_mm_yr", false), pop = array("population", false), reg = array("region", false);
        for (std::size_t i = 0; i < count; ++i) {
            if (!types.empty()) c.coast_type[i] = static_cast<unsigned char>(std::llround(types[i]));
            if (!crest.empty()) c.defence_crest_m[i] = crest[i] > 0.0 ? crest[i] : -1.0e4;
            if (!sub.empty()) c.subsidence_m_yr[i] = sub[i] / 1000.0;
            if (!pop.empty()) c.population[i] = pop[i];
            if (!reg.empty()) c.region[i] = static_cast<int>(std::llround(reg[i]));
        }
        for (const Json& n : cells.value("region_names", Json::array())) c.region_names.push_back(n.get<std::string>());
        if (cells.contains("sea")) {
            const auto sea = array("sea", false);
            c.sea.resize(count);
            for (std::size_t i = 0; i < count; ++i) c.sea[i] = sea[i] > 0.5 ? 1 : 0;
        } else {
            c.sea = connected_sea(c.elevation_m, c.grid.nx, c.grid.ny);
        }
        for (std::size_t i = 0; i < count; ++i) if (c.sea[i]) c.population[i] = 0.0;
        out.provenance = {{"kind", "prescribed_cells"}, {"sea", cells.contains("sea") ? "prescribed" : "below 0 m and connected to the grid edge"}};
        if (cells.contains("source")) out.provenance["source"] = cells.at("source");
    }
    // Water levels, the design storm and the process parameters.
    const Json coast = request.value("coast", Json::object());
    read_into(coast, "high_tide_m", c.high_tide_m);
    read_into(coast, "surge_location_m", c.surge_location_m);
    read_into(coast, "surge_scale_m", c.surge_scale_m);
    read_into(coast, "design_return_period_years", c.design_return_period_years);
    read_into(coast, "storm_wind_speed_m_s", c.storm_wind_speed_m_s);
    read_into(coast, "storm_wind_toward_deg", c.storm_wind_toward_deg);
    read_into(coast, "drag_coefficient", c.drag_coefficient);
    read_into(coast, "minimum_setup_depth_m", c.minimum_setup_depth_m);
    read_into(coast, "storm_attenuation_m_per_km", c.storm_attenuation_m_per_km);
    read_into(coast, "tidal_attenuation_m_per_km", c.tidal_attenuation_m_per_km);
    read_into(coast, "river_attenuation_m_per_km", c.river_attenuation_m_per_km);
    read_into(coast, "active_profile_width_m", c.active_profile_width_m);
    read_into(coast, "berm_height_m", c.berm_height_m);
    read_into(coast, "closure_depth_m", c.closure_depth_m);
    read_into(coast, "salt_limit_psu", c.salt_limit_psu);
    read_into(coast, "estuary_dispersion_m2_s", c.estuary_dispersion_m2_s);
    read_into(coast, "island_max_area_km2", c.island_max_area_km2);
    // Rivers, with optional dikes along their banks.
    Json rivers = Json::array();
    for (const Json& r : request.value("rivers", Json::array())) {
        CoastalRiver river;
        river.name = required_string(r, "name");
        river.cells = rasterise_river(required(r, "path"), c, river.name);
        read_into(r, "discharge_m3_s", river.discharge_m3_s);
        read_into(r, "channel_depth_m", river.channel_depth_m);
        read_into(r, "width_m", river.width_m);
        read_into(r, "manning_n", river.manning_n);
        // An optional valley: land within valley_width_m of the channel is lowered to a floor that rises from
        // valley_floor_m at the mouth by valley_gradient_m_per_km (a terrain edit for synthetic coasts).
        if (r.contains("valley_width_m")) {
            const double width = required_number(r, "valley_width_m"), floor0 = optional_number(r, "valley_floor_m", 2.0), gradient = optional_number(r, "valley_gradient_m_per_km", 0.2);
            if (width < 0.0 || gradient < 0.0) throw std::invalid_argument("river " + river.name + ": valley width and gradient must be non-negative");
            const auto& g = c.grid;
            double along = 0.0;
            for (std::size_t k = 0; k < river.cells.size(); ++k) {
                const std::size_t a = river.cells[k];
                if (k > 0) along += (river.cells[k - 1] % g.nx) != (a % g.nx) ? g.spacing_east_m : g.spacing_north_m;
                const double floor = floor0 + gradient * along / 1000.0;
                const long reach = static_cast<long>(std::ceil(width / g.spacing_east_m));
                const long ax = static_cast<long>(a % g.nx), ay = static_cast<long>(a / g.nx);
                for (long dy = -reach; dy <= reach; ++dy) for (long dx = -reach; dx <= reach; ++dx) {
                    const long x = ax + dx, y = ay + dy;
                    if (x < 0 || y < 0 || x >= static_cast<long>(g.nx) || y >= static_cast<long>(g.ny)) continue;
                    const std::size_t j = static_cast<std::size_t>(y) * g.nx + static_cast<std::size_t>(x);
                    if (c.sea[j]) continue;
                    const double dist = std::hypot(static_cast<double>(dx) * g.spacing_east_m, static_cast<double>(dy) * g.spacing_north_m);
                    if (dist > width) continue;
                    const double valley = floor + 0.5 * dist / 1000.0 + 0.05 * terrain_noise(j, 97);   // gentle valley sides
                    if (valley < c.elevation_m[j]) c.elevation_m[j] = valley;
                }
            }
        }
        const double bank = optional_number(r, "bank_crest_m", 0.0);
        if (bank > 0.0) {
            const auto& g = c.grid;
            for (std::size_t i : river.cells) {
                const std::size_t x = i % g.nx, y = i / g.nx;
                const std::size_t n[4] = {x + 1 < g.nx ? i + 1 : count, x > 0 ? i - 1 : count, y + 1 < g.ny ? i + g.nx : count, y > 0 ? i - g.nx : count};
                for (std::size_t j : n) if (j < count && !c.sea[j] && std::find(river.cells.begin(), river.cells.end(), j) == river.cells.end() && bank > c.elevation_m[j]) c.defence_crest_m[j] = std::max(c.defence_crest_m[j], bank);
            }
        }
        rivers.push_back({{"name", river.name}, {"cells", river.cells}, {"discharge_m3_s", river.discharge_m3_s}, {"channel_depth_m", river.channel_depth_m}, {"width_m", river.width_m},
                          {"manning_n", river.manning_n}, {"bank_crest_m", bank}});
        c.rivers.push_back(std::move(river));
    }
    out.provenance["rivers"] = rivers;
    for (std::size_t i = 0; i < count; ++i) if (c.defence_crest_m[i] <= c.elevation_m[i]) c.defence_crest_m[i] = c.elevation_m[i] - 1.0;   // no defence
    validate_coastal_config(c);
    return out;
}

// ---- Sea-level scenarios -------------------------------------------------------------------------------

struct SeaLevelRequest {
    std::vector<SeaLevelScenario> scenarios;
    SeaLevelBudgetConfig budget;
    Json provenance;
};

SeaLevelRequest parse_sea_level(const Json& request) {
    SeaLevelRequest out;
    const Json& j = required(request, "sea_level");
    if (j.contains("polar_timeline_path")) {
        // The scenarios, their warming and Greenland's contribution from a terraflux-run-polar-melt timeline.
        const std::string path = required_string(j, "polar_timeline_path");
        std::ifstream in(path);
        if (!in) throw std::invalid_argument("cannot open polar timeline: " + path);
        Json timeline;
        try { timeline = Json::parse(in); } catch (const Json::exception& e) { throw std::invalid_argument(std::string("invalid polar timeline JSON: ") + e.what()); }
        const Json& polar = required(timeline, "polar");
        if (polar.value("schema_version", "") != "terraflux.polar.melt/v1") throw std::invalid_argument("the polar timeline must carry a terraflux.polar.melt/v1 block");
        const Json& years = required(polar, "output_years");
        for (const Json& s : required(polar, "scenarios")) {
            if (s.value("control", false)) continue;
            SeaLevelScenario sc;
            sc.name = required_string(s, "name");
            const Json& series = required(s, "series");
            const Json& warming = required(series, "global_warming_k");
            const Json& greenland = series.contains("sea_level_rise_vs_control_m") ? series.at("sea_level_rise_vs_control_m") : required(series, "sea_level_rise_m");
            for (std::size_t k = 0; k < years.size(); ++k) {
                if (warming[k].is_null() || greenland[k].is_null()) continue;
                sc.warming.years.push_back(years[k].get<double>());
                sc.warming.values.push_back(warming[k].get<double>());
                sc.greenland.years.push_back(years[k].get<double>());
                sc.greenland.values.push_back(greenland[k].get<double>());
            }
            if (sc.warming.years.size() < 2) throw std::invalid_argument("polar scenario " + sc.name + " has too short a record");
            out.scenarios.push_back(std::move(sc));
        }
        out.provenance = {{"kind", "polar_timeline"}, {"path", path}, {"greenland", "series.sea_level_rise_vs_control_m (the ice-sheet drift of the control removed)"},
                          {"warming", "series.global_warming_k, linear between the output years, held beyond the last"}};
    } else {
        for (const Json& s : required(j, "scenarios")) {
            SeaLevelScenario sc;
            sc.name = required_string(s, "name");
            sc.warming = parse_curve(required(s, "global_warming_k"), "global_warming_k");
            sc.greenland = s.contains("greenland_m") ? parse_curve(s.at("greenland_m"), "greenland_m") : Curve{{sc.warming.years.front()}, {0.0}};
            out.scenarios.push_back(std::move(sc));
        }
        out.provenance = {{"kind", "prescribed_curves"}};
    }
    if (out.scenarios.empty()) throw std::invalid_argument("sea_level needs at least one scenario");
    auto& b = out.budget;
    const Json budget = j.value("budget", Json::object());
    read_into(budget, "thermal_expansion_m_per_k_yr", b.thermal_expansion_m_per_k_yr);
    read_into(budget, "deep_ocean_time_scale_years", b.deep_ocean_time_scale_years);
    read_into(budget, "glacier_m_per_k_yr", b.glacier_m_per_k_yr);
    read_into(budget, "glacier_store_m", b.glacier_store_m);
    read_into(budget, "antarctic_base_m_yr", b.antarctic_base_m_yr);
    read_into(budget, "antarctic_m_per_k_yr", b.antarctic_m_per_k_yr);
    read_into(budget, "antarctic_threshold_k", b.antarctic_threshold_k);
    read_into(budget, "land_water_m_yr", b.land_water_m_yr);
    const Json fp = j.value("fingerprints", Json::object());
    read_into(fp, "greenland", b.greenland_fingerprint);
    read_into(fp, "antarctic", b.antarctic_fingerprint);
    read_into(fp, "glaciers", b.glacier_fingerprint);
    validate_sea_level_budget(b);
    out.provenance["budget"] = {{"thermal_expansion_m_per_k_yr", b.thermal_expansion_m_per_k_yr}, {"deep_ocean_time_scale_years", b.deep_ocean_time_scale_years},
                                {"glacier_m_per_k_yr", b.glacier_m_per_k_yr}, {"glacier_store_m", b.glacier_store_m}, {"antarctic_base_m_yr", b.antarctic_base_m_yr},
                                {"antarctic_m_per_k_yr", b.antarctic_m_per_k_yr}, {"antarctic_threshold_k", b.antarctic_threshold_k}, {"land_water_m_yr", b.land_water_m_yr}};
    out.provenance["fingerprints"] = {{"greenland", b.greenland_fingerprint}, {"antarctic", b.antarctic_fingerprint}, {"glaciers", b.glacier_fingerprint}};
    return out;
}

void print_usage(const char* executable) { std::cerr << "Usage: " << executable << " <run-request.json> <output-timeline.json>\n"; }

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) { print_usage(argv[0]); return 2; }
    try {
        std::ifstream request_file(argv[1]);
        if (!request_file) throw std::runtime_error(std::string("cannot open run request: ") + argv[1]);
        Json request;
        try { request = Json::parse(request_file); } catch (const Json::exception& error) { throw std::invalid_argument(std::string("invalid run request JSON: ") + error.what()); }
        if (!request.is_object() || request.value("schema_version", "") != "terraflux.coastal.flooding-run/v1") throw std::invalid_argument("schema_version must be terraflux.coastal.flooding-run/v1");

        const Json& time = required(request, "time");
        SeaLevelRequest sl = parse_sea_level(request);
        const int reference = required_int(time, "reference_year", 1800, 2500);
        sl.budget.reference_year = reference;
        std::vector<int> output_years;
        for (const Json& y : required(time, "output_years")) {
            if (!y.is_number_integer()) throw std::invalid_argument("output_years must be integers");
            const int v = y.get<int>();
            if (v < reference || v > 3000 || (!output_years.empty() && v <= output_years.back())) throw std::invalid_argument("output_years must increase from the reference year");
            output_years.push_back(v);
        }
        if (output_years.size() < 2 || output_years.size() > 100 || output_years.front() != reference) throw std::invalid_argument("output_years need 2-100 years starting at the reference year");
        // The years with maps (frames): a subset of the output years, by default all of them.
        std::vector<int> map_years = output_years;
        if (time.contains("map_years")) {
            map_years.clear();
            for (const Json& y : required(time, "map_years")) {
                if (!y.is_number_integer() || std::find(output_years.begin(), output_years.end(), y.get<int>()) == output_years.end() || (!map_years.empty() && y.get<int>() <= map_years.back())) {
                    throw std::invalid_argument("map_years must be increasing output years");
                }
                map_years.push_back(y.get<int>());
            }
            if (map_years.size() < 2) throw std::invalid_argument("map_years need at least two years");
        }
        const int end = output_years.back();
        const int start = std::min(reference, static_cast<int>(std::floor(sl.scenarios.front().warming.years.front())));
        std::string map_scenario = request.value("map_scenario", sl.scenarios.back().name);
        if (std::none_of(sl.scenarios.begin(), sl.scenarios.end(), [&](const SeaLevelScenario& s) { return s.name == map_scenario; })) throw std::invalid_argument("map_scenario names no scenario");
        Coast coast = parse_coast(request);
        const auto& c = coast.config;
        const auto& g = c.grid;
        const std::size_t count = g.nx * g.ny;
        const CoastalBaseline baseline = coastal_baseline(c);
        const double design_surge = gumbel_return_level(c.surge_location_m, c.surge_scale_m, c.design_return_period_years);

        Json scenario_blocks = Json::array(), frames = Json::array(), map_frames = Json::array();
        Json grid_json = {{"nx", g.nx}, {"ny", g.ny}, {"origin_east_m", g.origin_east_m}, {"origin_north_m", g.origin_north_m}, {"spacing_east_m", g.spacing_east_m}, {"spacing_north_m", g.spacing_north_m},
                          {"geographic_anchor_longitude_deg", g.geographic_anchor_longitude_deg}, {"geographic_anchor_latitude_deg", g.geographic_anchor_latitude_deg},
                          {"coordinate_system", "local_azimuthal_equidistant_m"}, {"array_order", "row_major_y_then_x"}};
        const std::string start_time = std::to_string(reference) + "-01-01T00:00:00Z";
        const double theta = c.storm_wind_toward_deg * 3.14159265358979323846 / 180.0;
        // The frames' fields: the design storm's wind (uniform) and a placeholder air temperature (the viewer's timeline contract).
        const std::vector<double> wind_e(count, tidy(c.storm_wind_speed_m_s * std::sin(theta), 2)), wind_n(count, tidy(c.storm_wind_speed_m_s * std::cos(theta), 2)), air(count, 288.0);
        for (const auto& sc : sl.scenarios) {
            // The sea-level budget from the first year of the warming record to the end.
            std::vector<int> years;
            std::vector<double> warming, greenland;
            for (int y = start; y <= end; ++y) { years.push_back(y); warming.push_back(sc.warming.at(y)); greenland.push_back(sc.greenland.at(y)); }
            const SeaLevelComponents budget = sea_level_budget(years, warming, greenland, sl.budget);
            const bool maps = sc.name == map_scenario;
            // Every year from today: impacts, the year each cell is lost / storm-flooded, the events.
            std::vector<double> year_lost(count, std::numeric_limits<double>::quiet_NaN()), year_storm(count, std::numeric_limits<double>::quiet_NaN());
            const std::size_t regions = c.region_names.size();
            std::vector<int> ev_annual(regions, 0), ev_lost10(regions, 0), ev_overtopped(regions, 0), ev_island(regions, 0);
            std::map<std::string, std::vector<double>> totals;
            std::vector<std::map<std::string, std::vector<double>>> region_series(regions), river_series(c.rivers.size());
            std::vector<std::vector<double>> island_lost;
            Json island_info = Json::array();
            Json components = {{"thermal_expansion_m", Json::array()}, {"glaciers_m", Json::array()}, {"antarctic_m", Json::array()}, {"greenland_m", Json::array()},
                               {"land_water_m", Json::array()}, {"global_m", Json::array()}, {"regional_m", Json::array()}, {"global_warming_k", Json::array()}};
            std::size_t next_output = 0;
            for (int y = reference; y <= end; ++y) {
                const std::size_t k = static_cast<std::size_t>(y - start);
                const double rise = budget.regional_m[k], dt = static_cast<double>(y - reference);
                const CoastalImpact im = assess_coastal_impact(c, rise, dt, baseline);
                for (std::size_t i = 0; i < count; ++i) {
                    if (c.sea[i]) continue;
                    if (std::isnan(year_lost[i]) && im.lost_fraction[i] >= 0.5) year_lost[i] = y;
                    if (std::isnan(year_storm[i]) && im.storm_depth_m[i] > 0.0) year_storm[i] = y;
                }
                for (std::size_t r = 0; r < regions; ++r) {
                    const auto& R = im.regions[r];
                    if (!ev_annual[r] && R.land_km2 > 0.0 && R.design_flood_return_years <= 1.0) ev_annual[r] = y;
                    if (!ev_lost10[r] && R.land_km2 > 0.0 && R.lost_km2 >= 0.1 * R.land_km2) ev_lost10[r] = y;
                }
                for (std::size_t i = 0; i < count; ++i) {
                    // The design storm reaches land that lies below the high water behind a defence: the defence line is overtopped.
                    if (c.region[i] < 0 || c.sea[i]) continue;
                    const auto r = static_cast<std::size_t>(c.region[i]);
                    if (!ev_overtopped[r] && im.protected_low[i] && im.storm_depth_m[i] > 0.0) ev_overtopped[r] = y;
                }
                for (const auto& isl : im.islands) if (isl.region >= 0 && !ev_island[static_cast<std::size_t>(isl.region)] && isl.lost_fraction >= 0.5) ev_island[static_cast<std::size_t>(isl.region)] = y;
                if (next_output < output_years.size() && y == output_years[next_output]) {
                    ++next_output;
                    components["thermal_expansion_m"].push_back(tidy(budget.thermal_expansion_m[k], 4));
                    components["glaciers_m"].push_back(tidy(budget.glaciers_m[k], 4));
                    components["antarctic_m"].push_back(tidy(budget.antarctic_m[k], 4));
                    components["greenland_m"].push_back(tidy(budget.greenland_m[k], 4));
                    components["land_water_m"].push_back(tidy(budget.land_water_m[k], 4));
                    components["global_m"].push_back(tidy(budget.global_m[k], 4));
                    components["regional_m"].push_back(tidy(budget.regional_m[k], 4));
                    components["global_warming_k"].push_back(tidy(budget.warming_k[k], 4));
                    auto push_totals = [&](const char* key, double v) { totals[key].push_back(v); };
                    push_totals("lost_km2", im.lost_km2); push_totals("inundated_km2", im.inundated_km2); push_totals("eroded_km2", im.eroded_km2);
                    push_totals("storm_flooded_km2", im.storm_flooded_km2); push_totals("newly_storm_flooded_km2", im.newly_storm_flooded_km2); push_totals("protected_low_km2", im.protected_low_km2);
                    push_totals("people_on_lost_land", im.people_on_lost_land); push_totals("people_storm_flooded", im.people_storm_flooded); push_totals("people_protected_low", im.people_protected_low);
                    push_totals("max_setup_m", im.max_setup_m); push_totals("mean_coastal_setup_m", im.mean_coastal_setup_m);
                    for (std::size_t r = 0; r < regions; ++r) {
                        const auto& R = im.regions[r];
                        auto& s = region_series[r];
                        s["lost_km2"].push_back(R.lost_km2); s["inundated_km2"].push_back(R.inundated_km2); s["eroded_km2"].push_back(R.eroded_km2);
                        s["lost_pct"].push_back(R.land_km2 > 0.0 ? R.lost_km2 / R.land_km2 * 100.0 : 0.0);
                        s["storm_flooded_km2"].push_back(R.storm_flooded_km2); s["newly_storm_flooded_km2"].push_back(R.newly_storm_flooded_km2);
                        s["protected_low_km2"].push_back(R.protected_low_km2); s["people_on_lost_land"].push_back(R.people_on_lost_land);
                        s["people_storm_flooded"].push_back(R.people_storm_flooded); s["people_protected_low"].push_back(R.people_protected_low);
                        s["mean_relative_rise_m"].push_back(R.mean_relative_rise_m); s["design_flood_return_years"].push_back(R.design_flood_return_years);
                        s["max_storm_depth_m"].push_back(R.max_storm_depth_m);
                    }
                    for (std::size_t r = 0; r < c.rivers.size(); ++r) {
                        const auto& R = im.rivers[r];
                        auto& s = river_series[r];
                        s["tidal_reach_km"].push_back(R.tidal_reach_km); s["surge_reach_km"].push_back(R.surge_reach_km); s["salt_intrusion_km"].push_back(R.salt_intrusion_km);
                        s["mouth_storm_level_m"].push_back(R.mouth_storm_level_m); s["flooded_bank_km2"].push_back(R.flooded_bank_km2);
                    }
                    if (island_lost.empty()) {
                        island_lost.resize(im.islands.size());
                        for (const auto& isl : im.islands) island_info.push_back({{"id", isl.id}, {"region", isl.region}, {"land_km2", tidy(isl.land_km2)}, {"population", tidy(isl.population)},
                                                                                  {"highest_m", tidy(isl.highest_m, 4)}, {"top_cell", isl.top_cell}});
                    }
                    for (std::size_t n = 0; n < im.islands.size(); ++n) island_lost[n].push_back(im.islands[n].lost_fraction);
                    if (maps && std::find(map_years.begin(), map_years.end(), y) != map_years.end()) {
                        const double elapsed = static_cast<double>(y - reference) * 365.25 * 86400.0;
                        frames.push_back({{"schema_version", "terraflux.weather.forecast/v1"}, {"start_time_utc", start_time}, {"elapsed_seconds", elapsed}, {"grid", grid_json},
                                          {"observation_updates", Json::array()},
                                          {"fields", {{"air_temperature", {{"units", "K"}, {"values", air}}}, {"wind_east", {{"units", "m/s"}, {"values", wind_e}}}, {"wind_north", {{"units", "m/s"}, {"values", wind_n}}}}}});
                        Json protected_cells = Json::array(), salt_cells = Json::array();
                        for (std::size_t i = 0; i < count; ++i) { if (im.protected_low[i]) protected_cells.push_back(i); if (im.river_salt[i]) salt_cells.push_back(i); }
                        map_frames.push_back({{"year", y}, {"elapsed_seconds", elapsed}, {"regional_rise_m", tidy(rise, 4)},
                                              {"lost_fraction", tidy_array(im.lost_fraction, 3)}, {"inundation_depth_m", tidy_array(im.inundation_depth_m, 3)},
                                              {"storm_depth_m", tidy_array(im.storm_depth_m, 3)}, {"erosion_retreat_m", tidy_array(im.erosion_retreat_m, 3)},
                                              {"protected_low_cells", protected_cells}, {"river_salt_cells", salt_cells}});
                    }
                }
            }
            auto series_json = [&](const std::map<std::string, std::vector<double>>& m) {
                Json o = Json::object();
                for (const auto& [key, v] : m) o[key] = tidy_array(v, 5);
                return o;
            };
            auto event = [&](int year) { return year ? Json(year) : Json(nullptr); };
            Json region_blocks = Json::array();
            for (std::size_t r = 0; r < regions; ++r) {
                region_blocks.push_back({{"name", c.region_names[r]}, {"series", series_json(region_series[r])},
                                         {"events", {{"design_flood_every_year", event(ev_annual[r])}, {"lost_10pct", event(ev_lost10[r])}, {"defences_overtopped", event(ev_overtopped[r])},
                                                     {"island_half_lost", event(ev_island[r])}}}});
            }
            Json river_blocks = Json::array();
            for (std::size_t r = 0; r < c.rivers.size(); ++r) river_blocks.push_back({{"name", c.rivers[r].name}, {"series", series_json(river_series[r])}});
            Json islands = Json::array();
            for (std::size_t n = 0; n < island_lost.size(); ++n) { Json isl = island_info[n]; isl["lost_fraction"] = tidy_array(island_lost[n], 4); islands.push_back(isl); }
            // Sparse [cell, year] lists: the first year each cell is (half) lost / reached by the design storm.
            Json lost_year = Json::array(), storm_year = Json::array();
            for (std::size_t i = 0; i < count; ++i) {
                if (!std::isnan(year_lost[i])) lost_year.push_back({i, static_cast<int>(year_lost[i])});
                if (!std::isnan(year_storm[i])) storm_year.push_back({i, static_cast<int>(year_storm[i])});
            }
            scenario_blocks.push_back({{"name", sc.name}, {"sea_level", components}, {"totals", series_json(totals)}, {"regions", region_blocks}, {"rivers", river_blocks},
                                       {"islands", islands}, {"year_lost", lost_year}, {"year_storm_flooded", storm_year}});
        }

        // Static maps.
        Json sea = Json::array(), type = Json::array(), crest = Json::array(), sub = Json::array(), pop = Json::array(), region = Json::array(), today_perm = Json::array(), today_storm = Json::array();
        const CoastalImpact today = assess_coastal_impact(c, 0.0, 0.0, baseline);
        for (std::size_t i = 0; i < count; ++i) {
            if (c.sea[i]) sea.push_back(i);
            type.push_back(static_cast<int>(c.coast_type[i]));
            if (c.defence_crest_m[i] > c.elevation_m[i]) crest.push_back({i, tidy(c.defence_crest_m[i], 4)});
            sub.push_back(tidy(c.subsidence_m_yr[i] * 1000.0, 3));
            pop.push_back(tidy(c.population[i], 4));
            region.push_back(c.region[i]);
            if (baseline.permanent[i]) today_perm.push_back(i);
            today_storm.push_back(tidy(today.storm_depth_m[i], 3));
        }
        Json rivers = Json::array();
        for (const auto& r : c.rivers) rivers.push_back({{"name", r.name}, {"cells", r.cells}, {"discharge_m3_s", r.discharge_m3_s}, {"width_m", r.width_m}, {"channel_depth_m", r.channel_depth_m}});
        Json output = {
            {"schema_version", "terraflux.weather.timeline/v1"},
            {"frames", frames},
            {"terrain", {{"schema_version", "terraflux.terrain.grid/v1"}, {"grid", grid_json}, {"units", "m"}, {"vertical_reference", "present_mean_sea_level"},
                         {"source_id", coast.provenance.value("kind", std::string("coast"))}, {"values", tidy_array(c.elevation_m, 5)}}},
            {"coast", {
                {"schema_version", "terraflux.coastal.flooding/v1"},
                {"method", "bathtub_connectivity_attenuation_wind_setup_bruun_gumbel"},
                {"source_kind", "engine_output"},
                {"request", request},
                {"reference_year", reference},
                {"output_years", output_years},
                {"map_years", map_years},
                {"map_scenario", map_scenario},
                {"sea_cells", sea}, {"coast_type", type}, {"defences", crest}, {"subsidence_mm_yr", sub}, {"population", pop},
                {"region", region}, {"region_names", c.region_names}, {"rivers", rivers},
                {"today", {{"intertidal_cells", today_perm}, {"storm_depth_m", today_storm}, {"setup_m", tidy_array(today.setup_m, 3)}}},
                {"design_storm", {{"return_period_years", c.design_return_period_years}, {"high_tide_m", c.high_tide_m}, {"surge_m", tidy(design_surge, 4)},
                                  {"offshore_level_m", tidy(c.high_tide_m + design_surge, 4)}, {"surge_location_m", c.surge_location_m}, {"surge_scale_m", c.surge_scale_m},
                                  {"wind_speed_m_s", c.storm_wind_speed_m_s}, {"wind_toward_deg", c.storm_wind_toward_deg},
                                  {"wind_stress_pa", tidy(c.air_density_kg_m3 * c.drag_coefficient * c.storm_wind_speed_m_s * c.storm_wind_speed_m_s, 4)}}},
                {"coast_source", coast.provenance},
                {"sea_level_source", sl.provenance},
                {"scenarios", scenario_blocks},
                {"map_frames", map_frames},
                {"limitations", Json::array({"bathtub flooding with connectivity and a linear attenuation inland: no flow dynamics, no flood duration or volume limit, defences hold until overtopped and never breach",
                                             "wind setup without bottom friction along a straight design wind: an upper estimate over shallow water",
                                             "Bruun-rule retreat of sandy shores only; no sediment supply, no accretion of marshes or deltas",
                                             "salt intrusion from a steady one-dimensional advection-dispersion balance",
                                             "the coast is synthetic unless per-cell data are given; sea-level rise from a semi-empirical budget calibrated to IPCC AR6 medians, with Greenland from the polar-melt run"})},
            }},
        };
        std::ofstream out(argv[2]);
        if (!out) throw std::runtime_error(std::string("cannot write output: ") + argv[2]);
        out << output.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
