#!/usr/bin/env python3
"""Write the two ocean-carbon example requests (today and a 2100 slice under high emissions).

The basin is a Gulf-like semi-enclosed sea on a 30 x 22 grid of 36 km cells: a northern coast with a river
delta, a western coast with a smaller river, an eastern peninsula and the open ocean to the south (a sponge
that exchanges water with the ocean outside). The seafloor deepens from 6 m at the coast over a 160 km shelf
to 100 m and then down the slope to 3000 m. Four CO2 sources (two cities, a refinery belt and a port) sit on
the coasts.

  python3 tools/make_ocean_carbon_examples.py            # writes examples/ocean-carbon-request.json
                                                         # and examples/ocean-carbon-2100-request.json
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
K = 1000.0
NX, NY, DX = 30, 22, 36000.0


def elevation(x, y):
    """Terrain (m) at a cell centre: land above 0, the seafloor below."""
    coast_north = 690 * K - 90 * K * math.exp(-((x - 400 * K) / (90 * K)) ** 2)   # the delta lobe
    coast_west = 60 * K
    peninsula = 260 * K < y < 740 * K
    peninsula_west = 900 * K
    if y > coast_north or x < coast_west or (peninsula and x > peninsula_west):
        inland = max(y - coast_north, coast_west - x, (x - peninsula_west) if peninsula else -1e9)
        return round(min(300.0, 2.0 + 0.0015 * inland), 1)
    distance = min(coast_north - y, x - coast_west, (peninsula_west - x) if peninsula else 1e9,
                   math.hypot(x - peninsula_west, y - 260 * K) if x > peninsula_west else 1e9)
    shelf = 160 * K
    if distance < shelf:
        depth = 6.0 + distance / shelf * 94.0
    else:
        depth = min(3000.0, 100.0 + (distance - shelf) * 0.02)
    return round(-depth, 1)


def request(scenario):
    warm = scenario == '2100'
    dt = 3.0 if warm else 0.0   # SSP5-8.5-like warming of the Gulf by 2100
    values = [elevation((i + 0.5) * DX, (j + 0.5) * DX) for j in range(NY) for i in range(NX)]
    return {
        'schema_version': 'terraflux.ocean.circulation-run/v1',
        'time': {'start_time_utc': '2099-01-01T00:00:00Z' if warm else '2025-01-01T00:00:00Z',
                 'timestep_seconds': 3600.0, 'steps': 8760, 'spinup_steps': 8760},
        'grid': {'nx': NX, 'ny': NY, 'origin_east_m': 0.0, 'origin_north_m': 0.0, 'spacing_east_m': DX, 'spacing_north_m': DX,
                 'geographic_anchor_longitude_deg': -91.0, 'geographic_anchor_latitude_deg': 22.0},
        'basin': {'sea_level_m': 0.0, 'terrain': {'values': values}, 'sponge': {'south_rows': 2}},
        'ocean': {'initial_thickness_m': 150.0, 'initial_salinity_psu': 36.0, 'initial_temperature_k': 298.15 + dt},
        'physics': {'coriolis_mode': 'beta_plane', 'deep_temperature_k': 283.15 + 0.5 * dt, 'deep_salinity_psu': 35.2,
                    'linear_friction_s': 1e-6, 'lateral_viscosity_m2_s': 1000.0, 'tracer_diffusivity_m2_s': 300.0,
                    'stream_speed_threshold_m_s': 0.1,
                    'thermal_layers': {'count': 8, 'surface_layer_m': 6.0, 'initial_mixed_layer_depth_m': 30.0,
                                       'initial_thermocline_scale_m': 60.0, 'initial_surface_temperature': 'surface_temperature_target'}},
        'forcing': {
            'wind': {'kind': 'gyre', 'stress_amplitude_pa': 0.05, 'gyres': 1},
            'surface_temperature': {'south_k': 301.65 + dt, 'north_k': 297.15 + dt},
            'net_precipitation': {'south_m_s': 0.0, 'north_m_s': 0.0},
            'air_sea': {'air_temperature_offset_k': -0.8, 'relative_humidity_south': 0.78, 'relative_humidity_north': 0.75,
                        'cloud_fraction_south': 0.4, 'cloud_fraction_north': 0.35,
                        'seasonal_amplitude_south_k': 1.5, 'seasonal_amplitude_north_k': 6.0, 'shortwave': {'kind': 'astronomical'}},
            'rivers': [
                {'name': 'Great Delta River', 'east_m': 400 * K, 'north_m': 585 * K, 'discharge_m3_s': 16000.0, 'temperature_k': 293.15 + dt,
                 'chemistry': {'nitrate_umol_kg': 110.0, 'dic_umol_kg': 2300.0, 'alkalinity_umol_kg': 2400.0}},
                {'name': 'Western River', 'east_m': 90 * K, 'north_m': 520 * K, 'discharge_m3_s': 1500.0, 'temperature_k': 293.15 + dt,
                 'chemistry': {'nitrate_umol_kg': 40.0, 'dic_umol_kg': 1800.0, 'alkalinity_umol_kg': 1900.0}}]},
        'biogeochemistry': {
            'levels_m': [5, 10, 20, 30, 40, 60, 80, 100, 150, 250, 400, 600],
            'step_every': 2,
            # By 2100 anthropogenic carbon has reached the thermocline: the deep reference DIC is 40 umol/kg higher.
            'profiles': {'dic': {'surface': 2020.0, 'deep': 2270.0 if warm else 2230.0, 'scale_m': 250.0},
                         'alkalinity': {'surface': 2380.0, 'deep': 2320.0, 'scale_m': 400.0},
                         'oxygen': {'surface': 205.0, 'deep': 130.0, 'scale_m': 250.0},
                         'nitrate': {'surface': 0.3, 'deep': 25.0, 'scale_m': 200.0}},
            'biology': {'max_growth_per_day': 0.6, 'max_production_umol_n_kg_day': 1.2, 'light_attenuation_per_m': 0.1,
                        'martin_exponent': 0.858, 'rain_ratio': 0.07},
            'transport': {'horizontal_diffusivity_m2_s': 300.0, 'vertical_diffusivity_m2_s': 1e-5, 'ventilation_depth_m': 150.0,
                          'ventilation_time_years': 15.0},
            'habitat': {'hypoxia_umol_kg': 60.0, 'anoxia_umol_kg': 5.0, 'corrosive_omega': 1.0}},
        'atmosphere_co2': {
            'background_ppm': [[2095, 980.0], [2105, 1050.0]] if warm else [[2020, 412.0], [2030, 440.0], [2040, 470.0]],
            'boundary_layer_height_m': 800.0, 'ventilation_hours': 48.0, 'horizontal_diffusivity_m2_s': 5000.0,
            'land': {'photosynthesis_umol_per_j': 0.02, 'respiration_umol_m2_s': 1.6},
            'sources': [
                {'name': 'Delta City', 'east_m': 380 * K, 'north_m': 650 * K, 'radius_m': 25 * K, 'mt_co2_per_year': 25.0},
                {'name': 'Bayou Refineries', 'east_m': 560 * K, 'north_m': 720 * K, 'radius_m': 30 * K, 'mt_co2_per_year': 120.0},
                {'name': 'Coastal Port', 'east_m': 30 * K, 'north_m': 380 * K, 'radius_m': 20 * K, 'mt_co2_per_year': 15.0},
                {'name': 'Peninsula City', 'east_m': 960 * K, 'north_m': 450 * K, 'radius_m': 25 * K, 'mt_co2_per_year': 20.0}]},
        'snapshot_every_steps': 730,
    }


def dump(req):
    """JSON with one-space indentation; the terrain stays one line per grid row."""
    values = req['basin']['terrain']['values']
    req = json.loads(json.dumps(req))
    req['basin']['terrain']['values'] = '@VALUES@'
    text = json.dumps(req, indent=1)
    rows = ',\n     '.join(', '.join(json.dumps(v) for v in values[j * NX:(j + 1) * NX]) for j in range(NY))
    return text.replace('"@VALUES@"', '[\n     ' + rows + '\n    ]') + '\n'


def main():
    for scenario, name in [('today', 'ocean-carbon-request.json'), ('2100', 'ocean-carbon-2100-request.json')]:
        path = ROOT / 'examples' / name
        path.write_text(dump(request(scenario)))
        print(f'wrote {path.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
