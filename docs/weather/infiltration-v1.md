# Capacity-limited infiltration v1

`infiltrate_surface_water` transfers water from surface storage to a separate soil store per active cell:

`transfer = min(surface_depth, infiltration_rate × timestep, capacity − soil_storage)`

Depths and capacity are equivalent water depths in metres, rate is m/s, and timestep is seconds. Soil storage is not a volumetric moisture fraction. Inputs must be aligned, finite and nonnegative; initial soil storage may not exceed capacity. Masked cells retain zero internal storage and unknown/null values in the serialized example. Zero rate or exhausted capacity leaves surface water unchanged. No water is lost through this transfer.

The example performs rainfall addition and lateral routing, then infiltration, in each 30-second step. It uses a scenario rate of 1e-6 m/s and capacity 0.0015 m. These synthetic settings demonstrate capacity saturation and do not represent a calibrated soil. Soil storage is initially zero. The infiltration kernel has no drainage or evapotranspiration. The example appends a separate [prescribed loss step](water-losses-v1.md). There is no capillary return, lateral soil flow, depth-resolved profile or feedback into temperature.

The existing optional `surface_water` timeline object may additionally include all of:

- `soil_values_by_frame`: aligned metre-depth arrays, with the same null mask as surface water
- `soil_volume_m3`: total soil-water volume at each frame
- `soil_storage_capacity_m`: uniform finite nonnegative capacity
- `infiltration_rate_m_s`: uniform finite nonnegative transfer rate
- `process_order`: `rain_route_infiltrate`

`stored_volume_m3` continues to mean surface storage only. With soil present and no loss extension, `balance_error_m3 = surface_volume + soil_volume − cumulative_rainfall_volume` for the zero-initial-storage example. Without these optional fields, the previous surface-only contract is unchanged. The viewer verifies soil capacity, mask, volume, nondecreasing storage and combined budget, and displays the separate soil layer in millimetres.
