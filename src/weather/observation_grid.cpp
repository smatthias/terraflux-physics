#include "terraflux/weather/observation_grid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraflux::weather {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthMeanRadiusM = 6371008.8;
constexpr double kDegreesToRadians = kPi / 180.0;

struct ProjectedPoint {
    double east_m;
    double north_m;
};

ProjectedPoint project_epsg4326(
    const GeoLocation& location,
    const TransportGrid2D& grid) {
    if (location.crs != "EPSG:4326") {
        throw std::invalid_argument(
            "observation CRS must be EPSG:4326; other projections are not supported yet");
    }
    if (!std::isfinite(location.x) || !std::isfinite(location.y) ||
        location.x < -180.0 || location.x > 180.0 ||
        location.y < -90.0 || location.y > 90.0) {
        throw std::invalid_argument("EPSG:4326 observation coordinates are invalid");
    }

    const double longitude = location.x * kDegreesToRadians;
    const double latitude = location.y * kDegreesToRadians;
    const double anchor_longitude =
        grid.geographic_anchor_longitude_deg * kDegreesToRadians;
    const double anchor_latitude =
        grid.geographic_anchor_latitude_deg * kDegreesToRadians;
    const double delta_longitude = std::remainder(
        longitude - anchor_longitude, 2.0 * kPi);

    const double sin_latitude = std::sin(latitude);
    const double cos_latitude = std::cos(latitude);
    const double sin_anchor = std::sin(anchor_latitude);
    const double cos_anchor = std::cos(anchor_latitude);
    const double cosine_central_angle =
        std::clamp(
            sin_anchor * sin_latitude +
                cos_anchor * cos_latitude * std::cos(delta_longitude),
            -1.0,
            1.0);
    const double central_angle = std::acos(cosine_central_angle);

    if (central_angle < 1.0e-12) {
        return {0.0, 0.0};
    }
    if (kPi - central_angle < 1.0e-10) {
        throw std::invalid_argument(
            "cannot project a coordinate at or near the anchor antipode");
    }

    const double scale = central_angle / std::sin(central_angle);
    const ProjectedPoint point{
        kEarthMeanRadiusM * scale * cos_latitude * std::sin(delta_longitude),
        kEarthMeanRadiusM * scale *
            (cos_anchor * sin_latitude -
             sin_anchor * cos_latitude * std::cos(delta_longitude)),
    };
    if (!std::isfinite(point.east_m) || !std::isfinite(point.north_m)) {
        throw std::invalid_argument("geographic projection produced non-finite coordinates");
    }
    return point;
}

void validate_observation_value(const WeatherObservation& observation) {
    switch (observation.status) {
        case ObservationStatus::decoded:
            if (!observation.value || !std::isfinite(*observation.value)) {
                throw std::invalid_argument(
                    "decoded observation requires a finite numeric value");
            }
            break;
        case ObservationStatus::no_data:
            if (observation.value) {
                throw std::invalid_argument(
                    "no_data observation must not contain a numeric value");
            }
            break;
        case ObservationStatus::ambiguous:
            if (observation.value || observation.reason.empty()) {
                throw std::invalid_argument(
                    "ambiguous observation requires a reason and no numeric value");
            }
            break;
        default:
            throw std::invalid_argument("observation has an unsupported status");
    }
}

}  // namespace

GriddedObservationField map_weather_observations_to_grid(
    const std::vector<WeatherObservation>& observations,
    const DryTransportConfig& config,
    const std::string& variable,
    const std::string& units) {
    validate_dry_transport_config(config);
    if (variable.empty() || units.empty()) {
        throw std::invalid_argument("mapped variable and units are required");
    }

    const auto& grid = config.grid;
    const std::size_t cell_count = grid.nx * grid.ny;
    const double east_max =
        grid.origin_east_m + static_cast<double>(grid.nx) * grid.spacing_east_m;
    const double north_max =
        grid.origin_north_m + static_cast<double>(grid.ny) * grid.spacing_north_m;

    GriddedObservationField field;
    field.variable = variable;
    field.units = units;
    field.grid = grid;
    field.cells.resize(cell_count);
    field.source_offsets.resize(cell_count + 1, 0);

    std::vector<std::pair<std::size_t, std::size_t>> assignments;
    assignments.reserve(observations.size());

    for (std::size_t observation_index = 0;
         observation_index < observations.size();
         ++observation_index) {
        const WeatherObservation& observation = observations[observation_index];
        if (observation.variable != variable || observation.units != units) {
            throw std::invalid_argument(
                "all observations must match the requested variable and units");
        }
        validate_observation_value(observation);

        if (observation.valid_time_utc.empty() ||
            observation.valid_time_utc.back() != 'Z') {
            throw std::invalid_argument(
                "observation valid_time_utc must end in Z before gridding");
        }
        if (field.valid_time_utc.empty()) {
            field.valid_time_utc = observation.valid_time_utc;
        } else if (field.valid_time_utc != observation.valid_time_utc) {
            throw std::invalid_argument(
                "observations from different valid times cannot share one grid field");
        }

        const ProjectedPoint point = project_epsg4326(observation.location, grid);
        if (point.east_m < grid.origin_east_m || point.east_m >= east_max ||
            point.north_m < grid.origin_north_m || point.north_m >= north_max) {
            ++field.outside_domain_count;
            continue;
        }

        const double column_position =
            (point.east_m - grid.origin_east_m) / grid.spacing_east_m;
        const double row_position =
            (point.north_m - grid.origin_north_m) / grid.spacing_north_m;
        if (!std::isfinite(column_position) || !std::isfinite(row_position) ||
            column_position < 0.0 || row_position < 0.0 ||
            column_position >= static_cast<double>(grid.nx) ||
            row_position >= static_cast<double>(grid.ny)) {
            ++field.outside_domain_count;
            continue;
        }

        const std::size_t column = static_cast<std::size_t>(std::floor(column_position));
        const std::size_t row = static_cast<std::size_t>(std::floor(row_position));
        assignments.emplace_back(row * grid.nx + column, observation_index);
    }

    std::sort(assignments.begin(), assignments.end());
    std::size_t assignment = 0;
    for (std::size_t cell_index = 0; cell_index < cell_count; ++cell_index) {
        field.source_offsets[cell_index] = field.source_observation_indices.size();
        const std::size_t first = assignment;
        while (assignment < assignments.size() &&
               assignments[assignment].first == cell_index) {
            field.source_observation_indices.push_back(assignments[assignment].second);
            ++assignment;
        }

        const std::size_t count = assignment - first;
        MappedObservationCell& cell = field.cells[cell_index];
        cell.observation_count = count;
        if (count == 1) {
            const WeatherObservation& observation =
                observations[assignments[first].second];
            cell.status = observation.status;
            cell.value = observation.value;
            cell.reason = observation.reason;
        } else if (count > 1) {
            cell.status = ObservationStatus::ambiguous;
            cell.value.reset();
            cell.reason =
                "multiple observations map to this cell; values were not combined";
        }
    }
    field.source_offsets[cell_count] = field.source_observation_indices.size();
    return field;
}

}  // namespace terraflux::weather
