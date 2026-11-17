# Flight weather and sailing weather views v1

Two full views of the observatory's 3D (WebGL) scene, next to *Weather map* and *Storm watch*: **Flight weather** for pilots
and **Sailing weather** for small-craft sailors. Each view shows the weather over the whole map in 3D. Its report, for
the station point the user picks, fills the page below the scene. Winds aloft are drawn as wind barbs at every
winds-aloft level. Everything is a viewer diagnostic of engine output, not a new physics kernel.

Implementation:

- `viewer/src/station.js`: shared station diagnostics.
- `viewer/src/aviation.js` and `viewer/src/sailing.js`: the two reports.
- `viewer/src/windsAloft.js`: winds aloft.
- `viewer/src/barbs.js` and `viewer/src/windBarbs.js`: wind-barb geometry and the 3D barb layers.
- `viewer/src/stationMarkers.js`: windsock, runway and boat.
- `viewer/src/airports.js`: the aerodromes of a run.
- `viewer/src/flightPlan.js`: flight planning.
- `viewer/src/airportCard.js`: the weather cards over the aerodromes.
- `viewer/src/FlyOverlay.vue`: the fly-through's perspective window and flight display.
- `viewer/src/geo.js`: coordinates, great circles, and the engine's projection onto the grid.
- `viewer/src/weatherLayers.js`, `viewer/src/weatherIcons.js` and `viewer/src/WeatherIcon.vue`: weather symbols and the weather layers.
- `viewer/src/FlightPlan.vue`, `viewer/src/FlightWeather.vue`, `viewer/src/SailingWeather.vue` and `viewer/src/WindBarb.vue`: the planning and report sections and SVG barbs.
- `viewer/src/routeWeather.js`, `viewer/src/RouteWeather.vue` and `viewer/src/WxStrip.vue`: the weather along a flight route and the route strips.
- `viewer/src/polars.js`, `viewer/src/sailTrim.js`, `viewer/src/manoeuvre.js` and `viewer/src/sailRoute.js`, with `viewer/src/SailTrim.vue`, `viewer/src/SailPlan.vue` and `viewer/src/SailOverlay.vue`: polars, sails and trim, tacks and gybes, weather routing and the sail-through (see [route-weather-and-sail-routing.md](route-weather-and-sail-routing.md)).

Tests are in `viewer/src/{station,aviation,sailing,windsAloft,barbs,flightPlan,geo,weatherLayers,stationMarkers,routeWeather,sailTrim,sailRoute}.test.js`.

> **Not for navigation or flight planning.** All inputs in this repository are synthetic. There is no live observation
> feed. "Real time" means the reports follow the simulation live: they recompute on every frame change, during playback
> and when the station moves. Every report is stamped `SIMULATED`.

## 1. Requirements research

### 1.1 Interpretation of the request

| Request | Interpretation in this codebase | Reason |
| --- | --- | --- |
| "real time flight weather report" | A METAR-style report and a decoded pilot briefing. It recomputes live from the engine frame on display. | The handover excludes live providers ("No live weather feed is connected"). The engine's timeline is the only source of weather. |
| "sailing report" | A marine report in the style of a coastal/shipping forecast, with sailing diagnostics (true/apparent wind, point of sail, waves, current, warnings). | Small-craft sailors use these elements. |
| "add the two tools to the OpenGL view" | Two **views** of the WebGL scene, not pop-up panels: the whole map in 3D, with the report below it. Clicking the map selects (flight) or reads a point (sailing) and opens no overlay window. | Revised after review: "the flight and sailing weather should not open a drop down overlay; we want a full view of the weather". |
| "add planning for the flight weather … [the airport] should not be movable and the weather should be displayed on top of the airport" | Fixed **aerodromes** per run, sited from the run itself. Each has a **weather card** floating over it in the scene. Clicking selects the nearest aerodrome; nothing moves. **Flight planning** between two aerodromes runs through the timeline. | Revised after the second review. |
| "can the flight path be more visualized in 3D and also add the overlay with the 3D build-up perspective … to see what is faced during flight times" | A richer 3D route, plus a **fly-through**. An aircraft flies the plan in the scene, and a picture-in-picture chase or cockpit view shows the cloud build-ups, CBs, terrain and winds ahead, with a flight display. The run's frames step with the flight time, so the weather shown is the weather met at that time. | Revised after the third review. |
| "can we do more layered, it should simply tell weather by looking at it; also we need a flight planning tool, we know coordinates so it is simple to calculate the flight path" | **Weather symbols** on the map, cards and reports, and a **stack of colour-coded layers** at their true altitudes, with presets and a layer key. **Route planning by coordinates:** departure, waypoints and destination as aerodromes or typed coordinates, with great-circle legs projected with the engine's projection. | Revised after the fourth review. |
| "high altitude winds with the wind hook symbol … for each layer" | Winds aloft at the nine winds-aloft (FB) levels, drawn as **wind barbs**: in 3D as a barb layer per level, and as a barb mast at the station through every level. In the report they appear as a table with a barb, the FB code, the temperature and the shear per level. | The wind barb is the standard symbol that combines direction and speed. FB is the standard winds-aloft product. |
| "a return trip on a later day, e.g. out Monday, back Wednesday: is a simple return trip possible; highlight whether IFR is needed or VFR is possible" | **Flight rules** for every plan (VFR possible / VFR marginal / IFR required / not advisable) from the VFR band and the GAFOR category at the time each part is flown, and a **return trip**: the reversed route on a chosen later day and time, with its own rules, a verdict for the round trip and the VFR windows of both days (F17). The Germany example covers Monday to Thursday. | Added after the GAFOR review. Rules from SERA VFR minima and the GAFOR categories confirmed earlier (gafor-vfr.md). |
| "in the sailing navigation view clicking on data points should show data rather than moving the boat" | A click **reads the point** (wind, waves, current, water, weather, distance and bearing from the boat) in the report and on a card in the scene; the boat stays. Moving the boat or steering for the point are explicit buttons (S14). | Added after the GAFOR review. |
| "currently the weather is mapped to the arrival and takeoff airfields; can we have the whole weather along a designated route calculated; for the sailing weather this also means adjustments; draw the sail settings for the weather conditions and simulate a turn; including route planning by weather forecast" | The weather at every route point when it is passed (F18); for sailing, a passage planner with weather routing, the weather along the track, the sail plan and trim drawn, and tacks and gybes simulated (S15–S22). | Requirements researched in [route-weather-and-sail-routing.md](route-weather-and-sail-routing.md). |

