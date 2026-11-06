#include "terraflux/weather/cosmic_dust.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSigma = 5.670374419e-8;
constexpr double kGas = 8.314462618;
constexpr double kDeg = kPi / 180.0;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

// US Standard Atmosphere 1976.
constexpr double kAltitudeKm[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 180, 200};
constexpr double kDensity[] = {1.225, 4.135e-1, 8.891e-2, 1.841e-2, 3.996e-3, 1.027e-3, 3.097e-4, 8.283e-5, 1.846e-5, 3.416e-6,
                               5.604e-7, 9.708e-8, 2.222e-8, 8.152e-9, 3.831e-9, 2.076e-9, 1.233e-9, 5.194e-10, 2.541e-10};
constexpr double kPressure[] = {101325.0, 26500.0, 5529.3, 1197.0, 287.14, 79.779, 21.958, 5.2209, 1.0524, 0.18359,
                                0.032011, 7.1042e-3, 2.5382e-3, 1.2505e-3, 7.2028e-4, 4.5422e-4, 3.0395e-4, 1.5271e-4, 8.4736e-5};

double log_table(const double* values, double altitude_m) {
    const double km = altitude_m / 1000.0;
    constexpr std::size_t n = sizeof(kAltitudeKm) / sizeof(kAltitudeKm[0]);
    std::size_t k = 1;
    if (km <= kAltitudeKm[0]) k = 1;
    else if (km >= kAltitudeKm[n - 1]) k = n - 1;
    else while (kAltitudeKm[k] < km) ++k;
    const double w = (km - kAltitudeKm[k - 1]) / (kAltitudeKm[k] - kAltitudeKm[k - 1]);
    return std::exp(std::log(values[k - 1]) + w * (std::log(values[k]) - std::log(values[k - 1])));
}

std::size_t bin_of(const std::vector<double>& edges, double altitude) {
    if (altitude <= edges.front()) return 0;
    if (altitude >= edges.back()) return edges.size() - 2;
    return static_cast<std::size_t>(std::upper_bound(edges.begin(), edges.end(), altitude) - edges.begin()) - 1;
}

}  // namespace

// ---- Sizes --------------------------------------------------------------------------------------------------------

double gruen_cumulative_flux(double m) {
    constexpr double c4 = 2.2e3, c5 = 15.0, g4 = 0.306, g5 = -4.38;
    constexpr double c6 = 1.3e-9, c7 = 1.0e11, c8 = 1.0e27, g6 = 2.0, g7 = 4.0, g8 = -0.36;
    constexpr double c9 = 1.3e-16, c10 = 1.0e6, g9 = -0.85;
    return std::pow(c4 * std::pow(m, g4) + c5, g5) + c6 * std::pow(m + c7 * std::pow(m, g6) + c8 * std::pow(m, g7), g8) +
           c9 * std::pow(m + c10 * std::pow(m, g6), g9);
}

namespace {
// Mass flux (g m^-2 s^-1) between two masses by sub-binning the cumulative number flux.
double gruen_mass_between(double lo, double hi) {
    const int sub = 40;
    double s = 0.0;
    for (int i = 0; i < sub; ++i) {
        const double a = lo * std::pow(hi / lo, static_cast<double>(i) / sub), b = lo * std::pow(hi / lo, static_cast<double>(i + 1) / sub);
        s += (gruen_cumulative_flux(a) - gruen_cumulative_flux(b)) * std::sqrt(a * b);
    }
    return s;
}
}  // namespace

std::vector<DustMassBin> gruen_mass_bins(double min_g, double max_g, std::size_t bins) {
    require(min_g > 0.0 && max_g > min_g && max_g <= 1.0e6 && min_g >= 1.0e-20, "dust mass range must satisfy 1e-20 <= min < max <= 1e6 g");
    require(bins >= 2 && bins <= 400, "dust mass bins must number 2-400");
    std::vector<DustMassBin> out;
    double mass_total = 0.0, number_total = 0.0;
    for (std::size_t k = 0; k < bins; ++k) {
        DustMassBin b;
        b.lower_g = min_g * std::pow(max_g / min_g, static_cast<double>(k) / static_cast<double>(bins));
        b.upper_g = min_g * std::pow(max_g / min_g, static_cast<double>(k + 1) / static_cast<double>(bins));
        b.mass_g = std::sqrt(b.lower_g * b.upper_g);
        b.mass_fraction = gruen_mass_between(b.lower_g, b.upper_g);
        b.number_fraction = gruen_cumulative_flux(b.lower_g) - gruen_cumulative_flux(b.upper_g);
        mass_total += b.mass_fraction;
        number_total += b.number_fraction;
        out.push_back(b);
    }
    for (auto& b : out) { b.mass_fraction /= mass_total; b.number_fraction /= number_total; }
    return out;
}

