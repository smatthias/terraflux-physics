#include "terraflux/weather/magnetosphere.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraflux::weather {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kMu0 = 4.0e-7 * kPi;
constexpr double kLight = 2.99792458e8;
constexpr double kProtonMass = 1.67262192e-27;
constexpr double kBoltzmann = 1.380649e-23;
constexpr double kAmu = 1.66053907e-27;
constexpr double kGM = 3.986004418e14;
constexpr double kEarthRadius = 6.371e6;

void require(bool ok, const char* message) {
    if (!ok) throw std::invalid_argument(message);
}

bool within(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

double wrap_hours(double h) {
    h = std::fmod(h, 24.0);
    return h < 0.0 ? h + 24.0 : h;
}

// Signed difference of two local times (hours), within -12..12.
double hour_difference(double a, double b) {
    double d = std::fmod(a - b, 24.0);
    if (d > 12.0) d -= 24.0;
    if (d < -12.0) d += 24.0;
    return d;
}

double gaussian(double x, double sigma) { return std::exp(-0.5 * (x / sigma) * (x / sigma)); }

}  // namespace

// ---- The dipole and magnetic coordinates --------------------------------------------------------------------------

void validate_dipole(const GeomagneticDipole& d) {
    require(within(d.moment_a_m2, 1.0e20, 1.0e24), "dipole.moment_a_m2 must lie within 1e20-1e24");
    require(within(d.north_pole_latitude_deg, 0.0, 90.0) && within(d.north_pole_longitude_deg, -180.0, 360.0),
            "dipole: north pole latitude within 0-90 deg, longitude within -180-360 deg");
}

MagneticPosition magnetic_position(double lat, double lon, const GeomagneticDipole& d) {
    const double pl = d.north_pole_latitude_deg * kDeg, po = d.north_pole_longitude_deg * kDeg;
    const double px = std::cos(pl) * std::cos(po), py = std::cos(pl) * std::sin(po), pz = std::sin(pl);
    // y_m = z_geo x pole (east of the pole meridian), x_m = y_m x pole.
    double yx = -py, yy = px, yz = 0.0;
    double n = std::hypot(yx, yy);
    if (n < 1e-12) { yx = 0.0; yy = 1.0; n = 1.0; }
    yx /= n; yy /= n;
    const double xx = yy * pz - yz * py, xy = yz * px - yx * pz, xz = yx * py - yy * px;
    const double a = lat * kDeg, b = lon * kDeg;
    const double rx = std::cos(a) * std::cos(b), ry = std::cos(a) * std::sin(b), rz = std::sin(a);
    const double up = std::clamp(rx * px + ry * py + rz * pz, -1.0, 1.0);
    return {std::asin(up) / kDeg, std::atan2(rx * yx + ry * yy + rz * yz, rx * xx + ry * xy + rz * xz) / kDeg};
}

double magnetic_local_time_h(double mlon, double subsolar_mlon) { return wrap_hours(12.0 + (mlon - subsolar_mlon) / 15.0); }

double dipole_equatorial_field_t(double moment, double r) { return kMu0 * moment / (4.0 * kPi * r * r * r); }

double stormer_cutoff_gv(double mlat, double moment, double r) {
    const double c = std::cos(mlat * kDeg);
    return kMu0 * moment * kLight / (16.0 * kPi * r * r) * c * c * c * c / 1.0e9;
}

double polar_cap_flux_wb(double colat, double moment, double r) {
    const double s = std::sin(colat * kDeg);
    return kMu0 * moment * s * s / (2.0 * r);
}

double polar_cap_colatitude_deg(double flux, double moment, double r) {
    const double s2 = std::clamp(2.0 * r * std::max(0.0, flux) / (kMu0 * moment), 0.0, 1.0);
    return std::asin(std::sqrt(s2)) / kDeg;
}

// ---- The solar wind -----------------------------------------------------------------------------------------------

double dynamic_pressure_npa(const SolarWindState& w) { return kProtonMass * w.density_cm3 * 1.0e6 * std::pow(w.speed_km_s * 1.0e3, 2.0) * 1.0e9; }

namespace {
double half_clock_sine(const SolarWindState& w) {
    const double bt = std::hypot(w.by_nt, w.bz_nt);
    if (bt <= 0.0) return 0.0;
    return std::sqrt(std::max(0.0, 0.5 * (1.0 - w.bz_nt / bt)));   // sin(theta_c / 2)
}
}  // namespace

