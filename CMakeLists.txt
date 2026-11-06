cmake_minimum_required(VERSION 3.16)
project(terraflux_physics LANGUAGES CXX)

include(CTest)

find_package(PNG 1.6 REQUIRED)
find_package(nlohmann_json 3.10.05 REQUIRED)

add_library(terraflux_weather_map
    src/weather/map_sidecar.cpp
    src/weather/temperature_map_decoder.cpp
    src/weather/png_reflectivity_decoder.cpp
    src/weather/radar_reflectivity_decoder.cpp
    src/weather/weather_observation_json.cpp
    src/weather/transport_model_contract.cpp
    src/weather/dry_scalar_transport.cpp
    src/weather/pressure_wind_contract.cpp
    src/weather/pressure_wind.cpp
    src/weather/moist_air.cpp
    src/weather/coupled_wind_contract.cpp
    src/weather/coupled_wind.cpp
    src/weather/atmospheric_column.cpp
    src/weather/cloud_formation.cpp
    src/weather/precipitation_outcomes.cpp
    src/weather/storm_systems.cpp
    src/weather/severe_convection.cpp
    src/weather/synthetic_terrain.cpp
    src/weather/catchment_hydrology.cpp
    src/weather/ocean_circulation.cpp
    src/weather/ice_sheet.cpp
    src/weather/sea_ice.cpp
    src/weather/overturning_box.cpp
    src/weather/marine_indicators.cpp
    src/weather/sea_level.cpp
    src/weather/coastal_flooding.cpp
    src/weather/ocean_carbon.cpp
    src/weather/ocean_biogeochemistry.cpp
    src/weather/atmospheric_co2.cpp
    src/weather/solar_cycle.cpp
    src/weather/solar_irradiance.cpp
    src/weather/solar_forcing.cpp
    src/weather/solar_long_term.cpp
    src/weather/cosmic_dust.cpp
    src/weather/magnetosphere.cpp
    src/weather/cosmic_radiation.cpp
    src/weather/atmospheric_escape.cpp
    src/weather/earth_forecast.cpp
    src/weather/earth_climate.cpp
    src/weather/observation_grid.cpp
    src/weather/temperature_observation_overlay.cpp
    src/weather/time_indexed_temperature_observations.cpp
    src/weather/time_indexed_observation_mapper.cpp
    src/weather/temperature_observation_pipeline.cpp
    src/weather/precipitation_estimator.cpp
    src/weather/surface_outlets.cpp
    src/weather/water_losses.cpp
    src/weather/infiltration.cpp
    src/weather/surface_water.cpp
    src/weather/terrain_derivatives.cpp
    src/weather/terrain_temperature.cpp
    src/weather/precipitation_grid.cpp
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

add_executable(terraflux-run-pressure-wind src/weather/run_pressure_wind.cpp)
target_link_libraries(terraflux-run-pressure-wind
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-coupled-wind src/weather/run_coupled_wind.cpp)
target_link_libraries(terraflux-run-coupled-wind
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-atmospheric-column src/weather/run_atmospheric_column.cpp)
target_link_libraries(terraflux-run-atmospheric-column
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-catchment-hydrology src/weather/run_catchment_hydrology.cpp)
target_link_libraries(terraflux-run-catchment-hydrology
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-polar-melt src/weather/run_polar_melt.cpp)
target_link_libraries(terraflux-run-polar-melt
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-coastal-flooding src/weather/run_coastal_flooding.cpp)
target_link_libraries(terraflux-run-coastal-flooding
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-cosmic-dust src/weather/run_cosmic_dust.cpp)
target_link_libraries(terraflux-run-cosmic-dust
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

find_package(Threads REQUIRED)
add_executable(terraflux-run-earth-climate src/weather/run_earth_climate.cpp)
target_link_libraries(terraflux-run-earth-climate
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-earth-forecast src/weather/run_earth_forecast.cpp)
target_link_libraries(terraflux-run-earth-forecast
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json Threads::Threads
)

add_executable(terraflux-run-solar-wind src/weather/run_solar_wind.cpp)
target_link_libraries(terraflux-run-solar-wind
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-solar-cycle src/weather/run_solar_cycle.cpp)
target_link_libraries(terraflux-run-solar-cycle
    PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json
)

add_executable(terraflux-run-ocean-circulation src/weather/run_ocean_circulation.cpp)
target_link_libraries(terraflux-run-ocean-circulation
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

add_executable(terraflux-generate-viewer-example tools/generate_viewer_example.cpp)
target_link_libraries(terraflux-generate-viewer-example PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json)

add_executable(terraflux-grid-rainfall src/weather/grid_rainfall.cpp)
target_link_libraries(terraflux-grid-rainfall PRIVATE terraflux_weather_map nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_executable(terraflux-surface-outlets-test tests/weather/surface_outlets_test.cpp)
    target_link_libraries(terraflux-surface-outlets-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.surface_outlets COMMAND terraflux-surface-outlets-test)
    add_executable(terraflux-surface-water-basin-test tests/weather/surface_water_basin_test.cpp)
    target_link_libraries(terraflux-surface-water-basin-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.surface_water_basin COMMAND terraflux-surface-water-basin-test)
    add_executable(terraflux-water-losses-test tests/weather/water_losses_test.cpp)
    target_link_libraries(terraflux-water-losses-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.water_losses COMMAND terraflux-water-losses-test)
    add_executable(terraflux-infiltration-test tests/weather/infiltration_test.cpp)
    target_link_libraries(terraflux-infiltration-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.infiltration COMMAND terraflux-infiltration-test)
    add_executable(terraflux-surface-water-test tests/weather/surface_water_test.cpp)
    target_link_libraries(terraflux-surface-water-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.surface_water COMMAND terraflux-surface-water-test)
    add_executable(terraflux-terrain-derivatives-test tests/weather/terrain_derivatives_test.cpp)
    target_link_libraries(terraflux-terrain-derivatives-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.terrain_derivatives COMMAND terraflux-terrain-derivatives-test)
    add_executable(terraflux-terrain-temperature-test tests/weather/terrain_temperature_test.cpp)
    target_link_libraries(terraflux-terrain-temperature-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.terrain_temperature COMMAND terraflux-terrain-temperature-test)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME weather.viewer_outlet
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_viewer_example.py
        --build-dir ${CMAKE_CURRENT_BINARY_DIR} --scenario ${CMAKE_CURRENT_SOURCE_DIR}/examples/hydrology-outlet-scenario.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/outlet/engine-weather.json --timeline-only --check)
    add_test(NAME weather.water_timestep_sensitivity
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_water_timestep.py
        --build-dir ${CMAKE_CURRENT_BINARY_DIR} --check)
    add_test(NAME weather.viewer_comparison
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_viewer_example.py
        --build-dir ${CMAKE_CURRENT_BINARY_DIR} --scenario ${CMAKE_CURRENT_SOURCE_DIR}/examples/hydrology-low-infiltration.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/comparison/engine-weather.json --timeline-only --check)
    add_test(NAME weather.hydrology_scenario
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/hydrology_scenario_check.py $<TARGET_FILE:terraflux-generate-viewer-example>)
    add_test(NAME weather.viewer_example
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_viewer_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR} --check)
    add_test(NAME weather.rainfall_pipeline
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/rainfall_pipeline_check.py $<TARGET_FILE:terraflux-grid-rainfall>)
    add_executable(terraflux-precipitation-grid-test tests/weather/precipitation_grid_test.cpp)
    target_link_libraries(terraflux-precipitation-grid-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.precipitation_grid COMMAND terraflux-precipitation-grid-test)
    add_executable(terraflux-pressure-wind-test tests/weather/pressure_wind_test.cpp)
    target_link_libraries(terraflux-pressure-wind-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.pressure_wind COMMAND terraflux-pressure-wind-test)
    add_test(NAME weather.pressure_wind_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/pressure_wind_check.py $<TARGET_FILE:terraflux-run-pressure-wind>)
    add_executable(terraflux-moist-air-test tests/weather/moist_air_test.cpp)
    target_link_libraries(terraflux-moist-air-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.moist_air COMMAND terraflux-moist-air-test)
    add_executable(terraflux-coupled-wind-test tests/weather/coupled_wind_test.cpp)
    target_link_libraries(terraflux-coupled-wind-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.coupled_wind COMMAND terraflux-coupled-wind-test)
    add_test(NAME weather.coupled_wind_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/coupled_wind_check.py $<TARGET_FILE:terraflux-run-coupled-wind>)
    add_executable(terraflux-atmospheric-column-test tests/weather/atmospheric_column_test.cpp)
    target_link_libraries(terraflux-atmospheric-column-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.atmospheric_column COMMAND terraflux-atmospheric-column-test)
    add_test(NAME weather.atmospheric_column_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/column_check.py $<TARGET_FILE:terraflux-run-atmospheric-column>)
    add_executable(terraflux-cloud-formation-test tests/weather/cloud_formation_test.cpp)
    target_link_libraries(terraflux-cloud-formation-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.cloud_formation COMMAND terraflux-cloud-formation-test)
    add_test(NAME weather.cloud_formation_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/cloud_formation_check.py $<TARGET_FILE:terraflux-run-coupled-wind>)
    add_executable(terraflux-precipitation-outcomes-test tests/weather/precipitation_outcomes_test.cpp)
    target_link_libraries(terraflux-precipitation-outcomes-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.precipitation_outcomes COMMAND terraflux-precipitation-outcomes-test)
    add_executable(terraflux-storm-systems-test tests/weather/storm_systems_test.cpp)
    target_link_libraries(terraflux-storm-systems-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.storm_systems COMMAND terraflux-storm-systems-test)
    add_executable(terraflux-severe-convection-test tests/weather/severe_convection_test.cpp)
    target_link_libraries(terraflux-severe-convection-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.severe_convection COMMAND terraflux-severe-convection-test)
    add_test(NAME weather.storm_systems_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/storm_systems_check.py $<TARGET_FILE:terraflux-run-coupled-wind>)
    # The supercell & tornado watch view: the tornado-outbreak request with hourly snapshots.
    add_test(NAME weather.viewer_supercell_outbreak
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-coupled-wind --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/supercell-outbreak-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/supercell/engine-timeline.json --check)
    add_executable(terraflux-orography-test tests/weather/orography_test.cpp)
    target_link_libraries(terraflux-orography-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.orography COMMAND terraflux-orography-test)
    add_test(NAME weather.orography_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/orography_check.py $<TARGET_FILE:terraflux-run-coupled-wind>)
    add_executable(terraflux-catchment-hydrology-test tests/weather/catchment_hydrology_test.cpp)
    target_link_libraries(terraflux-catchment-hydrology-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.catchment_hydrology COMMAND terraflux-catchment-hydrology-test)
    add_test(NAME weather.catchment_hydrology_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/catchment_hydrology_check.py $<TARGET_FILE:terraflux-run-catchment-hydrology>)
    add_executable(terraflux-ocean-circulation-test tests/weather/ocean_circulation_test.cpp)
    target_link_libraries(terraflux-ocean-circulation-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.ocean_circulation COMMAND terraflux-ocean-circulation-test)
    add_test(NAME weather.ocean_circulation_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/ocean_circulation_check.py $<TARGET_FILE:terraflux-run-ocean-circulation>)
    add_executable(terraflux-ocean-heat-rain-test tests/weather/ocean_heat_rain_test.cpp)
    target_link_libraries(terraflux-ocean-heat-rain-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.ocean_heat_rain COMMAND terraflux-ocean-heat-rain-test)
    add_executable(terraflux-polar-melt-test tests/weather/polar_melt_test.cpp)
    target_link_libraries(terraflux-polar-melt-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.polar_melt COMMAND terraflux-polar-melt-test)
    add_executable(terraflux-ocean-carbon-test tests/weather/ocean_carbon_test.cpp)
    target_link_libraries(terraflux-ocean-carbon-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.ocean_carbon COMMAND terraflux-ocean-carbon-test)
    add_test(NAME weather.ocean_carbon_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/ocean_carbon_check.py $<TARGET_FILE:terraflux-run-ocean-circulation>)
    add_test(NAME weather.viewer_ocean_carbon
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/ocean-carbon-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/ocean-carbon/engine-timeline.json --check)
    add_test(NAME weather.viewer_ocean_carbon_2100
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/ocean-carbon-2100-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/ocean-carbon-2100/engine-timeline.json --check)
    set_tests_properties(weather.viewer_ocean_carbon weather.viewer_ocean_carbon_2100 PROPERTIES TIMEOUT 600)
    add_executable(terraflux-solar-cycle-test tests/weather/solar_cycle_test.cpp)
    target_link_libraries(terraflux-solar-cycle-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.solar_cycle COMMAND terraflux-solar-cycle-test)
    add_test(NAME weather.solar_cycle_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/solar_cycle_check.py $<TARGET_FILE:terraflux-run-solar-cycle>)
    add_test(NAME weather.solar_coupling
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/solar_coupling_check.py $<TARGET_FILE:terraflux-run-solar-cycle>
        $<TARGET_FILE:terraflux-run-ocean-circulation> $<TARGET_FILE:terraflux-run-polar-melt>)
    add_test(NAME weather.viewer_solar_cycle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-solar-cycle --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/solar-cycle-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/solar/engine-timeline.json --check)
    add_executable(terraflux-solar-wind-test tests/weather/solar_wind_test.cpp)
    target_link_libraries(terraflux-solar-wind-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.solar_wind COMMAND terraflux-solar-wind-test)
    add_test(NAME weather.solar_wind_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/solar_wind_check.py $<TARGET_FILE:terraflux-run-solar-wind>)
    set_tests_properties(weather.solar_wind_cli PROPERTIES TIMEOUT 300)
    add_test(NAME weather.viewer_solar_wind
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-solar-wind --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/solar-wind-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/solar-wind/engine-timeline.json --check)
    add_executable(terraflux-earth-system-test tests/weather/earth_system_test.cpp)
    target_link_libraries(terraflux-earth-system-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.earth_system COMMAND terraflux-earth-system-test)
    add_test(NAME weather.earth_forecast_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/earth_forecast_check.py $<TARGET_FILE:terraflux-run-earth-forecast>)
    set_tests_properties(weather.earth_forecast_cli PROPERTIES TIMEOUT 600)
    add_test(NAME weather.earth_climate_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/earth_climate_check.py $<TARGET_FILE:terraflux-run-earth-climate>)
    set_tests_properties(weather.earth_climate_cli PROPERTIES TIMEOUT 300)
    # The climate fixture links to the forecast fixture: regenerate the forecast first.
    add_test(NAME weather.viewer_earth_forecast
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-earth-forecast --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/earth-forecast-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/coupled-forecast/engine-timeline.json --check)
    set_tests_properties(weather.viewer_earth_forecast PROPERTIES TIMEOUT 900 LABELS slow)
    add_test(NAME weather.viewer_earth_climate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-earth-climate --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/earth-climate-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/coupled-climate/engine-timeline.json --check)
    set_tests_properties(weather.viewer_earth_climate PROPERTIES DEPENDS weather.viewer_earth_forecast)
    add_executable(terraflux-cosmic-dust-test tests/weather/cosmic_dust_test.cpp)
    target_link_libraries(terraflux-cosmic-dust-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.cosmic_dust COMMAND terraflux-cosmic-dust-test)
    add_test(NAME weather.cosmic_dust_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/cosmic_dust_check.py $<TARGET_FILE:terraflux-run-cosmic-dust>)
    add_test(NAME weather.viewer_cosmic_dust
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-cosmic-dust --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/cosmic-dust-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/dust/engine-timeline.json --check)
    add_executable(terraflux-coastal-flooding-test tests/weather/coastal_flooding_test.cpp)
    target_link_libraries(terraflux-coastal-flooding-test PRIVATE terraflux_weather_map)
    add_test(NAME weather.coastal_flooding COMMAND terraflux-coastal-flooding-test)
    add_test(NAME weather.coastal_flooding_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/coastal_flooding_check.py $<TARGET_FILE:terraflux-run-coastal-flooding>)
    add_test(NAME weather.viewer_coastal_flooding
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-coastal-flooding --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/coastal-flooding-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/coast/engine-timeline.json --check)
    add_test(NAME weather.polar_melt_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/polar_melt_check.py $<TARGET_FILE:terraflux-run-polar-melt>)
    add_test(NAME weather.viewer_polar_melt
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cli terraflux-run-polar-melt --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/polar-melt-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/polar/engine-timeline.json --check)
    add_test(NAME weather.polar_ocean_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/polar_ocean_check.py $<TARGET_FILE:terraflux-run-ocean-circulation>)
    # Regenerates the ten-year polar ocean fixture (about 5 minutes): excluded by `ctest -LE slow`. Ten years of
    # sea ice turn last-bit differences between compilers into different weather at the ice edge (same climate):
    # fields are compared by their relative RMS difference (10 %; the ocean fields differ by well under 1 %).
    add_test(NAME weather.viewer_polar_ocean
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/polar-ocean-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/polar-ocean/engine-timeline.json --check --field-tolerance 0.1)
    set_tests_properties(weather.viewer_polar_ocean PROPERTIES TIMEOUT 1200 LABELS slow)
    add_test(NAME weather.ocean_heat_rain_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/ocean_heat_rain_check.py $<TARGET_FILE:terraflux-run-ocean-circulation>)
    add_test(NAME weather.viewer_ocean_heat_rain
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/ocean-heat-rain-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/ocean-heat/engine-timeline.json --check)
    add_test(NAME weather.viewer_ocean_streams
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_ocean_example.py --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --request ${CMAKE_CURRENT_SOURCE_DIR}/examples/atlantic-streams-request.json
        --output ${CMAKE_CURRENT_SOURCE_DIR}/viewer/public/ocean/engine-timeline.json --check)
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
    target_compile_options(terraflux-run-pressure-wind PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-coupled-wind PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-atmospheric-column PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-catchment-hydrology PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-ocean-circulation PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-polar-melt PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-coastal-flooding PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-solar-cycle PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-cosmic-dust PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-solar-wind PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-earth-forecast PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-run-earth-climate PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-estimate-rain-rate PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(terraflux-accumulate-rainfall PRIVATE -Wall -Wextra -Wpedantic)
    if(BUILD_TESTING)
        target_compile_options(terraflux-weather-transport-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-pressure-wind-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-moist-air-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-coupled-wind-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-atmospheric-column-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-cloud-formation-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-precipitation-outcomes-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-storm-systems-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-severe-convection-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-orography-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-catchment-hydrology-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-ocean-circulation-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-ocean-heat-rain-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-polar-melt-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-coastal-flooding-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-solar-cycle-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-cosmic-dust-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-solar-wind-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-earth-system-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-ocean-carbon-test PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(terraflux-temperature-map-cli-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