double gruen_mass_flux_t_per_day(double min_g, double max_g, double earth_radius_m) {
    return gruen_mass_between(min_g, max_g) * 1.0e-3 * 4.0 * kPi * earth_radius_m * earth_radius_m * 86400.0 / 1000.0;
}

double standard_air_density_kg_m3(double altitude_m) { return log_table(kDensity, altitude_m); }
double standard_air_pressure_pa(double altitude_m) { return log_table(kPressure, altitude_m); }

// ---- Entry --------------------------------------------------------------------------------------------------------

void validate_dust_entry(const DustEntryConfig& c) {
    require(within(c.particle_density_kg_m3, 100.0, 8000.0), "entry.particle_density_kg_m3 must lie within 100-8000");
    require(within(c.specific_heat_j_kg_k, 100.0, 5000.0), "entry.specific_heat_j_kg_k must lie within 100-5000");
    require(within(c.heat_transfer, 0.05, 1.0) && within(c.drag, 0.1, 3.0) && within(c.emissivity, 0.1, 1.0), "entry: heat_transfer within 0.05-1, drag within 0.1-3, emissivity within 0.1-1");
    require(within(c.latent_heat_j_kg, 1.0e5, 2.0e7), "entry.latent_heat_j_kg must lie within 1e5-2e7");
    require(within(c.vapour_a, 0.0, 20.0) && within(c.vapour_b, 1000.0, 100000.0) && within(c.vapour_molar_mass_kg, 0.001, 0.5), "entry vapour pressure: A within 0-20, B within 1000-100000 K, molar mass within 0.001-0.5 kg");
    require(within(c.melt_start_k, 500.0, 3000.0) && c.melt_full_k > c.melt_start_k && c.melt_full_k <= 3500.0, "entry: melt_start_k < melt_full_k within 500-3500");
    require(within(c.dehydration_start_k, 300.0, 2000.0) && c.dehydration_end_k > c.dehydration_start_k && c.dehydration_end_k <= 2500.0, "entry: dehydration_start_k < dehydration_end_k within 300-2500");
    require(within(c.start_altitude_m, 120.0e3, 200.0e3), "entry.start_altitude_m must lie within 120-200 km");
    require(within(c.air_temperature_k, 150.0, 350.0) && within(c.max_step_m, 1.0, 1000.0), "entry: air_temperature_k within 150-350, max_step_m within 1-1000");
}

const char* dust_fate_name(DustFate f) {
    switch (f) {
        case DustFate::unmelted: return "unmelted";
        case DustFate::partially_melted: return "partially_melted";
        case DustFate::melted: return "melted";
        case DustFate::vaporised: return "vaporised";
    }
    return "unmelted";
}

