# Coupled Earth v1: climate drivers, CO₂ impact, the 3–10 day forecast, wind and severe weather

The other TerraFlux models each look at one part of the Earth. The **Coupled Earth** puts them together and answers five questions:

1. **Climate change:** which drivers have the largest effect, today, in 2050 and in 2100, and what the warming does.
2. **CO₂:** where it comes from, where it goes, and where it hits hardest.
3. **Weather forecast:** 3, 5, 7 and 10 days, with its uncertainty and skill.
4. **Wind forecast:** 1–3 days, hour by hour.
5. **Severe weather:** storms, floods, thunderstorms, tornadoes and hurricanes, as warnings.

Two engine runs answer them:

| Run | CLI | Request | Output block | Time |
|---|---|---|---|---|
| Coupled climate | `terraflux-run-earth-climate` | `terraflux.earth.climate-run/v1` ([schema](earth-climate-run-v1.schema.json), [example](../../examples/earth-climate-request.json)) | `earth_climate`, `terraflux.earth.climate/v1` ([schema](earth-climate-v1.schema.json)) | about 2 s |
| Coupled ensemble forecast | `terraflux-run-earth-forecast` | `terraflux.earth.forecast-run/v1` ([schema](earth-forecast-run-v1.schema.json), [example](../../examples/earth-forecast-request.json)) | `earth_forecast`, `terraflux.earth.forecast/v1` ([schema](earth-forecast-v1.schema.json)) | about 80 s on 4 threads |

Kernels: `include/terraflux/weather/earth_climate.hpp` and `earth_forecast.hpp`. In the viewer both are in the separate **Coupled Earth** workspace (the switch in the header, or *Coupled Earth* in the example list), with six views: Overview, Climate drivers, CO₂ impact, Weather forecast, Wind forecast, Severe weather.

```sh
build/terraflux-run-earth-forecast examples/earth-forecast-request.json /tmp/forecast.json   # about 80 s
build/terraflux-run-earth-climate examples/earth-climate-request.json /tmp/climate.json      # about 2 s
# The viewer fixtures (the climate run links to the forecast fixture: regenerate the forecast first)
python3 tools/build_ocean_example.py --build-dir build --cli terraflux-run-earth-forecast \
    --request examples/earth-forecast-request.json --output viewer/public/coupled-forecast/engine-timeline.json
python3 tools/build_ocean_example.py --build-dir build --cli terraflux-run-earth-climate \
    --request examples/earth-climate-request.json --output viewer/public/coupled-climate/engine-timeline.json
```

## What is coupled to what

| From → to | Mechanism | Where it is computed |
|---|---|---|
| Carbon → atmosphere | CO₂, CH₄, N₂O trap heat | climate run: forcing |
| Air pollution, land use, contrails → atmosphere | aerosols cool, ozone and black carbon warm, land use brightens | climate run: forcing |
| Sun → atmosphere | the 11-year cycle, the slow background | solar-cycle run (linked) |
| Volcanoes → atmosphere | eruptions cool for a few years | solar-cycle run's volcanic series (linked) |
| Space → atmosphere | cosmic rays, solar-wind energy, cosmic dust | solar-wind and cosmic-dust runs (linked, as bounds) |
| Atmosphere → water vapour, clouds, ice | feedbacks amplify the warming | climate run: feedback shares |
| Atmosphere → ocean | the ocean takes up heat and slows the warming | climate run: two-layer energy balance |
| Ice → ocean → atmosphere | Greenland meltwater weakens the Atlantic overturning, which cools the North Atlantic | polar-melt run (linked) |
| Ocean, ice → sea level | expansion, glaciers, ice sheets | sea-level budget |
| Carbon → ocean | CO₂ dissolves and acidifies the sea | ocean-carbon chemistry |
| Atmosphere, ocean → extremes | heat, heavy rain, hurricane intensity | climate run: impacts |
| Ocean → weather | the sea feeds heat and moisture into the air: convection and hurricanes | forecast run |
| Land → weather | soil moisture, mountains (cooling with height, lifting, blocking) | forecast run |
| Sun → weather | daytime heating and nocturnal cooling: the daily cycle and afternoon thunderstorms | forecast run |

The forecast measures its own couplings: the unperturbed control is rerun with one coupling switched off (`sensitivity.runs`). In the example, over the first three days:

| Run | Thunderstorms | Rain | Land Tmax / Tmin |
|---|---|---|---|
| no ocean fluxes | −23 % | −3 % | +0.06 / −0.01 K |
| uniform soil moisture | +5 % | +1.5 % | −0.10 / +0.14 K |
| no mountains | −30 % | −2.4 % | +0.72 / +0.81 K |
| no Sun over land | −48 % | −5.2 % | −1.23 / +1.89 K |

The climate run reads these numbers from the forecast fixture and lists them with its own couplings (23 in the example). The Overview draws them as a graph.

## 1. The climate: which drivers matter most

### Forcing

Effective radiative forcing relative to 1750 (W/m²):

| Agent | Forcing | Source of the numbers |
|---|---|---|
| CO₂ | 5.35 ln(C/C₀) | Myhre et al. 1998 |
| CH₄ | 0.036 (√M − √M₀) − [f(M, N₀) − f(M₀, N₀)] | Myhre et al. 1998 |
| N₂O | 0.12 (√N − √N₀) − [f(M₀, N) − f(M₀, N₀)] | Myhre et al. 1998 |
| band overlap | f(M, N) = 0.47 ln(1 + 2.01·10⁻⁵ (MN)^0.75 + 5.31·10⁻¹⁵ M (MN)^1.52) | |
| stratospheric water | a share of the CH₄ forcing | |
| halocarbons, ozone, aerosols, black carbon on snow, land use, contrails | indices scaled to their 2019 values | AR6 best estimates, rounded |
| the Sun, volcanoes | from the solar-cycle run | |
| cosmic rays, solar-wind energy, cosmic dust | bounds from the solar-wind and cosmic-dust runs | |

Each agent also has an uncertainty factor, low and high (aerosols 0.36–1.55).

The concentrations since 1850 and the three scenario paths are hand-entered. They are rounded to NOAA/AR6 values and approximate SSP1-2.6, SSP2-4.5 and SSP5-8.5. They have not been verified against the source files.

### Response

One two-layer energy balance: a mixed layer (8 W yr m⁻² K⁻¹) over a deep ocean (106), with an exchange of 1.0 W m⁻² K⁻¹. The exchange includes the efficacy of deep-ocean uptake.

- **Feedbacks**, rounded from AR6: Planck 3.22 W m⁻² K⁻¹ (restoring); water vapour and lapse rate 1.30, surface albedo 0.35 and clouds 0.42 (amplifying). Net 1.15 W m⁻² K⁻¹.
- **Sensitivity:** ECS = 3.71/1.15 = 3.2 K, TCR 1.84 K. Both lie within the AR6 likely ranges, near the best estimates of 3 K and 1.8 K.
- **By driver:** the response is linear in the forcing, so each agent is run on its own and the warmings add up to the total. The check verifies this.
- **By feedback:** a total warming T splits into T λ/λ_Planck without feedbacks plus T fᵢ/λ_Planck per feedback.
- **Uncertainty:** the band uses ECS 2.5–4 K.

### Results of the example

Warming today (2024, against 1850–1900): **1.38 K**. Observed: about 1.3 K of human-induced warming, and 1.55 K in the single year 2024 with El Niño.

Warming by driver today:

| Driver | Warming today |
|---|---|
| CO₂ | +1.04 K (0.92–1.17) |
| Aerosols | −0.55 K (−0.86 to −0.20) |
| Methane | +0.26 K |
| Tropospheric ozone | +0.24 K |
| Halocarbons | +0.22 K |
| N₂O | +0.10 K |
| Land use | −0.09 K |
| Volcanoes, black carbon, stratospheric water, contrails | a few hundredths of a kelvin each |
| The Sun | +0.01 K (against the 1850–1900 mean) |
| Cosmic rays and clouds | within ±0.016 K (constrained) |
| Cosmic dust | within ±0.005 K |
| Solar-wind energy | 0.0001 K |

Under the space drivers, the contested cosmic-ray–cloud hypothesis (0.9 W/m² over a cycle) is shown separately: about 0.8 K if it were true. It is not used.

Warming in 2100 by scenario: 1.7, 2.9 and 5.0 K (SSP1-2.6, SSP2-4.5, SSP5-8.5).

### Impacts

Driven by the warming path:

| Impact | How |
|---|---|
| Land and Arctic warming | fixed amplification factors (1.45 and 2.7) |
| Ocean heat | the heat stored by both layers |
| Heat extremes | a normal distribution (σ = 1.87 K) shifted by the land warming: how much more often the pre-industrial 1-in-10 and 1-in-50-year heat occurs. Calibrated to the AR6 frequencies: today ×4.1 and ×8.1; 2100 ×8 and ×29 under SSP2-4.5 |
| Heavy rain | ×1.3 per K of warming (7 %/K with a stronger tail) |
| Arctic September sea ice | linear in the warming; ice-free at about 2.8 K |
| Hurricane potential intensity | from the storm-systems kernel: about +3 % per K of sea warming |
| Ocean pH and aragonite saturation | carbonate chemistry of the ocean-carbon kernel |
| Atlantic overturning and Greenland | from the polar-melt run of the matching scenario |
| Sea level since 1900 | the sea-level budget |

### The map

A global 4° grid with a real land mask (`global-land-mask`). The local warming is the global warming times a fixed pattern:

- land 1.45;
- Arctic up to 2.7;
- the Southern Ocean lagging at 0.5.

Two regional terms are added:

- **aerosol cooling**, strongest over the polluted regions (a box list);
- **a North Atlantic cooling** of 0.15 K per Sv of lost overturning (the polar-melt run).

The map frames are per decade, 1850–2100.

## 2. CO₂: where it comes from, where it goes, where it hits

**Sources.** Fossil CO₂ of 18 regions spread over their land (rounded national totals for about 2023, hand-entered), with:

- the rest of the world spread over the remaining land;
- land-use emissions in deforestation boxes;
- cumulative emissions since 1850 per region.

**Sinks.** Scaled to the Global Carbon Budget totals (land 11.6, ocean 10.6 Gt CO₂/yr):

- **Land:** a productivity proxy by latitude, none over deserts and ice.
- **Ocean:** the Wanninkhof gas exchange (ocean-carbon kernel) with wind, SST and an air–sea pCO₂ difference by latitude. Upwelling boxes override it: the equatorial Pacific outgasses; the Southern Ocean's annual mean is small because winter outgassing offsets the summer uptake.
- **Result:** Southern Ocean −4.6, North Pacific −2.9, North Atlantic −2.4, equatorial Pacific +2.3 Gt/yr. This is the pattern of the pCO₂ climatologies, rounded.

**Transport.** The surface CO₂ anomaly is a steady diffusion–relaxation estimate on the sphere. It yields a north–south difference of 2.7 ppm (observed: about 3 ppm).

**Budget.** 40.9 Gt/yr are emitted and 18.7 Gt/yr stay in the air: an airborne fraction of 46 % and a growth of 2.4 ppm/yr. The cumulative 2,500 Gt CO₂ give 1.1 K through the TCRE of 0.45 K per 1000 Gt.

**Impact per +100 ppm.** Per cell:

- the local transient warming (TCR and the pattern);
- the pH drop (carbonate chemistry at the local SST);
- the aragonite saturation now and in 2100.

An impact score combines them. On land it counts the warming beyond the global mean; at sea, acidification and corrosive water. Named hotspots are ranked: permafrost (0.91), the Arctic Ocean (0.79), the Southern Ocean (0.60), the North Atlantic (0.59), then the Sahel, the Amazon, the Mediterranean and the coral reefs.

## 3. The forecast: ensemble, coupling, products

### The atmosphere

The coupled boundary layer of `terraflux-run-coupled-wind` runs on a 52 × 32 grid of 210 km over the North Atlantic sector (local azimuthal frame at 40° N, 45° W), with a real land mask. Each hour:

1. **Moving pressure systems.** The highs and lows are Gaussian pressure anomalies. They move with a steering flow (a westerly jet at 46° N, trade easterlies at 15° N), deepen, then fill. New cyclones form off the US East Coast every 60 h.
2. **Synoptic ascent.** The single layer cannot lift itself, so the ascent is prescribed:
   - lows lift by w = w₀ (|Δp|/15 hPa) exp(−(r/R)²) (1 + 0.6 cos(angle to the warm sector));
   - highs subside;
   - the frictional convergence −H div V is added;
   - and the forced ascent over terrain, f (u ∂h/∂x + v ∂h/∂y).

   Layered cloud follows from the humidity, with the ascent shifting the threshold. Layered rain is R = e ρ w q.
