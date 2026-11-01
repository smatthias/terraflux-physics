#include "terraflux/weather/observation_grid.hpp"
#include "utc_timestamp.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>

namespace terraflux::weather {
namespace {

bool same_grid(const TransportGrid2D& left, const TransportGrid2D& right) {
    return left.nx == right.nx && left.ny == right.ny &&
           left.origin_east_m == right.origin_east_m &&
           left.origin_north_m == right.origin_north_m &&
           left.spacing_east_m == right.spacing_east_m &&
           left.spacing_north_m == right.spacing_north_m &&
           left.geographic_anchor_longitude_deg ==
               right.geographic_anchor_longitude_deg &&
           left.geographic_anchor_latitude_deg ==
               right.geographic_anchor_latitude_deg;
}

void validate_field_structure(const GriddedObservationField& field, std::size_t count) {
    if (field.cells.size() != count || field.source_offsets.size() != count + 1) {
        throw std::invalid_argument(
            "observation field cell and provenance arrays must match the grid");
    }
    if (field.source_offsets.front() != 0 ||
        field.source_offsets.back() != field.source_observation_indices.size()) {
        throw std::invalid_argument("observation field source offsets are inconsistent");
    }

    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t begin = field.source_offsets[index];
        const std::size_t end = field.source_offsets[index + 1];
        if (end < begin || end > field.source_observation_indices.size() ||
            end - begin != field.cells[index].observation_count) {
            throw std::invalid_argument(
                "observation field source offsets do not match cell counts");
        }
    }
}

TemperatureInitializationResult apply_temperature_field(
    const DryTransportState& state,
    const GriddedObservationField& field,
    const DryTransportConfig& config) {
    if (field.variable != "air_temperature" || field.units != "K") {
        throw std::invalid_argument(
            "temperature updates require air_temperature observations in K");
    }
    if (field.coordinate_system != kTransportGridCoordinateSystem ||
        !same_grid(field.grid, config.grid)) {
        throw std::invalid_argument(
            "observation field must use the configured transport grid and coordinate system");
    }

    const std::size_t count = config.grid.nx * config.grid.ny;
    validate_field_structure(field, count);

    TemperatureInitializationResult result;
    result.state = state;
    for (std::size_t index = 0; index < count; ++index) {
        const MappedObservationCell& cell = field.cells[index];
        switch (cell.status) {
            case ObservationStatus::decoded:
                if (cell.observation_count != 1 || !cell.value ||
                    !std::isfinite(*cell.value) || *cell.value <= 0.0) {
                    throw std::invalid_argument(
                        "decoded air temperature cell must contain one finite value above 0 K");
                }
                result.state.air_temperature_k[index] = *cell.value;
                ++result.observation_cells_applied;
                break;
            case ObservationStatus::no_data:
                if (cell.value || cell.observation_count > 1) {
                    throw std::invalid_argument(
                        "no_data temperature cell has inconsistent value or count");
                }
                ++result.no_data_cells;
                ++result.background_cells_retained;
                break;
            case ObservationStatus::ambiguous:
                if (cell.value || cell.reason.empty() ||
                    cell.observation_count == 0) {
                    throw std::invalid_argument(
                        "ambiguous temperature cell must have a reason and no value");
                }
                ++result.ambiguous_cells;
                ++result.background_cells_retained;
                break;
            default:
                throw std::invalid_argument(
                    "temperature field contains an unsupported observation status");
        }
    }
    return result;
}

}  // namespace

TemperatureInitializationResult overlay_air_temperature_observations(
    const DryTransportState& background,
    const GriddedObservationField& field,
    const DryTransportConfig& config) {
    validate_dry_transport_config(config);
    validate_dry_transport_state(background, config);
    if (background.elapsed_seconds != 0.0) {
        throw std::invalid_argument(
            "temperature background must be at the configured start time");
    }
    if (field.valid_time_utc != config.start_time_utc) {
        throw std::invalid_argument(
            "temperature observations must match the configured start time");
    }
    return apply_temperature_field(background, field, config);
}

TemperatureInitializationResult apply_air_temperature_observation_update(
    const DryTransportState& state,
    const GriddedObservationField& field,
    const DryTransportConfig& config) {
    validate_dry_transport_state(state, config);
    const double start_seconds = detail::utc_timestamp_seconds(config.start_time_utc);
    const double valid_seconds = detail::utc_timestamp_seconds(field.valid_time_utc);
    const double observation_elapsed = valid_seconds - start_seconds;
    if (observation_elapsed < -1.0e-6 ||
        std::abs(observation_elapsed - state.elapsed_seconds) > 1.0e-5) {
        throw std::invalid_argument(
            "temperature observation valid time must match the forecast state's elapsed time");
    }
    return apply_temperature_field(state, field, config);
}

}  // namespace terraflux::weather
