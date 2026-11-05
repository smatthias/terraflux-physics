// Solar wind, magnetosphere, cosmic radiation and atmospheric escape (weather.solar_wind).
#include "terraflux/weather/atmospheric_escape.hpp"
#include "terraflux/weather/cosmic_radiation.hpp"
#include "terraflux/weather/magnetosphere.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace terraflux::weather;

namespace {

int failures = 0;

void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; ++failures; }
}

void require_near(double actual, double expected, const std::string& message, double tolerance) {
    require(std::abs(actual - expected) <= tolerance, message + " (got " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}

template <typename Function>
void require_rejected(Function&& function, const std::string& message) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    require(false, "rejected: " + message);
}

void dipole() {
    const GeomagneticDipole d;
    validate_dipole(d);
    require_near(magnetic_position(d.north_pole_latitude_deg, d.north_pole_longitude_deg, d).latitude_deg, 90.0, "the geomagnetic north pole has magnetic latitude 90", 1e-6);
    require_near(magnetic_position(-d.north_pole_latitude_deg, d.north_pole_longitude_deg + 180.0, d).latitude_deg, -90.0, "its antipode -90", 1e-6);
    // A point 90 deg from the pole along its meridian lies on the magnetic equator.
    require_near(magnetic_position(d.north_pole_latitude_deg - 90.0, d.north_pole_longitude_deg, d).latitude_deg, 0.0, "the magnetic equator", 1e-6);
    const auto berlin = magnetic_position(52.5, 13.4, d);
    require(berlin.latitude_deg > 50.0 && berlin.latitude_deg < 55.0, "Berlin lies near 52 deg magnetic latitude");
    require_near(magnetic_local_time_h(40.0, 40.0), 12.0, "the subsolar meridian is noon", 1e-12);
    require_near(magnetic_local_time_h(220.0, 40.0), 0.0, "the opposite meridian is midnight", 1e-12);
    require_near(magnetic_local_time_h(-50.0, 40.0), 6.0, "90 deg west of noon is dawn", 1e-12);
    require_near(dipole_equatorial_field_t(7.7e22, 6.371e6) * 1e6, 29.8, "equatorial surface field about 30 uT", 0.3);
    const double eq = stormer_cutoff_gv(0.0, 7.7e22);
    require(eq > 13.0 && eq < 15.0, "vertical cutoff at the magnetic equator about 14 GV");
    require_near(stormer_cutoff_gv(60.0, 7.7e22), eq / 16.0, "cos^4: a sixteenth at 60 deg", 1e-9);
    require(stormer_cutoff_gv(89.0, 7.7e22) < 1e-3, "no shielding at the pole");
    require_near(stormer_cutoff_gv(30.0, 3.85e22), 0.5 * stormer_cutoff_gv(30.0, 7.7e22), "the cutoff scales with the moment", 1e-9);
    for (double colat : {5.0, 15.0, 30.0})
        require_near(polar_cap_colatitude_deg(polar_cap_flux_wb(colat, 7.7e22, 6.481e6), 7.7e22, 6.481e6), colat, "flux and colatitude are inverse", 1e-9);
    require(polar_cap_flux_wb(15.0, 7.7e22, 6.481e6) > 4.0e8 && polar_cap_flux_wb(15.0, 7.7e22, 6.481e6) < 6.0e8, "a 15 deg cap holds about 0.5 GWb");
    require_rejected([] { validate_dipole({1.0e19, 80.0, 0.0}); }, "a tiny moment");
}

