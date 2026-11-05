# Coastal flooding v1 — a rising sea: land lost, storm floods, rivers pushed back, islands drowning

This model answers what follows when polar ice melts and the oceans warm: a higher sea. It maps, for every year from today to 2300, six things:

- which land goes permanently under water;
- how far the 100-year storm reaches on top of the higher sea;
- where flood defences are overtopped;
- how far the sea pushes up rivers;
- how far salt water intrudes;
- how sandy shores retreat and low islands drown.

It also ranks where it hits hardest.

It is the direct follow-up of [polar-melt-model.md](polar-melt-model.md): it takes that run's warming scenarios and Greenland's contribution to the sea level.

**Code and files**
- Kernels: `include/terraflux/weather/sea_level.hpp` (the sea-level budget and the coastal helpers) and `coastal_flooding.hpp` (the impacts).
- CLI: `terraflux-run-coastal-flooding`.
- Request: `terraflux.coastal.flooding-run/v1` ([schema](coastal-flooding-run-v1.schema.json)).
- Output: a timeline with a `coast` block, `terraflux.coastal.flooding/v1` ([schema](coastal-flooding-v1.schema.json)).
- Example: [examples/coastal-flooding-request.json](../../examples/coastal-flooding-request.json) → `viewer/public/coast/engine-timeline.json`. In the viewer, use **🏝 Load sea-level rise & coasts example** and the view **Sea level & coasts**.

## 1. From warming to the sea level at this coast

The sea-level budget (`sea_level_budget`) steps year by year through the global warming ΔT of a scenario, in K above pre-industrial:

| Component | Law | Default | Why |
|---|---|---|---|
| Thermal expansion | dE/dt = β (ΔT − T_d), dT_d/dt = (ΔT − T_d)/τ | β = 1.1 mm yr⁻¹ K⁻¹, τ = 400 yr | The deep ocean takes up heat over centuries, so expansion slows as it warms. At equilibrium it reaches βτ ≈ 0.44 m/K, within the 0.2–0.6 m/K range. |
| Mountain glaciers | dG/dt = γ max(0, ΔT) (1 − G/G_max) | γ = 1.0 mm yr⁻¹ K⁻¹, G_max = 0.40 m at the first year | A finite store: about 0.32 m remains today. |
| Antarctica | dA/dt = a₀ + a₁ max(0, ΔT − 1.2 K) | a₀ = 0.5 mm/yr, a₁ = 0.5 mm yr⁻¹ K⁻¹ | Follows the AR6 medians. The deep-uncertainty, high-end storylines (ice-cliff instability) are not included. |
| Land water | dW/dt = w | 0.3 mm/yr | Groundwater depletion minus reservoir storage. |
| Greenland | From the polar-melt run | `sea_level_rise_vs_control_m` | The run's ice-sheet drift is removed. |

- **Reference year.** Every component is counted from the reference year (today), so the numbers are the rise still to come.
- **Regional rise.** Each ice source has a fingerprint (`fingerprints`). A shrinking ice sheet pulls less water towards itself, so near it the sea falls and far away it rises by up to about 1.2× the global mean (Mitrovica et al. 2001).
  - The example sits at 53° N, like north-west Europe, and uses Greenland ×0.6, Antarctica ×1.1 and glaciers ×1.0.
  - A tropical delta would use about 1.1–1.2 for Greenland.
- **Land motion.** Subsidence comes on top, per cell. Deltas, drained peat and groundwater pumping can add millimetres to centimetres per year.

**Calibration.** The defaults were tuned so that the global rise by 2100 from 2020, with the polar-melt example's three warming paths, falls within the IPCC AR6 ranges.

| Scenario | This model | AR6 median, 1995–2014 baseline | AR6 likely range |
|---|---|---|---|
| Low | 0.37 m | 0.38 m | 0.28–0.55 m |
| Intermediate | 0.48 m | 0.56 m | 0.44–0.76 m |
| High | 0.67 m | 0.77 m | 0.63–1.01 m |

