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

// Apply a time-matched temperature field to an existing forecast state.
// The field valid time must correspond to config.start_time_utc plus the
// state's elapsed_seconds.
TemperatureInitializationResult apply_air_temperature_observation_update(
    const DryTransportState& state,
    const GriddedObservationField& field,
    const DryTransportConfig& config);

// Advance one timestep at a time and apply each chronological observation
// field at its valid time. Observation times must fall on timestep boundaries
// within the configured run, including its start or end time.
struct TimeIndexedTemperatureField {
    double elapsed_seconds{0.0};
    GriddedObservationField field;
};

// Group raw air-temperature observations by UTC valid time, map each group to
// the configured grid, and return updates in chronological order. Source
// indices in each field refer to positions in the original input vector.
std::vector<TimeIndexedTemperatureField>
map_air_temperature_observations_to_updates(
    const std::vector<WeatherObservation>& observations,
    const DryTransportConfig& config);

DryTransportState advance_dry_transport_with_temperature_observations(
    const DryTransportState& initial,
    const DryTransportConfig& config,
    const std::vector<TimeIndexedTemperatureField>& observation_updates);

}  // namespace terraflux::weather
