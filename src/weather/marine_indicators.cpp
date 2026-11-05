#include "terraflux/weather/marine_indicators.hpp"

#include <cmath>
#include <stdexcept>

namespace terraflux::weather {

double oxygen_solubility_umol_kg(double temperature_k, double salinity_psu) {
    const double t = temperature_k - 273.15;
    if (!std::isfinite(t) || !std::isfinite(salinity_psu) || t < -3.0 || t > 40.0 || salinity_psu < 0.0 || salinity_psu > 42.0) {
        throw std::invalid_argument("oxygen solubility needs -3..40 degC and 0..42 psu");
    }
    const double ts = std::log((298.15 - t) / (273.15 + t));
    const double a0 = 5.80871, a1 = 3.20291, a2 = 4.17887, a3 = 5.10006, a4 = -9.86643e-2, a5 = 3.80369;
    const double b0 = -7.01577e-3, b1 = -7.70028e-3, b2 = -1.13864e-2, b3 = -9.51519e-3, c0 = -2.75915e-7;
    const double log_c = a0 + ts * (a1 + ts * (a2 + ts * (a3 + ts * (a4 + ts * a5)))) + salinity_psu * (b0 + ts * (b1 + ts * (b2 + ts * b3))) + c0 * salinity_psu * salinity_psu;
    return std::exp(log_c);
}

}  // namespace terraflux::weather
