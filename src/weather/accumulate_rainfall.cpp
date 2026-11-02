#include "terraflux/weather/precipitation_accumulator.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <rate-series.json> <accumulation.json>\n"
              << "Integrates precipitation_rate (mm/h) from earliest to latest valid time.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 2;
    }

    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            throw std::runtime_error(std::string("cannot open input: ") + argv[1]);
        }
        const auto rates =
            terraflux::weather::read_weather_observations_json(input);
        const auto accumulated =
            terraflux::weather::accumulate_rainfall(rates);

        std::ofstream output(argv[2], std::ios::binary);
        if (!output) {
            throw std::runtime_error(std::string("cannot open output: ") + argv[2]);
        }
        terraflux::weather::write_weather_observations_json(output, accumulated);
        output.close();
        if (!output) {
            throw std::runtime_error(std::string("failed to finish output: ") + argv[2]);
        }

        std::cerr << "Wrote " << accumulated.size()
                  << " precipitation-accumulation observations to " << argv[2] << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
