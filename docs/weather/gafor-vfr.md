# VFR view with GAFOR categories v1

A viewer view (**VFR · GAFOR**) that classifies the simulated weather in the categories of the German General Aviation
Forecast (GAFOR) and shows the airspace usable under VFR: clear of cloud, with the ground in sight. It also provides a
contract for the official GAFOR areas, and an engine example run over Germany.

Implementation:

- `viewer/src/gafor.js`: categories, VFR airspace and areas.
- `viewer/src/VfrReport.vue`: the report below the scene.
- `viewer/src/geography.js` and `viewer/public/geo/countries-110m.geojson`: borders and cities for orientation.
- `examples/germany-vfr-request.json` and `viewer/public/gafor-germany/engine-timeline.json`: the example run.
- Tests: `viewer/src/gafor.test.js` and `tests/tools/germany_vfr_check.py` (CTest `weather.germany_vfr_cli`).

> Simulated weather from synthetic inputs: not a GAFOR forecast and not for flight planning.

## 1. Categories (as confirmed for this project)

| Category | Colour | Visibility | Main cloud base hs | Meaning / VFR status |
| --- | --- | --- | --- | --- |
| Charlie (C) | blue | ≥ 10 km | ≥ 5 000 ft | Clear: best VFR weather |
| Oscar (O) | green | ≥ 8 km | ≥ 2 000 ft | Open: good VFR conditions |
| Delta (D) | yellow | 5 000 m ≤ vis < 8 000 m | 1 000 ft ≤ hs < 2 000 ft | Difficult: extra planning required |
| Mike (M) | orange | 1 500 m ≤ vis < 5 000 m | 500 ft ≤ hs < 1 000 ft | Marginal: sub-optimal, high workload |
| X-ray (X) | red | < 1 500 m | < 500 ft | Closed: VFR flight practically impossible |

- The category of a place is the **worse** of its visibility category and its cloud-base category.
- Where one element is unknown, the category rests on the other and is marked partial. Where both are unknown, it is "unknown" (grey).

**hs, the main cloud base** ("Hauptwolkenuntergrenze"):
- It is the base of the lowest cloud layer covering **more than 4/8** of the sky (BKN or OVC). With no such layer it is unlimited.
- It is measured above the ground at a grid cell, and above the area's **reference altitude** for a GAFOR area.
- *Assumption to confirm:* "more than 4/8" (BKN/OVC) is how this project reads the German definition.

**Inputs:** visibility is the viewer's estimate (the engine has no visibility; see
[flight-sailing-reports.md](flight-sailing-reports.md)). Cloud comes from the engine's cloud-formation kernel, one layer
per cell.

## 2. Clear of cloud, ground in sight

The VFR airspace at a point (`vfrSpace`), after SERA.5001 (VMC minima) and SERA.5005(f) (minimum height):

- **Bottom:** 500 ft above the ground. The 1 000 ft over congested areas and night VFR are not modelled.
- **Low-level band:** up to 3 000 ft AMSL or 1 000 ft above the ground, whichever is higher. Here you may fly up to the cloud base, clear of cloud with the surface in sight, with 5 km visibility (1 500 m for slow aircraft).
- **Above the band:** 1 000 ft below the cloud (and 1 500 m horizontally) with 5 km visibility.
- **Above a BKN/OVC deck:** no VFR, because the ground is not in sight.
- **Cap:** the space stops at FL100, where 8 km visibility would apply.
- **Visibility:** below 1 500 m there is no VFR airspace; between 1 500 m and 5 km only the low band remains.

Airspace classes and national variations are not modelled. These are the class G low-level rules and the general
minima.

In the scene each point shows a **column** from the bottom to the top of its VFR airspace. Its height is the room below
the cloud and its colour is the GAFOR category. Where there is no room, a flat red tile is drawn. The main cloud deck is
drawn above the column, alongside the weather symbols (with night variants from the sun's elevation: NOAA
approximation, sunrise and sunset within a few minutes), the country borders and the cities.

## 3. GAFOR areas

