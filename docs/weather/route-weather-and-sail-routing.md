# Weather along the route, sail settings, manoeuvres and weather routing v1

This document covers four requests:

- the **whole weather along a planned flight route**, not only at the departure and destination aerodromes;
- the same for **sailing**;
- **drawing the sail settings** for the conditions and **simulating a turn** (tack or gybe);
- **route planning for sailing by the weather forecast** (weather routing).

It follows [flight-sailing-reports.md](flight-sailing-reports.md) (the flight and sailing views) and
[gafor-vfr.md](gafor-vfr.md) (GAFOR and VFR).

**Code:**

| File | Contents |
| --- | --- |
| `viewer/src/routeWeather.js` | Flight route weather |
| `viewer/src/polars.js` | Boat performance |
| `viewer/src/sailTrim.js` | Sail plan and trim |
| `viewer/src/manoeuvre.js` | Tack and gybe simulation |
| `viewer/src/sailRoute.js` | Weather routing and sailing route weather |

**Components:**

| Component | Contents |
| --- | --- |
| `RouteWeather.vue` | Flight route weather |
| `SailTrim.vue` | Sails, trim and the manoeuvre |
| `SailPlan.vue` | Sailing route planning |
| `SailOverlay.vue` | The sail-through display |

> **Not for navigation or flight planning.** The weather is the engine's simulation from synthetic inputs. The boat
> polar is a generic illustrative polar, not a measured or certified one.

## 1. Requirements research

### 1.1 Interpretation of the request

| Request | Interpretation | Reason |
| --- | --- | --- |
| "currently the weather is mapped to the arrival and takeoff airfields; can we have the whole weather along a designated route calculated" | Every sample of the route (about half a grid cell) gets the full weather at the time the aircraft passes it. This covers the surface (wind, visibility, present weather, cloud and ceiling, QNH, flight category, GAFOR) and the air at the flight's altitude (wind, temperature, in cloud, icing, turbulence, thunderstorms, freezing level, the VFR band). Stretches of similar weather make up a **route forecast**, and a summary follows the headings of the ICAO low-level area forecast (GAMET). | The departure and destination already had METAR-style reports. The en-route weather was only partly shown, through the hazard ranges and a 3–9 point cross-section. |
| "for the sailing weather this also means adjustments" | A **sailing route**: start (the boat), waypoints and destination. The boat sails it through the forecast, and the weather is reported along the track at the time of passage. | The same principle applies to a passage: the weather is read where the boat will be, when it gets there. |
| "draw the sail settings for the weather conditions" | For every condition the **sail plan** (mainsail reefs, headsail size or gennaker) and the **trim** (boom and headsail sheet angles to leeward, heel, traveller, vang, outhaul, backstay, car). These are drawn in 2D (top view and sail plan) and in 3D on the boat. | A sailor reads the conditions as "what sails, how trimmed". |
| "simulate a turn on this" | A **tack** or **gybe** simulated in time. The heading swings through the wind, the speed drops and recovers, the sails luff and cross, the boom swings and the heel changes sides. It is animated in 2D and 3D, with the time and distance lost. | The two manoeuvres change the sail settings and cost distance; both are essential to beating and to routing. |
| "including route planning by weather forecast" | **Weather routing** by the isochrone method. It uses the boat's polar, the forecast wind (in space and time), currents and waves, avoids land and, if asked, winds and seas above the user's limits. It is compared with the direct route sailed with tacks and gybes. | This is the standard method of commercial and open-source routers. |

### 1.2 Standards and conventions consulted

