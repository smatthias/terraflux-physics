#include "terraflux/weather/ocean_biogeochemistry.hpp"

#include "terraflux/weather/marine_indicators.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::weather {

namespace {

constexpr double kRho = 1025.0;          // kg/m3: umol/kg x m x kg/m3 x 1e-6 = mol/m2

void require_range(double v, double lo, double hi, const char* name) {
    if (!std::isfinite(v) || v < lo || v > hi) throw std::invalid_argument(std::string("biogeochemistry ") + name + " is out of range");
}

// Column geometry of one cell.
struct Column {
    std::size_t n{0};
    bool shelf{false};          // the seafloor lies within the levels (benthic respiration), else the column is open below
    double bottom{0.0};
};

Column column_of(const MarineBgcConfig& c, std::size_t i) {
    Column col;
    if (!c.sea[i]) return col;
    const double floor = std::max(c.seafloor_depth_m[i], 0.5);
    const std::size_t levels = c.level_bottoms_m.size();
    // A level starts only if the seafloor leaves it at least a quarter of its thickness (and 1 m); a thinner
    // remainder joins the level above, so no sliver of bottom water collects the whole seafloor respiration.
    while (col.n < levels) {
        const double top = level_top_m(c, col.n), nominal = c.level_bottoms_m[col.n] - top;
        if (col.n > 0 && floor - top < std::max(1.0, 0.25 * nominal)) break;
        if (top >= floor) break;
        ++col.n;
    }
    col.n = std::max<std::size_t>(col.n, 1);
    col.shelf = floor <= c.level_bottoms_m.back();
    col.bottom = col.shelf ? floor : c.level_bottoms_m.back();
    return col;
}

double thickness(const MarineBgcConfig& c, const Column& col, std::size_t k) {
    const double bottom = k + 1 == col.n ? col.bottom : c.level_bottoms_m[k];
    return std::max(0.1, bottom - level_top_m(c, k));
}

double inventory(const std::vector<double>& v, const MarineBgcState& s, const MarineBgcConfig& c) {
    const std::size_t count = c.sea.size(), levels = c.level_bottoms_m.size();
    const double area = c.grid.spacing_east_m * c.grid.spacing_north_m;
    long double sum = 0.0L;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        const Column col = column_of(c, i);
        for (std::size_t k = 0; k < s.levels[i]; ++k) sum += static_cast<long double>(v[i * levels + k]) * thickness(c, col, k);
    }
    return static_cast<double>(sum * area * kRho * 1.0e-6L);
}

void check_forcing(const MarineBgcForcing& f, const MarineBgcConfig& c) {
    const std::size_t count = c.sea.size(), cells = count * c.level_bottoms_m.size();
    if (f.temperature_k.size() != cells || f.salinity_psu.size() != cells) throw std::invalid_argument("biogeochemistry needs temperature and salinity on every level");
    for (const auto* v : {&f.mixed_layer_depth_m, &f.current_east_m_s, &f.current_north_m_s, &f.wind_speed_m_s, &f.shortwave_w_m2, &f.atmospheric_pco2_uatm}) {
        if (v->size() != count) throw std::invalid_argument("biogeochemistry forcing fields must match the grid");
    }
    if (!f.ice_fraction.empty() && f.ice_fraction.size() != count) throw std::invalid_argument("biogeochemistry ice fraction must be empty or match the grid");
    if (!f.active_layer_thickness_m.empty() && f.active_layer_thickness_m.size() != count) throw std::invalid_argument("biogeochemistry active layer thickness must be empty or match the grid");
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        if (!(f.atmospheric_pco2_uatm[i] > 0.0) || f.atmospheric_pco2_uatm[i] > 20000.0 || !(f.wind_speed_m_s[i] >= 0.0) || !(f.shortwave_w_m2[i] >= 0.0) || !(f.mixed_layer_depth_m[i] >= 0.0)) {
            throw std::invalid_argument("biogeochemistry forcing out of range");
        }
    }
    for (const auto& r : f.rivers) if (r.cell >= count || !c.sea[r.cell] || !(r.discharge_m3_s >= 0.0)) throw std::invalid_argument("biogeochemistry rivers enter sea cells with a non-negative discharge");
}

