// Cosmic dust: the Gruen distribution, the entry of single particles, the population and the sky geometry
// (weather.cosmic_dust).
#include "terraflux/weather/cosmic_dust.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace terraflux::weather;

constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    if (!(std::abs(actual - expected) <= tolerance)) throw std::runtime_error(message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, message);
}

std::vector<double> edges_km(double lo, double hi, double step) {
    std::vector<double> e;
    for (double km = lo; km <= hi + 1e-9; km += step) e.push_back(km * 1000.0);
    return e;
}

void sizes() {
    double previous = gruen_cumulative_flux(1.0e-15);
    for (double m = 1.0e-14; m <= 1.0; m *= 10.0) {
        const double f = gruen_cumulative_flux(m);
        require(f > 0.0 && f < previous, "fewer particles above a larger mass");
        previous = f;
    }
    const double t_day = gruen_mass_flux_t_per_day(1.0e-12, 1.0);
    require(t_day > 15.0 && t_day < 30.0, "the Gruen flux over the Earth is about 20 t/day");
    const auto bins = gruen_mass_bins(1.0e-12, 1.0, 36);
    double mass = 0.0, number = 0.0, peak = 0.0, peak_mass = 0.0;
    for (const auto& b : bins) {
        mass += b.mass_fraction;
        number += b.number_fraction;
        require(b.lower_g < b.mass_g && b.mass_g < b.upper_g, "bin centres inside their bins");
        if (b.mass_fraction > peak) { peak = b.mass_fraction; peak_mass = b.mass_g; }
    }
    require_near(mass, 1.0, "mass fractions add up to 1", 1e-12);
    require_near(number, 1.0, "number fractions add up to 1", 1e-12);
    require(peak_mass > 1.0e-7 && peak_mass < 1.0e-4, "most mass arrives in particles of 1e-7 to 1e-4 g (50-500 um)");
    require(bins.front().number_fraction > bins.back().number_fraction * 1e6, "small particles are far more numerous");
    require_rejected([] { gruen_mass_bins(1.0, 1.0e-3, 10); }, "a reversed mass range");
    // The atmosphere table.
    require_near(standard_air_density_kg_m3(0.0), 1.225, "sea-level density", 1e-12);
    require_near(standard_air_density_kg_m3(100.0e3), 5.604e-7, "density at 100 km", 1e-15);
    require_near(standard_air_pressure_pa(80.0e3), 1.0524, "pressure at 80 km", 1e-9);
    double rho = standard_air_density_kg_m3(0.0);
    for (double h = 5.0e3; h <= 200.0e3; h += 5.0e3) { const double r = standard_air_density_kg_m3(h); require(r < rho, "density falls with height"); rho = r; }
}