void solar_wind() {
    const SolarWindState slow{400.0, 5.0, 0.0, -5.0}, north{400.0, 5.0, 0.0, 5.0};
    require_near(dynamic_pressure_npa(slow), 1.338, "pressure of 5 protons/cm^3 at 400 km/s", 0.005);
    require_near(newell_coupling(north), 0.0, "no merging for a due-northward field", 1e-9);
    require(newell_coupling(slow) > newell_coupling({400.0, 5.0, 5.0, 0.0}), "southward couples more than duskward");
    require(kp_from_solar_wind(slow) > kp_from_solar_wind(north), "Kp rises with a southward field");
    require(kp_from_solar_wind({3000.0, 100.0, 0.0, -200.0}) <= 9.0, "Kp is bounded at 9");
    const double quiet = kp_from_solar_wind({420.0, 6.0, 3.0, -1.0});
    require(quiet > 1.0 && quiet < 3.5, "a typical solar wind gives Kp of about 2");
    require(noaa_g_scale(4.0) == 0 && noaa_g_scale(5.0) == 1 && noaa_g_scale(6.0) == 2 && noaa_g_scale(8.67) == 4 && noaa_g_scale(9.0) == 5, "NOAA G scale from Kp");
    const double milan = dayside_reconnection_v(slow);
    require(milan > 4.0e4 && milan < 6.0e4, "dayside reconnection about 50 kV for 5 nT southward at 400 km/s");
    const auto shue = shue_magnetopause(2.0, 0.0);
    require(shue.standoff_re > 9.5 && shue.standoff_re < 11.0, "magnetopause about 10 Earth radii");
    require(shue_magnetopause(20.0, -20.0).standoff_re < 6.6, "a strong storm pushes the magnetopause inside geostationary orbit");
    require_near(shue_magnetopause(2.0, 0.0, 0.1).standoff_re / shue.standoff_re, std::cbrt(0.1), "pressure balance: r0 ~ M^(1/3)", 1e-12);
    require(magnetopause_radius_re(shue, 90.0) > shue.standoff_re, "the flanks flare outward");
    // The ring current: injection -4.4 (E - 0.49), decay towards zero without driving.
    const SolarWindState storm{1000.0, 10.0, 0.0, -10.0};
    require_near(ring_current_injection_nt_h(storm), -4.4 * (10.0 - 0.49), "O'Brien-McPherron injection", 1e-9);
    require_near(ring_current_injection_nt_h(north), 0.0, "no injection for a northward field", 1e-12);
    double d = 0.0;
    for (int h = 0; h < 200; ++h) d = step_dst_star(d, storm, 1.0);
    require_near(d, ring_current_injection_nt_h(storm) * ring_current_decay_h(storm), "equilibrium Q tau", 1e-6);
    require(step_dst_star(-200.0, north, 48.0) > -20.0 && step_dst_star(-200.0, north, 12.0) < -100.0, "recovery within about two days");
    require_near(dst_star_from_dst(dst_from_star(-100.0, 3.0), 3.0), -100.0, "pressure correction is inverse", 1e-12);
}

