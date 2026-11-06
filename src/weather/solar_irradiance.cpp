#include "terraflux/weather/solar_irradiance.hpp"

#include "terraflux/weather/ocean_circulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kAstronomicalUnit = 1.495978707e11;        // m
constexpr double kSecondsPerYear = 365.25 * 86400.0;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

}  // namespace

// ---- Proxies -------------------------------------------------------------------------------------------------

std::vector<SolarUvBand> default_solar_uv_bands() {
    return {
        {"lyman_alpha", "Lyman-α 121.6 nm", 121.0, 122.0, 0.0060, 0.55},
        {"o2_herzberg", "200–242 nm (splits O₂: makes ozone)", 200.0, 242.0, 1.7, 0.06},
        {"uv_c", "UV-C 100–280 nm", 100.0, 280.0, 6.3, 0.035},
        {"uv_b", "UV-B 280–315 nm", 280.0, 315.0, 18.5, 0.009},
        {"uv_a", "UV-A 315–400 nm", 315.0, 400.0, 85.0, 0.003},
    };
}

void validate_solar_proxies(const SolarProxyConfig& c, const std::vector<SolarUvBand>& bands) {
    require(within(c.f107_base_sfu, 30.0, 120.0), "irradiance.f107_base_sfu must lie within 30-120");
    require(within(c.f107_per_sunspot, 0.01, 3.0), "irradiance.f107_per_sunspot must lie within 0.01-3");
    require(within(c.f107_per_sunspot_squared, -0.01, 0.01), "irradiance.f107_per_sunspot_squared must lie within -0.01-0.01");
    require(c.f107_per_sunspot + 2.0 * c.f107_per_sunspot_squared * 600.0 > 0.0, "F10.7 must increase with the sunspot number up to 600");
    require(within(c.mgii_quiet, 0.1, 0.2), "irradiance.mgii_quiet must lie within 0.1-0.2");
    require(within(c.mgii_per_sfu, 1e-7, 1e-3), "irradiance.mgii_per_sfu must lie within 1e-7-1e-3");
    require(within(c.reference_sunspot_number, 50.0, 400.0), "irradiance.reference_sunspot_number must lie within 50-400");
    require(within(c.tsi_quiet_w_m2, 1350.0, 1370.0), "irradiance.tsi_quiet_w_m2 must lie within 1350-1370");
    require(within(c.facular_brightening_w_m2, 0.0, 10.0), "irradiance.facular_brightening_w_m2 must lie within 0-10");
    require(within(c.spot_darkening_w_m2_per_sunspot, 0.0, 0.05), "irradiance.spot_darkening_w_m2_per_sunspot must lie within 0-0.05");
    require(!bands.empty() && bands.size() <= 12, "irradiance needs 1-12 UV bands");
    std::set<std::string> keys;
    for (const auto& b : bands) {
        require(!b.key.empty() && !b.label.empty() && keys.insert(b.key).second, "UV bands need unique non-empty keys and labels");
        require(within(b.from_nm, 10.0, 1000.0) && within(b.to_nm, 10.0, 1000.0) && b.to_nm > b.from_nm, "UV band wavelengths must lie within 10-1000 nm and increase");
        require(within(b.quiet_w_m2, 1e-6, 1000.0), "UV band quiet irradiance must lie within 1e-6-1000 W/m2");
        require(within(b.variability, 0.0, 5.0), "UV band variability must lie within 0-5");
    }
}

double solar_f107(double sn, const SolarProxyConfig& c) {
    const double s = std::max(0.0, sn);
    return c.f107_base_sfu + c.f107_per_sunspot * s + c.f107_per_sunspot_squared * s * s;
}

double solar_mgii(double sn, const SolarProxyConfig& c) { return c.mgii_quiet + c.mgii_per_sfu * (solar_f107(sn, c) - c.f107_base_sfu); }

double solar_facular_index(double sn, const SolarProxyConfig& c) {
    return (solar_mgii(sn, c) - c.mgii_quiet) / (solar_mgii(c.reference_sunspot_number, c) - c.mgii_quiet);
}

