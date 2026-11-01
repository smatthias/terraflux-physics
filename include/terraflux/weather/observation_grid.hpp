#pragma once

#include "terraflux/weather/radar_reflectivity_decoder.hpp"
#include "terraflux/weather/transport_model.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace terraflux::weather {

inline constexpr const char* kTransportGridCoordinateSystem =
    "local_azimuthal_equidistant_m";

struct MappedObservationCell {
    std::optional<double> value;
    ObservationStatus status{ObservationStatus::no_data};
    std::string reason{"no in-domain observation mapped to cell"};
    std::size_t observation_count{0};
};

struct GriddedObservationField {
    std::string variable;
    std::string units;
    std::string valid_time_utc;
    std::string coordinate_system{kTransportGridCoordinateSystem};
    TransportGrid2D grid;
    // Row-major cells, indexed by y * nx + x.
    std::vector<MappedObservationCell> cells;
    // Input observation indices for each cell occupy
    // [source_offsets[cell], source_offsets[cell + 1]).
    std::vector<std::size_t> source_offsets;
    std::vector<std::size_t> source_observation_indices;
    std::size_t outside_domain_count{0};
};

// Projects EPSG:4326 observations into the grid's local azimuthal-equidistant
// frame, then assigns each to its containing cell. All inputs must describe
// the requested variable, units, and one valid time. Unsupported CRSs are
// rejected; cells without data remain no_data and collisions become ambiguous.
GriddedObservationField map_weather_observations_to_grid(
    const std::vector<WeatherObservation>& observations,
    const DryTransportConfig& config,
    const std::string& variable,
    const std::string& units);

struct TemperatureInitializationResult {
    DryTransportState state;
    std::size_t observation_cells_applied{0};
    std::size_t background_cells_retained{0};
    std::size_t no_data_cells{0};
    std::size_t ambiguous_cells{0};
};

// Start from a complete background state at elapsed_seconds == 0, then
// replace only cells containing one decoded air-temperature value in kelvin.
// The gridded field must match the config grid and start time. No-data and
// ambiguous cells retain their background temperatures and remain represented
// in the original field, including its source-index provenance.
TemperatureInitializationResult overlay_air_temperature_observations(
    const DryTransportState& background,
    const GriddedObservationField& field,
    const DryTransportConfig& config);

}  // namespace terraflux::weather