DustTrajectory simulate_dust_entry(double mass_kg, double speed, double zenith_deg, const DustEntryConfig& c, const std::vector<double>& edges) {
    require(mass_kg > 0.0 && std::isfinite(mass_kg), "dust particle mass must be positive");
    require(within(speed, 11.0e3, 73.0e3), "dust entry speed must lie within 11-73 km/s");
    require(within(zenith_deg, 0.0, 85.0), "dust entry zenith angle must lie within 0-85 deg");
    require(edges.size() >= 2 && std::is_sorted(edges.begin(), edges.end()), "dust altitude bins need ascending edges");
    DustTrajectory out;
    out.initial_mass_kg = mass_kg;
    out.speed_m_s = speed;
    out.zenith_deg = zenith_deg;
    const std::size_t nb = edges.size() - 1;
    out.ablated_fraction_by_bin.assign(nb, 0.0);
    out.water_fraction_by_bin.assign(nb, 0.0);
    out.energy_j_by_bin.assign(nb, 0.0);
    const double cz = std::cos(zenith_deg * kDeg);
    const double m0 = mass_kg, ln10 = std::log(10.0);
    double m = mass_kg, v = speed, h = c.start_altitude_m, T = c.air_temperature_k;
    double peak = T, peak_h = h, water_released = 0.0;
    auto radius = [&](double mass) { return std::cbrt(3.0 * mass / (4.0 * kPi * c.particle_density_kg_m3)); };
    auto dehydrated = [&](double t) { return std::clamp((t - c.dehydration_start_k) / (c.dehydration_end_k - c.dehydration_start_k), 0.0, 1.0); };
    auto evaporation = [&](double t) {   // kg m^-2 s^-1 (Langmuir)
        return std::pow(10.0, c.vapour_a - c.vapour_b / t) * std::sqrt(c.vapour_molar_mass_kg / (2.0 * kPi * kGas * t));
    };
    for (int step = 0; step < 200000; ++step) {
        if (h < 10.0e3 || m < 1.0e-6 * m0) break;
        const double rho = standard_air_density_kg_m3(h);
        const double r = radius(m), area = kPi * r * r, surface = 4.0 * kPi * r * r;
        const double dt = std::min(0.01, c.max_step_m / std::max(v, 1.0));
        const double heating = 0.5 * c.heat_transfer * rho * v * v * v * area;
        // Implicit temperature with radiation and evaporative cooling: Newton on
        // f(T') = c m (T' - T) / dt - heating + eps sigma S (T'^4 - Ta^4) + L J(T') S.
        double t = std::max(T, c.air_temperature_k);
        for (int it = 0; it < 40; ++it) {
            const double j = evaporation(t);
            const double dj = j * (c.vapour_b * ln10 / (t * t) - 0.5 / t);
            const double f = c.specific_heat_j_kg_k * m * (t - T) / dt - heating + c.emissivity * kSigma * surface * (t * t * t * t - std::pow(c.air_temperature_k, 4.0)) + c.latent_heat_j_kg * j * surface;
            const double df = c.specific_heat_j_kg_k * m / dt + 4.0 * c.emissivity * kSigma * surface * t * t * t + c.latent_heat_j_kg * dj * surface;
            const double next = std::clamp(t - f / df, c.air_temperature_k, 6000.0);
            const bool done = std::abs(next - t) < 1e-6 * t;
            t = next;
            if (done) break;
        }
        T = t;
        const std::size_t bin = bin_of(edges, h);
        const double lost = std::min(m, evaporation(T) * surface * dt);
        // Water leaves with the heating (dehydration) and with the evaporating mass.
        const double before = water_released;
        water_released = std::max(water_released, dehydrated(T));
        water_released += (1.0 - water_released) * (lost / m);
        water_released = std::min(1.0, water_released);
        out.water_fraction_by_bin[bin] += water_released - before;
        if (T > peak) { peak = T; peak_h = h; }
        // Kinetic energy handed to the air: the particle's deceleration and the evaporated mass's motion.
        const double dv = (-c.drag * rho * area * v * v / m + 9.80665 * cz) * dt;
        const double kinetic_before = 0.5 * m * v * v;
        m -= lost;
        out.ablated_fraction_by_bin[bin] += lost / m0;
        v = std::max(0.0, v + dv);
        out.energy_j_by_bin[bin] += std::max(0.0, kinetic_before - 0.5 * m * v * v);
        h -= v * cz * dt;
        // Done once slow and cool: the heating is over, the rest is a gentle fall to the ground.
        if (v < 300.0 && T < c.air_temperature_k + 50.0) break;
    }
    out.peak_temperature_k = peak;
    out.peak_altitude_m = peak_h;
    out.surviving_fraction = m < 1.0e-6 * m0 ? 0.0 : m / m0;
    out.water_released_fraction = out.surviving_fraction == 0.0 ? 1.0 : water_released;
    if (out.surviving_fraction == 0.0) {
        // Whatever water was left leaves with the last of the mass, where it vanished.
        const double left = 1.0 - std::accumulate(out.water_fraction_by_bin.begin(), out.water_fraction_by_bin.end(), 0.0);
        if (left > 0.0) out.water_fraction_by_bin[bin_of(edges, h)] += left;
        out.ablated_fraction_by_bin[bin_of(edges, h)] += m / m0;
    }
    out.fate = out.surviving_fraction < 0.01 ? DustFate::vaporised : peak >= c.melt_full_k ? DustFate::melted : peak >= c.melt_start_k ? DustFate::partially_melted : DustFate::unmelted;
    return out;
}

// ---- Population ---------------------------------------------------------------------------------------------------

std::vector<DustSpeed> default_dust_speeds() {
    // Mass-weighted entry speeds dominated by slow asteroidal and Jupiter-family-comet dust, with a tail of fast
    // Halley-type and Oort-cloud-comet dust (after the source mix of Carrillo-Sanchez et al. 2016, rounded).
    return {{11.8, 0.16}, {13.0, 0.22}, {15.0, 0.20}, {18.0, 0.14}, {24.0, 0.10}, {33.0, 0.08}, {48.0, 0.06}, {62.0, 0.04}};
}