Subtracting the roughly 5 cm that came between 2005 and 2020 puts the low scenario slightly high and the others in the middle of their ranges. By 2300 the model reaches 1.0 / 1.8 / 4.0 m. AR6 gives 0.3–1.0 m (SSP1-2.6) and 2.3–5.4 m (SSP5-8.5), with low confidence.

## 2. From the sea level to the coast

`assess_coastal_impact(config, regional_rise, years_since_reference)` sinks the ground and the defences by subsidence × years. It then compares every result with today's state (rise 0), so intertidal land flooded today does not count as lost.

- **Permanent inundation (land lost).** Land below the daily high water (mean higher high water + rise) that the sea can reach.
  - Water flows from the sea cells through 4-connected land cells whose ground is lower (a bathtub with hydraulic connectivity, Kulp & Strauss 2019).
  - A depression behind a ridge stays dry until the ridge is drowned.
  - A defence (dike, wall) blocks the water until its crest is overtopped. The land it protects below high water is reported separately: it depends on the defences.
- **The design storm.** The T-year surge above high tide comes from Gumbel-distributed annual maxima: level = μ − σ ln(−ln(1 − 1/T)). The steady onshore wind of the storm adds a wind setup over the sea.
  - Over land the storm's water level loses 0.2 m per km (Vafeidis et al. 2019).
  - Along river channels it loses much less (0.01 m/km). Channels carry the water along their bed and through the defence line, so a storm pushes up rivers and floods their banks far inland.
- **Wind setup.** dη/ds = τ / (ρ g (h + η)), integrated along the wind over the sea cells (τ = ρ_air C_d U²). It is exact over each step at the cell's depth; depths below 2 m count as 2 m (breaking waves).
  - Shallow shelves and funnel-shaped estuaries pile up the most. A higher sea is deeper, so it sets up slightly less.
  - There is no bottom friction, so this is an upper estimate.
- **Frequency.** After a relative rise S, today's T-year flood comes every T′ = 1 / (1 − exp(−exp(−(z_T − μ − S)/σ))) years. For large T this is about T e^(−S/σ), so every σ of rise (here 0.22 m) makes it about 2.7 times more frequent (Hunter 2012).
- **Rivers.** The river's own level is its bed (banks − channel depth) plus the Manning normal depth on the local bed slope. Three reaches are reported:
  - The **tidal reach**: how far up the high water stands above that level.
  - The **storm push-back**: how far up the storm's water level stands above it.
  - The **salt intrusion**: L = D A ln(S₀/S_limit)/Q from a steady advection–dispersion balance. The cross-section A = width × (high water − bed) at the mouth deepens with the rise, and salt goes only where the bed lies below the high water.
