#pragma once

namespace terraflux::weather {

// Physical indicators of ocean conditions that matter to marine ecosystems (no biology is modelled).

// Oxygen solubility of sea water in equilibrium with the atmosphere (umol/kg), Garcia & Gordon (1992),
// combined fit to the Benson & Krause data; valid for -2 to 40 degC and 0 to 42 psu. Warmer water holds
// less oxygen.
double oxygen_solubility_umol_kg(double temperature_k, double salinity_psu);

}  // namespace terraflux::weather
