#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Pressure-gradient wind v1: a single horizontal layer of dry air accelerated
// by a prescribed, static pressure field, deflected by the Coriolis force and
// slowed by linear surface friction. Per cell:
//
//   du/dt = -(1/rho) dp/dx + f v - k u
//   dv/dt = -(1/rho) dp/dy - f u - k v
//
// u is eastward, v is northward (m/s); p is pressure (Pa); rho is air density
// (kg/m^3); f is the Coriolis parameter (1/s, positive in the Northern
// Hemisphere); k is a linear (Rayleigh) friction coefficient (1/s).
//
// Without friction the steady state is the geostrophic wind, which blows
// parallel to the isobars (low pressure on the left in the Northern
// Hemisphere, on the right in the Southern Hemisphere). With friction the
// steady state is turned across the isobars toward low pressure by the angle
// atan(k / |f|). Air therefore does not flow straight from high to low
// pressure unless the Coriolis parameter is zero.

struct TimeSteppingConfig {
    std::string start_time_utc;
    double timestep_seconds{0.0};
    std::size_t steps{0};
};

struct AtmosphereConfig {
    // Constant air density of the single layer.
    double air_density_kg_m3{1.225};
    // Linear surface friction (Rayleigh drag) coefficient, >= 0. Zero gives a
    // frictionless layer whose steady state is purely geostrophic.
    double surface_friction_coefficient_s{0.0};
    // "none":       f = 0 everywhere; air accelerates straight down-gradient.
    // "f_plane":    constant f from the grid's geographic anchor latitude.
    // "beta_plane": f = f0 + beta * north_m, linearized about the anchor.
    // "prescribed": constant f given by coriolis_parameter_s (idealized runs).
    std::string coriolis_mode{"f_plane"};
    double coriolis_parameter_s{0.0};
    double earth_angular_speed_rad_s{7.2921159e-5};
    double earth_radius_m{6371000.0};
};

struct PressureWindConfig {
    TimeSteppingConfig time;
    TransportGrid2D grid;
    AtmosphereConfig atmosphere;
    // Pressure-gradient stencil at the domain edge:
    // "one_sided": central differences inside, one-sided at the edges
    //              (no wrap, suited to regional fields).
    // "periodic":  central differences with periodic wrap on both axes. The
    //              resulting geostrophic wind is discretely divergence-free
    //              on an f-plane and can feed the dry transport model.
    std::string pressure_gradient_boundary{"one_sided"};
};

struct PressureWindState {
    double elapsed_seconds{0.0};
    // Row-major arrays: index = y * nx + x. Pressure is prescribed and fixed
    // during a run; the wind components evolve.
    std::vector<double> pressure_pa;
    std::vector<double> wind_east_m_s;
    std::vector<double> wind_north_m_s;
};

// Idealized synoptic pressure systems: a background pressure plus Gaussian
// anomalies. "high" centers require a positive anomaly; "low" centers require
// a negative anomaly. radius_m is the e-folding radius of the anomaly.
struct PressureCenter {
    std::string kind;  // "high" or "low"
    double east_m{0.0};
    double north_m{0.0};
    double pressure_anomaly_pa{0.0};
    double radius_m{0.0};
};

struct PressureSystemsConfig {
    double background_pressure_pa{101325.0};
    std::vector<PressureCenter> centers;
};

struct PressureGradient {
    // Pa/m, row-major.
    std::vector<double> east_pa_m;
    std::vector<double> north_pa_m;
};

struct WindField {
    // m/s, row-major.
    std::vector<double> east_m_s;
    std::vector<double> north_m_s;
};

// Validate structure, physical ranges, grid/array consistency, and the
// timestep accuracy bound timestep * (|f| + k) <= 1.
void validate_pressure_wind_config(const PressureWindConfig& config);
void validate_pressure_wind_state(
    const PressureWindState& state,
    const PressureWindConfig& config);
void validate_pressure_systems(
    const PressureSystemsConfig& systems,
    const TransportGrid2D& grid);

// Evaluate background + Gaussian anomalies at every cell center. Throws if
// the resulting pressure is not finite and positive everywhere.
std::vector<double> build_pressure_field(
    const PressureSystemsConfig& systems,
    const TransportGrid2D& grid);

// Coriolis parameter per cell according to the configured mode.
std::vector<double> coriolis_parameter(
    const TransportGrid2D& grid,
    const AtmosphereConfig& atmosphere);

// Cell-centered pressure gradient using the configured edge treatment.
PressureGradient calculate_pressure_gradient(
    const std::vector<double>& pressure_pa,
    const TransportGrid2D& grid,
    const std::string& boundary);

// Geostrophic balance: u = -(1/(rho f)) dp/dy, v = (1/(rho f)) dp/dx.
// Throws if the Coriolis parameter is zero in any cell.
WindField geostrophic_wind(
    const std::vector<double>& pressure_pa,
    const PressureWindConfig& config);

// Steady state of the friction/Coriolis/pressure-gradient balance:
//   u = -(k dp/dx + f dp/dy) / (rho (k^2 + f^2))
//   v =  (f dp/dx - k dp/dy) / (rho (k^2 + f^2))
// Reduces to the geostrophic wind when k = 0. Throws if k^2 + f^2 is zero in
// any cell, because no steady state exists there.
WindField balanced_wind(
    const std::vector<double>& pressure_pa,
    const PressureWindConfig& config);

// Apply all configured steps. The pressure-gradient force is constant per
// step; the Coriolis and friction terms use the trapezoidal (Crank-Nicolson)
// rule, which conserves the inertial-oscillation amplitude exactly when
// k = 0 and whose fixed point equals balanced_wind for any timestep.
// Throws if validation fails or an update becomes non-finite.
PressureWindState advance_pressure_wind(
    const PressureWindState& initial,
    const PressureWindConfig& config);

}  // namespace terraflux::weather