### 1.2 Standards and conventions consulted

| Topic | Convention adopted | Used for |
| --- | --- | --- |
| METAR code form | WMO FM 15 / ICAO Annex 3 | Group order, wind `dddffKT`, visibility in metres (`9999` ≥ 10 km), weather codes, cloud `NNNhhh` + `CB`/`TCU`, `NSC`, `CAVOK`, `TT/TdTd`, `QPPPP`, `RMK` |
| Visibility reporting steps | ICAO Annex 3 | 50 m steps below 800 m, 100 m steps to 5 km, 1 km steps to 10 km |
| Cloud height steps | ICAO Annex 3 | 100 ft steps to 10 000 ft, 1 000 ft steps above |
| QNH rounding | ICAO Annex 3 | Rounded **down** to the whole hPa |
| Missing elements | FM 15 practice | `/` solidi for elements the engine does not provide (never an invented value) |
| Flight categories | FAA / NWS (AIM 7-1) | VFR: ceiling > 3 000 ft and vis > 5 SM. MVFR: 1 000–3 000 ft and/or 3–5 SM. IFR: 500 to < 1 000 ft and/or 1 to < 3 SM. LIFR: < 500 ft and/or < 1 SM. Ceiling = lowest BKN/OVC layer. Colours green/blue/red/magenta |
| Present weather intensity | WMO / FMH-1 rain-rate classes (already used by the engine): light < 2.5, moderate < 10, heavy ≥ 10 mm/h | `-`, none, `+` prefixes |
| Fog and mist | WMO: `FG` below 1 000 m, `BR` 1 000–5 000 m in moist air | Obscuration codes |
| Pressure and density altitude | ICAO Standard Atmosphere (troposphere) | Pressure altitude from station pressure. Density altitude from the engine's diagnosed air density |
| Runway wind components | Standard trigonometry | Headwind `V cos θ`, crosswind `V sin θ`, θ = wind − runway heading |
| Mountain wave | Common forecasting rule: ridge-normal wind ≥ 25 kt with stable air | Mountain-wave turbulence flag |
| Wind barbs | WMO plotting practice | The staff points to where the wind comes from. Pennant 50 kt, barb 10 kt, half barb 5 kt, speeds rounded to 5 kt, circle for calm. Barbs stand on the clockwise side in the Northern Hemisphere (toward low pressure) and the anticlockwise side in the Southern |
| Winds-aloft forecast (FB) | NWS FB product | Levels 3 000, 6 000, 9 000, 12 000 ft and FL180, FL240, FL300, FL340, FL390. Group `DDff±TT`; +50 to the direction for 100–199 kt; 199 kt or more coded as 199; `9900` below 5 kt. No temperature at 3 000 ft. Sign left out above FL240. No wind within 1 500 ft and no temperature within 2 500 ft of the station |
| Clear-air turbulence | Rule of thumb | Vertical shear of about 6 kt per 1 000 ft or more |
| Marine forecast wording | Met Office shipping forecast | *Veering* (clockwise) and *backing* (anticlockwise); *increasing* and *decreasing*; *imminent* < 6 h, *soon* 6–12 h, *later* > 12 h; "gale 8", "severe gale 9", "storm 10", "violent storm 11", "hurricane force 12"; a calm is "calm" |
| Beaufort scale | WMO, thresholds in m/s | Force number and description |
| Sea state | Douglas sea scale (already in `waves.js`) | Sea-state words |
| Marine visibility words | UK Met Office shipping forecast: good > 5 NM, moderate 2–5 NM, poor 1 000 m–2 NM, very poor < 1 000 m | Marine visibility |
| Pressure tendency words | Met Office: steady < 0.1, slowly 0.1–1.5, (plain) 1.6–3.5, quickly 3.6–6.0, very rapidly > 6.0 hPa per 3 h | Barometer trend |
| Marine wind warnings | US NWS: gale 34–47 kt, storm 48–63 kt, hurricane force ≥ 64 kt. Small-craft advisory thresholds vary by region | Warning level. The small-craft thresholds are settings (defaults 22 kt, 2.0 m) |
| Apparent wind | Vector sum of true wind and the wind made by the boat's own motion | AWS/AWA |
| Points of sail | Customary sailing bands by true wind angle | No-go, close-hauled, close reach, beam reach, broad reach, running |
| Cold water | Cold-water-shock guidance: risk below 15 °C | Water-temperature hazard |

### 1.3 What the engine provides (per station cell, per frame)