void validate_dust_population(const DustPopulationConfig& p) {
    require(within(p.influx_t_per_day, 0.0, 1.0e7), "population.influx_t_per_day must lie within 0-1e7");
    require(p.mass_bins >= 4 && p.mass_bins <= 200, "population.mass_bins must lie within 4-200");
    require(p.min_mass_g >= 1.0e-18 && p.max_mass_g > p.min_mass_g && p.max_mass_g <= 1.0e3, "population mass range must satisfy 1e-18 <= min < max <= 1000 g");
    require(!p.speeds.empty() && p.speeds.size() <= 30, "population needs 1-30 entry speeds");
    for (const auto& s : p.speeds) require(within(s.speed_km_s, 11.2, 72.8) && within(s.weight, 0.0, 1.0e6), "entry speeds must lie within 11.2-72.8 km/s with non-negative weights");
    require(!p.zenith_deg.empty() && p.zenith_deg.size() <= 20, "population needs 1-20 zenith angles");
    for (double z : p.zenith_deg) require(within(z, 0.0, 85.0), "zenith angles must lie within 0-85 deg");
    const auto& k = p.composition;
    for (double f : {k.water, k.iron, k.sodium, k.magnesium, k.silicon, k.nickel}) require(within(f, 0.0, 1.0), "composition fractions must lie within 0-1");
    require(k.water + k.iron + k.sodium + k.magnesium + k.silicon + k.nickel <= 1.0, "composition fractions must add up to at most 1");
}

DustPopulationResult simulate_dust_population(const DustPopulationConfig& p, const DustEntryConfig& e, const std::vector<double>& edges) {
    validate_dust_population(p);
    validate_dust_entry(e);
    DustPopulationResult out;
    out.altitude_edges_m = edges;
    const std::size_t nb = edges.size() - 1;
    out.ablation_kg_s_by_bin.assign(nb, 0.0);
    out.water_kg_s_by_bin.assign(nb, 0.0);
    out.energy_w_by_bin.assign(nb, 0.0);
    out.influx_kg_s = p.influx_t_per_day * 1000.0 / 86400.0;
    double speed_total = 0.0;
    for (const auto& s : p.speeds) speed_total += s.weight;
    require(speed_total > 0.0, "entry speed weights must not all be zero");
    const auto bins = gruen_mass_bins(p.min_mass_g, p.max_mass_g, p.mass_bins);
    const double angle_weight = 1.0 / static_cast<double>(p.zenith_deg.size());
    for (const auto& b : bins) {
        DustBinResult r;
        r.bin = b;
        r.mass_flux_kg_s = out.influx_kg_s * b.mass_fraction;
        for (const auto& s : p.speeds) {
            for (double z : p.zenith_deg) {
                const double w = s.weight / speed_total * angle_weight;
                const auto t = simulate_dust_entry(b.mass_g * 1.0e-3, s.speed_km_s * 1000.0, z, e, edges);
                const double flux = r.mass_flux_kg_s * w;   // kg/s of this bin, speed and angle
                r.surviving_fraction += w * t.surviving_fraction;
                r.fate_fraction[static_cast<int>(t.fate)] += w;
                r.water_released_fraction += w * t.water_released_fraction;
                r.mean_peak_temperature_k += w * t.peak_temperature_k;
                const double particles_per_s = flux / (b.mass_g * 1.0e-3);
                out.kinetic_power_w += particles_per_s * 0.5 * b.mass_g * 1.0e-3 * std::pow(s.speed_km_s * 1000.0, 2.0);
                for (std::size_t k = 0; k < nb; ++k) {
                    out.ablation_kg_s_by_bin[k] += flux * t.ablated_fraction_by_bin[k];
                    out.water_kg_s_by_bin[k] += flux * p.composition.water * t.water_fraction_by_bin[k];
                    out.energy_w_by_bin[k] += particles_per_s * t.energy_j_by_bin[k];
                }
                const double survivors = flux * t.surviving_fraction;
                out.surviving_kg_s += survivors;
                if (t.surviving_fraction > 0.0) {
                    const double diameter = 2.0 * std::cbrt(3.0 * b.mass_g * 1.0e-3 * t.surviving_fraction / (4.0 * kPi * e.particle_density_kg_m3));
                    if (diameter >= 50.0e-6) out.survivors_over_50um_per_s += particles_per_s;
                    if (diameter >= 100.0e-6) out.survivors_over_100um_per_s += particles_per_s;
                }
                if (t.fate != DustFate::vaporised) out.surviving_by_fate_kg_s[static_cast<int>(t.fate)] += survivors;
                else out.surviving_by_fate_kg_s[static_cast<int>(DustFate::melted)] += survivors;   // a residue below 1 %
                out.water_to_surface_kg_s += flux * p.composition.water * (1.0 - t.water_released_fraction);
            }
        }
        out.bins.push_back(r);
    }
    out.water_in_kg_s = out.influx_kg_s * p.composition.water;
    for (std::size_t k = 0; k < nb; ++k) {
        out.ablated_kg_s += out.ablation_kg_s_by_bin[k];
        out.water_released_kg_s += out.water_kg_s_by_bin[k];
        if (edges[k] >= 80.0e3) out.water_released_above_80km_kg_s += out.water_kg_s_by_bin[k];
    }
    auto median = [&](const std::vector<double>& v) {
        double total = 0.0;
        for (double x : v) total += x;
        if (!(total > 0.0)) return 0.0;
        double s = 0.0;
        for (std::size_t k = 0; k < v.size(); ++k) {
            if (s + v[k] >= 0.5 * total) return edges[k] + (0.5 * total - s) / v[k] * (edges[k + 1] - edges[k]);
            s += v[k];
        }
        return edges.back();
    };
    out.median_ablation_altitude_m = median(out.ablation_kg_s_by_bin);
    out.median_water_altitude_m = median(out.water_kg_s_by_bin);
    return out;
}