void polar_cap() {
    const PolarCapConfig c;
    validate_polar_cap(c);
    const SolarWindState quiet{420.0, 6.0, 5.0, 0.0}, storm{900.0, 20.0, 5.0, -30.0};
    const double fq = steady_open_flux_wb(quiet, c, 1.0), fs = steady_open_flux_wb(storm, c, 1.0);
    require(fq > c.base_open_flux_wb && fs > fq, "reconnection opens flux; more in a storm");
    require(fs < c.base_open_flux_wb + c.saturation_potential_v * c.tail_closure_h * 3600.0, "the saturation bounds the open flux");
    double f = c.base_open_flux_wb;
    PolarCapDrive drive;
    for (int h = 0; h < 48; ++h) f = step_open_flux_wb(f, storm, c, 1.0, 1.0, &drive);
    require_near(f, fs, "the open flux relaxes to its steady value", 1e-3 * fs);
    require(drive.cross_cap_potential_v > 1.0e5 && drive.cross_cap_potential_v < 3.0e5, "storm cross-cap potential of 100-300 kV");
    require(steady_open_flux_wb(quiet, c, 0.1) < fq, "a weaker dipole holds less open flux");
    const double m = 7.7e22;
    const auto oq = oval_geometry(fq, 2.0, -10.0, m, c), os = oval_geometry(fs, 9.0, -300.0, m, c);
    require(90.0 - oq.cap_colatitude_deg > 72.0 && 90.0 - oq.cap_colatitude_deg < 80.0, "quiet polar cap boundary at 72-80 deg magnetic latitude");
    require(90.0 - os.equatorward_colatitude_deg(0.0) < 50.0, "a big storm brings the oval below 50 deg");
    require(oq.equatorward_colatitude_deg(0.0) - oq.poleward_colatitude_deg(0.0) > oq.equatorward_colatitude_deg(12.0) - oq.poleward_colatitude_deg(12.0), "the oval is wider at midnight");
    require(oq.poleward_colatitude_deg(0.0) > oq.poleward_colatitude_deg(12.0), "the cap is displaced towards midnight");
    require(magnetic_region(1.0, 3.0, oq) == 2 && magnetic_region(60.0, 3.0, oq) == 0, "the pole is open, low latitudes closed");
    require(magnetic_region(0.5 * (oq.poleward_colatitude_deg(0.0) + oq.equatorward_colatitude_deg(0.0)), 0.0, oq) == 1, "the oval at midnight");
    require(magnetic_region(oq.poleward_colatitude_deg(12.0), 12.0, oq) == 3, "the cusp at noon on the boundary");
    require(auroral_shape(oq.equatorward_colatitude_deg(23.0) - 1.0, 23.0, oq) > auroral_shape(60.0, 23.0, oq), "aurora in the oval, not at low latitudes");
    require(auroral_shape(0.5 * (oq.poleward_colatitude_deg(23.0) + oq.equatorward_colatitude_deg(23.0)), 23.0, oq) >
                auroral_shape(0.5 * (oq.poleward_colatitude_deg(11.0) + oq.equatorward_colatitude_deg(11.0)), 11.0, oq), "brighter on the night side");
    require(ion_outflow_shape(oq.poleward_colatitude_deg(12.0), 12.0, oq) > ion_outflow_shape(oq.poleward_colatitude_deg(6.0), 6.0, oq), "the cleft ion fountain at noon");
    const double r = 6.481e6;
    require(cap_area_m2(oq, r) < cap_and_oval_area_m2(oq, r) && cap_and_oval_area_m2(oq, r) < cap_and_oval_area_m2(os, r), "areas: cap < cap and oval < storm");
    require_near(cap_area_m2(oq, r), 2.0 * 3.14159265358979 * r * r * (1.0 - std::cos(oq.cap_colatitude_deg * 3.14159265358979 / 180.0)), "a displaced circle keeps about its area", 0.05 * cap_area_m2(oq, r));
    const double j1 = joule_heating_w(5.0e4, oq, 6.0, r), j2 = joule_heating_w(1.0e5, oq, 6.0, r);
    require(j1 > 1.0e9 && j1 < 1.0e11 && std::abs(j2 / j1 - 4.0) < 1e-9, "Joule heating of GW, with the square of the potential");
    require(hemispheric_power_gw(0.0) == 5.0 && hemispheric_power_gw(1.0e5) > 200.0, "hemispheric power from a few to hundreds of GW");
    auto bad = c;
    bad.tail_closure_h = 0.0;
    require_rejected([&] { validate_polar_cap(bad); }, "no tail closure");
}

void thermosphere() {
    require_near(jacchia_night_temperature_k(150.0, 150.0), 379.0 + 3.24 * 150.0, "Jacchia night minimum", 1e-9);
    require_near(jacchia_geomagnetic_heating_k(9.0), 28.0 * 9.0 + 0.03 * std::exp(9.0), "Jacchia geomagnetic term", 1e-9);
    const ThermosphereConfig c;
    const double day = exospheric_temperature_k(150.0, 150.0, 2.0, 14.0, 0.0, 0.0, 10.0, c), night = exospheric_temperature_k(150.0, 150.0, 2.0, 2.0, 0.0, 0.0, 10.0, c);
    require(day > night && day < 1.35 * night, "warmer in the afternoon, by less than a third");
    require(exospheric_temperature_k(150.0, 150.0, 7.0, 2.0, 70.0, 0.0, 75.0, c) - exospheric_temperature_k(150.0, 150.0, 2.0, 2.0, 70.0, 0.0, 75.0, c) >
                exospheric_temperature_k(150.0, 150.0, 7.0, 2.0, 0.0, 0.0, 5.0, c) - exospheric_temperature_k(150.0, 150.0, 2.0, 2.0, 0.0, 0.0, 5.0, c), "storms heat high latitudes most");
    require(thermosphere_density_ratio(1400.0, 1000.0) > 2.0 && thermosphere_density_ratio(1000.0, 1000.0) == 1.0, "a hotter thermosphere: denser at 400 km (satellite drag)");
}