// Transport and rivers, in flux form on the levels.
//
// Each face between two sea cells carries, level by level, an upwind volume flux with the face current over the
// part of the level inside the moving layer (the ocean's active layer, above both seafloors; the deep water below
// is at rest) and a diffusive flux over the thickness the two levels share (partial levels above the seafloor).
// The depth-integrated face transports are first made consistent with the column geometry: a potential-flow
// correction (graph Laplacian, conjugate gradients) removes the divergence the depth-uniform current has where the
// column depth changes along the flow, so that every interior column neither gains nor loses water except the
// river water entering it. Open-boundary cells (the ocean's sponge) take up or supply whatever the basin
// exchanges with the ocean outside: outflow leaves at the water's own concentration, inflow brings the reference
// (open-ocean) water, and they relax to the reference profiles. In a closed basin the river water displaces
// the same volume of sea water everywhere (a distributed outflow). Inside a column the level-by-level
// convergence that remains (flow over a sloping seafloor) is closed by vertical fluxes between the levels.
void transport_step(MarineBgcState& s, const MarineBgcForcing& f, const MarineBgcConfig& c, const std::vector<Column>& cols, double dt) {
    const auto& g = c.grid;
    const std::size_t nx = g.nx, count = c.sea.size(), levels = c.level_bottoms_m.size();
    const double area = g.spacing_east_m * g.spacing_north_m, dx = g.spacing_east_m, dy = g.spacing_north_m;
    std::array<std::vector<double>*, 4> tracers{&s.dic, &s.alkalinity, &s.oxygen, &s.nitrate};
    std::array<const std::vector<double>*, 4> references{&s.reference_dic, &s.reference_alkalinity, &s.reference_oxygen, &s.reference_nitrate};
    auto dz = [&](std::size_t i, std::size_t k) { return thickness(c, cols[i], k); };
    auto open_strength = [&](std::size_t i) { return c.open_boundary.empty() ? 0.0 : c.open_boundary[i]; };
    auto moving_depth = [&](std::size_t i) {
        const double layer = f.active_layer_thickness_m.empty() ? cols[i].bottom : f.active_layer_thickness_m[i];
        return std::max(0.0, std::min(layer, cols[i].bottom));
    };
    auto overlap = [&](std::size_t i, std::size_t k, double depth) { return std::max(0.0, std::min(level_top_m(c, k) + dz(i, k), depth) - level_top_m(c, k)); };
    const double m0 = inventory(s.dic, s, c), n0 = inventory(s.nitrate, s, c), o0 = inventory(s.oxygen, s, c);

    // Faces: the depth-integrated transport of the current (m3/s, i -> j) over the moving depth and the
    // conductances of the two potential flows (the current's correction, the river outflow in the plume layer).
    struct Face { std::size_t i, j; double transport, conductance, plume_conductance, moving, plume, length, spacing; };
    std::vector<Face> faces;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        const std::size_t x = i % nx, y = i / nx;
        for (int d = 0; d < 2; ++d) {
            const std::size_t j = d == 0 ? (x + 1 < nx ? i + 1 : count) : (y + 1 < g.ny ? i + nx : count);
            if (j >= count || !c.sea[j]) continue;
            const double u = d == 0 ? 0.5 * (f.current_east_m_s[i] + f.current_east_m_s[j]) : 0.5 * (f.current_north_m_s[i] + f.current_north_m_s[j]);
            const double length = d == 0 ? dy : dx, spacing = d == 0 ? dx : dy;
            const double floor = std::min(cols[i].bottom, cols[j].bottom);
            const double moving = std::min(0.5 * (moving_depth(i) + moving_depth(j)), floor);
            // River water spreads in the surface mixed layer (at least the top level).
            const double plume = std::min(floor, std::max(c.level_bottoms_m[0], 0.5 * (f.mixed_layer_depth_m[i] + f.mixed_layer_depth_m[j])));
            faces.push_back({i, j, u * moving * length, moving * length / spacing, plume * length / spacing, moving, plume, length, spacing});
        }
    }
    // River volume entering each cell (m3/s).
    std::vector<double> river(count, 0.0);
    for (const auto& r : f.rivers) river[r.cell] += r.discharge_m3_s;
    // Connected sea regions; which of them reach an open boundary.
    std::vector<std::size_t> region(count, count);
    std::vector<std::vector<std::size_t>> neighbours(count);
    for (std::size_t n = 0; n < faces.size(); ++n) { neighbours[faces[n].i].push_back(n); neighbours[faces[n].j].push_back(n); }
    std::vector<unsigned char> region_open;
    std::vector<double> region_river, region_weight;
    for (std::size_t start = 0; start < count; ++start) {
        if (!c.sea[start] || region[start] < count) continue;
        const std::size_t id = region_open.size();
        region_open.push_back(0); region_river.push_back(0.0); region_weight.push_back(0.0);
        std::vector<std::size_t> stack{start};
        region[start] = id;
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            if (open_strength(i) > 0.0) region_open[id] = 1;
            region_river[id] += river[i];
            region_weight[id] += cols[i].bottom;
            for (std::size_t n : neighbours[i]) {
                const std::size_t j = faces[n].i == i ? faces[n].j : faces[n].i;
                if (region[j] == count) { region[j] = id; stack.push_back(j); }
            }
        }
    }
    // The potential phi is held at zero in open cells (they take up or supply any water) and in one cell of each
    // closed region (there it is free to a constant).
    std::vector<unsigned char> fixed(count, 0), pinned(region_open.size(), 0);
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        if (open_strength(i) > 0.0) fixed[i] = 1;
        else if (!region_open[region[i]] && !pinned[region[i]]) { fixed[i] = 1; pinned[region[i]] = 1; }
    }
    // Solve sum_f G_f (phi_i - phi_j) = rhs_i on the free cells: Jacobi-preconditioned conjugate gradients.
    auto solve_potential = [&](bool plume_flow, const std::vector<double>& rhs) {
        std::vector<double> phi(count, 0.0), diagonal(count, 0.0);
        auto conductance = [&](const Face& face) { return plume_flow ? face.plume_conductance : face.conductance; };
        for (const Face& face : faces) { diagonal[face.i] += conductance(face); diagonal[face.j] += conductance(face); }
        auto free_cell = [&](std::size_t i) { return c.sea[i] && !fixed[i] && diagonal[i] > 0.0; };
        std::vector<double> r(count, 0.0), z(count, 0.0), p(count, 0.0), ap(count, 0.0);
        double norm0 = 0.0, rz = 0.0;
        for (std::size_t i = 0; i < count; ++i) if (free_cell(i)) { r[i] = rhs[i]; norm0 += r[i] * r[i]; z[i] = r[i] / diagonal[i]; rz += r[i] * z[i]; }
        if (!(norm0 > 0.0)) return phi;
        p = z;
        for (int iteration = 0; iteration < 4000 && rz > 0.0; ++iteration) {
            std::fill(ap.begin(), ap.end(), 0.0);
            for (const Face& face : faces) {
                const double flux = conductance(face) * (p[face.i] - p[face.j]);
                if (free_cell(face.i)) ap[face.i] += flux;
                if (free_cell(face.j)) ap[face.j] -= flux;
            }
            double pap = 0.0;
            for (std::size_t i = 0; i < count; ++i) if (free_cell(i)) pap += p[i] * ap[i];
            if (!(pap > 0.0)) break;
            const double alpha = rz / pap;
            double norm = 0.0, rz_new = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                if (!free_cell(i)) continue;
                phi[i] += alpha * p[i];
                r[i] -= alpha * ap[i];
                norm += r[i] * r[i];
                z[i] = r[i] / diagonal[i];
                rz_new += r[i] * z[i];
            }
            if (norm <= 1.0e-24 * norm0) break;
            const double beta = rz_new / rz;
            rz = rz_new;
            for (std::size_t i = 0; i < count; ++i) if (free_cell(i)) p[i] = z[i] + beta * p[i];
        }
        return phi;
    };
    // The current, corrected to carry no net water into or out of interior columns.
    std::vector<double> rhs(count, 0.0);
    for (const Face& face : faces) { rhs[face.i] -= face.transport; rhs[face.j] += face.transport; }
    const std::vector<double> phi = solve_potential(false, rhs);
    // The river outflow: each mouth sends its water on; in a closed region the same volume leaves everywhere.
    for (std::size_t i = 0; i < count; ++i) {
        rhs[i] = 0.0;
        if (!c.sea[i]) continue;
        rhs[i] = river[i];
        if (!region_open[region[i]]) rhs[i] -= region_river[region[i]] * cols[i].bottom / region_weight[region[i]];
    }
    const std::vector<double> psi = solve_potential(true, rhs);
    // Level by level: volume fluxes, diffusive conductances and the convergence they leave in each level.
    struct LevelFace { std::size_t a, b; double q, g; };
    std::vector<LevelFace> level_faces;
    std::vector<double> volume(count * levels, 0.0), convergence(count * levels, 0.0), outflow(count * levels, 0.0);
    for (std::size_t i = 0; i < count; ++i) for (std::size_t k = 0; k < s.levels[i]; ++k) volume[i * levels + k] = dz(i, k) * area;
    for (const Face& face : faces) {
        const double current = face.transport + face.conductance * (phi[face.i] - phi[face.j]);
        const double outflow_river = face.plume_conductance * (psi[face.i] - psi[face.j]);
        const std::size_t shared_levels = std::min(s.levels[face.i], s.levels[face.j]);
        auto carried = [&](std::size_t k, double depth) { return std::min(overlap(face.i, k, depth), overlap(face.j, k, depth)); };
        double moving_total = 0.0, plume_total = 0.0;
        for (std::size_t k = 0; k < shared_levels; ++k) { moving_total += carried(k, face.moving); plume_total += carried(k, face.plume); }
        for (std::size_t k = 0; k < shared_levels; ++k) {
            const double shared = std::min(dz(face.i, k), dz(face.j, k));
            const double q = (moving_total > 0.0 ? current * carried(k, face.moving) / moving_total : 0.0) + (plume_total > 0.0 ? outflow_river * carried(k, face.plume) / plume_total : 0.0);
            const LevelFace lf{face.i * levels + k, face.j * levels + k, q, c.horizontal_diffusivity_m2_s * shared * face.length / face.spacing};
            level_faces.push_back(lf);
            convergence[lf.a] -= lf.q;
            convergence[lf.b] += lf.q;
            outflow[lf.a] += std::max(0.0, lf.q) + lf.g;
            outflow[lf.b] += std::max(0.0, -lf.q) + lf.g;
        }
    }
    // Columns: the river enters the surface level; what the column gains or loses in all (only open-boundary
    // cells, the closed-basin outflow and the solver's tolerance) leaves or enters over the moving levels; the
    // rest is closed by upward fluxes through the level bottoms (zero at the column bottom).
    std::vector<double> excess(count * levels, 0.0), up(count * levels, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t n = s.levels[i], base = i * levels;
        if (n == 0) continue;
        double net = river[i];
        for (std::size_t k = 0; k < n; ++k) net += convergence[base + k];
        const double moving = moving_depth(i) > 0.0 ? moving_depth(i) : cols[i].bottom;
        double weights = 0.0;
        for (std::size_t k = 0; k < n; ++k) weights += overlap(i, k, moving);
        double top = 0.0;   // upward flux through the top of the level
        for (std::size_t k = 0; k < n; ++k) {
            excess[base + k] = weights > 0.0 ? net * overlap(i, k, moving) / weights : (k == 0 ? net : 0.0);
            const double gain = convergence[base + k] + (k == 0 ? river[i] : 0.0) - excess[base + k];
            up[base + k] = k + 1 < n ? top - gain : 0.0;
            top = up[base + k];
        }
        for (std::size_t k = 0; k < n; ++k) {
            const double below = up[base + k], above = k > 0 ? up[base + k - 1] : 0.0;
            outflow[base + k] += std::max(0.0, -below) + std::max(0.0, above) + std::max(0.0, excess[base + k]);
        }
    }
    // River water chemistry at the mouths.
    std::vector<std::array<double, 4>> river_in(count, {0.0, 0.0, 0.0, 0.0});
    for (const auto& r : f.rivers) {
        const std::size_t ik = r.cell * levels;
        const double sat = oxygen_solubility_umol_kg(std::max(271.0, std::min(313.0, f.temperature_k[ik])), std::min(42.0, std::max(0.0, f.salinity_psu[ik])));
        const double in[4] = {r.chemistry.dic_umol_kg, r.chemistry.alkalinity_umol_kg, r.chemistry.oxygen_saturation * sat, r.chemistry.nitrate_umol_kg};
        for (int m = 0; m < 4; ++m) river_in[r.cell][m] += in[m] * r.discharge_m3_s;   // umol/kg m3/s
    }
    double fastest = 0.0;
    for (std::size_t ik = 0; ik < count * levels; ++ik) if (volume[ik] > 0.0) fastest = std::max(fastest, outflow[ik] / volume[ik]);
    const int substeps = std::max(1, static_cast<int>(std::ceil(dt * fastest / 0.5)));
    const double h = dt / substeps, to_mol = kRho * 1.0e-6;
    std::vector<double> change(count * levels);
    double boundary[4] = {0.0, 0.0, 0.0, 0.0};
    for (int sub = 0; sub < substeps; ++sub) {
        for (int m = 0; m < 4; ++m) {
            auto& v = *tracers[m];
            const auto& ref = *references[m];
            std::fill(change.begin(), change.end(), 0.0);   // content change (umol/kg m3/s)
            for (const LevelFace& lf : level_faces) {
                const double flux = lf.q * (lf.q > 0.0 ? v[lf.a] : v[lf.b]) + lf.g * (v[lf.a] - v[lf.b]);
                change[lf.a] -= flux;
                change[lf.b] += flux;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t n = s.levels[i], base = i * levels;
                if (n == 0) continue;
                change[base] += river_in[i][m];
                const bool open = open_strength(i) > 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    const std::size_t ik = base + k;
                    if (k + 1 < n) {
                        const double w = up[ik], flux = w * (w > 0.0 ? v[ik + 1] : v[ik]);   // upward through the bottom of k
                        change[ik] += flux;
                        change[ik + 1] -= flux;
                    }
                    // Water leaving (at its own concentration) or entering (open boundary: the reference water).
                    const double exchange = -excess[ik] * (excess[ik] > 0.0 || !open ? v[ik] : ref[ik]);
                    change[ik] += exchange;
                    if (open || !region_open[region[i]]) boundary[m] += exchange * h * to_mol;
                }
            }
            for (std::size_t ik = 0; ik < count * levels; ++ik) if (volume[ik] > 0.0) v[ik] = std::max(0.0, v[ik] + h * change[ik] / volume[ik]);
        }
    }
    // Open-boundary cells relax to the reference (open-ocean) water below their mixed layer (the mixed layer
    // keeps its own biology and gas exchange).
    if (!c.open_boundary.empty()) {
        for (std::size_t i = 0; i < count; ++i) {
            const double strength = open_strength(i);
            if (!(strength > 0.0)) continue;
            const double rate = 1.0 - std::exp(-dt * strength / c.open_boundary_time_s);
            for (std::size_t k = 0; k < s.levels[i]; ++k) {
                if (level_top_m(c, k) < f.mixed_layer_depth_m[i]) continue;
                const std::size_t ik = i * levels + k;
                for (int m = 0; m < 4; ++m) {
                    const double d = ((*references[m])[ik] - (*tracers[m])[ik]) * rate;
                    (*tracers[m])[ik] += d;
                    boundary[m] += d * volume[ik] * to_mol;
                }
            }
        }
    }
    double river_mol[4] = {0.0, 0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < count; ++i) for (int m = 0; m < 4; ++m) river_mol[m] += river_in[i][m] * dt * to_mol;
    s.river_carbon_mol += river_mol[0];
    s.river_oxygen_mol += river_mol[2];
    s.river_nitrogen_mol += river_mol[3];
    s.boundary_carbon_mol += boundary[0];
    s.boundary_oxygen_mol += boundary[2];
    s.boundary_nitrogen_mol += boundary[3];
    const double m1 = inventory(s.dic, s, c), n1 = inventory(s.nitrate, s, c), o1 = inventory(s.oxygen, s, c);
    s.transport_carbon_mol += (m1 - m0) - river_mol[0] - boundary[0];
    s.transport_nitrogen_mol += (n1 - n0) - river_mol[3] - boundary[3];
    s.transport_oxygen_mol += (o1 - o0) - river_mol[2] - boundary[2];
}

}  // namespace