**No areas are built in.** The official GAFOR areas (code, name, reference altitude and boundary) are published by the DWD
and in the German AIP, but they could not be retrieved from the development environment. The viewer therefore loads them
from a file, so they always match the official publication.

File contract (*Load GAFOR areas* in the view's control panel):

- a GeoJSON `FeatureCollection`;
- one `Polygon` or `MultiPolygon` per area, in longitude and latitude (WGS 84), with holes allowed;
- properties `id` (the area code), `name`, and `reference_altitude_ft` (the reference altitude, "Bezugshöhe"). The aliases `code`/`gafor`/`area` and `reference_ft`/`bezugshoehe_ft`/`ref_ft` are accepted;
- codes must be unique, and reference altitudes must lie between −1 000 and 15 000 ft. Errors are reported with the feature concerned.

With areas loaded:

- **In the scene:** each area is outlined in the colour of its prevailing category and labelled with its code, its prevailing category and its worst category.
- **In the report:** a GAFOR-style table shows every area against every 2-hour frame. Each cell gives the prevailing category and, in small type, the worst where they differ. Clicking a cell jumps to that frame, and areas outside the run's grid are listed.
- **Classification:** a cell belongs to an area when its centre lies inside the polygon (even-odd rule). The cloud base is taken above the area's reference altitude: hs = terrain + base above ground − reference altitude.

## 4. The example run over Germany

`examples/germany-vfr-request.json` re-anchors the engine's cloud-formation configuration over Germany:

- 20 × 24 cells of 40 km about 51.2° N 10.4° E;
- 72 h from Monday 2 November 2026 00Z to Thursday 5 November 00Z, with snapshots every 2 h (the length of a GAFOR period), giving 37 frames, so that a Monday outbound and Wednesday return can be planned;
- a low over the North Sea and Denmark, a high over the south-east, a mild moist sector in the south-west against cold dry air in the north-east, and a 4.5 K diurnal heating cycle;
- an initial state below saturation everywhere, checked by CTest.

Regenerate with `terraflux-run-coupled-wind examples/germany-vfr-request.json viewer/public/gafor-germany/engine-timeline.json`. CTest `weather.germany_vfr_cli` compares the output with the fixture.

**What the run shows:**
- clearer nights (Charlie/Oscar/Delta);
- widespread mist and showers every morning from about 04Z to 12Z (Mike, visibility down to about 1.8 km);
- mixed afternoons;
- frontal low cloud with main cloud bases down to about 1 100 ft.

**What it cannot show:** X-ray fog. The coupled model has no radiative cooling of the surface layer and no surface
moisture source, so from a sub-saturated start the air stops short of fog. The inputs are synthetic, and the terrain is
flat (no elevation data): the grid is placed over Germany, but the weather is not a reconstruction of a real day.

## 5. Flight planning

The flight planner (see [flight-sailing-reports.md](flight-sailing-reports.md)) adds GAFOR along the route:

- the category of every segment at the time it is flown;
- the worst category per navigation-log row, and over the whole route;
- planning checks for stretches in X-ray (warning) and Mike (caution);
- the current category in the fly-through display.
- the **flight rules** of the plan: *VFR possible* (a VFR band from 500 ft over the terrain up to clear of cloud on every segment, Charlie/Oscar/Delta at both ends and en route), *VFR marginal* (Mike, or a band under 500 ft), *IFR required* (X-ray, or no band) or *Not advisable* (thunderstorms, tropical-storm winds);
- a **return trip** on a later day (the route reversed, at a chosen day and time), judged in the same way, with a verdict for the round trip and the flight rules for every departure time of the outbound and return days.

In the example run, on the default route XA01 → XA02, a Monday outbound at 08Z is *VFR marginal* (Mike at both ends)
and a Wednesday return at 14Z is *VFR possible*. On both days, departures from early afternoon until about midnight are VFR. The run never drops below Mike, so it never
shows *IFR required* (see §4); the unit tests exercise that case.

## 6. Orientation data

Country borders are Natural Earth 1:110m Admin 0 (public domain), extracted by `tools/make_country_outlines.py`. City
positions are listed in `geography.js`. Both are coarse, and for orientation only: they are not boundaries of any
aviation area.