SolarIrradiance solar_irradiance(double sn, const SolarProxyConfig& c, const std::vector<SolarUvBand>& bands) {
    SolarIrradiance out;
    out.sunspot_number = std::max(0.0, sn);
    out.f107_sfu = solar_f107(sn, c);
    out.mgii = solar_mgii(sn, c);
    out.facular_index = solar_facular_index(sn, c);
    out.facular_w_m2 = c.facular_brightening_w_m2 * out.facular_index;
    out.spot_w_m2 = c.spot_darkening_w_m2_per_sunspot * out.sunspot_number;
    out.tsi_w_m2 = c.tsi_quiet_w_m2 + out.facular_w_m2 - out.spot_w_m2;
    for (const auto& b : bands) {
        out.band_change.push_back(b.variability * out.facular_index);
        out.band_w_m2.push_back(b.quiet_w_m2 * (1.0 + out.band_change.back()));
    }
    return out;
}

// ---- Middle atmosphere and UV index ------------------------------------------------------------------------------

void validate_solar_atmosphere(const SolarAtmosphereConfig& c) {
    require(within(c.ozone_change_at_reference, 0.0, 0.2), "atmosphere.ozone_change_at_reference must lie within 0-0.2");
    require(within(c.stratosphere_warming_at_reference_k, 0.0, 10.0), "atmosphere.stratosphere_warming_at_reference_k must lie within 0-10");
    require(within(c.erythemal_variability, 0.0, 0.2), "atmosphere.erythemal_variability must lie within 0-0.2");
    require(within(c.radiation_amplification_factor, 0.5, 2.5), "atmosphere.radiation_amplification_factor must lie within 0.5-2.5");
}

void validate_solar_uv_site(const SolarUvSite& s) {
    require(!s.name.empty(), "UV sites need a name");
    require(within(s.latitude_deg, -90.0, 90.0), "UV site latitude must lie within -90-90");
    require(within(s.ozone_du, 100.0, 600.0), "UV site ozone must lie within 100-600 DU");
    require(within(s.ozone_seasonal_amplitude_du, 0.0, 200.0) && s.ozone_du - s.ozone_seasonal_amplitude_du >= 80.0, "UV site ozone seasonal amplitude must lie within 0-200 DU and keep the column above 80 DU");
    require(within(s.ozone_peak_day, 0.0, 366.0), "UV site ozone peak day must lie within 0-366");
}

double earth_sun_distance_factor(double day_of_year) { return 1.0 + 0.0334 * std::cos(2.0 * kPi * (day_of_year - 3.0) / 365.25); }

double clear_sky_uv_index(double cos_zenith, double ozone_du, double toa_factor, double amplification) {
    if (!(cos_zenith > 0.0)) return 0.0;
    return 12.5 * std::pow(cos_zenith, 2.42) * std::pow(ozone_du / 300.0, -amplification) * toa_factor;
}

SolarUvIndex solar_site_uv_index(const SolarUvSite& site, double day_of_year, double x, const SolarAtmosphereConfig& c) {
    SolarUvIndex out;
    // Local noon: longitude 0 at 12 UTC (no equation of time, like the ocean model's sun).
    out.cos_zenith = std::max(0.0, solar_cosine_zenith(site.latitude_deg, 0.0, day_of_year, 12.0));
    const double ozone = site.ozone_du + site.ozone_seasonal_amplitude_du * std::cos(2.0 * kPi * (day_of_year - site.ozone_peak_day) / 365.25);
    out.ozone_du = ozone * (1.0 + c.ozone_change_at_reference * x);
    const double distance = earth_sun_distance_factor(day_of_year);
    out.uv_index = clear_sky_uv_index(out.cos_zenith, out.ozone_du, distance * (1.0 + c.erythemal_variability * x), c.radiation_amplification_factor);
    out.quiet_uv_index = clear_sky_uv_index(out.cos_zenith, ozone, distance, c.radiation_amplification_factor);
    out.change = out.quiet_uv_index > 0.0 ? out.uv_index / out.quiet_uv_index - 1.0 : 0.0;
    return out;
}

