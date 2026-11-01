#include "terraflux/weather/radar_reflectivity_decoder.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <sidecar.json> <raster.png|ppm> <observations.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        print_usage(argv[0]);
        return 2;
    }

    try {
        const std::string sidecar_path = argv[1];
        const std::string raster_path = argv[2];
        const std::string output_path = argv[3];

        std::ifstream sidecar(sidecar_path);
        if (!sidecar) {
            throw std::runtime_error("cannot open sidecar: " + sidecar_path);
        }
        const auto metadata =
            terraflux::weather::read_radar_map_sidecar_json(sidecar, sidecar_path);

        std::ifstream raster(raster_path, std::ios::binary);
        if (!raster) {
            throw std::runtime_error("cannot open raster: " + raster_path);
        }
        const auto observations =
            terraflux::weather::decode_reflectivity_map(raster, metadata);

        std::ofstream output(output_path, std::ios::binary);
        if (!output) {
            throw std::runtime_error("cannot open output: " + output_path);
        }
        terraflux::weather::write_weather_observations_json(output, observations);
        output.close();
        if (!output) {
            throw std::runtime_error("failed to finish output: " + output_path);
        }

        std::cerr << "Wrote " << observations.size()
                  << " observations to " << output_path << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
