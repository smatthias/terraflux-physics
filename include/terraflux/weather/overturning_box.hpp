#pragma once

#include <cstdint>
#include <vector>

namespace terraflux::weather {

// Overturning box model v1: the Atlantic meridional overturning (AMOC) as two boxes (Stommel 1961 with
// mixed boundary conditions). North (subpolar North Atlantic, where the water sinks) and south (the
// tropical and South Atlantic that feeds it) exchange water at the overturning rate |q|:
//
//   q          = k [alpha (T_S - T_N) - beta (S_S - S_N)]              (m3/s; > 0 = sinking in the north)
//   dT_N/dt    = |q| (T_S - T_N) / V_N + (T*_N - T_N) / tau            (temperatures relax to targets that
//   dT_S/dt    = |q| (T_N - T_S) / V_S + (T*_S - T_S) / tau             warm with the climate)
//   dS_N/dt    = |q| (S_S - S_N) / V_N - F S0 / V_N                     (F: fresh water into the north box,
//   dS_S/dt    = |q| (S_N - S_S) / V_S + F S0 / V_S                      salt conserved)
//
// Steady states: with D* = T*_S - T*_N and c = 1/V_N + 1/V_S, the differences settle at
// D = D*/(1 + |q| tau c) and S_S - S_N = F S0 / |q|, so a sinking-north state q > 0 satisfies
//   F = psi(q) = [k alpha D* q / (1 + q tau c) - q^2] / (k beta S0).
// psi rises from 0 to a maximum and falls: F < max psi has two such states (the larger is stable, the smaller
// unstable), F > max psi has none. F_crit = max psi is the tipping point (saddle-node): past it the
// circulation collapses to the reversed, salinity-driven state, which exists for every F > 0, so it recovers
// only when the fresh water into the north turns negative (a wide hysteresis). For tau -> 0 the threshold is
// Stommel's F_crit = k alpha^2 D*^2 / (4 beta S0), at q = k alpha D* / 2: half the thermal strength.
//
// Warming enters through the targets: polar amplification warms T*_N more than T*_S, shrinking D* and with
// it both the circulation and the threshold. Northward heat transport = rho c_p q (T_S - T_N).

struct OverturningBoxConfig {
    double north_volume_m3{1.0e16};             // subpolar North Atlantic, upper ~1000 m
    double south_volume_m3{5.0e16};
    double flow_constant_m3_s{9.5e9};           // k (m3/s per unit density fraction)
    double thermal_expansion_per_k{1.7e-4};
    double haline_contraction_per_psu{7.6e-4};
    double reference_salinity_psu{35.0};
    double temperature_relaxation_s{5.0 * 365.25 * 86400.0};   // rho c_p H / lambda: 1000 m with ~26 W/m2/K
    double density_kg_m3{1027.0};
    double heat_capacity_j_kg_k{3990.0};
};

// Observable present-day state from which the internal parameters are derived.
struct OverturningCalibration {
    double overturning_sv{17.0};                // present AMOC strength
    double temperature_difference_k{15.0};      // T_S - T_N
    double salinity_difference_psu{1.0};        // S_S - S_N: sets how close the circulation is to the threshold
    double north_temperature_k{279.15};
    double north_salinity_psu{35.0};
};

struct CalibratedOverturning {
    OverturningBoxConfig config;
    double freshwater_sv{0.0};                  // present F (steady state)
    double north_target_k{0.0};                 // T*_N, T*_S reproducing the present temperatures
    double south_target_k{0.0};
};

// k from q = k (alpha D - beta Sigma), F = q Sigma / S0, and targets with D* = D (1 + q tau c) such that the
// present state is steady. Throws std::invalid_argument if the present state is not a sinking-north state.
CalibratedOverturning calibrate_overturning(const OverturningCalibration& calibration, OverturningBoxConfig base);

struct OverturningBoxState {
    double elapsed_seconds{0.0};
    double north_temperature_k{279.15};
    double south_temperature_k{294.15};
    double north_salinity_psu{35.0};
    double south_salinity_psu{36.0};
};

void validate_overturning(const OverturningBoxConfig& config);
double overturning_m3_s(const OverturningBoxState& state, const OverturningBoxConfig& config);
double heat_transport_w(const OverturningBoxState& state, const OverturningBoxConfig& config);
// Classical fourth-order Runge-Kutta step with F (m3/s of fresh water into the north) and the targets held.
OverturningBoxState advance_overturning(const OverturningBoxState& state, const OverturningBoxConfig& config, double freshwater_m3_s,
                                        double north_target_k, double south_target_k, double dt_seconds);

struct OverturningEquilibrium {
    double overturning_m3_s{0.0};
    double temperature_difference_k{0.0};       // T_S - T_N
    double salinity_difference_psu{0.0};        // S_S - S_N
    bool stable{false};
};
// All steady states for a freshwater forcing and target difference (sinking-north states, then the reversed one).
std::vector<OverturningEquilibrium> overturning_equilibria(const OverturningBoxConfig& config, double freshwater_m3_s, double target_difference_k);
// The tipping point F_crit = max psi and the overturning there.
double critical_freshwater_m3_s(const OverturningBoxConfig& config, double target_difference_k, double* overturning_at_threshold = nullptr);
// Stommel's closed form for tau -> 0.
double stommel_critical_freshwater_m3_s(double flow_constant_m3_s, double alpha, double beta, double temperature_difference_k, double reference_salinity_psu);
// Linear stability of a steady state of the (D, Sigma) system.
bool overturning_state_stable(const OverturningBoxConfig& config, double overturning_m3_s, double temperature_difference_k, double salinity_difference_psu);

// Recovery time -1 / max Re(lambda) of the linearised (D, Sigma) system at a state: how fast the circulation
// recovers from a small perturbation. It grows without bound as the forcing approaches the threshold (critical
// slowing down), so it is a noise-free resilience indicator. Infinite for an unstable state.
double overturning_recovery_time_s(const OverturningBoxConfig& config, double overturning_m3_s, double temperature_difference_k, double salinity_difference_psu);
// The same for the strong (sinking-north) steady state at a forcing; infinite past the threshold.
double equilibrium_recovery_time_s(const OverturningBoxConfig& config, double freshwater_m3_s, double target_difference_k);

// Statistical early-warning indicators (critical slowing down in a noisy record): over a sliding window of
// `window` samples the series is linearly detrended and its variance and lag-1 autocorrelation computed
// (NaN-free: the first window-1 entries repeat the first full window). Kendall's tau measures their trend.
// They need windows much longer than the recovery time: for an overturning that recovers in decades to
// centuries, annual records of a century or two cannot show the slowing down reliably.
struct EarlyWarning {
    std::vector<double> variance;
    std::vector<double> autocorrelation;
};
EarlyWarning early_warning_indicators(const std::vector<double>& series, std::size_t window);
double kendall_tau(const std::vector<double>& values);

// Deterministic standard normal numbers (xorshift64* + Box-Muller) for reproducible stochastic forcing.
class NormalSequence {
public:
    explicit NormalSequence(std::uint64_t seed);
    double next();
    double uniform();                           // (0, 1); shares the state with next()
private:
    std::uint64_t state_;
    bool has_spare_{false};
    double spare_{0.0};
};

}  // namespace terraflux::weather
