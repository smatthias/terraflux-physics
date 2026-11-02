# Prescribed evaporation and drainage v1

The C++ `apply_water_losses` step takes aligned surface/soil storage arrays (m), a binary active mask, timestep (s), evaporation rate (m/s), and drainage rate (m/s). All storage and rates must be finite and nonnegative. Masked cells must have zero internal storage and remain null in the example output.

One evaporation demand is computed per cell. It removes surface water first, then uses any remaining demand to remove soil water. This avoids charging the full evaporation demand independently to both stores. Drainage then removes soil water up to its separate prescribed rate. Each transfer is bounded by available storage. The function returns updated storage plus separate per-cell evaporated and drained depths, preserving inputs. Zero rates disable losses.

The example uses evaporation 5e-8 m/s (0.18 mm/h) and drainage 1e-7 m/s (0.36 mm/h), both explicitly chosen synthetic scenario parameters. Each 30-second step performs rain → lateral routing → infiltration → evaporation → drainage. Evaporation is not computed from temperature, wind, humidity or radiation, and has no latent-heat feedback. Drainage leaves the modeled soil store and is not tracked in a groundwater compartment.

The optional surface-water timeline extension requires all four fields when enabled:

- `evaporation_rate_m_s`
- `drainage_rate_m_s`
- `cumulative_evaporation_volume_m3`: one nonnegative cumulative value per frame
- `cumulative_drainage_volume_m3`: one nonnegative cumulative value per frame

`process_order` is `rain_route_infiltrate_evaporate_drain`. Existing timelines without these fields retain their previous semantics. Zero-initial-storage mass balance becomes:

`surface volume + soil volume + evaporated volume + drained volume = cumulative rainfall volume + residual`.

The viewer checks budget closure, nondecreasing loss totals, rate bounds over each frame interval, and storage coverage. Soil storage is allowed to decrease when losses are present. Readouts show remaining surface/soil storage and each loss independently. There is no external surface outlet, infiltration feedback into wind, energy balance, vegetation transpiration or groundwater model.