void entry() {
    const DustEntryConfig c;
    validate_dust_entry(c);
    const auto edges = edges_km(50.0, 180.0, 1.0);
    auto sum = [](const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0); };
    // A tiny slow particle decelerates before it gets hot: unmelted, keeps some water.
    const auto tiny = simulate_dust_entry(1.0e-13, 11.8e3, 20.0, c, edges);
    require(tiny.fate == DustFate::unmelted && tiny.surviving_fraction > 0.99 && tiny.peak_temperature_k < c.melt_start_k, "a small slow particle survives unmelted");
    require(tiny.peak_altitude_m > 80.0e3 && tiny.peak_altitude_m < 130.0e3, "it heats between 80 and 130 km");
    // A large fast one evaporates completely and releases all its water.
    const auto fast = simulate_dust_entry(1.0e-7, 60.0e3, 45.0, c, edges);
    require(fast.fate == DustFate::vaporised && fast.surviving_fraction == 0.0 && fast.water_released_fraction == 1.0, "a large fast particle vaporises");
    require_near(sum(fast.ablated_fraction_by_bin), 1.0, "all its mass is ablated", 1e-9);
    require_near(sum(fast.water_fraction_by_bin), 1.0, "all its water is released", 1e-9);
    // Mass and water bookkeeping for every case, and the trends with speed and size.
    for (double mass : {1.0e-12, 1.0e-9, 1.0e-7, 1.0e-5}) {
        double previous = 2.0;
        for (double speed : {11.8e3, 15.0e3, 20.0e3, 35.0e3, 60.0e3}) {
            const auto t = simulate_dust_entry(mass, speed, 45.0, c, edges);
            require_near(sum(t.ablated_fraction_by_bin) + t.surviving_fraction, 1.0, "ablated + surviving = 1", 1e-9);
            require_near(sum(t.water_fraction_by_bin), t.water_released_fraction, "released water sits in the altitude bins", 1e-9);
            require(t.surviving_fraction <= previous + 1e-12, "faster particles lose more");
            require(t.water_released_fraction >= 0.0 && t.water_released_fraction <= 1.0, "released water share within 0-1");
            if (t.peak_temperature_k >= c.dehydration_end_k) require(t.water_released_fraction > 0.999, "hot particles lose all bound water");
            previous = t.surviving_fraction;
            require(sum(t.energy_j_by_bin) <= mass * (0.5 * speed * speed + 9.80665 * c.start_altitude_m) * (1.0 + 1e-9), "no more energy than the particle brought (kinetic and potential)");
        }
    }
    const auto small = simulate_dust_entry(1.0e-9, 15.0e3, 45.0, c, edges), big = simulate_dust_entry(1.0e-5, 15.0e3, 45.0, c, edges);
    require(big.peak_temperature_k > small.peak_temperature_k && big.surviving_fraction < small.surviving_fraction, "larger particles get hotter (less surface per mass)");
    const auto steep = simulate_dust_entry(1.0e-8, 20.0e3, 10.0, c, edges), grazing = simulate_dust_entry(1.0e-8, 20.0e3, 70.0, c, edges);
    require(grazing.peak_altitude_m > steep.peak_altitude_m, "grazing entries heat higher up");
    // Without evaporation (a very low vapour pressure) the energy goes into the air and into radiation only.
    auto dry = c;
    dry.vapour_a = 0.0;
    const auto refractory = simulate_dust_entry(1.0e-6, 15.0e3, 45.0, dry, edges);
    require(refractory.surviving_fraction > 0.999, "no evaporation, no mass loss");
    require_rejected([&] { simulate_dust_entry(1.0e-9, 5.0e3, 45.0, c, edges); }, "below the escape speed");
    require_rejected([&] { simulate_dust_entry(1.0e-9, 20.0e3, 89.0, c, edges); }, "a grazing angle beyond 85 deg");
    auto bad = c;
    bad.melt_full_k = 1000.0;
    require_rejected([&] { validate_dust_entry(bad); }, "full melting below its start");
}