- **Erosion.** Sandy shores retreat by the Bruun rule, R = S L/(B + h*), about 80 m per metre with L = 1000 m, B = 2 m and h* = 10 m. On each new shoreline cell the exposed edge length × R is lost.
- **Islands and regions.** Land masses up to `island_max_area_km2` are islands. The model reports each one's land lost, and per named region:
  - the land lost, inundated and eroded;
  - the land flooded by the storm (all of it, and beyond today's extent);
  - the land below high water behind defences;
  - the people on lost land, under the storm and behind defences;
  - the mean relative rise at the coast and the frequency of today's design flood.

## 3. The CLI

`terraflux-run-coastal-flooding` reads the request and builds the coast. There are two ways to give it:

- **`cells`:** per-cell arrays, for example a digital elevation model resampled to the grid:
  - `elevation_m`;
  - optional `sea` (otherwise the sea is everything below 0 m connected to the grid edge, so enclosed depressions stay land);
  - `coast_type` (0 rock, 1 sandy, 2 muddy);
  - `defence_crest_m`, `subsidence_mm_yr`, `population`, `region` with `region_names`.
- **`synthetic_coast`:** a straight shoreline with a shelf and rising land, reshaped by named features (each a region) and cities (population peaks):
  - `delta`: a protruding plain with a shallow front, a sea dike, polders below sea level with ring dikes, and subsidence;
  - `estuary`: a funnel of width W₀e^(−y/L) with low banks and a flood wall;
  - `barrier`: barrier islands with inlets, a lagoon and a low sandy mainland;
  - `cliffs`;
  - `islands`: low mounds rising from a shore elevation.

Rivers are polylines from the sea upstream, rasterised into 4-connected channels that start at the last land cell next to the sea. Optional extras:

- `bank_crest_m`: river dikes.
- `valley_width_m` / `valley_floor_m` / `valley_gradient_m_per_km`: carve a valley floor (a terrain edit for synthetic coasts).

The sea level comes from `polar_timeline_path`, a polar-melt timeline. The CLI takes its non-control scenarios, their `global_warming_k` and Greenland's `sea_level_rise_vs_control_m`, linear between the output years. Alternatively, give `scenarios` with warming (and Greenland) curves. The `budget` and the `fingerprints` can be tuned.

The CLI assesses every year from the reference year to the last output year, for every scenario (about 4 s for the example). It writes:

- **Frames:** the `map_years` of the `map_scenario`. The fields are the design-storm wind and a placeholder temperature.
- **Static data:** `sea_cells`, `coast_type`, `defences` ([cell, crest]), `subsidence_mm_yr`, `population`, `region`/`region_names`, `rivers`. `today` holds today's intertidal cells, storm depth and wind setup; `design_storm` and the provenance of the coast and the sea level are also included.
- **Per scenario:**
  - `sea_level`: the components, global and regional, and the warming per output year;
  - `totals`, `regions` (series and `events`: `defences_overtopped`, `island_half_lost`, `lost_10pct`, `design_flood_every_year`), `rivers` and `islands`;
  - `year_lost` and `year_storm_flooded`: sparse [cell, year] lists giving the first year each cell is at least half lost, or flooded by the design storm.
- **`map_frames`:** per map year, `lost_fraction`, `inundation_depth_m`, `storm_depth_m`, `erosion_retreat_m`, `protected_low_cells` and `river_salt_cells`.

## 4. The example and what it shows

The example is a 420 × 252 km coast on 140 × 84 cells of 3 km, about 10 million people. From west to east:

| Region | Coast | People | What it stands for |
|---|---|---|---|
| Delta Lowlands | A delta 30 km into the sea, 0.9 m at the shore rising 4.5 cm/km. Polders at −2.0 and −1.2 m with ring dikes. A 5.5 m sea dike. Subsiding 5 mm/yr. The Great River (6000 m³/s) with 5.5 m river dikes. | 450 /km², a capital of 6000 /km² inside a polder | A Rhine–Meuse or Mekong-like delta with reclaimed land |
| Estuary City | A funnel estuary (16 km mouth, 60 km long). Banks at 2.3 m, a 4.2 m flood wall upstream. The Estuary River. | A city of 8000 /km² | A Thames- or Elbe-like estuary city |
| Barrier Coast | Barrier islands at 2.4 m, 9 km offshore, with inlets. A 2 m lagoon. A sandy mainland at 1.6 m. The Lagoon River. | 150 /km², a town | A Wadden-Sea or Gulf-coast barrier system |
| Cliff Coast | Rocky cliffs of 22 m. | 60 /km² | The contrast |
| Low Islands | Five islands 2.0–3.2 m high with shores at 1.5 m. | 400 /km², a capital | Halligen- or atoll-like islands |

Today's water levels: high tide 1.2 m, a 100-year surge of 1.6 m above high tide (Gumbel μ = 0.6 m, σ = 0.22 m), and a design storm of 26 m/s blowing onshore from the south-southwest. The shallow shelf piles up to 2.3 m of setup in front of the delta, 0.9 m on average along the coast. Today's 100-year storm already floods the homes of about 400,000 people: unprotected low land along the barrier coast, around the estuary and on the islands.

What happens:

| | Low (about SSP1-2.6) | Intermediate (about SSP2-4.5) | High (about SSP5-8.5) |
|---|---|---|---|
| Sea level here, 2100 / 2300 | +0.35 / +0.98 m | +0.44 / +1.68 m | +0.61 / +3.44 m |
| Land lost, 2100 / 2300 | 77 / 657 km² | 152 / 1161 km² | 210 / 10,058 km² |
| Half an island lost (Low Islands) | 2102 | 2087 | 2076 |
| Delta defences overtopped by the 100-year storm | 2143 | 2127 | 2111 |
| Estuary City wall overtopped | — | 2227 | 2161 |
| Today's 100-year flood every year: delta / cliffs | 2225 / — | 2184 / — | 2147 / 2193 |
| People under the 100-year storm, 2100 / 2150 / 2300 | 0.45 / 0.77 / 4.3 M | 0.45 / 2.9 / 5.5 M | 0.46 / 4.0 / 6.2 M |

Where it hits hardest:

- **The low islands**, in share of land. Under the high scenario they lose 43 % of their land by 2100 and all of it by 2200. Under the low scenario they lose 30 % by 2150 and 85 % by 2300. Their 100-year flood comes every 7 years by 2100 (high scenario).
- **The subsiding delta**, in people. By 2100 under the high scenario, 4,700 km² with 3.6 million people lie below the high water, kept dry by dikes. Its 100-year flood comes every 1.6 years, since 0.6 m of sea and 0.4 m of subsidence make 1 m of relative rise.
  - The storm overtops the defences in 2111 (2127 under the intermediate scenario, 2143 under the low one).
  - Once the high water itself tops the sunken sea dike (2274 under the high scenario), the delta is lost. By 2300, 85 % of the delta is gone, and 6.8 million people across the coast lived on land that is lost.
- **The barrier coast** loses its islands to the rising sea and erosion: half lost by 2135 under the high scenario, 2267 under the low one.
- **The estuary city** is protected by its flood wall until 2161 (high scenario), then floods: 56 % of its land is lost by 2300.
- **The cliffs** lose nothing, but their 100-year flood still comes every 7 years by 2100 (high scenario).
- **The rivers.** The storm pushes 84 km up the Great River by 2100 (high scenario) and 114 km by 2300, against 75 km today. Salt intrudes 12.9 km instead of 8.0 km up the Estuary River by 2300, and the flooded river banks grow three- to tenfold.

This is a synthetic coast: the numbers illustrate the processes and their relative sizes, not a place.

## 5. Viewer

- **Sea level & coasts view:**
  - the sea coloured by depth;
  - the land by elevation with square-root heights, so metres of lowland stay visible;
  - alternatively the land coloured by the year it is lost, how often today's 100-year flood comes, population, subsidence, or the storm's wind setup over the sea.
- **Map overlays:**
  - land lost (red);
  - land flooded by the 100-year storm (blue by depth);
  - land below high water behind defences (orange);
  - defences (black, raised to their crest);
  - rivers (white where salty);
  - the design-storm wind;
  - region and river labels.
- **Scenario in focus.** This drives the metrics, the tables and the first-year maps. For scenarios other than the map scenario, the overlays come from the first-year maps.
- **Metrics:** sea level here and globally, land lost and its people, people under the 100-year storm (today → now), land behind defences, how often today's 100-year flood comes, and the wind setup.
- **Sections:**
  - **What raises the sea:** a bar of the components.
  - **Where does it hit hardest?:** a ranking by land lost, people displaced, people under the storm, people behind defences or flood frequency. Clicking a row selects the region.
  - **When?:** the events per region and scenario.
  - **Over time:** charts of sea level, land lost, people under the storm, land lost per region, river push-back and salt.
  - **Rivers:** tidal reach, push-back, salt and flooded banks, today → now, plus the islands' losses.
- **Inspector:** for a cell, its region, coast type, elevation, defence, river, people, subsidence and relative rise; how often the 100-year flood comes; when it is lost and first flooded; and its state in the frame's year.
- **Checks on load.** The viewer validates the block on load (`viewer/src/coastal.js`, tests in `coastal.test.js`): maps against totals, components against the global rise, land-only losses, events and first-year maps.

The **field-layer list** of the viewer is now folded into topic groups (Weather & air, Clouds & rain, Storms, Terrain, Water on land, Land hazards, Ocean currents, Ocean heat & rain, Polar ice, Sea level & coasts). Each group shows how many of its layers the loaded data can show. By default only layers with data are listed, and every layer and group has a tooltip (`viewer/src/layers.js`, tests in `layers.test.js`). The example loaders are grouped the same way with tooltips, and the coupled-wind request form folds away.

## 6. Verification

`tests/weather/coastal_flooding_test.cpp` (`weather.coastal_flooding`) covers:

- **The budget:** thermal expansion against the step-warming closed form βτΔT(1 − e^(−t/τ)), glaciers against their store law, Antarctica above its threshold, the sum, the reference year, fingerprints, and rejections.
- **Gumbel:** return level and period, no rise no change, one σ of rise being e times more frequent, a metre making today's 100-year flood roughly annual.
- **Wind setup:** the exact uniform-depth solution cell by cell, a higher sea setting up less, wind from the other side, shallow water piling up more.
- **Connectivity:** intertidal land is not counted as lost; a ridge keeps a depression dry until it drowns; depth = high water − ground.
- **Defences and subsidence:** a dike keeps its polder dry until overtopped; subsidence sinks land and dike.
- **Storm attenuation:** reach inland = (level − ground)/0.2 m/km.
- **Rivers:** the surge pushes far further up a river than over land and floods its banks; a higher sea reaches further; salt intrudes in proportion to the deeper mouth, as the formula gives.
- **Bruun erosion:** the retreat and the exposed-edge loss; rocky coasts hold.
- **Islands:** a low island drowning monotonically; people and regions adding up; the region's flood frequency.
- **The contract.**

The suite was mutation-checked: ignoring defences, channel attenuation, coast types, the baseline, or the exact setup integration makes it fail.

Other checks:

- `tests/tools/coastal_flooding_check.py` (`weather.coastal_flooding_cli`, about 10 s):
  - runs the example, checking the output contract, maps against totals and regions, lost cells carrying their year, components summing to the global rise, the rise within the AR6 ranges, more warming giving more rise and earlier losses, and the expected behaviour (cliffs hold, islands drown first, the delta's dikes are overtopped, more land depends on defences, the sea reaches further up every river, setup over the shelf);
  - runs a per-cell request with explicit scenarios (an enclosed depression below sea level stays land and dry);
  - checks that invalid requests fail with their message and write nothing;
  - validates requests and outputs against the schemas when `jsonschema` is installed.
- `weather.viewer_coastal_flooding` regenerates the fixture and compares it. The fixture depends on `viewer/public/polar/engine-timeline.json`: regenerate it after the polar fixture changes.

## 7. Limitations

- **Flooding.** Bathtub flooding with connectivity and a linear loss of height inland. There are no flow dynamics, no flood duration or volume limit (a short storm cannot fill a large polder in reality), no waves, and no breaching: defences hold until overtopped.
- **Wind setup.** No bottom friction and one straight design wind, so it is an upper estimate over shallow water. Surge statistics are uniform along the coast.
- **Rivers.** Compound flooding (storm surge and river flood at once) is not added. The river's level is its normal flow depth, and salt intrusion is a steady one-dimensional balance.
- **Erosion.** Only sandy shores follow the Bruun rule. There is no sediment supply and no accretion of marshes or deltas, which can keep pace with slow rise, and no nourishment.
- **No adaptation.** No raising of dikes, no retreat and no population change: the numbers show exposure under today's defences and settlement.
- **Sea-level budget.** A semi-empirical budget calibrated to the AR6 medians. The Antarctic high-end storylines, which could add metres by 2300, are not included.
- **The example coast** is synthetic. Per-cell data (`cells`) let the same code run on a real digital elevation model with real defences and population.