// ---- Flares ------------------------------------------------------------------------------------------------------

void validate_solar_flares(const SolarFlareConfig& c) {
    require(within(c.x_rate_per_year, 0.0, 1000.0), "flares.x_rate_per_year must lie within 0-1000");
    require(within(c.reference_sunspot_number, 10.0, 400.0), "flares.reference_sunspot_number must lie within 10-400");
    require(within(c.rate_exponent, 0.5, 3.0), "flares.rate_exponent must lie within 0.5-3");
    require(within(c.size_index, 1.2, 3.0), "flares.size_index must lie within 1.2-3");
    require(within(c.max_peak_flux_w_m2, 1.5 * kFlareX20, 1.0), "flares.max_peak_flux_w_m2 must lie within 3e-3-1 (above X20)");
    require(within(c.energy_x1_j, 1e20, 1e28), "flares.energy_x1_j must lie within 1e20-1e28");
    require(within(c.energy_exponent, 0.0, 2.0), "flares.energy_exponent must lie within 0-2");
}

double flare_rate_per_year(double sn, double threshold, const SolarFlareConfig& c) {
    if (!(sn > 0.0) || threshold >= c.max_peak_flux_w_m2) return 0.0;
    const double e = 1.0 - c.size_index;
    const double q = std::pow(threshold / kFlareX1, e), qmax = std::pow(c.max_peak_flux_w_m2 / kFlareX1, e);
    return c.x_rate_per_year * std::pow(sn / c.reference_sunspot_number, c.rate_exponent) * (q - qmax) / (1.0 - qmax);
}

double flare_probability(double rate_per_year, double years) { return 1.0 - std::exp(-std::max(0.0, rate_per_year) * std::max(0.0, years)); }

double flare_energy_j(double peak_flux, const SolarFlareConfig& c) { return c.energy_x1_j * std::pow(peak_flux / kFlareX1, c.energy_exponent); }

double mean_flare_irradiance_w_m2(double sn, double lower, const SolarFlareConfig& c) {
    if (!(sn > 0.0) || lower >= c.max_peak_flux_w_m2) return 0.0;
    const double alpha = c.size_index, beta = c.energy_exponent;
    const double qmax = std::pow(c.max_peak_flux_w_m2 / kFlareX1, 1.0 - alpha);
    const double rate = c.x_rate_per_year * std::pow(sn / c.reference_sunspot_number, c.rate_exponent);
    // Energy per year: rate (alpha - 1) / (1 - qmax) E_X1 int_{u_lo}^{u_max} u^(beta - alpha) du, u = S / S_X1.
    const double p = beta - alpha + 1.0, ulo = lower / kFlareX1, umax = c.max_peak_flux_w_m2 / kFlareX1;
    const double integral = std::abs(p) < 1e-9 ? std::log(umax / ulo) : (std::pow(umax, p) - std::pow(ulo, p)) / p;
    const double energy_per_year = rate * (alpha - 1.0) / (1.0 - qmax) * c.energy_x1_j * integral;
    return energy_per_year / kSecondsPerYear / (4.0 * kPi * kAstronomicalUnit * kAstronomicalUnit);
}

double sample_flare_peak_flux(double u, double lower, const SolarFlareConfig& c) {
    const double e = 1.0 - c.size_index;
    const double qlo = std::pow(lower / kFlareX1, e), qmax = std::pow(c.max_peak_flux_w_m2 / kFlareX1, e);
    const double q = qmax + std::clamp(u, 0.0, 1.0) * (qlo - qmax);
    return std::clamp(kFlareX1 * std::pow(q, 1.0 / e), lower, c.max_peak_flux_w_m2);
}

std::string flare_class_label(double s) {
    static const char letters[] = {'A', 'B', 'C', 'M', 'X'};
    int k = static_cast<int>(std::floor(std::log10(std::max(s, 1e-9)) + 8.0 + 1e-9));
    k = std::clamp(k, 0, 4);
    double v = s / std::pow(10.0, k - 8);
    v = std::round(v * 10.0) / 10.0;
    if (v >= 10.0 && k < 4) { ++k; v = std::round(s / std::pow(10.0, k - 8) * 10.0) / 10.0; }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%c%.1f", letters[k], v);
    return buffer;
}