| Topic | Convention adopted | Used for |
| --- | --- | --- |
| Flight documentation (ICAO Annex 3, ch. 9) | Met information for a flight: upper winds and temperatures, significant en-route weather, aerodrome forecasts for departure, destination and alternates, and warnings along the route | The route weather complements the end reports |
| Low-level area forecast GAMET (ICAO Annex 3, App. 5; Doc 8896) | Section I: SFC WIND (widespread mean above 30 kt), SFC VIS (below 5 000 m, with cause), SIGWX (TS, heavy precipitation, FZRA), MT OBSC (mountains obscured), SIG CLD (BKN/OVC base below 1 000 ft AGL; CB, TCU), ICE, TURB. Section II: WIND/T, CLD, FZLVL, MNM QNH | Headings of the route summary |
| Pre-flight briefing (FAA AIM 7-1; EASA NCO.OP.135) | A standard briefing includes the **en-route forecast in logical order (climb-out, en route, descent)** and the winds aloft | Route forecast by stretch, in the order flown |
| Performance: polar diagram (ORC VPP certificates) | Boat speed through the water by true wind speed (TWS) and true wind angle (TWA). Best VMG up- and downwind, with its angles. Hull speed ≈ 1.34 √LWL(ft) kt | `polars.js` |
| Froude scaling | Speeds of geometrically similar displacement boats scale with √(waterline length) | Boat sizes from one reference polar |
| Weather routing: isochrones (James 1957; Hagiwara 1989) | From the start, fan out headings for a time step, keep the farthest point per bearing sector (the isochrone), repeat until the destination is reached, then trace back. The same scheme is used by OpenCPN weather routing, qtVlm and Expedition | `sailRoute.js` |
| Current | Velocity over the ground = velocity through the water + current | Routing and log (COG/SOG against heading/STW) |
| Reefing practice (sailing manuals, e.g. RYA *Day Skipper*, ASA) | Reef by the **apparent** wind upwind and by the **true** wind downwind, because the apparent wind drops when you bear away. A typical cruiser takes the 1st reef at about 18 kt, the 2nd at about 25 kt and the 3rd at about 30 kt, with storm jib and trysail from gale force. The genoa is reduced in steps as the wind rises. Gennaker or spinnaker only in moderate winds off the wind | `sailTrim.js` (thresholds relative to the user's "reef at" wind) |
| Trim (sail-trim guides) | The mainsail is eased to keep its angle of attack: the boom is near the centreline close-hauled, at about 40–50° on a beam reach and against the shrouds running. The headsail sheet is limited by the lead. Goose-wing (poled-out headsail) on a dead run | `sailTrim.js` |
| Manoeuvres | Tack: the bow turns through the wind and the sails luff, cross and fill; the boat slows and accelerates again. A typical loss is one to a few boat lengths. Gybe: the stern turns through the wind and the boom crosses fast; in strong wind it is dangerous, so sheet the main in, or "chicken gybe" (tack round instead) | `manoeuvre.js` |

### 1.3 Functional requirements

**Flight, weather along the route (F18):**

- **F18.1** Every route sample (about half a cell) is read at the time the aircraft passes it. The reading has two parts:
  - *surface:* wind, present weather in words and symbol, visibility (estimated), cloud (cover, genus, base and top AMSL), ceiling, QNH, flight category and GAFOR;
  - *at the flight's altitude:* wind, temperature and ISA deviation, in cloud (IMC), icing, turbulence, thunderstorm, freezing level, the VFR band, and mountain obscuration (a cloud base at or below the corridor terrain).
- **F18.2** Stretches: consecutive samples with the same weather symbol, flight category, GAFOR and hazards form a stretch. Short stretches are merged, so there are at most 12. Each gives:
  - NM, times and phase (climb, cruise, descent);
  - the weather in words;
  - the worst visibility and lowest cloud base and ceiling;
  - the wind and temperature at the altitude flown;
  - the freezing level and the hazards.
- **F18.3** A summary under the GAMET headings, each with its NM range, or "nil": SFC WIND, SFC VIS, SIGWX, MT OBSC, SIG CLD, ICE, TURB, WIND/T (the range at the altitude flown), CLD (lowest base and highest top), FZLVL (range) and MNM QNH.
- **F18.4** Display:
  - a **route weather strip** along the distance (with the times), with rows for weather symbols, flight category, GAFOR, visibility, ceiling, precipitation, wind barbs at altitude and hazards;
  - the stretch table and the summary;
  - a **continuous cross-section**, with the cloud, icing and freezing level of every sample rather than of 3–9 points;
  - in 3D, weather symbols along the route.

**Sailing:**

- **S15 Polar:**
  - *Boats:* a generic 40 ft cruiser polar (table, bilinear), scaled by Froude for a 30 ft cruiser and a 45 ft performance cruiser.
  - *Best VMG:* upwind and downwind, with the angles, for every wind.
  - *Waves:* a loss in waves from ahead, 1 − k Hs (1 + cos TWA)/2, with k = 0.06 /m for the 40 ft boat and k ∝ 1/LWL, and at least 0.5.
  - *Motoring:* the engine is used when the speed under sail is below a threshold (a setting that can be turned off).
- **S16 Sail plan and trim:**
  - *Effective wind:* the apparent wind when the apparent wind angle is below 90°, otherwise the larger of the true and apparent winds.
  - *Mainsail:* full, 1st, 2nd or 3rd reef at the "reef at" wind R, R + 7 and R + 14 kt; trysail and storm jib from 34 kt (gale); bare poles from 48 kt (storm).
  - *Headsail:* genoa, genoa furled to 75 %, jib, small jib or storm jib, in steps of the same wind. A gennaker (if carried) is set off the wind (TWA ≥ 100°) in TWS ≤ 16 kt with the full main. On a dead run (TWA ≥ 165°) the headsail is goose-winged (poled out to windward).
  - *Trim:* boom angle = clamp(0.75 (|AWA| − 18°), 3°, 85°); headsail sheet = clamp(0.6 (|AWA| − 20°), 8°, 40°). The sails are to leeward: wind over starboard puts them to port.
  - *Heel:* 0.065 AWS² f(|AWA|) × sail-area fraction, at most 30°, with f(A) = cos A + 0.35 sin A up to 90° and 0.35 sin A beyond. This is the heeling part of the sail force: lift across the apparent wind plus drag (about 0.35 of the lift) along it. An illustrative, uncalibrated model: about 10° close-hauled in 12 kt apparent, and 20° in 20 kt with a reef.
  - *Trim words:* traveller, vang, outhaul, backstay and car.
  - *Drawing:* in 2D (top view with the wind arrows, and the sail plan) and on the 3D boat (reefed main, headsail type, boom and sheets, heel).
- **S17 Manoeuvre:**
  - *Turn:* a tack or gybe (the side is mirrored) or a turn to a heading. The heading follows an eased S-curve at the boat's turn rate.
  - *Speed:* follows the polar with a first-order lag (τ, per boat), with drag while turning. The speed under sail is nil in the no-go zone.
  - *Sails:* they luff below 20° AWA. The boom crosses when the apparent wind crosses, rate-limited (fast in a gybe).
  - *Heel:* the heel changes sides.
  - *Output:* samples every 0.25 s. Duration, least speed, and the distance and time lost against steady VMG (in boat lengths).
  - *Warnings:* a gybe in 25 kt or more (suggest a chicken gybe), and a tack in seas from 2.5 m.
  - *Animation:* in 2D and 3D. The boat's heading setting takes the new heading.
- **S18 Routing:**
  - *Modes:* **weather-routed** (isochrones) or **direct** (the legs as planned, beating or running in tacks or gybes within a corridor around the rhumb line where the leg is too close to the wind).
  - *Inputs:*
    - wind interpolated bilinearly in space and linearly in time between frames (the tropical-cyclone wind where stronger);
    - the current at every place and time from the current model (ocean model, loaded tables or wind drift; [sailing-currents.md](sailing-currents.md));
    - waves from the viewer's wind-sea estimate per frame;
    - land avoided (the run's land/sea mask, otherwise the country outlines);
    - optionally, winds above the small-craft limit and seas above the sea limit avoided (points beyond them are not kept). When no route can keep them, the route is computed without them and says so.
  - *Manoeuvre penalty:* a time penalty for every tack or gybe, taken from the manoeuvre simulation.
  - *Waypoints:* sailed in order.
  - *Output:*
    - the track, with time, position, heading, COG, STW and SOG;
    - along the track: TWS, TWD, TWA, AWS, AWA, point of sail and tack, sail plan, Beaufort force and warning, waves, current and weather;
    - the manoeuvres;
    - the isochrones;
    - a summary: distances, time, ETA, average speed, tacks, gybes, motoring, strongest wind and highest seas, and time beyond the run.
- **S19 Weather along the sailing route:** stretches by sail plan, point of sail and Beaufort force, and a strip over time with rows for Beaufort force, wind barbs, point of sail, sails, waves and weather.
- **S20 Departure planning:** the route for every departure frame within 48 h either side of the departure chosen, giving the duration, strongest wind and highest seas for each. The best departure (the shortest time within the limits) is marked, and a click picks it.
- **S21 Sail the route in 3D:**
  - the boat follows the track with its sails and heel;
  - turns are played in real time (slow motion) at each manoeuvre;
  - the run's frame follows the time sailed;
  - a chase view and a display (time, SOG, STW, heading, COG, TWS, TWA, AWS, AWA, point of sail, sails, heel, and the next manoeuvre).
- **S22 The route in 3D:** the track on the water coloured by point of sail, the manoeuvres marked (T, G), the waypoints and destination, the direct route dashed, and the isochrones faint.

### 1.4 Limits

- *Boat:* the polar is generic and illustrative. Leeway, heel-induced speed loss (beyond the polar), sea-state steering and crew fatigue are ignored.
- *Weather:* the wind is the engine's 10 m wind on its grid (20–60 km cells in the examples), with no gusts. Waves are the viewer's local wind-sea estimate (no swell).
- *Isochrones:* the steps are between 15 min and 3 h, and the headings 10° apart. The route is optimal only to that resolution, and sector pruning can miss routes around large obstacles.
- *Flight route weather:* it has the resolution of the engine grid and the engine's single cloud layer.
- *Coast:* where a run has no land/sea mask, the coast comes from the 1:110m country outlines. Small islands are missing, and harbours and narrow passages are not resolved.

## 2. Derivations and implementation

### 2.1 Flight route weather (`routeWeather.js`)

- **Samples:** the samples are the plan's segments (`flightPlan.js`, about half a cell, at most ~160), each already flown at its time. The surface reading comes from the frame the segment uses:
  - `flightConditions`: visibility, ceiling and category;
  - `weatherSymbol` and `weatherWords`;
  - `surfaceWind`;
  - `pressures` (QNH);
  - `vfrAt`: GAFOR and the VFR band, whose floor is raised to 500 ft over the corridor terrain.
- **Mountains obscured:** a broken or overcast base (AMSL) at or below the highest terrain of the corridor.
- **Stretches:** the key is the symbol, flight category, GAFOR and hazards. While there are more than 12 stretches, the shortest is merged into its shorter neighbour. A stretch's symbol is its most significant one, in the order hail, thunder, snow, sleet, showers, rain, drizzle, fog, mist, overcast, cloudy, partly, few, clear.
- **GAMET thresholds:**
  - SFC WIND from 30 kt;
  - SFC VIS below 5 000 m (severe below 1 500 m, the GAFOR X-ray limit);
  - SIG CLD: BKN/OVC below 1 000 ft AGL, CB (severe) and TCU (caution);
  - TURB: shear at the altitude flown, and severe near thunderstorms.
- **Cross-section:** the cloud, icing and freezing level of every sample. The barbs stay at the 3–9 section points.

### 2.2 Polar, sails and manoeuvres

- **Polar:**
  - *Table:* speeds at TWS 0, 4, 6, 8, 10, 12, 14, 16, 20, 25, 30 and 35 kt and TWA 0, 25, 32, 40, 45, 52, 60, 75, 90, 110, 120, 135, 150, 165 and 180°, interpolated bilinearly and held above 35 kt.
  - *Best VMG:* searched every 0.5°.
  - *Boats:* the 30 ft boat is scaled by √(8.2/11), the 45 ft boat by √(12.6/11) × 1.06.
- **Points of sail:** by |TWA|: in irons below 35°, close-hauled to 55°, close reach to 80°, beam reach to 100°, broad reach to 160°, then running.
- **Manoeuvre:**
  - *Timing:* 3 s on the old course, then the turn over T = max(4 s, |Δheading| / turn rate), with heading = h₀ + Δ·smoothstep(t / T). Δ is the signed change through the wind for a tack, or through the stern for a gybe.
  - *Target speed:* the polar at the current TWA, reduced by up to 30 % while the rudder works.
  - *Speed:* v follows the target with τ (8 s for the 40 ft boat), slowing with 0.7 τ.
  - *Boom:* moves towards its trim angle at no more than 20°/s (tack) or 70°/s (gybe), and to the centreline while the sails luff.
  - *End:* 2 s after the speed is back to 95 % of the polar on the new side.
  - *Loss:* the reference VMG (towards or away from the wind) × duration, minus the progress made along that axis. It is given in metres, boat lengths and seconds. The 40 ft boat loses about 1.5 lengths (8 s) per tack and 0.5 length (2 s) per gybe in 12 kt.

### 2.3 Routing (`sailRoute.js`)

- **Isochrone step:** the expected passage (at 5 kt) over 30, rounded to 5 min and kept between 15 min and 3 h. A mark is reached when a point can fetch it within one step: straight if the course can be sailed, otherwise on the two VMG headings (the laylines), plus one manoeuvre penalty.
- **Pruning:** points are kept by 3° sector of bearing from the start. The score is the distance from the start, counted only up to the mark's distance, minus 0.1 × the distance still to go. Points more than 1.6 × the start–mark distance + 20 km from the mark are dropped, and so are land crossings (sampled every quarter cell) and points beyond the limits when they are avoided.
- **Smoothing:** consecutive steps on the same side of the wind are joined into straight legs. A join is made only where it stays on the water and is no slower (within 0.2 %). The joined route is sailed again from the start and kept only if it arrives no later.
- **Current:** headings correct for the cross-current, so that the course over the ground is made good (the current triangle, iterated with the polar).
- **Direct route:** steps of 2–20 min along the rhumb line. Where the mark lies inside the best VMG angle, the boat sails at that angle and tacks (or gybes) when its cross-track distance exceeds the corridor: 15 % of the leg, between 1 and 10 NM.
- **Comparison:** a weather-routed passage also reports the direct route from the same time, and the time saved (or "no gain").
- **Log:** every segment becomes a log row:
  - true heading and COG (through the grid's convergence);
  - STW and SOG;
  - TWS, TWD, TWA, AWS, AWA, point of sail and tack;
  - sails, Beaufort force and marine warning;
  - waves, current, weather and visibility, and night (sun more than 0.833° below the horizon).
- **Manoeuvres:** a change of side under sail is a tack (|TWA| < 90°) or a gybe. It is simulated (S17) and played in real time in the sail-through, centred on its time.

### 2.4 The Baltic sailing example

`examples/baltic-sailing-request.json` runs the coupled engine over the western Baltic:

- **Grid:** 28 × 18 cells of 20 km about 54.8° N 12.3° E, from Kiel Bight to Bornholm.
- **Time:** Thursday 4 June 2026 12Z to Monday 8 June 00Z, with 3-hour snapshots (29 frames).
- **Pressure:** a weak low over southern Sweden (−450 Pa) and a high over Poland (+350 Pa). Surface friction is 3 × 10⁻⁵ s⁻¹.
- **Weather:** the engine spins up from rest, so the first half day is calm and then overshoots. From Friday there is a south-westerly of 12–16 kt that veers and backs with the inertial oscillation and the diurnal heating.
- **Sea:** the engine has no land/sea mask for this area. The viewer takes the water between the Natural Earth 1:110m country outlines as the sea, so the coast is coarse and Bornholm and several Danish islands are missing.

Regenerate with `terraflux-run-coupled-wind examples/baltic-sailing-request.json viewer/public/baltic-sailing/engine-timeline.json`. CTest `weather.baltic_sailing_cli` compares the output with the fixture and checks the grid, the 3-hour frames, the wind after the spin-up (median 8–20 kt from 200–280°) and a sub-saturated start.

*Load Baltic sailing example* opens the sailing view. The boat is in Kiel Bight, and a passage to Bornholm is weather-routed from Friday 06Z: about 21 h on a broad reach and run with the gennaker, with two or three gybes. Reversing the route gives the beat home, with tacks, a reefed main and a furled genoa.

## 3. Verification

`npm test` (in `viewer/`):

- **`routeWeather.test.js`:**
  - one reading per route segment, at its frame and in the order flown;
  - clear, thunderstorm and low-stratus stretches with their categories, ceilings, symbols and visibility;
  - in cloud only inside the deck;
  - contiguous stretches;
  - every GAMET heading (SIGWX TS, SIG CLD below 1 000 ft with "down to 300 ft", CB, MNM QNH 1013 hPa, FZLVL), and mountains obscured on a ridge;
  - the Germany fixture.
- **`sailTrim.test.js`:**
  - *Polar:* the table at its points and bilinear between them, symmetric, held above 35 kt, Froude scaling and hull speed; best VMG angles against a brute-force search; the wave factor.
  - *Reefs:* at 18, 25 and 32 kt (and the user's own), trysail and storm jib at gale force, bare poles in a storm, true wind when running.
  - *Headsails:* furling steps, gennaker conditions, goose-wing.
  - *Trim:* sheets to leeward and monotonic with the AWA, most heel close-hauled, less with a reef, luffing.
  - *Tack:* through head to wind; boom and heel change sides; speed lost and regained; 0.5–4 boat lengths.
  - *Gybe:* through stern to wind; the boom crosses in under 3 s; less loss than a tack.
  - *Warnings and penalties:* the warnings, the penalties and the interpolation.
- **`sailRoute.test.js`:**
  - the laylines;
  - a beam reach sailed straight at the polar speed; a beat at the best VMG, never in irons and tacking; a run gybing at the best downwind angle, faster than a dead run;
  - routing no slower than the direct route;
  - an island avoided, with every point of the track on the water, while the direct route reports running aground;
  - a 30 kt band avoided within the limits; limits that cannot be kept said;
  - motoring in a calm;
  - the current triangle (heading into the current, SOG along the course);
  - the sail-through state at the start, at the end and inside a tack's window (boom across);
  - departure scan and best departure;
  - the grid convergence;
  - the Baltic fixture: out off the wind on the water all the way, back beating with tacks, and slower.
- **`stationMarkers.test.js`:** the 3D boat's reefed main and reef bundle, gennaker, trysail, bare poles and heel to leeward, the pose of a turn, and the App wiring.