double MarineTracerProfile::at(double depth) const { return deep + (surface - deep) * std::exp(-depth / scale_m); }

double level_top_m(const MarineBgcConfig& c, std::size_t k) { return k == 0 ? 0.0 : c.level_bottoms_m[k - 1]; }

void validate_bgc_config(const MarineBgcConfig& c) {
    validate_transport_grid(c.grid);
    const std::size_t count = c.grid.nx * c.grid.ny;
    if (c.sea.size() != count || c.seafloor_depth_m.size() != count) throw std::invalid_argument("biogeochemistry sea mask and seafloor must match the grid");
    if (c.level_bottoms_m.empty() || c.level_bottoms_m.size() > 60) throw std::invalid_argument("biogeochemistry needs 1-60 levels");
    for (std::size_t k = 0; k < c.level_bottoms_m.size(); ++k) {
        if (!(c.level_bottoms_m[k] > level_top_m(c, k)) || c.level_bottoms_m[k] > 6000.0) throw std::invalid_argument("biogeochemistry level bottoms must increase (below 6000 m)");
    }
    for (std::size_t i = 0; i < count; ++i) if (c.sea[i] && (!std::isfinite(c.seafloor_depth_m[i]) || c.seafloor_depth_m[i] <= 0.0)) throw std::invalid_argument("biogeochemistry seafloor depths must be positive at sea");
    for (const auto* p : {&c.dic, &c.alkalinity, &c.oxygen, &c.nitrate}) {
        require_range(p->surface, 0.0, 6000.0, "profile surface value");
        require_range(p->deep, 0.0, 6000.0, "profile deep value");
        require_range(p->scale_m, 1.0, 10000.0, "profile scale");
    }
    require_range(c.max_growth_per_day, 0.0, 10.0, "max_growth_per_day");
    require_range(c.max_production_umol_n_kg_day, 0.0, 100.0, "max_production_umol_n_kg_day");
    require_range(c.temperature_factor_per_k, 0.0, 0.2, "temperature_factor_per_k");
    require_range(c.nitrate_half_saturation_umol_kg, 0.01, 100.0, "nitrate_half_saturation_umol_kg");
    require_range(c.light_half_saturation_w_m2, 0.1, 1000.0, "light_half_saturation_w_m2");
    require_range(c.par_fraction, 0.0, 1.0, "par_fraction");
    require_range(c.light_attenuation_per_m, 0.001, 5.0, "light_attenuation_per_m");
    require_range(c.martin_exponent, 0.1, 3.0, "martin_exponent");
    require_range(c.rain_ratio, 0.0, 1.0, "rain_ratio");
    require_range(c.carbon_to_nitrogen, 1.0, 20.0, "carbon_to_nitrogen");
    require_range(c.oxygen_to_nitrogen, 1.0, 20.0, "oxygen_to_nitrogen");
    require_range(c.denitrification_nitrate_per_oxygen, 0.0, 2.0, "denitrification_nitrate_per_oxygen");
    require_range(c.horizontal_diffusivity_m2_s, 0.0, 1.0e5, "horizontal_diffusivity_m2_s");
    require_range(c.vertical_diffusivity_m2_s, 0.0, 1.0, "vertical_diffusivity_m2_s");
    require_range(c.ventilation_depth_m, 0.0, 6000.0, "ventilation_depth_m");
    require_range(c.ventilation_time_s, 86400.0, 1.0e12, "ventilation_time_s");
    require_range(c.hypoxia_umol_kg, 0.0, 400.0, "hypoxia_umol_kg");
    require_range(c.anoxia_umol_kg, 0.0, c.hypoxia_umol_kg, "anoxia_umol_kg");
    require_range(c.corrosive_omega, 0.0, 10.0, "corrosive_omega");
    require_range(c.dead_zone_column_fraction, 0.0, 1.0, "dead_zone_column_fraction");
    require_range(c.open_boundary_time_s, 3600.0, 1.0e10, "open_boundary_time_s");
    if (!c.open_boundary.empty()) {
        if (c.open_boundary.size() != count) throw std::invalid_argument("biogeochemistry open boundary must be empty or match the grid");
        for (double v : c.open_boundary) require_range(v, 0.0, 1.0, "open boundary strength");
    }
}

