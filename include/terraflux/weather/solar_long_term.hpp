#pragma once

#include "terraflux/weather/overturning_box.hpp"
#include "terraflux/weather/solar_cycle.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terraflux::weather {

// Solar long-term activity v1: grand minima and maxima (centuries), how they arise and what they do.
//
// 1. The historical envelope. Before the telescopic cycle table (SILSO cycle 1 begins in 1755) the cycles are
//    synthesised from a catalogue of documented epochs (Oort, Wolf, Sporer, Maunder minima, the Medieval Maximum,
//    ...): each epoch sets the typical cycle amplitude between its start and end, with smooth (tanh) transitions,
//    and `normal_amplitude` applies outside them. Cycle lengths scatter around 11 years, amplitudes lognormally
//    around the envelope. The envelope is a summary of the literature, not a reconstruction.
//
// 2. Episodes. In an annual sunspot series a grand minimum (maximum) is a stretch of at least `min_years` years in
//    which the centred `window_years` running mean stays below (above) a threshold. The defaults follow the
//    isotope-era definition of Usoskin et al. (2007) in the version-2 scale: decadal mean below 25 (above 83) for at
//    least 20 years.
//
// 3. The slow background. Besides the cycle, the Sun's quiet network brightens with the activity of the last decades
//    (the open flux and ephemeral regions that survive minima). It enters as an extra facular index
//        x_bg = k (M(t) - M_ref) / SN_ref,   M = exponential moving average of the annual sunspot number (tau years),
//    M_ref its mean over a reference period (the satellite era, where the quiet-Sun TSI is calibrated). The size k
//    is uncertain: reconstructions put the TSI of the Maunder Minimum 0.5-1.5 W/m2 below today's solar minima,
//    older ones up to several W/m2, hence low / central / high values.
//
// 4. Cosmic rays. A weaker heliospheric field lets in more galactic cosmic rays, which make 14C and 10Be: the
//    archive of past solar activity. The modulation potential phi = phi_min + c (w SN_3yr + (1 - w) M) (MV) and the
//    relative production Q/Q_ref = ((phi_ref + phi_0) / (phi + phi_0))^g are a calibrated proxy: g is chosen so that
//    a Maunder-like minimum raises the production by about 30 % over the modern mean, as the isotope records show.
//
// 5. Comparison forcings for the climate: volcanic eruptions (a pulse rising and decaying over about a year) and
//    CO2 (5.35 ln(C / C_ref) W/m2). They put the solar part of past climate changes in proportion.
//
// 6. The dynamo map (cause). A one-dimensional stochastic map of the toroidal field b_n at the n-th cycle, in the
//    spirit of Babcock-Leighton dynamo maps with a weak mean-field alpha effect (Charbonneau et al. 2005; Passos
//    et al. 2014):
//        e(b)     = (1 + erf((b - b_e) / w)) / 2                 share of the field strong enough to emerge as spots
//        b_{n+1}  = | alpha_BL (1 + sigma eps_n) e(b_n) + alpha_w | b_n / (1 + (b_n / b_q)^2) + eta |zeta_n|
//        A_n      = A_0 b_n^2 e(b_n)                             sunspot amplitude of the cycle
//    The Babcock-Leighton source works through the decay of tilted spot groups: it needs spots. It fluctuates from
//    cycle to cycle (scatter of the tilts, eps normal). When a strong fluctuation drops the field below the
//    emergence threshold b_e, spots vanish, the Babcock-Leighton source switches off, and only the weak,
//    slightly supercritical alpha_w effect sustains a weak magnetic cycle (the 11-year rhythm that 10Be shows through
//    the Maunder Minimum) until the field grows back above the threshold: a grand minimum. alpha_BL = 1 + 1/b_q^2 -
//    alpha_w puts the mean state at b = 1. The defaults are calibrated to the isotope statistics of the Holocene
//    (about 27 grand minima in 11 400 years, 17 % of the time, mean length about 70 years).

