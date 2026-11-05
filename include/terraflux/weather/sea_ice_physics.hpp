#pragma once

namespace terraflux::weather {

// Parameters of thermodynamic sea ice (see sea_ice.hpp), in their own header so the ocean configuration can
// carry them.
struct SeaIcePhysics {
    double ice_density_kg_m3{917.0};
    double latent_heat_fusion_j_kg{3.34e5};
    double conductivity_w_m_k{2.03};
    double ice_salinity_psu{5.0};
    double albedo_cold{0.65};
    double albedo_melting{0.50};
    double minimum_thickness_m{0.05};            // thinner ice melts away at once
    double new_ice_thickness_m{0.5};             // h_0 of new ice in open water
    double heat_transfer_coefficient{0.006};     // c_h of the ocean-ice heat flux
    double friction_velocity_m_s{0.01};          // u* under the ice
    double maximum_concentration{0.99};          // leads never close completely
};

}  // namespace terraflux::weather