double newell_coupling(const SolarWindState& w) {
    const double bt = std::hypot(w.by_nt, w.bz_nt);
    return std::pow(std::max(0.0, w.speed_km_s), 4.0 / 3.0) * std::pow(bt, 2.0 / 3.0) * std::pow(half_clock_sine(w), 8.0 / 3.0);
}

double kp_from_solar_wind(const SolarWindState& w) {
    // The viscous (pressure) term is limited to 3 Kp units: the fit overshoots for dense, fast sheaths with a
    // northward field.
    const double viscous = 2.844e-6 * std::sqrt(std::max(0.0, w.density_cm3)) * w.speed_km_s * w.speed_km_s;
    const double linear = 0.05 + 2.244e-4 * newell_coupling(w) + 3.0 * std::tanh(viscous / 3.0);
    // The linear fit keeps growing in extreme driving; Kp is bounded at 9: saturate softly above 6.
    const double kp = linear <= 6.0 ? linear : 6.0 + 3.0 * std::tanh((linear - 6.0) / 3.0);
    return std::clamp(kp, 0.0, 9.0);
}

int noaa_g_scale(double kp) {
    // Kp is reported in thirds (5-, 5o, 5+ ...): G1 from 5-, G2 from 6-, G3 from 7-, G4 from 8-, G5 at 9o.
    const double thirds = std::round(kp * 3.0) / 3.0;
    if (thirds >= 9.0 - 1e-9) return 5;
    for (int g = 4; g >= 1; --g)
        if (thirds >= 4.0 + g - 1.0 / 3.0 - 1e-9) return g;
    return 0;
}

double dayside_reconnection_v(const SolarWindState& w, double coefficient) {
    const double byz = std::hypot(w.by_nt, w.bz_nt) * 1.0e-9;
    return coefficient * std::pow(std::max(0.0, w.speed_km_s) * 1.0e3, 4.0 / 3.0) * byz * std::pow(half_clock_sine(w), 4.5);
}

MagnetopauseShape shue_magnetopause(double p, double bz, double m) {
    require(p > 0.0 && std::isfinite(p) && m > 0.0, "magnetopause needs a positive pressure and moment ratio");
    MagnetopauseShape s;
    // Pressure balance with a dipole: r0 grows as the cube root of the moment (m = M / M_today).
    s.standoff_re = (10.22 + 1.29 * std::tanh(0.184 * (bz + 8.14))) * std::pow(p, -1.0 / 6.6) * std::cbrt(m);
    s.flaring = (0.58 - 0.007 * bz) * (1.0 + 0.024 * std::log(p));
    return s;
}

double magnetopause_radius_re(const MagnetopauseShape& s, double angle_deg) {
    const double c = std::cos(angle_deg * kDeg);
    if (c <= -1.0 + 1e-9) return std::numeric_limits<double>::infinity();
    return s.standoff_re * std::pow(2.0 / (1.0 + c), s.flaring);
}

double ring_current_injection_nt_h(const SolarWindState& w) {
    const double e = w.speed_km_s * std::max(0.0, -w.bz_nt) * 1.0e-3;   // mV/m
    return e > 0.49 ? -4.4 * (e - 0.49) : 0.0;
}

double ring_current_decay_h(const SolarWindState& w) {
    const double e = w.speed_km_s * std::max(0.0, -w.bz_nt) * 1.0e-3;
    return 2.4 * std::exp(9.74 / (4.69 + e));
}

double step_dst_star(double d, const SolarWindState& w, double hours) {
    const double q = ring_current_injection_nt_h(w), tau = ring_current_decay_h(w);
    const double eq = q * tau;
    return eq + (d - eq) * std::exp(-hours / tau);
}

double dst_from_star(double s, double p) { return s + 7.26 * std::sqrt(std::max(0.0, p)) - 11.0; }
double dst_star_from_dst(double d, double p) { return d - 7.26 * std::sqrt(std::max(0.0, p)) + 11.0; }

// ---- The polar cap and the auroral oval ---------------------------------------------------------------------------