enum class SolarEpochKind : unsigned char { grand_minimum = 0, grand_maximum = 1, minimum = 2, maximum = 3 };
const char* solar_epoch_kind_name(SolarEpochKind kind);

struct SolarEpoch {
    std::string name;
    SolarEpochKind kind{SolarEpochKind::grand_minimum};
    double start_year{0.0}, end_year{0.0};
    double cycle_amplitude{0.0};               // typical smoothed maximum of its cycles (v2); <= 0: annotation only
    std::string evidence;                      // how it is known (telescopic, isotopes, ...)
};

struct SolarEnvelopeConfig {
    double normal_amplitude{120.0};
    double transition_years{12.0};             // tanh half-width of the transitions into and out of an epoch
};
void validate_solar_epochs(const std::vector<SolarEpoch>& epochs, const SolarEnvelopeConfig& config);
// Typical cycle amplitude at a date: the epochs with an amplitude blended into the normal amplitude.
double solar_envelope(double year, const std::vector<SolarEpoch>& epochs, const SolarEnvelopeConfig& config);

struct HistoricalCycleConfig {
    double period_years{11.0};
    double period_sd_years{1.0};
    double amplitude_log_sd{0.25};
    double lead_years{0.3};                    // onset before the cycle's minimum (for the first table cycle)
    std::uint64_t seed{7};
};
struct HistoricalCycles {
    std::vector<SolarCycleCurve> curves;       // in time order
    std::vector<int> numbers;                  // counted backwards from the first table cycle (0, -1, -2, ...)
};
// Cycles before `first_onset` (the onset of the first table cycle, number `first_number`) back to `start_year`.
HistoricalCycles synthesize_historical_cycles(double first_onset, int first_number, double start_year, const std::vector<SolarEpoch>& epochs,
                                              const SolarEnvelopeConfig& envelope, const HistoricalCycleConfig& config);

// Annual means (12 monthly samples) of summed curves for the years first_year..last_year.
std::vector<double> annual_cycle_means(const std::vector<SolarCycleCurve>& curves, int first_year, int last_year);
// Exponential moving average with a time constant in samples (years), started at the mean of the first `spin` values.
std::vector<double> exponential_moving_average(const std::vector<double>& values, double timescale, std::size_t spin = 11);
// Centred running mean over `window` values (shorter at the ends).
std::vector<double> running_mean(const std::vector<double>& values, std::size_t window);

struct SolarEpisode {
    bool grand_minimum{true};
    int start_year{0}, end_year{0};            // inclusive
    double mean_sunspot{0.0};
};
std::vector<SolarEpisode> detect_solar_episodes(const std::vector<double>& annual, int first_year, double low_threshold, double high_threshold,
                                                std::size_t window_years = 11, int min_years = 20);

struct CosmicRayConfig {
    double phi_min_mv{150.0};
    double phi_per_sunspot_mv{4.0};
    double background_weight{0.5};             // 1 - w: weight of the slow background M in the heliospheric field
    double production_offset_mv{300.0};
    double production_exponent{0.53};
};
void validate_cosmic_rays(const CosmicRayConfig& config);
double modulation_potential_mv(double sunspot_3yr, double background_sunspot, const CosmicRayConfig& config);
double relative_cosmogenic_production(double phi_mv, double reference_phi_mv, const CosmicRayConfig& config);

struct VolcanicEruption {
    std::string name;
    double year{0.0};                          // decimal year of the eruption
    double peak_forcing_w_m2{0.0};             // negative
};
// Global-mean forcing of the eruptions at a date: peak (u / r) exp(1 - u / r), u = t - t_eruption >= 0, r the rise time.
double volcanic_forcing_w_m2(double year, const std::vector<VolcanicEruption>& eruptions, double rise_years = 0.75);
double annual_volcanic_forcing_w_m2(int year, const std::vector<VolcanicEruption>& eruptions, double rise_years = 0.75);
double co2_forcing_w_m2(double ppm, double reference_ppm = 278.0);

