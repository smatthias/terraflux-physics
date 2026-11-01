#include "terraflux/weather/precipitation_estimator.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

double parse_number(const char* text, const char* label) {
    std::size_t parsed = 0;
    const std::string value(text);
    const double number = std::stod(value, &parsed);
    if (parsed != value.size()) {
        throw std::invalid_argument(std::string("invalid ") + label + ": " + value);
    }
    return number;
}

void print_usage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <reflectivity-observations.json> <rain-rate-observations.json>"
                 " [z-r-coefficient z-r-exponent]\n"
              << "Default relationship: Z = 300 R^1.4; units Z=mm^6 m^-3, R=mm/h\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) {
        print_usage(argv[0]);
        return 2;
    }

    try {
        terraflux::weather::ReflectivityRainfallRelation relation;
        if (argc == 5) {
            relation.coefficient = parse_number(argv[3], "Z-R coefficient");
            relation.exponent = parse_number(argv[4], "Z-R exponent");
        }

        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            throw std::runtime_error(std::string("cannot open input: ") + argv[1]);
        }
        const auto reflectivity =
            terraflux::weather::read_weather_observations_json(input);
        const auto rainfall =
            terraflux::weather::estimate_rain_rate(reflectivity, relation);

        std::ofstream output(argv[2], std::ios::binary);
        if (!output) {
            throw std::runtime_error(std::string("cannot open output: ") + argv[2]);
        }
        terraflux::weather::write_weather_observations_json(output, rainfall);
        output.close();
        if (!output) {
            throw std::runtime_error(std::string("failed to finish output: ") + argv[2]);
        }

        std::cerr << "Wrote " << rainfall.size()
                  << " estimated precipitation observations to " << argv[2] << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
