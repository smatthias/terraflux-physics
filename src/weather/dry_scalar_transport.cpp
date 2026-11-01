#include "terraflux/weather/transport_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace terraflux::weather {
namespace {

struct FaceWind {
    double east;
    double west;
    double north;
    double south;
};

std::size_t at(std::size_t x, std::size_t y, std::size_t nx) {
    return y * nx + x;
}

FaceWind faces_at(
    const DryTransportState& state,
    std::size_t x,
    std::size_t y,
    const TransportGrid2D& grid) {
    const std::size_t east_x = (x + 1 == grid.nx) ? 0 : x + 1;
    const std::size_t west_x = (x == 0) ? grid.nx - 1 : x - 1;
    const std::size_t north_y = (y + 1 == grid.ny) ? 0 : y + 1;
    const std::size_t south_y = (y == 0) ? grid.ny - 1 : y - 1;

    const std::size_t center = at(x, y, grid.nx);
    const std::size_t east = at(east_x, y, grid.nx);
    const std::size_t west = at(west_x, y, grid.nx);
    const std::size_t north = at(x, north_y, grid.nx);
    const std::size_t south = at(x, south_y, grid.nx);

    return {
        0.5 * (state.wind_east_m_s[center] + state.wind_east_m_s[east]),
        0.5 * (state.wind_east_m_s[west] + state.wind_east_m_s[center]),
        0.5 * (state.wind_north_m_s[center] + state.wind_north_m_s[north]),
        0.5 * (state.wind_north_m_s[south] + state.wind_north_m_s[center]),
    };
}

void require_finite_rate(double rate, const char* message) {
    if (!std::isfinite(rate)) {
        throw std::invalid_argument(message);
    }
}

void validate_wind_divergence(
    const DryTransportState& state,
    const DryTransportConfig& config,
    double inverse_dx,
    double inverse_dy) {
    const auto& grid = config.grid;
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const FaceWind faces = faces_at(state, x, y, grid);
            const double divergence =
                (faces.east - faces.west) * inverse_dx +
                (faces.north - faces.south) * inverse_dy;
            const double scale =
                (std::max(std::abs(faces.east), std::abs(faces.west)) * inverse_dx) +
                (std::max(std::abs(faces.north), std::abs(faces.south)) * inverse_dy);
            require_finite_rate(divergence, "wind divergence is not finite");
            require_finite_rate(scale, "wind rate scale is not finite");
            if (std::abs(divergence) > std::max(1.0e-12, 1.0e-10 * scale)) {
                throw std::invalid_argument(
                    "dry transport v1 requires a discretely divergence-free periodic wind field");
            }
        }
    }
}

double stability_number(
    const DryTransportState& state,
    const DryTransportConfig& config,
    double inverse_dx,
    double inverse_dy,
    double inverse_dx2,
    double inverse_dy2) {
    const auto& grid = config.grid;
    const double diffusion_rate =
        2.0 * config.horizontal_mixing_diffusivity_m2_s *
        (inverse_dx2 + inverse_dy2);
    require_finite_rate(diffusion_rate, "diffusion rate is not finite");

    double maximum = 0.0;
    for (std::size_t y = 0; y < grid.ny; ++y) {
        for (std::size_t x = 0; x < grid.nx; ++x) {
            const FaceWind faces = faces_at(state, x, y, grid);
            const double outgoing_rate =
                (std::max(faces.east, 0.0) + std::max(-faces.west, 0.0)) * inverse_dx +
                (std::max(faces.north, 0.0) + std::max(-faces.south, 0.0)) * inverse_dy;
            require_finite_rate(outgoing_rate, "advection rate is not finite");
            const double number =
                config.timestep_seconds * (outgoing_rate + diffusion_rate);
            require_finite_rate(number, "combined stability number is not finite");
            maximum = std::max(maximum, number);
        }
    }
    return maximum;
}

}  // namespace