// ---- Dynamo map ---------------------------------------------------------------------------------------------------

struct SolarDynamoConfig {
    double weak_alpha{1.033};                  // alpha_w: slightly supercritical mean-field alpha effect
    double babcock_leighton_noise{0.73};       // sigma
    double emergence_threshold{0.50};          // b_e
    double emergence_width{0.10};              // w
    double quenching_field{1.5};               // b_q
    double floor_noise{0.01};                  // eta
    double amplitude_scale{121.0};             // A_0
    double cycle_mean_fraction{0.45};          // cycle-mean sunspot number / amplitude (Hathaway shape)
    double grand_minimum_mean{25.0};           // cycle-mean thresholds of the episodes (v2 decadal)
    double grand_maximum_mean{83.0};
    std::size_t episode_cycles{2};             // consecutive cycles
    double babcock_leighton_efficiency() const { return 1.0 + 1.0 / (quenching_field * quenching_field) - weak_alpha; }
};
void validate_solar_dynamo(const SolarDynamoConfig& config);

struct SolarDynamoStep {
    double field{0.0};                         // b_n
    double emergence{0.0};                     // e(b_n)
    double efficiency{0.0};                    // 1 + sigma eps_n: the Babcock-Leighton source of this cycle (relative)
    double amplitude{0.0};                     // A_n
};
double dynamo_emergence(double field, const SolarDynamoConfig& config);
double dynamo_amplitude(double field, const SolarDynamoConfig& config);
// The field whose cycle has the amplitude (inverse of dynamo_amplitude, bisection).
double dynamo_field_for_amplitude(double amplitude, const SolarDynamoConfig& config);
std::vector<SolarDynamoStep> simulate_solar_dynamo(const SolarDynamoConfig& config, double initial_field, std::size_t cycles, NormalSequence& random);

struct SolarCycleEpisode {
    bool grand_minimum{true};
    std::size_t start{0}, length{0};           // cycle index and number of cycles
};
std::vector<SolarCycleEpisode> dynamo_episodes(const std::vector<double>& amplitudes, const SolarDynamoConfig& config);

struct SolarDynamoStatistics {
    std::size_t cycles{0};
    double years{0.0};
    double grand_minimum_fraction{0.0}, grand_minima_per_11400_years{0.0}, grand_minimum_mean_years{0.0}, grand_minimum_longest_years{0.0};
    double grand_maximum_fraction{0.0}, grand_maxima_per_11400_years{0.0}, grand_maximum_mean_years{0.0};
    double mean_amplitude_outside_minima{0.0};
    double amplitude_autocorrelation{0.0};
    std::vector<std::size_t> grand_minimum_lengths;   // histogram by number of cycles (index = cycles)
    double mean_waiting_years{0.0};            // from the end of one grand minimum to the start of the next
};
SolarDynamoStatistics dynamo_statistics(const std::vector<double>& amplitudes, const SolarDynamoConfig& config, double years_per_cycle);

struct SolarDynamoOutlook {
    std::size_t realizations{0};
    std::vector<double> grand_minimum_begun;   // [k]: share of realizations in which a grand minimum has started by cycle k+1
    std::vector<double> amplitude_p5, amplitude_p50, amplitude_p95;   // [k]: amplitude of cycle k+1
    std::vector<double> below_threshold;       // [k]: share with cycle k+1 below the grand-minimum amplitude
};
// Realisations forward from the last observed cycle amplitudes (the field of the last one; a grand minimum needs
// `episode_cycles` consecutive low cycles, counting the observed ones).
SolarDynamoOutlook dynamo_outlook(const SolarDynamoConfig& config, const std::vector<double>& observed_amplitudes, std::size_t cycles_ahead,
                                  std::size_t realizations, std::uint64_t seed);

}  // namespace terraflux::weather