void validate_polar_cap(const PolarCapConfig& c) {
    require(within(c.base_open_flux_wb, 1.0e7, 2.0e9) && within(c.tail_closure_h, 0.1, 24.0), "polar_cap: base_open_flux_wb within 1e7-2e9, tail_closure_h within 0.1-24");
    require(within(c.reconnection_coefficient, 1.0e4, 1.0e7) && within(c.saturation_potential_v, 1.0e4, 1.0e7) && within(c.viscous_potential_v, 0.0, 1.0e5),
            "polar_cap: reconnection_coefficient within 1e4-1e7, saturation_potential_v within 1e4-1e7, viscous_potential_v within 0-1e5");
    require(within(c.ionosphere_altitude_m, 80.0e3, 500.0e3) && within(c.midnight_offset_fraction, 0.0, 0.6), "polar_cap: ionosphere_altitude_m within 80-500 km, midnight_offset_fraction within 0-0.6");
    require(within(c.oval_width_night_deg, 0.5, 20.0) && within(c.oval_width_day_deg, 0.5, 20.0) && within(c.oval_width_night_per_kp_deg, 0.0, 5.0) &&
                within(c.oval_width_day_per_kp_deg, 0.0, 5.0),
            "polar_cap: oval widths within 0.5-20 deg, per-Kp widths within 0-5 deg");
    require(within(c.ring_current_shift_deg_per_nt, 0.0, 0.1) && within(c.max_colatitude_deg, 20.0, 89.0) && within(c.pedersen_conductance_s, 0.1, 100.0),
            "polar_cap: ring_current_shift_deg_per_nt within 0-0.1, max_colatitude_deg within 20-89, pedersen_conductance_s within 0.1-100");
}

double step_open_flux_wb(double f, const SolarWindState& w, const PolarCapConfig& c, double m, double hours, PolarCapDrive* drive) {
    const double raw = dayside_reconnection_v(w, c.reconnection_coefficient) * std::cbrt(m);
    const double day = raw / (1.0 + raw / c.saturation_potential_v);
    const double base = c.base_open_flux_wb * std::pow(m, 2.0 / 3.0);
    const double tau = c.tail_closure_h * 3600.0;
    const double eq = base + day * tau;
    const double next = eq + (f - eq) * std::exp(-hours * 3600.0 / tau);
    if (drive) {
        drive->reconnection_v = day;
        drive->tail_closure_v = std::max(0.0, next - base) / tau;
        drive->cross_cap_potential_v = c.viscous_potential_v + 0.5 * (day + drive->tail_closure_v);
    }
    return next;
}

double steady_open_flux_wb(const SolarWindState& w, const PolarCapConfig& c, double m) {
    const double raw = dayside_reconnection_v(w, c.reconnection_coefficient) * std::cbrt(m);
    return c.base_open_flux_wb * std::pow(m, 2.0 / 3.0) + raw / (1.0 + raw / c.saturation_potential_v) * c.tail_closure_h * 3600.0;
}

double OvalGeometry::poleward_colatitude_deg(double mlt) const { return cap_colatitude_deg + midnight_offset_deg * std::cos(2.0 * kPi * mlt / 24.0); }

double OvalGeometry::equatorward_colatitude_deg(double mlt) const {
    const double night = 0.5 * (1.0 + std::cos(2.0 * kPi * mlt / 24.0));
    return poleward_colatitude_deg(mlt) + width_day_deg + (width_night_deg - width_day_deg) * night + storm_shift_deg;
}

OvalGeometry oval_geometry(double flux, double kp, double dst, double moment, const PolarCapConfig& c) {
    OvalGeometry o;
    const double r = kEarthRadius + c.ionosphere_altitude_m;
    o.cap_colatitude_deg = std::min(polar_cap_colatitude_deg(flux, moment, r), c.max_colatitude_deg * 0.7);
    o.midnight_offset_deg = c.midnight_offset_fraction * o.cap_colatitude_deg;
    o.width_night_deg = c.oval_width_night_deg + c.oval_width_night_per_kp_deg * kp;
    o.width_day_deg = c.oval_width_day_deg + c.oval_width_day_per_kp_deg * kp;
    o.storm_shift_deg = c.ring_current_shift_deg_per_nt * std::max(0.0, -dst);
    // Keep the whole oval within the allowed colatitude.
    const double widest = o.cap_colatitude_deg + o.midnight_offset_deg + o.width_night_deg + o.storm_shift_deg;
    if (widest > c.max_colatitude_deg) o.storm_shift_deg = std::max(0.0, o.storm_shift_deg - (widest - c.max_colatitude_deg));
    return o;
}

