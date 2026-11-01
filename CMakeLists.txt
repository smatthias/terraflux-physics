cmake_minimum_required(VERSION 3.16)
project(terraflux_physics LANGUAGES CXX)

include(CTest)

find_package(PNG 1.6.59 REQUIRED)
find_package(nlohmann_json 3.12.0 REQUIRED)

add_library(terraflux_weather_map
    src/weather/map_sidecar.cpp
    src/weather/temperature_map_decoder.cpp
    src/weather/png_reflectivity_decoder.cpp
    src/weather/radar_reflectivity_decoder.cpp
    src/weather/weather_observation_json.cpp
    src/weather/transport_model_contract.cpp
    src/weather/dry_scalar_transport.cpp
    src/weather/observation_grid.cpp
    src/weather/temperature_observation_overlay.cpp
    src/weather/time_indexed_temperature_observations.cpp
    src/weather/time_indexed_observation_mapper.cpp
    src/weather/temperature_observation_pipeline.cpp
    src/weather/precipitation_estimator.cpp
    src/weather/precipitation_accumulator.cpp
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

add_executable(terraflux-decode-radar-map src/weather/decode_radar_map.cpp)
target_link_libraries(terraflux-decode-radar-map
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-decode-temperature-map src/weather/decode_temperature_map.cpp)
target_link_libraries(terraflux-decode-temperature-map
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-temperature-map src/weather/run_temperature_map.cpp)
target_link_libraries(terraflux-run-temperature-map
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-estimate-rain-rate src/weather/estimate_rain_rate.cpp)
target_link_libraries(terraflux-estimate-rain-rate
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-accumulate-rainfall src/weather/accumulate_rainfall.cpp)
target_link_libraries(terraflux-accumulate-rainfall
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

if(BUILD_TESTING)
    add_executable(terraflux-weather-transport-test
        tests/weather/dry_scalar_transport_test.cpp
    )
    target_link_libraries(terraflux-weather-transport-test PRIVATE terraflux_weather_map)
    add_test(
        NAME weather.dry_scalar_transport
        COMMAND terraflux-weather-transport-test
    )

    add_executable(terraflux-temperature-map-cli-test
        tests/weather/temperature_map_cli_test.cpp
    )
    target_compile_definitions(terraflux-temperature-map-cli-test
        PRIVATE TERRAFLUX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(terraflux-temperature-map-cli-test
        PRIVATE
            terraflux_weather_map
            nlohmann_json::nlohmann_json
    )
    add_test(
        NAME weather.temperature_map_cli
        COMMAND terraflux-temperature-map-cli-test
    )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(terraflux_weather_map PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-decode-radar-map PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-decode-temperature-map PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-temperature-map PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-estimate-rain-rate PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-accumulate-rainfall PRIVATE -Wall -Wextra -Wpedantic)
    if(BUILD_TESTING)
        target_compile_options(terraflux-weather-transport-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-temperature-map-cli-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
