# Currents for the sailing weather v1

The sailing view now has a current everywhere on the water. It is taken from:

- the run's ocean model, where it has one;
- otherwise, current tables the user loads from public sources (tidal stream atlases and chart tidal diamonds, NOAA current predictions, or model currents such as Copernicus Marine exported to CSV);
- otherwise, an estimate of the wind-driven drift.

The current is shown in the report, the point read-out and the map (arrows, or the sea coloured by current speed), and it is used by the route planning.

| Part | Files |
| --- | --- |
| Model and parsers | `viewer/src/currents.js` (`currents.test.js`) |
| Use | `sailing.js` (report, point read-out), `sailRoute.js` (routing), `App.vue` (*01c / Currents*, map, arrows, stations) |
| Converter | `tools/currents_to_csv.py` (`tests/tools/test_currents_to_csv.py`) |
| Examples | `viewer/public/currents/` (synthetic, in the two formats) |

> **Not for navigation.** The wind drift is a rule-of-thumb estimate. The example tables are **synthetic**: they
> demonstrate the formats and are not taken from any atlas or model. Replace them with published data you are
> entitled to use.

## 1. Requirements research

### 1.1 The request

| Request | Interpretation |
| --- | --- |
| "add the currents for the sailing weather" | A current (set and drift) at every sea point, in the report, the point read-out and the map, and in the routing (current triangle, SOG and COG). Before this, currents existed only in runs with the engine's ocean model (the Atlantic, polar and carbon examples). |
| "for convenience reasons those currents can be also taken from public available tables" | Load current tables from public sources in their own formats, or converted, without running an ocean model. |

### 1.2 What sailors use, and what is publicly available

| Source | What it gives | Access and licence | How it gets into the viewer |
| --- | --- | --- | --- |
| **Tidal stream atlases and chart tidal diamonds** (UKHO Admiralty NP2xx, BSH *Gezeitenstromatlas*, SHOM atlases) | For each point, the set and rate (springs and neaps) every hour from HW −6 h to HW +6 h at a reference port. The standard form for tidal streams in European waters | Published products. Crown, BSH or SHOM copyright: not open data, but usable for one's own navigation. The values are transcribed | The **tidal stream table** (JSON, §2.3). The high-water times and ranges come from the reference port's tide table |
| **NOAA CO-OPS tidal current predictions** (USA) | Predictions at about 3 000 current stations: 6-minute, hourly or slack/max-flood/max-ebb events, speed along the major axis with the flood and ebb directions | **Public domain** (U.S. Government). API: `api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=currents_predictions&station=…&time_zone=gmt&units=english&format=json` | **Directly (JSON)**. The station position comes from the metadata API (`mdapi/prod/webapi/stations/<id>.json`); `currents_to_csv.py noaa-merge` adds it to the file, or it can be entered |
| **Copernicus Marine Service** (EU) | Analysis and 10-day forecasts of currents (variables `uo`, `vo`, m/s), hourly at the surface. Products include: global (1/12°); Baltic Sea (BALTICSEA_ANALYSISFORECAST_PHY_003_006, ~1 NM); North-West European Shelf (NWSHELF_ANALYSISFORECAST_PHY_004_013, ~1.5 km, with tides); Iberia-Biscay-Ireland; Mediterranean | Free with registration. The Copernicus Marine licence requires attribution ("E.U. Copernicus Marine Service Information") | Subset with the Copernicus Marine Toolbox (`copernicusmarine subset --dataset-id … -v uo -v vo --minimum-longitude … --start-datetime …`) to NetCDF, then `currents_to_csv.py netcdf` to CSV. The viewer also reads `uo`/`vo` CSV columns directly |
| **DMI open data** (Denmark) | Ocean model forecasts for the North Sea and Baltic (currents, water level) | Free open data (DMI Open Data API, CC BY 4.0); check the current product list | GRIB: convert to NetCDF or CSV (`cfgrib`/`eccodes`), then as for Copernicus |
| **NOAA RTOFS / HYCOM, NASA OSCAR** | Global model and altimetry currents (1/12° to 1/4°, daily) | Public domain | NetCDF, then `currents_to_csv.py netcdf`. Offshore passages only: too coarse for coasts and tides |
| Tidal current harmonic constituents (NOAA `harcon`) | Amplitude and phase per constituent along the major axis | Public domain | Not read directly: use the predictions, which NOAA computes from them |

### 1.3 Conventions and methods

- **Set and drift:** the set is the direction the current flows **towards** (°T), and the drift its speed (kt), as on charts and in atlases. The opposite of the wind's "from".
- **Tidal stream atlas** (the Admiralty "computation of rates"):
  - the hours from the nearest high water at the reference port (±6 h, up to 6.5 h accepted);
  - set and rates interpolated between the hours (the set on the circle);
  - the rate between neaps and springs by that tide's range: rate = neap + (spring − neap) × (range − neap range) / (spring range − neap range), allowed from −20 % to +120 % and at least 0.