int radio_blackout_level(double s) {
    if (s >= kFlareX20) return 5;
    if (s >= kFlareX10) return 4;
    if (s >= kFlareX1) return 3;
    if (s >= kFlareM5) return 2;
    if (s >= kFlareM1) return 1;
    return 0;
}

// ---- Climate -----------------------------------------------------------------------------------------------------

void validate_solar_climate(const SolarClimateConfig& c) {
    require(within(c.albedo, 0.0, 0.9), "climate.albedo must lie within 0-0.9");
    require(within(c.upper_heat_capacity, 0.1, 1000.0), "climate.upper_heat_capacity must lie within 0.1-1000 W yr m-2 K-1");
    require(within(c.deep_heat_capacity, 1.0, 10000.0), "climate.deep_heat_capacity must lie within 1-10000 W yr m-2 K-1");
    require(within(c.feedback_w_m2_k, 0.1, 10.0), "climate.feedback_w_m2_k must lie within 0.1-10");
    require(within(c.exchange_w_m2_k, 0.0, 10.0), "climate.exchange_w_m2_k must lie within 0-10");
}

double solar_radiative_forcing_w_m2(double tsi_anomaly, const SolarClimateConfig& c) { return tsi_anomaly * (1.0 - c.albedo) / 4.0; }

SolarClimateState step_solar_climate(const SolarClimateState& s, double forcing, double dt, const SolarClimateConfig& c) {
    // Backward Euler: [C/dt + lambda + gamma, -gamma; -gamma, C_d/dt + gamma] [T; T_d] = [C T0 / dt + F; C_d T_d0 / dt].
    const double a11 = c.upper_heat_capacity / dt + c.feedback_w_m2_k + c.exchange_w_m2_k, a12 = -c.exchange_w_m2_k;
    const double a21 = -c.exchange_w_m2_k, a22 = c.deep_heat_capacity / dt + c.exchange_w_m2_k;
    const double b1 = c.upper_heat_capacity / dt * s.surface_k + forcing, b2 = c.deep_heat_capacity / dt * s.deep_k;
    const double det = a11 * a22 - a12 * a21;
    return {(b1 * a22 - a12 * b2) / det, (a11 * b2 - a21 * b1) / det};
}

// ---- Disk --------------------------------------------------------------------------------------------------------

void validate_solar_disk(const SolarDiskConfig& c) {
    require(c.cells >= 8 && c.cells <= 256, "disk.cells must lie within 8-256");
    require(within(c.extent_radii, 1.0, 2.0), "disk.extent_radii must lie within 1-2");
    require(c.subsamples >= 1 && c.subsamples <= 16, "disk.subsamples must lie within 1-16");
    require(within(c.solar_radius_m, 1e8, 1e10), "disk.solar_radius_m must lie within 1e8-1e10");
    require(within(c.effective_temperature_k, 3000.0, 10000.0), "disk.effective_temperature_k must lie within 3000-10000");
    require(within(c.limb_darkening, 0.0, 1.0), "disk.limb_darkening must lie within 0-1");
    require(within(c.groups_per_sunspot, 0.0, 1.0), "disk.groups_per_sunspot must lie within 0-1");
    require(within(c.group_area_median_uhem, 1.0, 5000.0), "disk.group_area_median_uhem must lie within 1-5000");
    require(within(c.group_area_log_sd, 0.0, 3.0), "disk.group_area_log_sd must lie within 0-3");
    require(within(c.umbra_fraction, 0.0, 1.0), "disk.umbra_fraction must lie within 0-1");
    require(within(c.umbra_contrast, 0.0, 1.0) && within(c.penumbra_contrast, c.umbra_contrast, 1.0), "disk contrasts must satisfy 0 <= umbra <= penumbra <= 1");
    require(within(c.facular_area_ratio, 0.0, 100.0), "disk.facular_area_ratio must lie within 0-100");
    require(within(c.facular_contrast_centre, 0.0, 1.0) && within(c.facular_contrast_limb, 0.0, 1.0), "disk facular contrasts must lie within 0-1");
    require(within(c.onset_latitude_deg, 0.0, 60.0), "disk.onset_latitude_deg must lie within 0-60");
    require(within(c.drift_efold_months, 1.0, 1000.0), "disk.drift_efold_months must lie within 1-1000");
    require(within(c.zone_width_fraction, 0.0, 1.0) && within(c.zone_min_width_deg, 0.1, 20.0), "disk zone width must have a fraction within 0-1 and a minimum within 0.1-20 deg");
    require(within(c.max_longitude_deg, 1.0, 90.0), "disk.max_longitude_deg must lie within 1-90");
    require(within(c.area_per_sunspot_uhem, 0.1, 100.0), "disk.area_per_sunspot_uhem must lie within 0.1-100");
}

