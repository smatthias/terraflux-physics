#pragma once
#include "terraflux/weather/transport_model.hpp"
#include <optional>
#include <vector>

namespace terraflux::weather {
struct TerrainDerivatives {
    std::vector<double> gradient_east;
    std::vector<double> gradient_north;
    std::vector<double> slope_deg;
    // Downhill bearing: clockwise from north in [0,360). Flat cells have no aspect.
    std::vector<std::optional<double>> aspect_deg;
};
// Cell-centered finite differences in local metric coordinates. Central inside,
// one-sided at domain edges; no periodic wrap. Elevation and spacing in metres.
TerrainDerivatives calculate_terrain_derivatives(
    const std::vector<double>& elevation_m, const TransportGrid2D& grid);
}