// ---- Sources on the sky -------------------------------------------------------------------------------------------

std::vector<MeteoroidSource> default_meteoroid_sources() {
    // Sporadic sources (Jones & Brown 1993; Campbell-Brown 2008), weights rounded: the apex, helion and antihelion
    // about 70 deg either side of the apex along the ecliptic, and the northern and southern toroidal sources.
    return {{"apex", 0.0, 0.0, 0.35}, {"helion", 70.0, 0.0, 0.25}, {"antihelion", -70.0, 0.0, 0.25}, {"north toroidal", 0.0, 55.0, 0.075}, {"south toroidal", 0.0, -55.0, 0.075}};
}

double solar_ecliptic_longitude_deg(double d) {
    const double g = (357.528 + 0.9856003 * d) * kDeg;
    const double lambda = 280.460 + 0.9856474 * d + 1.915 * std::sin(g) + 0.020 * std::sin(2.0 * g);
    return std::fmod(std::fmod(lambda, 360.0) + 360.0, 360.0);
}

double greenwich_sidereal_deg(double d) { return std::fmod(std::fmod(280.46061837 + 360.98564736629 * d, 360.0) + 360.0, 360.0); }

std::array<double, 2> meteoroid_source_equatorial(const MeteoroidSource& s, double d) {
    // The apex lies 90 deg west of the Sun on the ecliptic; "toward the Sun" is increasing longitude from the apex.
    const double lambda = (solar_ecliptic_longitude_deg(d) - 90.0 + s.longitude_from_apex_deg) * kDeg, beta = s.ecliptic_latitude_deg * kDeg;
    const double eps = 23.439 * kDeg;
    const double x = std::cos(beta) * std::cos(lambda);
    const double y = std::cos(beta) * std::sin(lambda) * std::cos(eps) - std::sin(beta) * std::sin(eps);
    const double z = std::cos(beta) * std::sin(lambda) * std::sin(eps) + std::sin(beta) * std::cos(eps);
    return {std::atan2(y, x) / kDeg, std::asin(std::clamp(z, -1.0, 1.0)) / kDeg};
}

double relative_dust_influx(double lat, double lon, double d, const std::vector<MeteoroidSource>& sources, double isotropic) {
    double total_weight = 0.0, sum = 0.0;
    const double lst = (greenwich_sidereal_deg(d) + lon) * kDeg, phi = lat * kDeg;
    for (const auto& s : sources) {
        const auto eq = meteoroid_source_equatorial(s, d);
        const double ra = eq[0] * kDeg, dec = eq[1] * kDeg;
        const double cos_psi = std::sin(phi) * std::sin(dec) + std::cos(phi) * std::cos(dec) * std::cos(lst - ra);
        sum += s.weight * 4.0 * std::max(0.0, cos_psi);   // a point source averages 1/4 over the sphere
        total_weight += s.weight;
    }
    return isotropic + (1.0 - isotropic) * (total_weight > 0.0 ? sum / total_weight : 1.0);
}

}  // namespace terraflux::weather