double spot_zone_latitude_deg(double t, const SolarDiskConfig& c) { return c.onset_latitude_deg * std::exp(-12.0 * std::max(0.0, t) / c.drift_efold_months); }

double spot_zone_width_deg(double latitude, const SolarDiskConfig& c) { return std::max(c.zone_min_width_deg, c.zone_width_fraction * latitude); }

int sample_poisson(double mean, NormalSequence& random) {
    if (!(mean > 0.0)) return 0;
    if (mean > 30.0) return static_cast<int>(std::max(0.0, std::round(mean + std::sqrt(mean) * random.next())));
    const double limit = std::exp(-mean);
    int k = 0;
    double p = random.uniform();
    while (p > limit) { ++k; p *= random.uniform(); }
    return k;
}

std::vector<SolarActiveRegion> sample_active_regions(double year, double sn, const std::vector<SolarCycleCurve>& curves, const std::vector<int>& numbers,
                                                     NormalSequence& random, const SolarDiskConfig& c) {
    if (curves.empty() || curves.size() != numbers.size()) throw std::invalid_argument("active regions need one cycle number per curve");
    std::vector<double> weights;
    double total = 0.0;
    for (const auto& k : curves) { weights.push_back(k.value(year)); total += weights.back(); }
    std::size_t fallback = 0;
    for (std::size_t k = 0; k < curves.size(); ++k) if (curves[k].onset_year <= year) fallback = k;
    std::vector<SolarActiveRegion> out;
    const int n = sample_poisson(c.groups_per_sunspot * std::max(0.0, sn), random);
    for (int g = 0; g < n; ++g) {
        std::size_t k = fallback;
        if (total > 0.0) {
            double u = random.uniform() * total;
            for (k = 0; k + 1 < curves.size() && u > weights[k]; ++k) u -= weights[k];
        }
        const double mean_lat = spot_zone_latitude_deg(year - curves[k].onset_year, c);
        double lat = std::abs(mean_lat + spot_zone_width_deg(mean_lat, c) * random.next());
        lat = std::min(lat, 70.0);
        if (random.uniform() < 0.5) lat = -lat;
        SolarActiveRegion r;
        r.latitude_deg = lat;
        r.longitude_deg = (2.0 * random.uniform() - 1.0) * c.max_longitude_deg;
        r.area_uhem = std::clamp(c.group_area_median_uhem * std::exp(c.group_area_log_sd * random.next()), 5.0, 6000.0);
        r.cycle = numbers[k];
        out.push_back(r);
    }
    return out;
}