3. **The surface** (`couple_surface`):
   - **Sea:** the air relaxes to the sea-surface temperature (24 h) and to 80 % humidity over it.
   - **Land by day and night:** heating at 1.6 K/h times (cos z − its daily mean). This gives no net drift over a day. Cloud and wet soil damp it.
   - **Land towards its climate:** the land relaxes to its climate, warmer where the soil is drier than normal.
   - **Evapotranspiration** moves soil water into the air.
   - **Condensation:** supersaturated air rains out with its latent heat.
   - **Rain on the soil:** it wets the soil; saturated and steep soils shed runoff; wet soils drain slowly.
4. **Diagnoses:**
   - clouds, convective and frontal precipitation, and severe convection (CAPE, shear, the tornado parameter), with the existing kernels: every 6 h to 72 h, then every 12 h;
   - convective rain limited by the moisture of a 3 km layer;
   - large hail only where the freezing level is low enough (full below 3 km, none above 4.5 km; the column keeps a constant 6.5 K/km, so its tropical freezing level sits near 4.4 km).
5. **Tropical cyclones:** the storm-systems kernel, seeded with a hurricane near Puerto Rico. On the coarse grid:
   - the wind is a modified Rankine vortex ((R/r)^0.6 outside);
   - the rain shield and the storm surge have widths of at least a cell.
6. **Wind at 10 m:** the layer wind × 0.6 over land and × 0.8 over the sea (less on mountains), plus the hurricane vortex.
7. **Gusts:** the 10 m wind × 1.45, plus 8 m/s of downdrafts under thunderstorms.

### The ensemble

There are 10 members and one hidden "truth" member. All are drawn from the same error distribution:

- initial positions of the systems (50 km);
- speeds (0.8 m/s, scaled by how mobile the system is);
- deepening and size;
- the place and timing of the new cyclones;
- a smooth initial temperature error;
- the hurricane's position and strength.

The members run in parallel threads. Every member has its own seed, so the output does not depend on the number of threads (checked).

### Products

- **Frames** every 3 h to 72 h, then every 12 h:
  - ensemble-mean pressure, 10 m wind and temperature;
  - precipitation, cloud, gusts;
  - probabilities of rain, storm gusts, thunder and tornadoes.
- **Days 1–10:**
  - Tmax and Tmin with their spread;
  - precipitation with its spread, and P(≥ 1 mm), P(≥ 10 mm);
  - gusts, cloud, the 72-hour runoff, surge;
  - a weather symbol;
  - exceedance probabilities of every warning threshold;
  - hail, tornado, tropical-storm and hurricane chances.
- **Cities** (25):
  - a daily table with the 10–90 % range;
  - for the first 72 hours: hourly 10 m wind (mean, 10 %, 90 %), gusts, direction, temperature, rain, and the truth's wind.
- **Tracks:** the hurricane in every member and in the truth.

### Warnings

Yellow, orange and red per day and hazard. A level is issued where at least 30 % of the members exceed its threshold; red needs at least 50 %, tornadoes 15 %. Warnings cover land and the sea next to it.

| Hazard | Yellow | Orange | Red |
|---|---|---|---|
| Wind storm (gust) | 18 m/s (65 km/h) | 25 m/s (90 km/h) | 33 m/s (120 km/h) |
| Heavy rain (24 h) | 30 mm | 60 mm | 100 mm |
| Flooding (runoff in 72 h) | 40 mm | 80 mm | 150 mm |
| Thunderstorm | any | hail 3 cm | hail 5 cm |
| Tornado (significant tornado parameter) | possible | STP ≥ 3 | STP ≥ 6 |
| Hurricane (storm wind) | 18 m/s | 33 m/s | 50 m/s |
| Storm surge | 1 m | 2 m | 3 m |

Connected cells of the same level form warning areas. Each area gets its peak, its probability, the cities inside it and the nearest city.

In the example:

- the hurricane brings red wind and rain near Puerto Rico on day 1 and off Miami on days 2–3;
- red flooding follows near Tampa on days 4–6 after the landfall;
- a tornado watch (yellow) comes with the Central Plains low on day 1;
- thunderstorm and rain warnings spread over the Gulf states and the tropics.

