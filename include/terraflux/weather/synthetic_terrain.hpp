#pragma once

#include "terraflux/weather/transport_model.hpp"

#include <cstdint>
#include <vector>

namespace terraflux::weather {

// Synthetic terrain shared by the coupled-wind orography and the catchment
// hydrology request formats. Elevations are built on the cell centres of a
// TransportGrid2D from a base level, an optional tilted plane, Gaussian
// ridges (negative heights carve valleys), Gaussian basins and optional
// deterministic cell-scale roughness. The roughness is an integer hash of the
// cell index and seed, so the same request yields the same terrain on every
// platform; it is what turns smooth flanks into dendritic drainage.
struct SyntheticRidge {
    double orientation_deg{0.0};   // ridge axis from north: 0 = north-south, 90 = east-west
    double center_east_m{0.0};
    double center_north_m{0.0};
    double height_m{0.0};          // < 0 carves a valley
    double half_width_m{1.0};      // Gaussian e-folding half width across the axis
    double length_m{1.0};          // super-Gaussian length along the axis
};

struct SyntheticBasin {
    double center_east_m{0.0};
    double center_north_m{0.0};
    double depth_m{0.0};
    double radius_m{1.0};
};

struct SyntheticTerrainSpec {
    double base_elevation_m{0.0};
    double slope_east_m_per_m{0.0};    // tilted plane through the grid origin
    double slope_north_m_per_m{0.0};
    std::vector<SyntheticRidge> ridges;
    std::vector<SyntheticBasin> basins;
    double roughness_m{0.0};           // peak amplitude of the deterministic noise
    std::uint32_t roughness_seed{1};
};

// Deterministic noise in [-1, 1] for a cell index and seed.
double terrain_noise(std::size_t index, std::uint32_t seed);

// Throws std::invalid_argument for non-positive widths/lengths/radii,
// negative roughness or non-finite values. The grid must be valid.
std::vector<double> build_synthetic_terrain(const SyntheticTerrainSpec& spec, const TransportGrid2D& grid);

}  // namespace terraflux::weather