SolarDiskImage render_solar_disk(const std::vector<SolarActiveRegion>& regions, const SolarDiskConfig& c) {
    struct Region { double x, y, z, cos_umbra, cos_spot, cos_facula; };
    std::vector<Region> rs;
    for (const auto& r : regions) {
        const double lat = r.latitude_deg * kPi / 180.0, lon = r.longitude_deg * kPi / 180.0;
        const double f = std::min(0.5, r.area_uhem * 1e-6);   // cap area as a fraction of the hemisphere: 1 - cos(theta)
        rs.push_back({std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon), 1.0 - c.umbra_fraction * f, 1.0 - f,
                      1.0 - std::min(1.0, (1.0 + c.facular_area_ratio) * f)});
    }
    const std::size_t n = c.cells, s = c.subsamples;
    const double cell = 2.0 * c.extent_radii / static_cast<double>(n);
    const double mean_intensity = 1.0 - c.limb_darkening / 3.0;   // disk average of the limb-darkened intensity
    SolarDiskImage out;
    out.brightness_k.assign(n * n, kNaN);
    out.quiet_brightness_k.assign(n * n, kNaN);
    double quiet_total = 0.0, spot = 0.0, facula = 0.0;
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t col = 0; col < n; ++col) {
            double sum = 0.0, quiet = 0.0;
            std::size_t on = 0;
            for (std::size_t j = 0; j < s; ++j) {
                for (std::size_t i = 0; i < s; ++i) {
                    const double x = -c.extent_radii + (static_cast<double>(col) + (static_cast<double>(i) + 0.5) / static_cast<double>(s)) * cell;
                    const double y = -c.extent_radii + (static_cast<double>(row) + (static_cast<double>(j) + 0.5) / static_cast<double>(s)) * cell;
                    const double r2 = x * x + y * y;
                    if (r2 >= 1.0) continue;
                    ++on;
                    const double mu = std::sqrt(1.0 - r2);
                    const double ld = 1.0 - c.limb_darkening * (1.0 - mu);
                    // Spots win over faculae; the darkest part of any region that covers the point applies.
                    int kind = 0;   // 0 quiet, 1 facula, 2 penumbra, 3 umbra
                    for (const auto& rg : rs) {
                        const double cd = x * rg.x + y * rg.y + mu * rg.z;
                        if (cd >= rg.cos_umbra) { kind = 3; break; }
                        if (cd >= rg.cos_spot) kind = std::max(kind, 2);
                        else if (cd >= rg.cos_facula) kind = std::max(kind, 1);
                    }
                    double intensity = ld;
                    if (kind == 3) intensity = ld * c.umbra_contrast;
                    else if (kind == 2) intensity = ld * c.penumbra_contrast;
                    else if (kind == 1) intensity = ld * (1.0 + c.facular_contrast_centre + (c.facular_contrast_limb - c.facular_contrast_centre) * (1.0 - mu));
                    sum += intensity;
                    quiet += ld;
                    if (kind >= 2) spot += intensity - ld;
                    else if (kind == 1) facula += intensity - ld;
                }
            }
            quiet_total += quiet;
            if (on == 0) continue;
            const double count = static_cast<double>(s * s);
            const std::size_t index = row * n + col;
            out.brightness_k[index] = c.effective_temperature_k * std::pow(sum / count / mean_intensity, 0.25);
            out.quiet_brightness_k[index] = c.effective_temperature_k * std::pow(quiet / count / mean_intensity, 0.25);
        }
    }
    out.spot_change = quiet_total > 0.0 ? spot / quiet_total : 0.0;
    out.facular_change = quiet_total > 0.0 ? facula / quiet_total : 0.0;
    out.net_change = out.spot_change + out.facular_change;
    return out;
}

std::vector<double> butterfly_density(double year, const std::vector<SolarCycleCurve>& curves, const std::vector<double>& latitudes, const SolarDiskConfig& c) {
    std::vector<double> out(latitudes.size(), 0.0);
    for (const auto& k : curves) {
        const double r = k.value(year);
        if (!(r > 0.0)) continue;
        const double lambda = spot_zone_latitude_deg(year - k.onset_year, c), sigma = spot_zone_width_deg(lambda, c);
        const double area = c.area_per_sunspot_uhem * r;
        for (std::size_t i = 0; i < latitudes.size(); ++i) {
            const double a = (latitudes[i] - lambda) / sigma, b = (latitudes[i] + lambda) / sigma;
            out[i] += 0.5 * area * (std::exp(-0.5 * a * a) + std::exp(-0.5 * b * b)) / (sigma * std::sqrt(2.0 * kPi));
        }
    }
    return out;
}

}  // namespace terraflux::weather