| Report element | Engine source | Fallback when absent |
| --- | --- | --- |
| Surface wind | `forecast.fields.wind_east/north` (every run). Inside a tropical cyclone, the storm wind (`storms.env.storm_wind_speed_m_s`) where it exceeds the background wind, blowing around the eye. This is the same rule the wave field uses. | (always present) |
| Temperature | `forecast.fields.air_temperature` | (always present) |
| Dew point | Inverted Magnus form with the engine's coefficients (`moist_air.cpp`) from `coupled.humidity` RH | `//` |
| QNH | `coupled.pressure` (the prescribed sea-level pressure field), plus `storms.env.storm_pressure_anomaly_pa` inside a tropical cyclone | `Q////` |
| Station pressure | `orography.surfacePressure`. Otherwise QNH reduced to the terrain height by the ISA barometric law | QNH (flat, sea-level field) |
| Density | `coupled.density` (engine diagnosed moist density). With a station pressure from terrain it is recomputed with the engine's relation `ρ = p / (R_d T (1 + 0.608 q))` | not shown |
| Cloud layer | `clouds.type/base/top/fraction` (one layer per cell). Coverage in oktas = round(8 × fraction): FEW 1–2, SCT 3–4, BKN 5–7, OVC 8 | `//////` (not modelled, no `NSC` claimed) |
| Present weather | `precipitation.type/intensity/phase/lightning/hail/virga`, surface temperature for the `FZ` prefix | none reported. The report states that precipitation is not modelled |
| Freezing level, cloud-base/top temperature | `precipitation.freezingLevel/baseTemperature/topTemperature` | icing band from base/top heights and the ISA lapse rate, marked estimated |
| Convective and shear hazards | `clouds.thunderstorm`, `precipitation.lightning/hail/updraft`, `coupled.columns.cape`, `severe.shear1/risk`, `storms` | omitted |
| Mountain wave / foehn | `orography.cross`, `orography.foehn`, terrain | omitted |
| Waves | `waves.js` parametric wind sea (JONSWAP / Pierson–Moskowitz, fetch to land, depth and ice limits). This is the viewer's existing estimate; the engine has no wave model | none where no land/sea mask exists |
| Current and water temperature | `ocean.u/v/mask` and `ocean.thermal.sst` or `ocean.temperature`. Otherwise `surface.sst` | omitted |
| Winds aloft | `coupled.pressure` and the temperature field, through the balance of the engine's severe-convection kernel (see §3a). Inside a cyclone the storm's extra surface wind weakens with height | not available without a pressure field (the report says so) |
| Pressure tendency | Difference to the frame closest to 3 h earlier (at most 4.5 h; otherwise the previous frame), scaled to hPa per 3 h. The coupled model's pressure is prescribed and constant in time; when nothing changed the shared field, the tendency is reported as not modelled instead of "steady" | omitted |

**Gaps the engine does not model**, and how the reports treat them:

1. **Visibility** is not simulated anywhere. The reports estimate it and mark it `est.`. The method is the Koschmieder law
   `V = 3.912 / σ` (2 % contrast threshold) with an extinction σ summed from:
   - precipitation: a power law in the rate, `σ = a Rᵇ` (rain a = 0.25 km⁻¹, b = 0.63; snow a = 1.6, b = 0.70; drizzle a = 0.55, b = 0.63);
   - humidity haze and mist above 85 % RH: `σ₀ ((1 − 0.85) / (1 − RH))^1.5 − σ₀` with clean air `σ₀ = 0.1 km⁻¹` (about 39 km). RH is capped at 99.5 %, so supersaturated engine states (the coupled model reports rather than condenses them) count as saturated;
   - fog where the cloud base is at or near the ground.

   These are illustrative empirical coefficients, not calibrated to observations. Without precipitation and humidity data
   the report gives no visibility (`////`). `FG` or `BR` is reported only when humidity or fog, not precipitation, causes
   most of the extinction (`FG` below 1 000 m; `BR` below 5 000 m with RH ≥ 80 %).
2. **Gusts** are not modelled. No `G` group is reported, and the briefing says so.
3. **Airports and runways** do not exist on the synthetic grids. The station indicator is `ZZZZ` (the ICAO placeholder
   for "no location indicator"). The runway heading is a user setting, referenced to true north. Real runways are
   magnetic.
4. **Swell, tides and tidal streams** are not modelled. The only currents are the ocean model's, and they are only in ocean runs.
5. **Sea/land** is known only where the run carries a mask (terrain, ocean, polar, coast, carbon or storm surface).
   Elsewhere the sailing report is wind-only and says so.

## 2. Functional requirements

### Flight weather report (F)

- **F1** Station: one of the run's fixed aerodromes (§3b), chosen with the aerodrome chips or by clicking near it on the map. The first is preselected. The report shows the identifier and name, the column/row, the approximate latitude/longitude (inverse azimuthal-equidistant projection, R = 6 371 008.8 m, as in `observation_grid.cpp`) and the elevation.
- **F2** Valid time: `start_time_utc + elapsed_seconds`, METAR group `DDHHMMZ`.
- **F3** A METAR line in FM 15 order: `METAR ZZZZ DDHHMMZ dddffKT VVVV [w'w'] NNNhhh[CB|TCU] | NSC | CAVOK T'T'/T'dT'd QPPPP RMK SIMULATED …`. It uses solidi for missing data. It never prints a value the engine or a documented estimate does not provide.
- **F4** Decoded briefing: wind (true direction, kt, m/s, Beaufort words), visibility (m and SM, marked estimated), weather, cloud (oktas, base and top ft AGL, genus), ceiling, temperature and dew point (°C) with spread, QNH (hPa and inHg).
- **F5** Flight category (VFR/MVFR/IFR/LIFR) from ceiling and visibility, in the FAA colours. When the ceiling or visibility is unknown, the category is marked as based only on the known element.
- **F6** Runway wind on the aerodrome's fixed runway. The end in use is the one with the most headwind, as a tower would choose. The report gives head/tailwind and crosswind components, and the crosswind limit is a setting.
- **F7** Performance: pressure altitude and density altitude (ft), with the ISA temperature deviation.
- **F8** Hazards list, most serious first: thunderstorm/CB, hail, towering cumulus, icing band (freezing level to −20 °C inside cloud), freezing precipitation, tornado possible, low-level shear (0–1 km shear ≥ 20 kt), mountain wave, foehn, virga, high density altitude (≥ 2 000 ft above the field), strong surface wind, tailwind, and crosswind over a user limit (default 15 kt). A tropical cyclone is **severe** where its wind at the point reaches 34 kt. In its weaker outer circulation it is a **caution**: the storm's peak does not describe the point.
- **F9** Trend: the flight category at this station for every frame of the run, as a strip with the current frame marked.
- **F10** Live: recompute on frame change (playback, scrubbing, arrow keys), station change and setting change.
- **F12** Flight planning (§3c):
  - inputs: departure, destination, cruise level, true airspeed, fuel burn, and departure time (the frame shown, which follows playback, or a chosen frame);
  - outputs: distance and course, time en route with off and on times, average ground speed and wind component, trip fuel plus a 45-minute reserve, and the best level;
  - the planning checks and a verdict, the departure weather, the destination weather at the ETA, and an alternate when needed;
  - a route cross-section with the profile, terrain, clouds, CB, icing, freezing level and wind barbs at every level;
  - a navigation log of about eight legs (altitude, course, wind with barb, head/tail component, wind correction angle, heading, ground speed, time, ETA, temperature/ISA, hazards);
  - a cruise-level comparison, where "fly" applies a level.