void radiation() {
    require_near(proton_kinetic_gev(proton_rigidity_gv(2.5)), 2.5, "rigidity and kinetic energy are inverse", 1e-9);
    require_near(proton_rigidity_gv(0.01), 0.1372, "10 MeV protons have 137 MV", 1e-3);
    require(lis_proton_flux(1.0) > 1.0e3 && lis_proton_flux(1.0) < 1.0e4, "LIS near 1 GeV of some thousand per m^2 s sr GeV");
    require(force_field_proton_flux(1.0, 1.0) < force_field_proton_flux(1.0, 0.4) && force_field_proton_flux(1.0, 0.0) == lis_proton_flux(1.0), "modulation lowers the flux");
    require(force_field_proton_flux(100.0, 1.0) / force_field_proton_flux(100.0, 0.4) > 0.97, "hardly any modulation at 100 GeV");
    for (const auto& r : default_radiation_responses()) {
        validate_radiation_response(r);
        const CutoffResponse at_reference(r, r.reference_phi_gv), at_maximum(r, 1.1);
        require_near(at_reference(0.0), r.anchor, r.name + ": anchored at the pole", 1e-6 * r.anchor);
        require(at_reference(2.0) <= at_reference(0.0) && at_reference(14.0) < at_reference(2.0), r.name + ": less behind a higher cutoff");
        require(at_maximum(0.0) < at_reference(0.0), r.name + ": less at solar maximum");
        require((at_reference(0.0) / at_maximum(0.0)) > (at_reference(14.0) / at_maximum(14.0)), r.name + ": the solar cycle matters more at the pole");
        const double ratio = at_reference(0.0) / at_reference(14.2);
        require(ratio > 1.3 && ratio < 10.0, r.name + ": a latitude effect between 1.3 and 10");
    }
    const SepSpectrum sep{1000.0, 0.1};
    require_near(sep_flux_above_pfu(proton_rigidity_gv(0.010), sep), 1000.0, "the >10 MeV flux is the pfu", 1e-9);
    require(sep_flux_above_pfu(1.0, sep) < 1.0, "few SEP protons above 1 GV for a soft spectrum");
    const auto d = sep_deposition(0.0, sep, SepAtmosphereConfig{});
    double sum = 0.0;
    for (double v : d.layer_mev_cm2_s) sum += v;
    require_near(sum, d.energy_mev_cm2_s, "the layers add up", 1e-9 * sum);
    require_near(d.ion_pairs_cm2_s, d.energy_mev_cm2_s * 1e6 / 35.0, "35 eV per ion pair", 1e-6 * d.ion_pairs_cm2_s);
    require(d.layer_mev_cm2_s[0] > d.layer_mev_cm2_s[3], "soft protons stop high up (above 50 km more than below 20 km)");
    require(sep_deposition(1.0, sep, SepAtmosphereConfig{}).energy_mev_cm2_s < 0.01 * d.energy_mev_cm2_s, "a 1 GV cutoff keeps out most solar protons");
    const SepSpectrum hard{1000.0, 0.4};
    const CutoffResponse with_soft(default_radiation_responses()[0], 0.6, &sep), with_hard(default_radiation_responses()[0], 0.6, &hard), none(default_radiation_responses()[0], 0.6);
    require(with_hard(0.0) / none(0.0) > 1.5 && with_soft(0.0) / none(0.0) < 1.05, "only hard spectra reach the ground (ground-level enhancements)");
    require_rejected([] { validate_sep_atmosphere(SepAtmosphereConfig{0.0022, 1.77, 2.0, 35.0, 1.25, 2.0, {50.0, 100.0}}); }, "increasing layer tops");
}

