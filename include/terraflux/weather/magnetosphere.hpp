#pragma once

#include <cstddef>

namespace terraflux::weather {

// Magnetosphere v1: how the solar wind couples to the Earth's magnetic field, and where that reaches the atmosphere.
//
// 1. The field. A centred, tilted dipole (moment M, geomagnetic north pole given). Its field is strongest at the
//    poles (about twice the equatorial value), but there the field lines are nearly vertical and those poleward of
//    the auroral oval are *open*: connected to the solar wind. Charged particles enter and leave along them, so the
//    polar caps are the magnetosphere's windows, not places of weak field.
// 2. Cosmic-ray shielding. The vertical Stoermer cutoff rigidity Rc = mu0 M c cos^4(lambda_m) / (16 pi r^2): about
//    14 GV at the magnetic equator, below 1 GV poleward of about 60 deg magnetic latitude.
// 3. The solar wind. Dynamic pressure P = m_p n v^2; the magnetopause of Shue et al. (1998), scaled by M^(1/3)
//    (pressure balance); the coupling function of Newell et al. (2007) and the Kp of Newell et al. (2008); the ring
//    current (Dst) of O'Brien & McPherron (2000).
// 4. The polar cap. Open magnetic flux F grows by dayside reconnection (Milan et al. 2012, with a Hill-Siscoe
//    saturation) and is closed again in the tail (a relaxation towards a base value): the expanding/contracting
//    polar cap. Its boundary follows from the dipole's flux, F = mu0 M sin^2(theta) / (2 r). The auroral oval lies
//    equatorward of it, displaced towards midnight (Holzworth & Meng 1975), wider with Kp and pushed to lower
//    latitudes by the ring current in storms.
// 5. Energy input. Hemispheric auroral power from the coupling function; Joule heating Sigma_P E^2 over the cap and
//    the oval; the exospheric temperature of Jacchia (1970/71) with its geomagnetic term, heaviest at high latitudes.

// ---- The dipole and magnetic coordinates --------------------------------------------------------------------------

struct GeomagneticDipole {
    double moment_a_m2{7.7e22};                 // about the IGRF value for 2025
    double north_pole_latitude_deg{80.8};       // geomagnetic north pole (the dipole axis), about 2025
    double north_pole_longitude_deg{-72.7};
};
void validate_dipole(const GeomagneticDipole& dipole);

struct MagneticPosition {
    double latitude_deg{0.0};
    double longitude_deg{0.0};
};
MagneticPosition magnetic_position(double latitude_deg, double longitude_deg, const GeomagneticDipole& dipole);
// Magnetic local time (hours) of a magnetic longitude, given the magnetic longitude of the subsolar point.
double magnetic_local_time_h(double magnetic_longitude_deg, double subsolar_magnetic_longitude_deg);
// Equatorial dipole field (T) at a radius (m).
double dipole_equatorial_field_t(double moment_a_m2, double radius_m);
// Vertical Stoermer cutoff rigidity (GV) at a magnetic latitude and a radius (default: 20 km above the surface).
double stormer_cutoff_gv(double magnetic_latitude_deg, double moment_a_m2, double radius_m = 6.391e6);
// Magnetic flux (Wb) through a polar cap bounded at a colatitude (deg) at radius r (m).
double polar_cap_flux_wb(double colatitude_deg, double moment_a_m2, double radius_m);
double polar_cap_colatitude_deg(double flux_wb, double moment_a_m2, double radius_m);

// ---- The solar wind -----------------------------------------------------------------------------------------------

struct SolarWindState {
    double speed_km_s{420.0};
    double density_cm3{6.0};
    double by_nt{0.0};                           // GSM components of the interplanetary field
    double bz_nt{0.0};
};
double dynamic_pressure_npa(const SolarWindState& wind);
// Newell et al. (2007): d(Phi_MP)/dt = v^(4/3) B_T^(2/3) sin^(8/3)(theta_c / 2), with v in km/s and B in nT.
double newell_coupling(const SolarWindState& wind);
// Newell et al. (2008): Kp = 0.05 + 2.244e-4 dPhi/dt + 2.844e-6 n^(1/2) v^2, with the viscous term limited to 3
// (3 tanh(x / 3)) and a soft saturation above 6 (6 + 3 tanh((Kp_lin - 6) / 3)), because the index is bounded at 9.
double kp_from_solar_wind(const SolarWindState& wind);
// NOAA geomagnetic storm scale (0 = none, G1 ... G5) from Kp rounded to thirds: G1 from 5-, ..., G4 from 8-, G5 at 9o.
int noaa_g_scale(double kp);
// Dayside reconnection voltage (V) of Milan et al. (2012): Lambda v^(4/3) B_yz sin^(9/2)(theta_c / 2).
double dayside_reconnection_v(const SolarWindState& wind, double coefficient = 3.3e5);
// Shue et al. (1998) magnetopause: stand-off distance r0 (Earth radii) and flaring alpha; r(theta) = r0 (2 / (1 + cos theta))^alpha.
struct MagnetopauseShape {
    double standoff_re{10.0};
    double flaring{0.58};
};
MagnetopauseShape shue_magnetopause(double pressure_npa, double bz_nt, double moment_ratio = 1.0);
double magnetopause_radius_re(const MagnetopauseShape& shape, double angle_from_sun_deg);

// O'Brien & McPherron (2000): dDst*/dt = Q(VBs) - Dst*/tau(VBs); Dst = Dst* + 7.26 sqrt(P) - 11 (nT).
double ring_current_injection_nt_h(const SolarWindState& wind);
double ring_current_decay_h(const SolarWindState& wind);
double step_dst_star(double dst_star_nt, const SolarWindState& wind, double hours);
double dst_from_star(double dst_star_nt, double pressure_npa);
double dst_star_from_dst(double dst_nt, double pressure_npa);

// ---- The polar cap and the auroral oval ---------------------------------------------------------------------------

struct PolarCapConfig {
    double base_open_flux_wb{3.5e8};             // the open flux the tail relaxes to (at today's dipole)
    double tail_closure_h{1.5};                  // time scale of nightside reconnection
    double reconnection_coefficient{3.3e5};      // Milan et al. (2012), SI
    double saturation_potential_v{2.5e5};        // Hill-Siscoe saturation of the dayside reconnection
    double viscous_potential_v{1.0e4};
    double ionosphere_altitude_m{110.0e3};
    double midnight_offset_fraction{0.25};       // the cap's centre lies towards midnight by this share of its radius
    double oval_width_night_deg{4.0};
    double oval_width_night_per_kp_deg{0.6};
    double oval_width_day_deg{1.5};
    double oval_width_day_per_kp_deg{0.25};
    double ring_current_shift_deg_per_nt{0.015}; // storm-time equatorward shift of the oval with -Dst
    double max_colatitude_deg{70.0};
    double pedersen_conductance_s{6.0};
};
void validate_polar_cap(const PolarCapConfig& config);

struct PolarCapDrive {
    double reconnection_v{0.0};                  // dayside, after saturation
    double tail_closure_v{0.0};
    double cross_cap_potential_v{0.0};
};
// Open flux after a step: dF/dt = Phi_D(sat) - (F - F_base) / tau. The moment ratio scales F_base by m^(2/3) and the
// reconnection voltage by m^(1/3) (a smaller magnetosphere; Siscoe & Chen 1975 for the cap).
double step_open_flux_wb(double open_flux_wb, const SolarWindState& wind, const PolarCapConfig& config, double moment_ratio, double hours, PolarCapDrive* drive = nullptr);
double steady_open_flux_wb(const SolarWindState& wind, const PolarCapConfig& config, double moment_ratio);

// The oval's geometry in magnetic colatitude (deg) around one pole.
struct OvalGeometry {
    double cap_colatitude_deg{15.0};             // mean open/closed boundary
    double midnight_offset_deg{3.75};
    double width_night_deg{5.0}, width_day_deg{2.0};
    double storm_shift_deg{0.0};
    double poleward_colatitude_deg(double mlt_h) const;    // the open/closed boundary
    double equatorward_colatitude_deg(double mlt_h) const;
};
OvalGeometry oval_geometry(double open_flux_wb, double kp, double dst_nt, double moment_a_m2, const PolarCapConfig& config);
// Region of a point: 0 closed field (inside the plasmasphere/ring current), 1 auroral oval, 2 polar cap (open), 3 cusp.
int magnetic_region(double magnetic_colatitude_deg, double mlt_h, const OvalGeometry& oval);
// Unnormalised shape of the auroral energy flux (night-heavy Gaussian across the oval, a weak cusp and polar rain).
double auroral_shape(double magnetic_colatitude_deg, double mlt_h, const OvalGeometry& oval);
// Unnormalised shape of the heavy-ion outflow: the cleft ion fountain near noon and the nightside auroral outflow.
double ion_outflow_shape(double magnetic_colatitude_deg, double mlt_h, const OvalGeometry& oval);
// Area (m^2) of one polar cap (open field) and of cap plus oval, at a radius.
double cap_area_m2(const OvalGeometry& oval, double radius_m);
double cap_and_oval_area_m2(const OvalGeometry& oval, double radius_m);

// ---- Energy input and the thermosphere ----------------------------------------------------------------------------

// Hemispheric auroral power (GW) from the Newell coupling: base + slope * dPhi/dt.
double hemispheric_power_gw(double newell, double base_gw = 5.0, double per_unit = 2.3e-3);
// Joule heating (W) in one hemisphere: Sigma_P E^2 over cap and oval, E = Phi_PC / (2 r sin theta_pc).
double joule_heating_w(double cross_cap_potential_v, const OvalGeometry& oval, double pedersen_s, double radius_m);

struct ThermosphereConfig {
    double diurnal_amplitude{0.3};               // day maximum over the night minimum (Jacchia)
    double peak_local_time_h{14.0};
    double kp_response_h{4.0};                   // the thermosphere follows Kp with this lag (e-folding)
    double high_latitude_weight{1.2};            // geomagnetic heating weight 0.6 + w sin^2(mlat): area mean 1
};
// Jacchia night-time minimum: Tc = 379 + 3.24 F81 + 1.3 (F - F81) (K).
double jacchia_night_temperature_k(double f107, double f107_81);
// Jacchia (1970) geomagnetic heating: 28 Kp + 0.03 e^Kp (K).
double jacchia_geomagnetic_heating_k(double kp);
double exospheric_temperature_k(double f107, double f107_81, double kp_lagged, double local_solar_time_h, double latitude_deg,
                                double subsolar_latitude_deg, double magnetic_latitude_deg, const ThermosphereConfig& config);
// Density ratio at an altitude for two exospheric temperatures (isothermal above 120 km, atomic oxygen): satellite drag.
double thermosphere_density_ratio(double exospheric_k, double reference_k, double altitude_m = 400.0e3);

}  // namespace terraflux::weather