MarineBgcState initialize_bgc(const MarineBgcConfig& c, const MarineBgcForcing& f) {
    validate_bgc_config(c);
    check_forcing(f, c);
    const std::size_t count = c.sea.size(), levels = c.level_bottoms_m.size();
    MarineBgcState s;
    s.levels.assign(count, 0);
    for (auto* v : {&s.dic, &s.alkalinity, &s.oxygen, &s.nitrate}) v->assign(count * levels, 0.0);
    for (auto* v : {&s.co2_flux_mol_m2_s, &s.o2_flux_mol_m2_s, &s.production_mol_c_m2_s, &s.export_mol_c_m2_s, &s.benthic_mol_c_m2_s, &s.surface_pco2_uatm}) v->assign(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        const Column col = column_of(c, i);
        s.levels[i] = col.n;
        const double t0 = f.temperature_k[i * levels], s0 = f.salinity_psu[i * levels];
        const double z0 = 0.5 * thickness(c, col, 0);
        // The surface starts in equilibrium with the air (DIC) and saturated (O2); the offsets fade with depth.
        double dic_offset = 0.0;
        if (c.surface_dic_from_atmosphere) dic_offset = dic_at_equilibrium(f.atmospheric_pco2_uatm[i], c.alkalinity.at(z0), t0, s0) - c.dic.at(z0);
        const double o2_offset = oxygen_solubility_umol_kg(std::max(271.0, std::min(313.0, t0)), std::min(42.0, s0)) - c.oxygen.at(z0);
        for (std::size_t k = 0; k < col.n; ++k) {
            const double z = level_top_m(c, k) + 0.5 * thickness(c, col, k), ik = i * levels + k;
            s.dic[ik] = std::max(0.0, c.dic.at(z) + dic_offset * std::exp(-(z - z0) / c.dic.scale_m));
            s.alkalinity[ik] = c.alkalinity.at(z);
            s.oxygen[ik] = std::max(0.0, c.oxygen.at(z) + o2_offset * std::exp(-(z - z0) / c.oxygen.scale_m));
            s.nitrate[ik] = c.nitrate.at(z);
        }
    }
    s.reference_dic = s.dic;
    s.reference_alkalinity = s.alkalinity;
    s.reference_oxygen = s.oxygen;
    s.reference_nitrate = s.nitrate;
    s.carbon_inventory_mol = inventory(s.dic, s, c);
    s.nitrogen_inventory_mol = inventory(s.nitrate, s, c);
    s.oxygen_inventory_mol = inventory(s.oxygen, s, c);
    return s;
}

