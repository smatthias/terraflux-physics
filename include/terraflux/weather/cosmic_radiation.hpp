#pragma once

#include <string>
#include <vector>

namespace terraflux::weather {

// Cosmic radiation v1: galactic cosmic rays and solar energetic particles at the top of the atmosphere, what the
// geomagnetic field lets in (the cutoff rigidity, lowest in the polar caps) and what the atmosphere receives.
//
// 1. Galactic cosmic rays (GCR). The local interstellar proton spectrum of Burger et al. (2000) in the form of
//    Usoskin et al. (2005), J_LIS(T) = 1.9e4 P^-2.78 / (1 + 0.4866 P^-2.51) (m^-2 s^-1 sr^-1 GeV^-1, T in GeV, P in
//    GV), modulated by the heliosphere with the force-field approximation (Gleeson & Axford 1968):
//        J(T, phi) = J_LIS(T + phi) T (T + 2 Tr) / ((T + phi)(T + phi + 2 Tr)).
//    The modulation potential phi rises with solar activity (about 0.35 GV at solar minimum, 1 GV and more at maximum)
//    and jumps for days when a coronal mass ejection passes (a Forbush decrease).
// 2. What reaches a level of the atmosphere: the flux above the local cutoff rigidity, weighted with an effective
//    yield Y(P) = P^beta exp(-(P_a / P)^kappa). P_a is the rigidity the atmosphere above the level absorbs; beta and
//    kappa are calibrated so that the latitude effect (pole / equator) and the solar-cycle change resemble
//    observations (neutron monitors, aviation dose codes, balloon soundings). The result is scaled to an anchor
//    value at the pole at a reference modulation.
// 3. Solar energetic particles (SEP). An exponential spectrum in rigidity, J(>R) = J10 exp(-(R - R10) / R0), given
//    by the >10 MeV flux (pfu) and the e-folding rigidity R0. Only where the cutoff lies below a few hundred MV, in
//    the polar caps, do they enter. Their energy is deposited by range (R(E) = a E^b g/cm^2, slant paths) in layers
//    of the US Standard Atmosphere: ion pairs (35 eV each), NOx (1.25 per ion pair), HOx (2 per ion pair).

// ---- Spectra ------------------------------------------------------------------------------------------------------

double proton_rigidity_gv(double kinetic_gev);
double proton_kinetic_gev(double rigidity_gv);
double lis_proton_flux(double kinetic_gev);                            // m^-2 s^-1 sr^-1 GeV^-1
double force_field_proton_flux(double kinetic_gev, double phi_gv);     // m^-2 s^-1 sr^-1 GeV^-1
double gcr_flux_per_rigidity(double rigidity_gv, double phi_gv);       // m^-2 s^-1 sr^-1 GV^-1

// ---- Responses of the atmosphere ----------------------------------------------------------------------------------

struct RadiationResponse {
    std::string name;
    std::string unit;
    double yield_exponent{1.0};                  // beta
    double threshold_gv{2.0};                    // P_a
    double sharpness{3.0};                       // kappa
    double anchor{1.0};                          // value at the pole (cutoff 0) at the reference modulation
    double reference_phi_gv{0.4};
};
// Sea-level neutron monitor (% of the polar value at solar minimum), effective dose at 12 km (uSv/h), ionisation
// at the Pfotzer maximum (ion pairs per cm^3 and s) and cosmogenic isotope production (relative).
std::vector<RadiationResponse> default_radiation_responses();
void validate_radiation_response(const RadiationResponse& response);
double response_yield(double rigidity_gv, const RadiationResponse& response);

// A response as a function of the cutoff rigidity, for one GCR modulation and optionally one SEP spectrum.
struct SepSpectrum {
    double flux_above_10mev_pfu{0.0};            // particles cm^-2 s^-1 sr^-1 above 10 MeV
    double efold_rigidity_gv{0.1};
};
double sep_flux_above_pfu(double rigidity_gv, const SepSpectrum& sep);
double sep_flux_per_rigidity(double rigidity_gv, const SepSpectrum& sep);   // m^-2 s^-1 sr^-1 GV^-1

class CutoffResponse {
public:
    // Tabulates the anchored response from cutoff 0 to max_cutoff_gv for the GCR at phi (and the SEP if given).
    CutoffResponse(const RadiationResponse& response, double phi_gv, const SepSpectrum* sep = nullptr, double max_cutoff_gv = 25.0);
    double operator()(double cutoff_gv) const;   // anchored value (GCR + SEP)
    double gcr(double cutoff_gv) const;
    double sep(double cutoff_gv) const;
private:
    double step_;
    std::vector<double> gcr_, sep_;
    double lookup(const std::vector<double>& table, double cutoff_gv) const;
};

// ---- SEP energy deposition ----------------------------------------------------------------------------------------

struct SepAtmosphereConfig {
    double range_coefficient_g_cm2{0.0022};      // proton range in air R = a E^b (E in MeV)
    double range_exponent{1.77};
    double mean_secant{2.0};                     // isotropic flux through a horizontal surface
    double ion_pair_energy_ev{35.0};
    double nox_per_ion_pair{1.25};
    double hox_per_ion_pair{2.0};
    std::vector<double> layer_tops_km{1000.0, 50.0, 35.0, 20.0};   // layers: above 50, 35-50, 20-35, below 20 km
};
void validate_sep_atmosphere(const SepAtmosphereConfig& config);

struct SepDeposition {
    double energy_mev_cm2_s{0.0};                // column total
    std::vector<double> layer_mev_cm2_s;         // by layer
    double ion_pairs_cm2_s{0.0};
};
SepDeposition sep_deposition(double cutoff_gv, const SepSpectrum& sep, const SepAtmosphereConfig& config);

}  // namespace terraflux::weather