### Verification

There are no observations of a synthetic situation. The forecasts are verified against the hidden truth member, so the skill is the **predictability of the model world**, not the skill of a real forecast:

- the pressure pattern and its anomaly correlation against the time mean of the truth (the climatology) and against persistence;
- the temperature error against the ensemble spread;
- the Brier skill of rain ≥ 1 mm.

In the example:

| Lead | Pressure anomaly correlation | Pressure error / spread | Tmax error | Rain Brier skill |
|---|---|---|---|---|
| day 1 | 0.97 | 1.1 / 1.5 hPa | 0.2 K | 0.88 |
| day 3 | 0.65 | 3.0 / 3.9 hPa | 0.7 K | 0.82 |
| day 5 | 0.37 | 4.4 / 5.0 hPa | 0.8 K | 0.64 |
| day 7 | 0.20 | 5.9 / 6.0 hPa | 1.2 K | 0.56 |
| day 10 | 0.49 | 6.6 / 7.3 hPa | 1.5 K | 0.36 |

The useful lead (pattern correlation above 0.6) is 72 h. The spread is close to the error, so the ensemble knows its uncertainty. With a single truth the day-to-day scores are noisy (day 10 beats day 7 by chance).

## In the viewer

The **Coupled Earth** workspace loads `viewer/public/coupled-forecast/` and `coupled-climate/` on demand. *Open coupled JSON* (or the header's *Open JSON*) takes either kind of output.

| View | Map | Panels |
|---|---|---|
| Overview | — | Key answers; the coupling graph (hover an arrow for the mechanism and its size); what the forecast owes to each coupling; the table of all couplings |
| Climate drivers | local warming by decade and scenario, aerosol cooling, the overturning cooling, the pattern | drivers ranked with their ranges (today, 2050, 2100, per scenario); series of all scenarios with the sensitivity band; forcing by group; feedbacks; impacts table |
| CO₂ impact | emissions, sinks, net flux, CO₂ anomaly, warming / pH drop per +100 ppm, aragonite 2100, impact score | budget bars, hotspots, emitters (annual and cumulative, warming caused), CO₂ and pH by scenario, sinks |
| Weather forecast | Tmax, Tmin, rain, rain chance, cloud, gusts, spread or weather symbols for a day; isobars | 3 / 5 / 7 / 10-day range and day; cell inspector (click to pin); city table; skill charts and table; the surface couplings of the control run |
| Wind forecast | 10 m wind, gusts or storm chance hour by hour (play), arrows, isobars, highs and lows, the hurricane in every member | meteogram of a city (click the map or a row) with the member range, gusts and the truth; strongest gusts per city and day |
| Severe weather | warning levels (all or one hazard) or chances of thunder, hail, tornado, tropical-storm and hurricane winds, runoff, surge; hurricane track plume | day × hazard matrix, warning areas with cities, track spread by lead time |

## Tests

| Test | What it checks |
|---|---|
| `weather.earth_system` (`tests/weather/earth_system_test.cpp`) | Projection round trips and distances; solar geometry; the steering flow; the reproducible member errors; systems that move, deepen, fill and are born; ascent (warm sector, subsidence, convergence, upslope); the surface: SST relaxation, moist enthalpy conserved by condensation, no diurnal drift, evapotranspiration, drainage, the soil water balance; gusts, the 10 m wind, the cyclone profile, surge only on the coast, cyclonic rotation; hail and the freezing level; warning levels and areas; skill and Brier scores; CH₄ and N₂O forcing against Myhre; feedback shares; the two-layer equilibrium and TCR; heat extremes, heavy rain, sea ice; the warming pattern; ocean CO₂ uptake; the zero-mean transport anomaly |
| `weather.earth_forecast_cli` (`tests/tools/earth_forecast_check.py`) | A 3-day, 4-member run against the schemas; frames, static fields, the land mask; probabilities as multiples of one member; wind and rain warnings recomputed from the probabilities; warnings only on land and the coast; warning areas covering the warned cells; city tables and hourly wind; the verification (growing error and spread, skill on day 1); tracks; the sensitivity run without the Sun; the same output with 1 and 2 threads; seven invalid requests |
| `weather.earth_climate_cli` (`tests/tools/earth_climate_check.py`) | The example against the schemas; ECS from the feedbacks; the CO₂ forcing from the concentrations; total forcing = sum of the agents; drivers adding up to the warming in every scenario and period; present-day warming within 1.1–1.6 K; the ordering of the scenarios and the impacts; the pattern averaging to one and the maps to the global warming; the regional aerosol and overturning terms; the carbon budget of the map (emissions, sinks, airborne fraction, growth); the zero-mean concentration anomaly; the couplings with and without linked runs; seven invalid requests |
| `weather.viewer_earth_forecast` (slow), `weather.viewer_earth_climate` | The viewer fixtures match a regeneration |
| `viewer/src/coupledEarth.test.js` | Both fixtures validate; the projection puts cells and cities back on their cells; horizons, noon frames, city tables, the inspector, skill; hourly wind, meteograms, the gust table; the warning matrix, levels and areas; the track plume; drivers adding up; charts, forcing groups, impacts, feedbacks; the CO₂ budget, the coupling graph, the key answers; contours and colours |

Run them with `ctest --test-dir build -R earth`.

## Limitations

- **Climate:**
  - one global two-layer energy balance with a fixed feedback;
  - no carbon-cycle feedback: the concentrations are prescribed;
  - no regional dynamics: the map is a fixed pattern plus two regional terms;
  - the forcing indices, emissions, sinks and scenario paths are hand-entered, rounded literature values, not verified against their source files.
- **Forecast:**
  - a single coupled boundary layer under prescribed, moving Gaussian pressure systems: no dynamics of the pressure field, no jet stream, no upper-level waves, no fronts that form by themselves;
  - the ensemble's errors grow roughly linearly, not chaotically;
  - clouds, precipitation and severe convection are rule-of-thumb diagnostics every few hours;
  - the terrain is a set of idealised ridges; the sea-surface temperatures, soil moisture and the weather situation are synthetic;
  - the land mask is real;
  - floods are a runoff index, not river levels.
- **Verification:** against a member of the same model, so it measures the model's predictability, not real forecast skill.
- **Hail:** the large-hail thresholds compensate the constant 6.5 K/km lapse rate of the diagnostic column.
- **Wind:** the 10 m wind factors (0.6 land, 0.8 sea) are textbook averages.

## References

- Myhre, G., Highwood, E. J., Shine, K. P. & Stordal, F. (1998). New estimates of radiative forcing due to well mixed greenhouse gases. *GRL* 25, 2715–2718.
- Forster, P. et al. (2021). The Earth's energy budget, climate feedbacks and climate sensitivity. In *IPCC AR6 WG1*, chapter 7.
- Forster, P. et al. (2024). Indicators of Global Climate Change 2023. *ESSD* 16, 2625–2658.
- Seneviratne, S. I. et al. (2021). Weather and climate extreme events in a changing climate. In *IPCC AR6 WG1*, chapter 11.
- Friedlingstein, P. et al. (2023). Global Carbon Budget 2023. *ESSD* 15, 5301–5369.
- Wanninkhof, R. (2014). Relationship between wind speed and gas exchange over the ocean revisited. *L&O Methods* 12, 351–362.
- Landschützer, P., Gruber, N. & Bakker, D. C. E. (2016). Decadal variations and trends of the global ocean carbon sink. *GBC* 30, 1396–1417.
- Held, I. M. et al. (2010). Probing the fast and slow components of global warming by returning abruptly to preindustrial forcing. *J. Climate* 23, 2418–2427 (two-layer model).
- Holton, J. R. (2004). *An Introduction to Dynamic Meteorology* (quasi-geostrophic ascent, Ekman convergence).
- Thompson, R. L. et al. (2003). Close proximity soundings within supercell environments. *Wea. Forecasting* 18, 1243–1261 (significant tornado parameter).
- Holland, G. J. (1980). An analytic model of the wind and pressure profiles in hurricanes. *MWR* 108, 1212–1218; Rankine vortex profiles as in the storm-systems kernel.
- Leutbecher, M. & Palmer, T. N. (2008). Ensemble forecasting. *J. Comput. Phys.* 227, 3515–3539 (spread–error relation).
- Wilks, D. S. (2011). *Statistical Methods in the Atmospheric Sciences* (Brier score, anomaly correlation).