MarineBgcState advance_bgc(const MarineBgcState& previous, const MarineBgcForcing& f, const MarineBgcConfig& c, double dt) {
    validate_bgc_config(c);
    check_forcing(f, c);
    if (!(dt > 0.0) || dt > 10.0 * 86400.0) throw std::invalid_argument("biogeochemistry timestep must lie within 0-10 days");
    const auto& g = c.grid;
    const std::size_t count = c.sea.size(), levels = c.level_bottoms_m.size();
    if (previous.dic.size() != count * levels || previous.levels.size() != count) throw std::invalid_argument("biogeochemistry state does not match the configuration");
    const double area = g.spacing_east_m * g.spacing_north_m;
    MarineBgcState s = previous;
    std::array<std::vector<double>*, 4> tracers{&s.dic, &s.alkalinity, &s.oxygen, &s.nitrate};
    std::vector<Column> cols(count);
    for (std::size_t i = 0; i < count; ++i) cols[i] = column_of(c, i);
    auto dz = [&](std::size_t i, std::size_t k) { return thickness(c, cols[i], k); };
    // 1-2. Transport and rivers in flux form (see transport_step).
    transport_step(s, f, c, cols, dt);

    // 3-6. The column physics, chemistry and biology of every sea cell.
    const double vent = 1.0 - std::exp(-dt / c.ventilation_time_s);
    std::vector<double> thick(levels), remin(levels), work(levels), a(levels), b(levels), cc(levels);
    for (std::size_t i = 0; i < count; ++i) {
        s.co2_flux_mol_m2_s[i] = s.o2_flux_mol_m2_s[i] = s.production_mol_c_m2_s[i] = s.export_mol_c_m2_s[i] = s.benthic_mol_c_m2_s[i] = s.surface_pco2_uatm[i] = 0.0;
        if (!c.sea[i]) continue;
        const std::size_t n = s.levels[i], base = i * levels;
        for (std::size_t k = 0; k < n; ++k) thick[k] = dz(i, k);
        // 3. Mixing: the mixed layer is homogeneous; implicit diffusion below and across its base.
        const double mld = std::min(f.mixed_layer_depth_m[i], cols[i].bottom);
        std::size_t mixed = 0;
        while (mixed < n && level_top_m(c, mixed) < mld) ++mixed;
        for (auto* v : tracers) {
            if (mixed > 1) {
                double content = 0.0, depth = 0.0;
                for (std::size_t k = 0; k < mixed; ++k) { content += (*v)[base + k] * thick[k]; depth += thick[k]; }
                for (std::size_t k = 0; k < mixed; ++k) (*v)[base + k] = content / depth;
            }
            if (c.vertical_diffusivity_m2_s > 0.0 && n > 1) {
                for (std::size_t k = 0; k < n; ++k) {
                    const double up = k > 0 ? c.vertical_diffusivity_m2_s * dt / (0.5 * (thick[k - 1] + thick[k])) : 0.0;
                    const double down = k + 1 < n ? c.vertical_diffusivity_m2_s * dt / (0.5 * (thick[k] + thick[k + 1])) : 0.0;
                    a[k] = -up; cc[k] = -down; b[k] = thick[k] + up + down; work[k] = thick[k] * (*v)[base + k];
                }
                for (std::size_t k = 1; k < n; ++k) { const double w = a[k] / b[k - 1]; b[k] -= w * cc[k - 1]; work[k] -= w * work[k - 1]; }
                (*v)[base + n - 1] = work[n - 1] / b[n - 1];
                for (std::size_t k = n - 1; k-- > 0;) (*v)[base + k] = (work[k] - cc[k] * (*v)[base + k + 1]) / b[k];
            }
        }
        // 4. Air-sea exchange of CO2 and O2 through the open water.
        const double open = f.ice_fraction.empty() ? 1.0 : std::max(0.0, 1.0 - f.ice_fraction[i]);
        const double ts = f.temperature_k[base], ss = std::max(0.0, f.salinity_psu[base]);
        const CarbonateState surface = solve_carbonate(std::max(1.0, s.dic[base]), std::max(1.0, s.alkalinity[base]), ts, ss);
        s.surface_pco2_uatm[i] = surface.pco2_uatm;
        if (open > 0.0) {
            const double k0 = carbonate_constants(ts, ss).k0;
            const double kco2 = open * gas_transfer_velocity_m_s(f.wind_speed_m_s[i], co2_schmidt_number(ts));
            const double dp = f.atmospheric_pco2_uatm[i] - surface.pco2_uatm;
            double ddic = kco2 * k0 * dp * dt / thick[0];   // umol/kg: (m/s)(mol/kg/atm)(uatm)(s)/m
            // Never more than 90 % of the way to equilibrium in one step (buffer factor about 10).
            const double to_equilibrium = dp / std::max(1.0, surface.pco2_uatm) * s.dic[base] / 10.0;
            if (std::abs(ddic) > 0.9 * std::abs(to_equilibrium)) ddic = 0.9 * to_equilibrium;
            s.dic[base] += ddic;
            s.co2_flux_mol_m2_s[i] = ddic * thick[0] * kRho * 1.0e-6 / dt;
            s.air_sea_carbon_mol += ddic * thick[0] * kRho * 1.0e-6 * area;
            const double sat = oxygen_solubility_umol_kg(std::max(271.0, std::min(313.0, ts)), std::min(42.0, ss));
            const double ko2 = open * gas_transfer_velocity_m_s(f.wind_speed_m_s[i], o2_schmidt_number(ts));
            const double do2 = (sat - s.oxygen[base]) * (1.0 - std::exp(-ko2 * dt / thick[0]));
            s.oxygen[base] += do2;
            s.o2_flux_mol_m2_s[i] = do2 * thick[0] * kRho * 1.0e-6 / dt;
            s.air_sea_oxygen_mol += do2 * thick[0] * kRho * 1.0e-6 * area;
        }
        // 5. Production in the light, export down the Martin curve, respiration on the way and at the seafloor.
        std::fill(remin.begin(), remin.begin() + static_cast<long>(n), 0.0);
        double pic_exported = 0.0, column_production = 0.0, leaving = 0.0, benthic = 0.0;
        const double par = f.shortwave_w_m2[i] * c.par_fraction * open;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t ik = base + k;
            const double z_mid = level_top_m(c, k) + 0.5 * thick[k];
            const double light = par * std::exp(-c.light_attenuation_per_m * z_mid);
            if (light < 0.1 || s.nitrate[ik] <= 0.0) continue;
            const double no3 = s.nitrate[ik];
            const double factor = std::exp(c.temperature_factor_per_k * (f.temperature_k[ik] - 293.15)) * light / (light + c.light_half_saturation_w_m2);
            // First order in scarce nitrate (exact over the step), capped by the light-saturated production in eutrophic water.
            const double scarce = no3 * (1.0 - std::exp(-c.max_growth_per_day / 86400.0 * factor * dt));
            const double ceiling = c.max_production_umol_n_kg_day / 86400.0 * factor * no3 / (no3 + c.nitrate_half_saturation_umol_kg) * dt;
            const double uptake = std::min({scarce, ceiling, no3});
            if (!(uptake > 0.0)) continue;
            const double carbon = c.carbon_to_nitrogen * uptake, calcite = c.rain_ratio * carbon;
            s.nitrate[ik] -= uptake;
            s.dic[ik] -= carbon + calcite;
            s.alkalinity[ik] += uptake - 2.0 * calcite;
            s.oxygen[ik] += c.oxygen_to_nitrogen * uptake;
            column_production += carbon * thick[k];
            pic_exported += calcite * thick[k];
            // The organic nitrogen sinks from the level bottom: F(z) = E (z / z_b)^-b.
            const double exported = uptake * thick[k], zb = c.level_bottoms_m[k];
            double above = 1.0;   // the share still sinking at the top of level j
            for (std::size_t j = k + 1; j < n; ++j) {
                const double below = std::pow((level_top_m(c, j) + thick[j]) / zb, -c.martin_exponent);
                remin[j] += exported * (above - below) / thick[j];
                above = below;
            }
            if (k + 1 == n) above = 1.0;   // produced in the bottom level: it all reaches the bottom of the column
            if (cols[i].shelf) { remin[n - 1] += exported * above / thick[n - 1]; benthic += exported * above; }
            else leaving += exported * above;
        }
        // CaCO3 dissolves at the seafloor on shelves and leaves open columns.
        if (cols[i].shelf) { s.dic[base + n - 1] += pic_exported / thick[n - 1]; s.alkalinity[base + n - 1] += 2.0 * pic_exported / thick[n - 1]; }
        for (std::size_t k = 0; k < n; ++k) {
            const double r = remin[k];
            if (!(r > 0.0)) continue;
            const std::size_t ik = base + k;
            s.nitrate[ik] += r;
            s.dic[ik] += c.carbon_to_nitrogen * r;
            s.alkalinity[ik] -= r;
            const double demand = c.oxygen_to_nitrogen * r;
            if (s.oxygen[ik] >= demand) { s.oxygen[ik] -= demand; continue; }
            // Oxygen runs out: nitrate takes over (denitrification), then the demand is sulfidic.
            const double deficit = demand - s.oxygen[ik];
            s.oxygen[ik] = 0.0;
            const double nitrate_used = std::min(s.nitrate[ik], c.denitrification_nitrate_per_oxygen * deficit);
            s.nitrate[ik] -= nitrate_used;
            s.denitrified_nitrogen_mol += nitrate_used * thick[k] * kRho * 1.0e-6 * area;
            const double unmet = deficit - (c.denitrification_nitrate_per_oxygen > 0.0 ? nitrate_used / c.denitrification_nitrate_per_oxygen : 0.0);
            s.sulfidic_oxygen_demand_mol += std::max(0.0, unmet) * thick[k] * kRho * 1.0e-6 * area;
        }
        s.production_mol_c_m2_s[i] = column_production * kRho * 1.0e-6 / dt;
        s.export_mol_c_m2_s[i] = (c.carbon_to_nitrogen * leaving + (cols[i].shelf ? 0.0 : pic_exported)) * kRho * 1.0e-6 / dt;
        s.benthic_mol_c_m2_s[i] = c.carbon_to_nitrogen * benthic * kRho * 1.0e-6 / dt;
        s.export_carbon_mol += s.export_mol_c_m2_s[i] * dt * area;
        s.export_nitrogen_mol += leaving * kRho * 1.0e-6 * area;
        // 6. Ventilation of the deep levels toward the reference profiles.
        for (std::size_t k = 0; k < n; ++k) {
            if (level_top_m(c, k) < c.ventilation_depth_m) continue;
            const std::size_t ik = base + k;
            const double w = thick[k] * kRho * 1.0e-6 * area;
            const double ddic = (s.reference_dic[ik] - s.dic[ik]) * vent, do2 = (s.reference_oxygen[ik] - s.oxygen[ik]) * vent, dno3 = (s.reference_nitrate[ik] - s.nitrate[ik]) * vent;
            s.dic[ik] += ddic; s.oxygen[ik] += do2; s.nitrate[ik] += dno3;
            s.alkalinity[ik] += (s.reference_alkalinity[ik] - s.alkalinity[ik]) * vent;
            s.ventilation_carbon_mol += ddic * w; s.ventilation_oxygen_mol += do2 * w; s.ventilation_nitrogen_mol += dno3 * w;
        }
        for (std::size_t k = 0; k < n; ++k) for (auto* v : tracers) (*v)[base + k] = std::max(0.0, (*v)[base + k]);
    }
    s.elapsed_seconds = previous.elapsed_seconds + dt;
    s.carbon_inventory_mol = inventory(s.dic, s, c);
    s.nitrogen_inventory_mol = inventory(s.nitrate, s, c);
    s.oxygen_inventory_mol = inventory(s.oxygen, s, c);
    return s;
}