void escape() {
    const HydrogenEscapeConfig c;
    validate_hydrogen_escape(c);
    require_near(total_hydrogen_mixing_ratio(c), (0.53 + 3.8 + 2.0 * 1.93) * 1e-6, "f_T = H2 + H2O + 2 CH4", 1e-15);
    const double supply = diffusion_limited_hydrogen_per_s(c);
    require(supply > 1.5e27 && supply < 3.0e27, "diffusion-limited escape of about 2e27 H/s (3-4 kg/s)");
    require(jeans_effusion_velocity_m_s(1400.0, 1.008, 6.871e6) > 5.0 * jeans_effusion_velocity_m_s(1000.0, 1.008, 6.871e6), "Jeans escape rises steeply with temperature");
    require(jeans_effusion_velocity_m_s(1000.0, 16.0, 6.871e6) < 1e-30, "oxygen cannot escape thermally");
    const auto e = hydrogen_escape_equilibrium(1000.0, 0.0, c);
    require_near(e.total_per_s, supply, "in equilibrium the routes carry the supply", 1e-9 * supply);
    require_near(e.jeans_per_s / e.total_per_s, c.jeans_share_at_1000k, "calibrated Jeans share at 1000 K", 1e-9);
    require(e.exobase_density_m3 > 3e10 && e.exobase_density_m3 < 1e12, "exobase hydrogen of about 1e5 per cm^3");
    const auto hot = hydrogen_escape_equilibrium(1400.0, 2.0e13, c);
    require(hot.jeans_per_s / hot.total_per_s > e.jeans_per_s / e.total_per_s && hot.polar_wind_per_s > 0.0, "hot: more Jeans share; open field lines carry a polar wind");
    auto x = step_hydrogen_escape(e.reservoir_atoms, 1400.0, 0.0, 1.0, c);
    require(x.total_per_s > supply, "a sudden heating briefly raises the escape above the supply");
    for (int h = 0; h < 200; ++h) x = step_hydrogen_escape(x.reservoir_atoms, 1400.0, 0.0, 1.0, c);
    require_near(x.total_per_s, supply, "and relaxes back to the supply", 1e-6 * supply);
    auto more_methane = c;
    more_methane.methane_ppmv = 3.5;
    require(diffusion_limited_hydrogen_per_s(more_methane) > supply, "more methane, more hydrogen escape");
    const IonOutflowConfig o;
    validate_ion_outflow(o);
    require(oxygen_upflow_per_s(6.0, 150.0, 1.0, o) > 10.0 * oxygen_upflow_per_s(0.0, 150.0, 1.0, o), "O+ outflow rises steeply with Kp");
    require_near(oxygen_escape_per_s(3.0, 100.0, 1.0, o), o.escape_fraction * oxygen_upflow_per_s(3.0, 100.0, 1.0, o), "the escaping share", 1.0);
    require(oxygen_escape_monthly_per_s(2.0, 1.2, 100.0, 1.0, o) > oxygen_escape_per_s(2.0, 100.0, 1.0, o), "the monthly mean of a convex rate exceeds the rate at the mean");
    const double o_kg = oxygen_escape_per_s(2.0, 120.0, 1.0, o) * 16.0 * 1.6605e-27;
    require(o_kg > 0.05 && o_kg < 3.0, "O+ escape of a fraction of a kg/s in moderate conditions");
    require_rejected([] { HydrogenEscapeConfig bad; bad.jeans_share_at_1000k = 1.0; validate_hydrogen_escape(bad); }, "a Jeans share of 1");
}

}  // namespace

int main() {
    dipole();
    solar_wind();
    polar_cap();
    thermosphere();
    radiation();
    escape();
    if (failures) { std::cerr << failures << " check(s) failed\n"; return 1; }
    std::cout << "solar wind checks passed\n";
    return 0;
}