int magnetic_region(double colat, double mlt, const OvalGeometry& o) {
    const double pole = o.poleward_colatitude_deg(mlt);
    if (std::abs(hour_difference(mlt, 12.0)) < 1.5 && std::abs(colat - pole) < 1.5) return 3;
    if (colat < pole) return 2;
    if (colat <= o.equatorward_colatitude_deg(mlt)) return 1;
    return 0;
}

double auroral_shape(double colat, double mlt, const OvalGeometry& o) {
    const double pole = o.poleward_colatitude_deg(mlt), eq = o.equatorward_colatitude_deg(mlt);
    const double centre = 0.5 * (pole + eq), sigma = std::max(0.5, 0.25 * (eq - pole));
    const double mlt_weight = 1.0 + 0.7 * std::cos(2.0 * kPi * hour_difference(mlt, 23.0) / 24.0);
    double s = mlt_weight * gaussian(colat - centre, sigma);
    s += 0.15 * gaussian(colat - o.poleward_colatitude_deg(12.0), 1.2) * gaussian(hour_difference(mlt, 12.0), 1.5);   // cusp
    if (colat < pole) s += 0.02;                                                                                   // polar rain
    return s;
}

double ion_outflow_shape(double colat, double mlt, const OvalGeometry& o) {
    const double pole = o.poleward_colatitude_deg(mlt), eq = o.equatorward_colatitude_deg(mlt);
    const double centre = 0.5 * (pole + eq), sigma = std::max(0.5, 0.25 * (eq - pole));
    double s = gaussian(colat - o.poleward_colatitude_deg(12.0), 2.0) * gaussian(hour_difference(mlt, 12.0), 3.0);   // cleft ion fountain
    s += 0.6 * gaussian(colat - centre, sigma) * gaussian(hour_difference(mlt, 22.0), 4.0);                          // nightside aurora
    if (colat < pole) s += 0.03;                                                                                      // polar cap upflow
    return s;
}

namespace {
double cap_area(const OvalGeometry& o, double r, bool with_oval) {
    const int n = 96;
    double a = 0.0;
    for (int k = 0; k < n; ++k) {
        const double mlt = 24.0 * (k + 0.5) / n;
        const double colat = with_oval ? o.equatorward_colatitude_deg(mlt) : o.poleward_colatitude_deg(mlt);
        a += r * r * (1.0 - std::cos(std::min(colat, 90.0) * kDeg)) * 2.0 * kPi / n;
    }
    return a;
}
}  // namespace

double cap_area_m2(const OvalGeometry& o, double r) { return cap_area(o, r, false); }
double cap_and_oval_area_m2(const OvalGeometry& o, double r) { return cap_area(o, r, true); }

// ---- Energy input and the thermosphere ----------------------------------------------------------------------------

double hemispheric_power_gw(double newell, double base, double per_unit) { return base + per_unit * std::max(0.0, newell); }

double joule_heating_w(double potential, const OvalGeometry& o, double pedersen, double r) {
    const double chord = 2.0 * r * std::sin(std::max(1.0, o.cap_colatitude_deg) * kDeg);
    const double e = potential / chord;
    return pedersen * e * e * cap_and_oval_area_m2(o, r);
}

double jacchia_night_temperature_k(double f, double f81) { return 379.0 + 3.24 * f81 + 1.3 * (f - f81); }

double jacchia_geomagnetic_heating_k(double kp) { return 28.0 * kp + 0.03 * std::exp(kp); }

double exospheric_temperature_k(double f, double f81, double kp, double lst, double lat, double sun_lat, double mlat, const ThermosphereConfig& c) {
    const double night = jacchia_night_temperature_k(f, f81);
    const double diurnal = 0.5 * (1.0 + std::cos(2.0 * kPi * (lst - c.peak_local_time_h) / 24.0)) * (0.5 + 0.5 * std::cos((lat - sun_lat) * kDeg));
    const double s = std::sin(mlat * kDeg);
    const double weight = 1.0 - c.high_latitude_weight / 3.0 + c.high_latitude_weight * s * s;
    return night * (1.0 + c.diurnal_amplitude * diurnal) + jacchia_geomagnetic_heating_k(kp) * weight;
}

double thermosphere_density_ratio(double t, double t0, double z) {
    const double g = kGM / std::pow(kEarthRadius + z, 2.0), m = 16.0 * kAmu;
    const double h = kBoltzmann * t / (m * g), h0 = kBoltzmann * t0 / (m * g);
    const double dz = z - 120.0e3;
    return std::exp(-dz / h + dz / h0);
}

}  // namespace terraflux::weather
