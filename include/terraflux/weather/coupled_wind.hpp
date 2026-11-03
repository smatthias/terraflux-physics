#pragma once

#include "terraflux/weather/pressure_wind.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace terraflux::weather {

// Coupled wind / moist-layer model v1.
//
// A single dry-to-moist layer over a prescribed, static pressure field. Per
// step the model:
//   1. diagnoses air density rho = p / (R_d T (1 + 0.608 q)) from pressure,
//      temperature T and specific humidity q (or uses the constant
//      atmosphere density when density_mode is "constant");
//   2. advances the wind with the pressure-gradient force -(1/rho) grad p,
//      Coriolis deflection, linear friction and explicit momentum diffusion
//      nu laplacian(u, v);
//   3. transports T and q with the new wind in advective form using
//      first-order upwind differences plus explicit diffusion kappa_T and
//      kappa_q.
//
// Air density therefore responds to temperature and humidity, and the wind
// responds to density; temperature and humidity respond to the wind. The
// pressure field does not respond to anything in v1. Relative humidity,
// vapour pressure and virtual temperature are diagnostics; no condensation
// or latent heating is modelled, so supersaturation is reported, not removed.

struct DiffusionConfig {
    // Horizontal mixing diffusivities in m^2/s, each >= 0. These smooth the
    // fields between the pressure systems and are explicit per-step
    // Laplacian terms with the same stability bound as the transport model.
    double temperature_m2_s{0.0};
    double humidity_m2_s{0.0};
    double momentum_m2_s{0.0};
};

struct CoupledWindConfig {
    TimeSteppingConfig time;
    TransportGrid2D grid;
    AtmosphereConfig atmosphere;
    DiffusionConfig diffusion;
    // "one_sided" or "periodic", as in PressureWindConfig.
    std::string pressure_gradient_boundary{"one_sided"};
    // Boundary for scalar transport and momentum/scalar diffusion:
    // "zero_gradient": the value outside the domain equals the edge cell
    //                  (no diffusive flux, inflow carries the edge value).
    // "periodic":      wrap on both axes.
    std::string scalar_boundary{"zero_gradient"};
    // "diagnosed": rho from p, T, q each step (the coupling).
    // "constant":  rho = atmosphere.air_density_kg_m3; T and q are then
    //              passive tracers, useful to isolate the feedback.
    std::string density_mode{"diagnosed"};
};

struct CoupledWindState {
    double elapsed_seconds{0.0};
    // Row-major arrays: index = y * nx + x. Pressure is prescribed and fixed.
    std::vector<double> pressure_pa;
    std::vector<double> wind_east_m_s;
    std::vector<double> wind_north_m_s;
    std::vector<double> air_temperature_k;
    std::vector<double> specific_humidity_kg_kg;
};

struct MoistLayerDiagnostics {
    std::vector<double> air_density_kg_m3;
    std::vector<double> virtual_temperature_k;
    std::vector<double> vapour_pressure_pa;
    std::vector<double> saturation_vapour_pressure_pa;
    std::vector<double> relative_humidity_fraction;
    std::size_t supersaturated_cells{0};
};

// Gaussian bump field: background + sum(amplitude * exp(-(r / radius)^2)).
// Used for idealized temperature and humidity anomalies co-located with
// pressure systems (warm, moist lows; cool, dry highs) or anywhere else.
struct GaussianAnomaly {
    double east_m{0.0};
    double north_m{0.0};
    double radius_m{0.0};
    double amplitude{0.0};
};

std::vector<double> build_gaussian_field(
    double background,
    const std::vector<GaussianAnomaly>& anomalies,
    const TransportGrid2D& grid);

void validate_coupled_wind_config(const CoupledWindConfig& config);
void validate_coupled_wind_state(
    const CoupledWindState& state,
    const CoupledWindConfig& config);

// Density, virtual temperature, vapour pressures and relative humidity for
// every cell of a validated state. With density_mode "constant" the density
// array still reports the diagnosed moist-air density for inspection.
MoistLayerDiagnostics diagnose_moist_layer(
    const CoupledWindState& state,
    const CoupledWindConfig& config);

// Largest per-cell scalar transport stability number for the given wind:
//   dt [ |u|/dx + |v|/dy + 2 kappa (1/dx^2 + 1/dy^2) ]  with kappa = max(kappa_T, kappa_q)
// advance_coupled_wind throws if this exceeds 1 at any step.
double coupled_scalar_stability_number(
    const CoupledWindState& state,
    const CoupledWindConfig& config);

// Apply all configured steps. Throws std::invalid_argument on validation
// failure or an unresolved scalar stability bound, and std::runtime_error if
// an update becomes non-finite, non-positive (temperature) or negative
// (humidity). Rejects rather than substepping.
CoupledWindState advance_coupled_wind(
    const CoupledWindState& initial,
    const CoupledWindConfig& config);

}  // namespace terraflux::weather