- **F13** The route in 3D:
  - a glowing tube coloured by the wind component;
  - translucent **sleeves** over the stretches with thunderstorms, tropical-storm winds, terrain conflict, icing or turbulence, each labelled with its NM range;
  - a curtain to the ground and the dashed ground track;
  - waypoints at the leg ends, and TOC and TOD, as beads with drop lines and labels (altitude, time);
  - barbs of the winds met;
  - clouds drawn along the route corridor (within a cell of the route) even when the map's clouds are off, so the build-ups flown through are visible without the whole deck hiding the map.
- **F14** Fly-through ("✈ Fly the route in 3D"):
  - **Aircraft:** it follows the plan (`planStateAt`): position, altitude, nose on the heading (it crabs into the wind while the path follows the track), pitch in climb and descent, nav lights and beacon.
  - **Perspective window:** bottom-left of the scene, in *chase* (behind and above the aircraft, with the route) or *cockpit* (from the nose, without the route tube) view. It is rendered from the same scene into a scissored viewport, with a sky backdrop and haze. Labels and cards are on a layer this camera does not draw.
  - **Flight display:** phase (climb, cruise, descent), IMC, altitude and vertical speed, ground and true airspeed, heading against track with the wind correction, outside air temperature and ISA deviation, wind barb with head and cross components, time into the flight and UTC. The alert shows the hazard the aircraft is in, otherwise the next one ahead with its distance and minutes.
  - **Controls:** play/pause, back to departure, simulation speed ×60–×1 800, a scrubber, and a mini profile with the hazard stretches (click to jump).
  - **Weather over time:** the run's frame follows the segment being flown, so the clouds, barbs and categories in both views are those of that time. While flying, the departure is held at its frame, so the plan does not change under the aircraft. Closing returns to the departure frame and restores the departure setting.
- **F15** The weather in layers (`weatherLayers.js`), so it can be read at a glance:
  - **weather symbols** on the map, on every aerodrome card, in the aerodrome chips and report, and at departure, destination (at the ETA) and alternate. The symbols are sun, a few clouds, partly cloudy, cloudy, overcast, drizzle, rain, showers, thunderstorm, thunderstorm with hail, snow, sleet, fog and mist. Priority: thunder (hail from 5 mm), what falls, fog or mist, then the sky by the cloud cover. There is no symbol where the run says nothing;
  - the **air above, in layers at their altitudes**: red thunderstorm columns (cumulonimbus or lightning, from the ground to the cloud top), cloud slabs from base to top (low, middle, high, CB), blue icing slabs (the cloud between 0 and −20 °C), orange turbulent bands (shear ≥ 6 kt per 1 000 ft between winds-aloft levels) and the cyan **0 °C freezing-level surface**, over the flight-category ground;
  - a **layer key** (top to bottom, with the symbols) and **presets**: *Layered weather* (the default), *Winds aloft* (barb layer, mast, turbulence and storms) and *Minimal* (symbols, category, storms). Every layer can be toggled, and the barb layer can be off.
