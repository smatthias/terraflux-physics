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

// Orography v1: mountains and valleys in the coupled layer. The layer is
// terrain-following, so air moving along a slope changes height at the
// orographic vertical velocity w = V . grad h:
//   * unsaturated air rising or any air sinking changes temperature at the
//     dry adiabatic rate g/c_p (about 1 K per 100 m);
//   * saturated rising air cools at the moist adiabatic rate (0.4-0.7 K per
//     100 m) and the excess vapour condenses as orographic precipitation;
//   so air that crosses a ridge arrives on the lee warmer and drier than it
//   left the windward foot: the foehn.
//   * Flow that cannot climb an obstacle is blocked and deflected along it,
//     controlled by the cross-barrier Froude number Fr = U_n / (N dh) (U_n:
//     upslope wind component, dh: obstacle height ahead of the air within
//     the obstacle radius) weighted by the angle of attack (U_n/|V|)^2, so
//     flow parallel to a ridge is untouched. The upslope component is
//     removed at the blocking time scale in proportion to the blocked
//     fraction; the Coriolis response turns the blocked air along the range
//     (barrier jet). An east-west range therefore blocks north-south
//     exchange while a north-south range blocks zonal flow.
//   * A diurnal surface heating/cooling cycle acts on the layer; both are
//     enhanced in valleys in proportion to the depth below the surrounding
//     terrain (a sheltered, shallow air mass), so cold air pools in valleys
//     and basins at night and the pool is eroded by day.
//   * The surface pressure follows the terrain hydrostatically for density.
struct OrographyConfig {
    bool enabled{false};
    std::vector<double> elevation_m;           // per cell, row-major
    double blocking_time_scale_s{1800.0};
    double obstacle_radius_m{200000.0};        // how far ahead the air "sees" the terrain it must climb
    double influence_radius_m{100000.0};       // valley neighbourhood and cold-pool reference
    double brunt_vaisala_s{0.01};              // N of the ambient stability for the Froude number
    double boundary_layer_depth_m{1500.0};     // layer mass for orographic precipitation
    double daytime_heating_k_per_s{0.0};       // peak surface heating rate (sine cycle)
    double nocturnal_cooling_k_per_s{0.0};     // peak nocturnal cooling rate
    double diurnal_phase_seconds{0.0};         // elapsed time at sunrise
    double valley_pooling_per_km{1.0};         // heating/cooling enhancement per km of valley depth
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
    OrographyConfig orography;
};

struct CoupledWindState {
    double elapsed_seconds{0.0};
    // Row-major arrays: index = y * nx + x. Pressure is prescribed and fixed.
    std::vector<double> pressure_pa;
    std::vector<double> wind_east_m_s;
    std::vector<double> wind_north_m_s;
    std::vector<double> air_temperature_k;
    std::vector<double> specific_humidity_kg_kg;
    // Orographic precipitation accumulated by advance_coupled_wind (mm);
    // empty unless orography is enabled. Callers may reset it per interval.
    std::vector<double> orographic_precipitation_mm;
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

// Terrain-related fields derived from the configuration and state.
struct OrographyField {
    std::vector<double> slope_east;              // dh/dx
    std::vector<double> slope_north;             // dh/dy
    std::vector<double> upslope_lifting_m_s;     // V . grad h (negative = descent)
    std::vector<double> obstacle_height_m;       // rise ahead of the air within the radius
    std::vector<double> froude_number;           // NaN where no obstacle lies ahead
    std::vector<double> blocked_fraction;        // 0..1
    std::vector<double> valley_depth_m;          // neighbourhood mean elevation minus local (>= 0)
    std::vector<double> cross_barrier_wind_m_s;  // wind component up the local slope
    std::vector<double> along_barrier_wind_m_s;  // wind component along the local contour
    std::vector<double> ridge_orientation_deg;   // contour direction, 0 = north-south, 90 = east-west
    std::vector<double> surface_pressure_pa;     // hydrostatic at the terrain height
    std::vector<double> cold_pool_k;             // temperature deficit versus the neighbourhood at equal height
    std::vector<unsigned char> cold_pool;        // deficit >= 1 K in a valley >= 100 m deep
    std::vector<unsigned char> foehn;            // descending flow, warm anomaly, RH < 0.5
    std::vector<double> foehn_warming_k;         // warm anomaly of foehn cells
    std::size_t cold_pool_cells{0};
    std::size_t foehn_cells{0};
};

OrographyField diagnose_orography(const CoupledWindState& state, const CoupledWindConfig& config);

// Diurnal surface tendency of the orography component (K/s): heating by day,
// cooling by night.
double diurnal_surface_tendency_k_s(double elapsed_seconds, const OrographyConfig& config);
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
