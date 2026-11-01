cmake_minimum_required(VERSION 3.16)
project(terraflux_physics LANGUAGES CXX)

find_package(PNG 1.6.59 REQUIRED)
find_package(nlohmann_json 3.12.0 REQUIRED)

add_library(terraflux_weather_map
    src/weather/map_sidecar.cpp
    src/weather/png_reflectivity_decoder.cpp
    src/weather/radar_reflectivity_decoder.cpp
)
target_include_directories(terraflux_weather_map
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(terraflux_weather_map PUBLIC cxx_std_17)
target_link_libraries(terraflux_weather_map
    PRIVATE
        PNG::PNG
        nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(terraflux_weather_map PRIVATE -Wall -Wextra -Wpedantic)
endif()