MarineHabitat assess_habitat(const MarineBgcState& s, const MarineBgcForcing& f, const MarineBgcConfig& c) {
    validate_bgc_config(c);
    check_forcing(f, c);
    const std::size_t count = c.sea.size(), levels = c.level_bottoms_m.size();
    const double area_km2 = c.grid.spacing_east_m * c.grid.spacing_north_m / 1.0e6;
    MarineHabitat h;
    h.level_class.assign(count * levels, -1);
    for (auto* v : {&h.level_ph, &h.level_omega_aragonite, &h.level_metabolic_index}) v->assign(count * levels, 0.0);
    h.column_class.assign(count, -1);
    for (auto* v : {&h.habitable_fraction, &h.hypoxic_thickness_m, &h.bottom_oxygen_umol_kg, &h.minimum_oxygen_umol_kg, &h.minimum_oxygen_depth_m, &h.surface_ph, &h.surface_omega_aragonite, &h.bottom_omega_aragonite, &h.surface_pco2_uatm}) v->assign(count, 0.0);
    h.dead_zone = std::vector<unsigned char>(count, 0);
    h.corrosive_bottom = h.dead_zone;
    h.corrosive_surface = h.dead_zone;
    double ph_sum = 0.0, omega_sum = 0.0;
    std::size_t sea = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!c.sea[i]) continue;
        const Column col = column_of(c, i);
        const std::size_t n = s.levels[i], base = i * levels;
        double total = 0.0, habitable = 0.0, hypoxic = 0.0, min_o2 = 1.0e9, min_depth = 0.0;
        int worst = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t ik = base + k;
            const double t = f.temperature_k[ik], sal = std::max(0.0, f.salinity_psu[ik]), o2 = s.oxygen[ik], dz = thickness(c, col, k);
            const CarbonateState cs = solve_carbonate(std::max(1.0, s.dic[ik]), std::max(1.0, s.alkalinity[ik]), t, sal);
            const double phi = metabolic_index(o2, t, sal, c.metabolic);
            h.level_ph[ik] = cs.ph_total;
            h.level_omega_aragonite[ik] = cs.omega_aragonite;
            h.level_metabolic_index[ik] = phi;
            int cls = 0;
            if (o2 < c.anoxia_umol_kg) cls = 3;
            else if (o2 < c.hypoxia_umol_kg) cls = 2;
            else if (cs.omega_aragonite < c.corrosive_omega || phi < c.metabolic.ecological_threshold) cls = 1;
            h.level_class[ik] = cls;
            worst = std::max(worst, cls);
            total += dz;
            if (cls == 0) habitable += dz;
            if (cls >= 2) hypoxic += dz;
            if (o2 < min_o2) { min_o2 = o2; min_depth = level_top_m(c, k) + 0.5 * dz; }
            if (k == 0) { h.surface_ph[i] = cs.ph_total; h.surface_omega_aragonite[i] = cs.omega_aragonite; h.surface_pco2_uatm[i] = cs.pco2_uatm; h.corrosive_surface[i] = cs.omega_aragonite < c.corrosive_omega; }
            if (k + 1 == n) { h.bottom_oxygen_umol_kg[i] = o2; h.bottom_omega_aragonite[i] = cs.omega_aragonite; h.corrosive_bottom[i] = cs.omega_aragonite < c.corrosive_omega; }
        }
        h.column_class[i] = worst;
        h.habitable_fraction[i] = total > 0.0 ? habitable / total : 0.0;
        h.hypoxic_thickness_m[i] = hypoxic;
        h.minimum_oxygen_umol_kg[i] = min_o2;
        h.minimum_oxygen_depth_m[i] = min_depth;
        h.dead_zone[i] = h.bottom_oxygen_umol_kg[i] < c.hypoxia_umol_kg || (total > 0.0 && hypoxic / total >= c.dead_zone_column_fraction);
        h.sea_area_km2 += area_km2;
        if (h.dead_zone[i]) h.dead_zone_area_km2 += area_km2;
        if (worst == 3) h.anoxic_area_km2 += area_km2;
        if (h.corrosive_surface[i] || h.corrosive_bottom[i]) h.corrosive_area_km2 += area_km2;
        h.volume_km3 += total * area_km2 / 1000.0;
        h.hypoxic_volume_km3 += hypoxic * area_km2 / 1000.0;
        h.habitable_volume_km3 += habitable * area_km2 / 1000.0;
        ph_sum += h.surface_ph[i];
        omega_sum += h.surface_omega_aragonite[i];
        ++sea;
    }
    h.mean_surface_ph = sea ? ph_sum / static_cast<double>(sea) : 0.0;
    h.mean_surface_omega_aragonite = sea ? omega_sum / static_cast<double>(sea) : 0.0;
    return h;
}

}  // namespace terraflux::weather