void population() {
    DustPopulationConfig p;
    p.speeds = default_dust_speeds();
    p.mass_bins = 18;
    const DustEntryConfig e;
    const auto edges = edges_km(50.0, 180.0, 2.0);
    const auto r = simulate_dust_population(p, e, edges);
    require_near(r.influx_kg_s, 60.0 * 1000.0 / 86400.0, "influx in kg/s", 1e-12);
    require_near(r.ablated_kg_s + r.surviving_kg_s, r.influx_kg_s, "ablated + surviving = influx", 1e-9 * r.influx_kg_s);
    require_near(r.surviving_by_fate_kg_s[0] + r.surviving_by_fate_kg_s[1] + r.surviving_by_fate_kg_s[2] + r.surviving_by_fate_kg_s[3], r.surviving_kg_s, "the survivors by state add up", 1e-12);
    require_near(r.water_in_kg_s, r.influx_kg_s * p.composition.water, "water in", 1e-15);
    require_near(r.water_released_kg_s + r.water_to_surface_kg_s, r.water_in_kg_s, "water released + delivered = water in", 1e-9 * r.water_in_kg_s);
    require_near(std::accumulate(r.ablation_kg_s_by_bin.begin(), r.ablation_kg_s_by_bin.end(), 0.0), r.ablated_kg_s, "the ablation profile holds the ablated mass", 1e-12);
    require(r.surviving_kg_s > 0.1 * r.influx_kg_s && r.surviving_kg_s < 0.5 * r.influx_kg_s, "roughly a quarter reaches the ground (calibration)");
    require(r.median_ablation_altitude_m > 80.0e3 && r.median_ablation_altitude_m < 100.0e3, "ablation around 80-100 km, where the metal layers are");
    require(r.water_released_above_80km_kg_s > 0.95 * r.water_released_kg_s, "the water is released high up");
    double mean_square_speed = 0.0, weights = 0.0;
    for (const auto& s : p.speeds) { mean_square_speed += s.weight * s.speed_km_s * s.speed_km_s * 1.0e6; weights += s.weight; }
    require_near(r.kinetic_power_w, 0.5 * r.influx_kg_s * mean_square_speed / weights, "kinetic power = 1/2 influx <v^2>", 1e-9 * r.kinetic_power_w);
    require(r.survivors_over_100um_per_s > 0.0 && r.survivors_over_100um_per_s < r.survivors_over_50um_per_s, "spherules above 100 um are fewer than above 50 um");
    // Everything scales with the influx.
    auto twice = p;
    twice.influx_t_per_day = 120.0;
    const auto r2 = simulate_dust_population(twice, e, edges);
    require_near(r2.surviving_kg_s, 2.0 * r.surviving_kg_s, "linear in the influx", 1e-9 * r.surviving_kg_s);
    auto bad = p;
    bad.composition.water = 0.9;
    require_rejected([&] { validate_dust_population(bad); }, "composition above 1");
    bad = p;
    bad.speeds = {{5.0, 1.0}};
    require_rejected([&] { validate_dust_population(bad); }, "entry speed below 11.2 km/s");
}

void sky() {
    const auto sources = default_meteoroid_sources();
    require_near(greenwich_sidereal_deg(0.0), 280.46061837, "sidereal time at J2000", 1e-9);
    // Equinox near 20 March 2026 (day 9575.1 since J2000): the Sun at longitude 0.
    const double lam = solar_ecliptic_longitude_deg(9575.1);
    require(lam < 1.5 || lam > 358.5, "the Sun at the March equinox");
    // The apex lies on the ecliptic 90 deg west of the Sun.
    const auto apex = meteoroid_source_equatorial(sources[0], 0.0);
    require(std::abs(apex[1]) <= 23.5, "the apex on the ecliptic, within the obliquity");
    for (double d : {0.0, 91.3, 3652.5, 9804.25}) {
        // The area-weighted global mean is 1.
        double mean = 0.0, weight = 0.0;
        for (double lat = -89.5; lat < 90.0; lat += 1.0)
            for (double lon = -179.5; lon < 180.0; lon += 2.0) {
                const double w = std::cos(lat * kPi / 180.0);
                mean += w * relative_dust_influx(lat, lon, d, sources, 0.3);
                weight += w;
            }
        require_near(mean / weight, 1.0, "global mean influx 1", 0.01);
        // The side of the Earth facing the apex (local morning) receives more than the side facing away.
        const auto a = meteoroid_source_equatorial(sources[0], d);
        const double lon = std::fmod(a[0] - greenwich_sidereal_deg(d) + 540.0, 360.0) - 180.0;
        const double front = relative_dust_influx(a[1], lon, d, sources, 0.3), back = relative_dust_influx(-a[1], lon + (lon > 0 ? -180.0 : 180.0), d, sources, 0.3);
        require(front > 2.0 * back, "the morning side sweeps up more dust");
    }
    require_near(relative_dust_influx(10.0, 20.0, 100.0, sources, 1.0), 1.0, "an isotropic sky gives the mean everywhere", 1e-12);
}

}  // namespace

int main() {
    try {
        sizes();
        entry();
        population();
        sky();
        std::cout << "cosmic dust tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