DryTransportState advance_dry_transport(
    const DryTransportState& initial,
    const DryTransportConfig& config) {
    validate_dry_transport_state(initial, config);

    const auto& grid = config.grid;
    const double inverse_dx = 1.0 / grid.spacing_east_m;
    const double inverse_dy = 1.0 / grid.spacing_north_m;
    const double inverse_dx2 = inverse_dx * inverse_dx;
    const double inverse_dy2 = inverse_dy * inverse_dy;
    require_finite_rate(inverse_dx2, "east grid spacing is too small for this solver");
    require_finite_rate(inverse_dy2, "north grid spacing is too small for this solver");

    validate_wind_divergence(initial, config, inverse_dx, inverse_dy);
    if (stability_number(
            initial, config, inverse_dx, inverse_dy, inverse_dx2, inverse_dy2) > 1.0) {
        throw std::invalid_argument(
            "timestep exceeds the combined advection/diffusion stability limit");
    }

    const long double duration =
        static_cast<long double>(config.timestep_seconds) *
        static_cast<long double>(config.steps);
    const long double final_elapsed =
        static_cast<long double>(initial.elapsed_seconds) + duration;
    if (!std::isfinite(final_elapsed) ||
        final_elapsed > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::invalid_argument("advanced simulation time overflows");
    }

    DryTransportState result = initial;
    const std::size_t count = result.air_temperature_k.size();
    std::vector<double> next_temperature(count);
    const double diffusivity = config.horizontal_mixing_diffusivity_m2_s;
    const double timestep = config.timestep_seconds;

    for (std::size_t step = 0; step < config.steps; ++step) {
        for (std::size_t y = 0; y < grid.ny; ++y) {
            const std::size_t north_y = (y + 1 == grid.ny) ? 0 : y + 1;
            const std::size_t south_y = (y == 0) ? grid.ny - 1 : y - 1;
            for (std::size_t x = 0; x < grid.nx; ++x) {
                const std::size_t east_x = (x + 1 == grid.nx) ? 0 : x + 1;
                const std::size_t west_x = (x == 0) ? grid.nx - 1 : x - 1;

                const std::size_t center = at(x, y, grid.nx);
                const std::size_t east = at(east_x, y, grid.nx);
                const std::size_t west = at(west_x, y, grid.nx);
                const std::size_t north = at(x, north_y, grid.nx);
                const std::size_t south = at(x, south_y, grid.nx);

                const FaceWind faces = faces_at(result, x, y, grid);
                const double temperature = result.air_temperature_k[center];
                const double east_upwind =
                    faces.east >= 0.0 ? temperature : result.air_temperature_k[east];
                const double west_upwind =
                    faces.west >= 0.0 ? result.air_temperature_k[west] : temperature;
                const double north_upwind =
                    faces.north >= 0.0 ? temperature : result.air_temperature_k[north];
                const double south_upwind =
                    faces.south >= 0.0 ? result.air_temperature_k[south] : temperature;

                const double divergence =
                    (faces.east * east_upwind - faces.west * west_upwind) * inverse_dx +
                    (faces.north * north_upwind - faces.south * south_upwind) * inverse_dy;
                const double laplacian =
                    (result.air_temperature_k[east] - 2.0 * temperature +
                     result.air_temperature_k[west]) * inverse_dx2 +
                    (result.air_temperature_k[north] - 2.0 * temperature +
                     result.air_temperature_k[south]) * inverse_dy2;
                const double updated =
                    temperature - timestep * divergence +
                    timestep * diffusivity * laplacian;
                if (!std::isfinite(updated) || updated <= 0.0) {
                    throw std::runtime_error(
                        "dry transport update produced a non-finite or non-positive temperature");
                }
                next_temperature[center] = updated;
            }
        }
        result.air_temperature_k.swap(next_temperature);
    }
    result.elapsed_seconds = static_cast<double>(final_elapsed);
    return result;
}

}  // namespace terraflux::weather