- **F16** Route planning by coordinates (`geo.js`):
  - **Route editor:** departure, any number of waypoints and destination, each an aerodrome or coordinates with a name. Rows can be moved, removed or reversed, waypoints can be added or **picked on the map** (the clicked cell's centre), and the simulated area's latitude/longitude bounds are shown.
  - **Accepted notations:** decimal degrees (`46.95, 7.45`, `-37.8 144.9`, `46.95N 7.45E`), degrees and minutes or degrees, minutes and seconds (`46°57.0'N 007°27.0'E`), and the ICAO flight-plan form (`4657N00727E`, `465700N0072700E`). Each row shows its reading in chart and ICAO form, or a message saying what is wrong. A point outside the simulated area is refused, because there is no weather there.
  - **Legs:** great circles, giving the distance and the initial and final true course of each leg, and the total.
  - **Sampling:** every leg is sampled about every half cell, and each sample is projected onto the grid with the engine's projection to pick its cell.
  - **Wind triangle:** worked in the grid frame (the engine's winds are grid east/north). The log reports true courses and headings; the correction angle is the same in both frames.
  - **Navigation-log checkpoints:** the route points, TOC and TOD; long stretches are split.
  - **3D:** the route follows the projected great-circle path, with the waypoints marked.
  - **Coordinate ends:** the weather is reported for the point, without a runway.
- **F17** Flight rules and the return trip (`flightRules`, `returnTrip`, `ruleWindows` in `flightPlan.js`). Request: a round trip flown out on one day and back on a later one (for example out Monday, back Wednesday): is it possible, and can it be flown VFR or is IFR needed?
  - **Flight rules of a plan**, from the weather valid when each part is flown:
    - *VFR possible:* on every segment a VFR band exists, from the higher of the VFR minimum height (`vfrSpace`, 500 ft above ground) and 500 ft over the corridor's highest terrain, up to the clear-of-cloud limit with the ground in sight (SERA.5001); and the GAFOR category is Charlie, Oscar or Delta en route, at departure (at the departure time) and at the destination (at the ETA);
    - *VFR marginal:* GAFOR Mike somewhere, a band thinner than 500 ft, no data to judge an end, or no single altitude that works all the way;
    - *IFR required:* GAFOR X-ray at an end or en route, or no VFR band on some stretch (cloud down to the minimum height, visibility under 1 500 m). When IFR meets icing in cloud at the planned level, this is added;
    - *Not advisable:* thunderstorms (CB, lightning) or tropical-storm winds on the route, which neither VFR nor IFR should fly through.
  - The card shows the rule, the VFR band along the route (for example "VFR band 500–3 000 ft AMSL all the way"), and every reason with its NM range.
  - **Return trip:** a checkbox adds the return, which is the same route reversed. Its departure is chosen by day (buttons for every day of the run from the outbound day on) and time (the run's frames). The default is two days after the outbound at the same time. The return is planned through the forecast valid then, with its own flight rules, ends, GAFOR and reasons. The **verdict** for the round trip is "VFR possible both ways", the worse of the two rules, or "Not advisable". A return that departs before the outbound lands, or that cannot be planned, is refused with the reason. The time on the ground is shown.
  - **Windows:** for the outbound day and the return day, a strip of the flight rules for a departure at every frame of that day (colour, hour, VFR / VFR·M / IFR / NO-GO), with the VFR departure windows in words. A click picks that departure time.
  - Limited to the run's horizon: the Germany VFR example covers Monday 2 November 00Z to Thursday 5 November 00Z. The engine has no fog or low-stratus physics, so X-ray (and hence "IFR required") occurs only where its clouds or estimated visibility are that low. In the Germany run the worst category is Mike (mornings); the IFR and no-go paths are exercised by the unit tests.
- **F18** Weather along the route (`routeWeather.js`, `RouteWeather.vue`): every route point read when the aircraft passes it. It includes the surface weather, visibility, cloud and ceiling, category and GAFOR, and the air at the altitude flown. Stretches of similar weather form a route forecast in the order flown, with a summary under the GAMET headings; there is a strip along the distance, a continuous cross-section and symbols along the 3D route. Requirements and derivations: [route-weather-and-sail-routing.md](route-weather-and-sail-routing.md).
- **F11** Winds aloft at the station: a table from FL390 down to the surface. Each level gives the wind barb, the direction and speed, the temperature, the FB code and the shear to the level above (flagged from 6 kt per 1 000 ft). Levels too close to the ground are left blank, as in the FB. The report also gives the strongest wind aloft on the map.

### Sailing report (S)

- **S1** Station: the boat's cell, the centre of the run until the boat is moved (S14). It snaps to the nearest sea cell when the run has a land/sea mask, and the snap is shown. With no mask, the report covers the picked cell and states that waves are unavailable.
- **S2** Wind: true wind direction (from), speed (kt), Beaufort force and description, and the marine warning level (none / small-craft advisory / gale / storm / hurricane force).
- **S3** Boat: user-set heading (°T), and the boat speed from the boat's polar (S15) or as set (kt). Outputs are the true wind angle, the apparent wind speed and angle, tack (port/starboard), point of sail and VMG toward the wind.
- **S4** Sea: significant wave height, peak period, wavelength, direction the waves come from, Douglas sea state, growth regime (fetch-limited / fully developed / depth-limited / damped by ice), fetch and steepness class.
- **S5** Current: set (direction toward) and drift (kt), with its source: the run's ocean model, a loaded current table (tidal stream atlas, NOAA predictions, CSV such as a Copernicus Marine export) or the estimated wind drift. See [sailing-currents.md](sailing-currents.md). A wind-against-current flag (steeper, breaking seas) when the current opposes the waves by more than 0.5 kt.
- **S6** Weather and visibility: present weather in plain words, and the shipping-forecast visibility word (good / moderate / poor / very poor) from the estimated visibility.
- **S7** Barometer: QNH and its tendency per 3 h, in shipping-forecast words.
- **S8** Water temperature (ocean or SST runs) with a cold-water-shock note below 15 °C.
- **S9** Sail-plan hint from user thresholds: reef above the "reef at" wind (default 18 kt), stay in harbour above the small-craft limit. It is a reminder of the user's own limits, not advice.
- **S10** Trend strip: Beaufort force at the station for every frame.
- **S11** Live, as in F10.
- **S12** Marine forecast line in shipping-forecast style, from the coming 24 h of the run at the station, for example "Southwest 5, veering west gale 8 soon." or "Calm, becoming south 6 soon.". It is followed by the sea state, weather and visibility.
- **S13** The wind above the boat: barbs from the surface to 12 000 ft (where winds aloft are available).
- **S15–S22** Boat polar, sail plan and trim drawn in 2D and 3D, simulated tacks and gybes, weather routing by isochrones (or the direct route), the weather along the sailing route, departure planning and the sail-through. *Route here* in the point read-out plans a passage to the point. See [route-weather-and-sail-routing.md](route-weather-and-sail-routing.md).
- **S14** Reading a point (`pointReadout` in `sailing.js`). Request: in the sailing navigation view, clicking the map shows the data there instead of moving the boat.
  - A click selects a cell, and the boat stays where it is. The report gets a **POINT READ** card with the position, the distance (NM) and true bearing from the boat, and the same quantities as the report at that cell: wind barb, Beaufort force, wind and marine warning; waves (height, state, direction, period) or "land"; current; water temperature; weather and visibility; QNH. A thin strip gives the Beaufort force there over the run.
  - **Actions:** *Move the boat here* (sea cells only), *Steer NNN°* (sets the boat's heading to the bearing of the point), and close. *Boat to start* puts the boat back at the centre.
  - **Scene:** a post in the warning colour at the point, with a card (force, wind barb, wind, waves, current, water, visibility and QNH, headed "NN NM · NNN° from ⛵"), and a dashed line from the boat to the point.

### The views (V)

- **V1** The view switch has two more buttons: **Flight weather** and **Sailing weather**. They are disabled for the space-weather runs (Sun, cosmic dust, solar wind). There are no pop-up panels. The reports are full-width sections below the scene, in the page's layout, like the storm-watch bulletin.
- **V2** Flight weather scene:
  - the ground is coloured by the flight category of every cell (VFR green, MVFR blue, IFR red, LIFR magenta);
  - the winds aloft show as a layer of wind barbs at the chosen level (SFC, 3 000 ft to FL390), over a translucent level sheet with a label giving its strongest wind. Barbs are coloured by speed: < 20, 20–50, 50–80, 80–120, ≥ 120 kt;
  - **All levels stacked** draws every level at once, giving a column of barbs through the atmosphere at each sample point;
  - a **barb mast** at the station carries a larger barb and a `FL340 270/85` label at every level;
  - every aerodrome: windsock, runway on its fixed heading, a ring in its category colour, and a **weather card** over it (category, identifier, runway in use, wind barb and wind, visibility and weather, cloud and ceiling, temperature/dew point, QNH, the main severe hazard). The selected aerodrome's card is larger and outlined;
  - the planned route: a tube at its altitude coloured by the wind component (green tailwind, red headwind), a faint curtain to the ground, barbs along it with the winds met at the time of passage, and labels over hazard stretches;
  - cloud volumes can be switched on (off by default, because deep Cb decks hide the wind layers).
- **V3** Sailing weather scene:
  - the sea is coloured by the Beaufort force of the surface wind, or by wave height;
  - surface wind barbs cover the map, and isobars, ocean currents (flowing arrows) and the 3D wind sea can be shown;
  - a wind-aloft layer can be added (off by default);
  - the boat, sheeted to the apparent wind, marks the station.
- **V4** One vertical scale: wind layers, cloud volumes and (in these two views) the terrain relief share 6 scene units per 12 km. A level that cuts a ridge is drawn cutting it.
- **V5** No inspector window opens in these views. In Flight weather a click selects the nearest aerodrome; aerodromes never move. In Sailing weather a click reads the point (S14) and the boat stays where it is; the read-out can move the boat there or steer for it. The trend strips are buttons that jump the timeline. Everything follows playback.
- **V6** Layer settings are in the control panel (*01b / Flight weather layers*, *01b / Sailing weather layers*): level, all levels, barb density, mast, sheets, labels, category ground, clouds and station marker; or sea colour, barbs, wind aloft, isobars, currents, waves, clouds and boat.
- **V7** Over the sea, markers stand on the drawn water surface. The boat heaves and rolls with the waves and the windsock flutters. Animation stops under `prefers-reduced-motion`. In the 2D fallback the reports work without the 3D layers.

## 3. Derivations

- Wind from components: `from = atan2(−u, −v)` (meteorological, true). Speed in kt = m/s × 1.943 84.
- Dew point (inverse Magnus, engine coefficients): `γ = ln(RH) + 17.625 t / (243.04 + t)`, `Td = 243.04 γ / (17.625 − γ)`.
- Pressure altitude (ft): `145 366.45 (1 − (p / 1013.25 hPa)^0.190 284)`.
- Density altitude (ft): the ISA height with the same density, `44 330.8 m × (1 − (ρ / 1.225)^0.234 97)` (ISA troposphere `ρ ∝ (1 − h / 44 330.8 m)^4.2559`).
- Station pressure from QNH and elevation: `p = QNH (1 − 2.255 77 × 10⁻⁵ h)^5.255 88`.
- Apparent wind: `A = W_true − V_boat` as air-motion vectors. AWS = |A|; AWA = angle between the bow and the direction A comes from.
- VMG to windward: `V_boat cos(TWA)`.
- Wave steepness: `s = 2π Hs / (g Tp²)`. Gentle < 0.025, typical wind sea 0.025–0.04, steep > 0.04. The fully developed Pierson–Moskowitz sea is ≈ 0.03.
- Icing band: inside the cloud, between the freezing level and the −20 °C level. The −20 °C level is interpolated linearly between the cloud-base and cloud-top temperatures, or taken from the ISA lapse rate 6.5 K/km when those are absent.

## 3a. Winds aloft

The engine is a single-layer model and has no upper-air wind. The winds aloft are diagnosed with the balance the engine's
severe-convection kernel already uses for its shear (`src/weather/severe_convection.cpp`). The same constants and
stencil are used:

- **Geostrophic wind** of the pressure field, `V_g = (1/(ρ f)) k × ∇p`:
  - on an f-plane at the grid anchor (Ω = 7.292 1159 × 10⁻⁵ s⁻¹), with the kernel's density (severe config, default 1.2 kg/m³);
  - central differences inside the grid, one-sided at its edges.
- **Boundary layer** (0–1 000 m): a straight blend from the engine's 10 m wind to `V_g`. This is the straight low-level hodograph the kernel assumes.
- **Thermal wind** of the temperature gradient, `dV/dz = (g/(f T)) k × ∇T` with g = 9.806 65 m/s². Over terrain the temperature is first reduced to sea level at 6.5 K/km, because the engine's surface temperature follows the terrain.
- **Depth by scale (quasi-geostrophic).** A surface temperature anomaly of horizontal scale L is in balance only over the depth `H = |f| L / N`, the Prandtl ratio of the Eady / surface-QG problem, with N = 0.01 s⁻¹. Its wind therefore grows as `rate · H · (1 − e^(−z/H))`.
  - The temperature field is split into the domain-wide least-squares plane, taken as synoptic and balanced through the whole troposphere, plus Gaussian scale bands of 100, 200, 400, 800 and 1 600 km.
  - Near the ground every band gives the kernel's local thermal wind. Aloft, only the large scales keep growing.
  - Without this split, the sharp edges of the synthetic air masses and the shallow foehn and cold-pool contrasts of the Alps would be extrapolated to the tropopause. That gave 400–1 150 kt "jets". With it, the strongest winds aloft in the bundled runs are 66–145 kt.
- **Tropopause** at 11 km: the thermal wind stops growing above it, where the jet core sits.
- **Temperatures aloft:** the surface temperature cooled at 6.5 K/km to the tropopause.
- **Tropical cyclones:** the storm's extra surface wind over the background weakens linearly to zero at 12 km (a warm-core vortex).
- **Altitudes:** the FB levels are taken as altitudes above mean sea level. Flight levels are treated as ISA altitudes. Heights above the ground subtract the terrain elevation.

**Validation against the engine** (`windsAloft.test.js`):
- For a planar temperature field the thermal wind equals the kernel's formula exactly at every height.
- On the supercell run the 0–6 km shear matches the kernel's `deep_shear_m_s` to within 4 × 10⁻⁴ m/s in a quarter of all cell-frames, with a median difference of 0.09 m/s. It differs, by design, where sharp small-scale gradients sit.
- No bundled run exceeds 250 kt aloft.

Without a pressure field (ocean, coast, polar, hydrology and demo runs) the winds aloft are not available, and the
report says so. At the equator (f = 0) the wind neither turns nor strengthens with height.

## 3b. Aerodromes

The runs are synthetic, so real airports would sit in the wrong places (in a synthetic sea, on a synthetic ridge).
Each run therefore gets a fixed set of synthetic aerodromes, sited deterministically from the run itself
(`airports.js`):

- on land: by the run's land/sea mask where it has one, otherwise by the country outlines (`viewer/public/geo/countries-110m.geojson`, Natural Earth 110 m, `landMaskFromCountries`), so that no aerodrome is placed in the sea; at least one cell in from the edge;
- on low ground, within the lowest 40 % of the land where the run has terrain;
- spread over the map: the first nearest the centre, each further one as far as possible from those chosen (farthest-point sampling). There are 3–6, by the size of the map;
- the runway along the site's **prevailing wind** over the whole run (the vector-mean surface wind, to 10°), which is how runways are oriented;
- identifiers `XA01`, `XA02`, …: the letter X is used by no ICAO region, so they cannot be mistaken for real airports. The names give the position on the map ("North-west field").

The set depends only on the run, never on the frame shown or on clicks.

## 3c. Flight planning

`planFlight` (`flightPlan.js`) flies the route through the simulation's timeline:

- **Route:** the straight line between the two aerodromes on the grid's azimuthal-equidistant map, which is close to the great circle over regional grids. Courses and winds are both in the grid's frame (true at its anchor). It is sampled about every half cell, with 12–80 segments.
- **Vertical profile:** climb and descent at 300 ft per NM (the "3 to 1" rule). Cruise is capped where the route is too short to reach it, and the plan says so.
- **Each segment:**
  - the wind at its altitude and position from the frame valid nearest to the time the aircraft gets there (`windsAloft.js`; the surface wind where the run has no pressure field, which is said);
  - the wind triangle: wind correction angle `asin(crosswind / TAS)`, heading and ground speed `TAS cos(WCA) − headwind`. It is impossible when the crosswind exceeds the TAS;
  - the hazards at that point and altitude: thunderstorm/CB (cumulonimbus or lightning), tropical-storm winds (≥ 34 kt), icing (inside the cloud's 0 to −20 °C band), cloud (IMC), clear-air turbulence (vertical shear ≥ 6 kt per 1 000 ft over ±1 000 ft), and terrain clearance in cruise (1 000 ft over the corridor's highest terrain, 2 000 ft over terrain from 3 000 ft; simplified, not a published minimum altitude).
- **Time, fuel:** the segment times add up to the time en route and the ETA. Trip fuel is at the burn rate set; the reserve is 45 minutes at that rate (the IFR reserve of 14 CFR 91.167).
- **Departure and destination:** their reports at departure and at the ETA (the nearest frame), each on its runway in use.
- **Alternate:** needed when the destination is IFR/LIFR at the ETA or its crosswind exceeds the limit. It is the nearest other aerodrome that is VFR/MVFR with an acceptable crosswind when the aircraft would get there.
- **Best level:** the same route at every winds-aloft level through the same timeline. The quickest level reached without terrain conflict, and without icing where possible, is recommended.
- **Checks and verdict:** severe items (CB on the route, tropical-storm winds, terrain) make the plan "Not advisable as planned". Warnings (IFR/LIFR at an end, crosswind over the limit, icing) make it "Possible with conditions".
- **Cross-section:** 3–9 points along the route, each with the weather when the aircraft passes it. Clouds and icing are drawn with their MSL heights (cloud heights above ground plus the elevation), along with the freezing level, the corridor terrain and barbs at every level.

The plan ignores airspace and uses no aircraft performance model beyond a constant TAS and a fixed climb gradient. It
uses simulated weather and is not for real flight planning.

## 3d. Geodesy

- **The sphere:** radius R = 6 371 008.8 m, as the engine's projection uses. Distances use the haversine central angle (1 NM = 1 852 m). The initial course is `atan2(sin Δλ cos φ₂, cos φ₁ sin φ₂ − sin φ₁ cos φ₂ cos Δλ)`; the final course is the reverse of the initial course from the destination. Intermediate points are found by spherical linear interpolation.
- **Accuracy:** the sphere differs from the WGS-84 ellipsoid by up to about 0.5 % in distance; for example, New York JFK to London Heathrow comes out at 2 991 NM against about 3 000 NM on the ellipsoid. Courses are true, not magnetic: no variation model is used.
- **Projection:** the forward projection is the engine's spherical azimuthal-equidistant projection about the grid anchor (`observation_grid.cpp`: haversine central angle, scale `c / sin c`). A point lies in cell `(floor((x − x₀)/Δx), floor((y − y₀)/Δy))` when `x₀ ≤ x < x₀ + nₓΔx` (and likewise for y), the engine's rule. The inverse is exact to rounding.

## 3e. GAFOR

The VFR view with the German GAFOR categories, and GAFOR along the planned route, are described in [gafor-vfr.md](gafor-vfr.md).

## 4. Limits

Single-layer engine state. The winds aloft are a balance diagnostic (§3a), not an upper-air model: there are no
jet-stream dynamics or stratospheric temperature reversal, and a single surface temperature field stands for the
troposphere. There is one cloud layer. Visibility is
estimated and gusts are not modelled. Swell, tides and refraction are absent. The sailing point-of-sail bands and the
steepness classes are customary rules of thumb. The warning thresholds are settings. None of this replaces an official
METAR/TAF, a marine forecast or a pre-flight briefing.

## 5. Verification

`npm test` (in `viewer/`) runs the report tests with the rest of the viewer suite:

- `station.test.js`: the inverse projection against the forward projection, wind conventions, cyclone wind and hazard levels, Beaufort limits, dew point against the Magnus form, pressures (storm anomaly, ISA reduction, orography), cloud cover in oktas, present weather, visibility behaviour and the pressure tendency (including the static-field flag).
- `aviation.test.js`: every METAR group against the FM 15 / Annex 3 rules, flight-category boundaries, runway components, ISA pressure and density altitude, the icing band, complete METAR lines (including CAVOK and the all-solidi report of a run without moisture, pressure or cloud), hazards, and the hurricane and Alps engine fixtures.
- `sailing.test.js`: the point read-out (distance and bearing from the boat, sea and land, the same weather as the report, the scene card); marine-forecast wording (veering, backing, increasing, decreasing, calm, timing, named forces); apparent wind (beating, running, outrunning a tailwind), points of sail, warnings, shipping-forecast words, steepness, snapping to the sea, currents against the waves, the barometer, and the ocean and hurricane fixtures.
- `windsAloft.test.js`:
  - FB levels, the engine stencil and the geostrophic wind;
  - the boundary-layer blend and the planar thermal wind against the engine formula, the tropopause cap and the ISA temperatures aloft;
  - the quasi-geostrophic decay of a shallow anomaly, and the no-rotation case;
  - the FB rules near the ground and the FB code;
  - the supercell deep-shear agreement, jet-speed bounds on all coupled runs, and the cyclone weakening with height.
- `barbs.test.js`: barb parts and rounding, staff direction, the hemisphere side, the order of pennants and barbs, the lone half barb, SVG coordinates, and the 3D layer mesh.
- `flightPlan.test.js`:
  - aerodrome siting (land, low ground, edges, spread, runway along the prevailing wind, deterministic, independent of the frame);
  - the wind triangle, the climb/descent profile and frames by time;
  - a plan through a changing timeline, with destination at the ETA, alternate, thunderstorm and icing checks, best level, and terrain clearance;
  - the engine-fixture plans (legs add up to distance and time, no tailwind on the runway in use);
  - the weather card content.
- `flightPlan.test.js` (fly-through): the aircraft's state through the flight (climb at 300 ft/NM times the ground speed, TOC/TOD, cruise, the frame of the weather met, monotone progress, time), and the hazard around it and the next one ahead with its distance and minutes.
- `geo.test.js`: every coordinate notation and the error messages, chart and ICAO formats, meridian, equator and JFK–LHR great circles, intermediate points, the projection's anchor and inverse, and the cell rule at the edges.
- `weatherLayers.test.js`: the symbol priority (thunder/hail, what falls, fog/mist, sky cover, nothing without a model), the column (freezing level from the kernel or the lapse rate, cloud, icing inside the cloud, the storm column to the cloud top, turbulent bands from the shear), the cloud families, and painting every symbol.
- `flightPlan.test.js` (coordinates): great-circle legs through a waypoint, the log's checkpoints in order, the path through the waypoint, the turn in both frames, refusal outside the area and of repeated points.
- `flightPlan.test.js` (flight rules): VFR under a clear sky (GAFOR Charlie at both ends, the band up to the FL100 cap), IFR required under a 300 ft overcast (X-ray at both ends, the stretch without VFR airspace named), not advisable through a thunderstorm line, marginal under a 900 ft deck (Mike, a band under 500 ft).
- `flightPlan.test.js` (return trip): out on day 1 and back on day 3 along the reversed route, VFR both ways once the cloud lifts, IFR on the return while it is low, a return before the outbound lands refused, the hours on the ground, and the windows of the return day (IFR, then VFR).
- `geo.test.js` (land): land from the country outlines (a test square; Germany as land, the North Sea and the Baltic as sea in the shipped Natural Earth file), used to site aerodromes on land in runs without a land/sea mask.
- `stationMarkers.test.js`: the aircraft's nose along its heading, pitch up in a climb, and red/green nav lights on the left and right. In world coordinates, the sock points downwind, the runway lies along its heading, the bow points along the heading, the sail sits to leeward of the apparent wind and the masthead arrow points into it. It also checks the App wiring.