- **NOAA predictions:**
  - *6-minute or hourly values:* flood positive along `meanFloodDir`, ebb negative along `meanEbbDir`, interpolated linearly in time (u, v).
  - *Slack, maximum flood and maximum ebb events (`interval=MAX_SLACK`):* a sine curve between slack and the maximum (NOAA Tidal Current Tables, Table 3 method).
  - *Units:* knots with `units=english`, cm/s with `units=metric`.
- **CSV series:**
  - *Interpolation:* linear in time, without crossing gaps over 6 h. Nothing outside the series.
  - *Columns:* `u`/`v` (m/s), `uo`/`vo`, `u_kt`/`v_kt`, or `set` with `drift_kt`, `speed_ms` or `speed_cms`.
  - *Comment lines:* `# kind: tidal|total`, `# synthetic: true` and `# source: …` describe the table.
- **In space:**
  - the stations within a reach R (30 km by default) of a point are weighted by 1/d² (at least 200 m);
  - the coverage w = 1 − d/R of the nearest station says how much the table replaces the rest;
  - a **total** table (model or measured currents) replaces the drift by w;
  - a **tidal** table (atlas, NOAA predictions) is the tidal stream only, and the wind drift is added to it.
- **Wind drift** (no tables, no ocean model):
  - 2.5 % of the 10 m wind, turned 20° to the right of the wind in the northern hemisphere (left in the southern). This is the usual sailor's rule of 2–3 %, within the observed 10–45° deflection (Ekman: 45° in deep water, less near the surface and in shallow seas).
  - It builds up with a 6-hour lag: a first-order relaxation to the mean wind of each interval between frames, starting from the first frame's wind.
  - It is estimated on the water only. There are no tides, and no currents from sea-level differences.
- **Priority at a point:** the ocean model (where it has water) comes first; then the tables (if loaded and used); then the wind drift (if on).

### 1.4 Functional requirements

- **C1:** the current at every sea point and time, with its source. It feeds:
  - *the sailing report:* set (compass and degrees), drift and the source; wind against current;
  - *the point read-out;*
  - *the sail-through display;*
  - *the weather strip of the route.*
- **C2 Map:** current arrows (cyan ocean model, gold table, grey wind drift), the sea coloured by current speed (a *Sea colour* option), and the table stations as gold diamonds with the name and the current now.
- **C3 Routing:** the weather model of the routing takes the current at every place and time. The heading corrects for the cross-current, and SOG and COG follow.
- **C4 Tables** (*01c / Currents*):
  - *Loading:* a CSV, NOAA JSON or tidal stream JSON. For a CSV the user says whether it holds the total current or the tidal stream only; a NOAA file without a position takes the one entered.
  - *The list:* format, kind, stations, the stations inside this run and the hours covered, with "does not cover this run: no effect" when it does not apply, and Remove.
  - *Settings:* the reach, the wind drift on or off, and the tables on or off.
  - *Errors* are said in words.
- **C5 Examples:**
  - the **Baltic straits** CSV (synthetic inflow through the Fehmarn Belt, Kadet Channel, Langelandsbælt and Drogden, for the Baltic sailing example, which loads it);
  - the **German Bight** tidal stream table (synthetic, HW Helgoland, for the Germany run's North Sea coast).

  Both are marked synthetic in the file and in the list.
- **C6 Converter:** NetCDF (Copernicus Marine and other CF current fields) to CSV, NOAA predictions merged with the station position, and the NOAA URLs.

### 1.5 Limits

- *Wind drift:* an estimate, with no tide, no seiches and no inflow or outflow through straits.
- *Tables:* values between stations and beyond the reach are interpolated or replaced, not modelled.
- *Coast:* the land/sea mask of the run (or the 1:110m country outlines) decides where water is, so currents in narrow straits fall on the nearest sea cells.

## 2. Verification

`npm test`:

- **`currents.test.js`:**
  - *Set and drift.*
  - *CSV:* set/drift, u/v, knots, semicolons, comments, stations, interpolation and gaps, errors.
  - *NOAA predictions:* flood and ebb, cm/s, the missing position, the slack/max sine curve.
  - *Tidal stream atlas:* slack at HW, springs and neaps by the range, the set across north, outside the HW window, errors.
  - *Wind drift:* 2.5 %, 20° right (left in the south), the 6-hour lag, none on land.
  - *The model:* priority ocean > table > drift; a total table replacing the drift within its reach; a tidal table adding it; nothing known; the frame field; the report reading; coverage.
  - *The shipped examples:* coverage of their runs, table near the straits, drift elsewhere, none on land.
- **`sailing.test.js`, `sailRoute.test.js`:** the current triangle with a current field.

`python3 -m unittest discover -s tests/tools` (`test_currents_to_csv.py`): CF times; a NetCDF-3 current field with a fill value and longitude wrap to CSV, and the bounding box; NOAA merge and URLs.
