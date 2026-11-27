# Viewer control audit: does every selectable control act on the view? v1

Request: "with some of the data points selectable we have the issue it has no influence on the view; can you research
it for all". This document records how the selectable items of the side panel were checked, what was found, what was
changed, and what is left (by design).

## 1. Method

`tools/audit_view_controls.py` drives the viewer in Chromium (Playwright):

- **Examples:** all 17 data examples (Coupled Earth opens its own workspace and is not part of this panel).
- **Views:** every view each example enables, 79 example/view pairs in all, at a frame 60 % into the run. The first frame is often a calm spin-up.
- **Controls:** every enabled control of the panel's "01 …" sections:
  - the field layer list and its toggles;
  - the view's own layer section (01b–01k);
  - the currents section.

  Outside the Field explorer three field layers per view were tried; they act alike there.
- **Procedure:** each control is changed, the 3D scene compared before and after, and the control put back. Each option of a select is tried, and sliders are moved.

The comparison is a **fingerprint of the scene graph**:

- the visible objects and their placement;
- geometry positions and vertex colours;
- instance matrices;
- material colours, opacity and defines;
- shader uniforms (including those on `material.userData`).

It is taken with rendering paused but the per-frame updates running. This needs a development build, which exposes `window.__terrafluxScene` and `window.__terrafluxPauseRender`; the tool's docstring gives the commands. Production builds leave both out.

The tool was made robust during the work:

- controls are found again by section and label each time, because controls appear and disappear as others change;
- labels are compared without their numbers;
- the field layer laid on a view's ground is taken off again after each field-layer test.

## 2. Findings before the changes

| # | Finding | Where | Why |
| --- | --- | --- | --- |
| A | **Picking a field layer changed nothing** | Every view except the Field explorer (14 views) | The field layers colour only the Field explorer. Every other view colours its own ground (weather map base, storm ground colour, Beaufort sea, flight category, GAFOR, ocean, ice, coast, carbon, Sun, dust). |
| B | **General toggles that belong to one view** | Section 01 in the other views | *Wind direction vectors*, *Cloud volumes* and *Front overlay* are the Field explorer's; the weather map has its own. *Place fields on terrain* did nothing where the view's relief decides. *Height scale* did nothing in the flight, sailing and VFR views, which keep the true altitude scale, and nothing without relief. *Realistic terrain*, *Data colours over the terrain* and *Terrain-adjusted temperature* did nothing without relief or another layer. *Waves in 3D* did nothing in the storm watch and the flat Field explorer. |
| C | **Options that cannot change anything here** | Flight and sailing barb density; flight level sheets | Small grids are already at one barb per cell, so Sparse or Dense gave the same barbs. Level sheets with no level aloft shown. Density with no barb layer shown. |
| D | **Layers the run does not have** | Weather map, storm watch, flight, VFR, sailing, field | Fronts, precipitation, thunderstorms, storm surge and tornado risk in runs without them. Tornado watches and supercells in the hurricane run. Borders and cities outside the map. Weather symbols without clouds or precipitation. VFR labels (they label the GAFOR areas) without an area file. The sailing route and isochrones before a route is planned. Current tables that do not cover the run. The boat, currents and wind drift without sea or wind. Wind vectors in calm runs. |
| E | **Layers with nothing in the frame shown** | Many | Clouds, icing, CB columns, turbulence, precipitation, landslides, deep-water formation, corrosive water, flares, watches, supercells and currents: each exists in some frames only. |
| F | **A scenario normalised away** | Cosmic dust, *Total influx* | The colour scale was re-fitted to each scenario, so low, central and high looked the same. Only the numbers changed. |
| G | **Controls that act elsewhere** | Solar *Chart window*; polar and coast *Scenario in focus*; the CSV loading option; *Only layers with data* | They act on the charts and tables below the scene, on the next file loaded, or on the list itself. |
| H | **A view's own ground shadowed** (appeared with fix A) | All non-field views | Once a field layer lies on the ground, the view's own ground choice has no effect. |

## 3. Changes

| Finding | Change |
| --- | --- |
| A | In every view other than the Field explorer, **picking a field layer lays it on that view's ground** under the view's own layers: wind barbs, routes, aerodromes, isobars and the rest stay. The section says so, and a toggle *Ground of this view: <layer>* takes it off again. The eyebrow of the scene shows "GROUND: <layer>", and the field colour scale appears at the bottom of the scene. |
| H | Choosing the view's own ground (base, ground, sea, ice, water, disk or dust colour, or the flight category) takes the field layer off. |
| B | Section 01 shows a control only where it acts. *Wind direction vectors*, *Cloud volumes* and *Front overlay* appear in the Field explorer only. Other views get *Terrain relief* (the view's relief), except where the view has its own relief control or is drawn flat (carbon). *Height scale* appears only with relief, and not in the flight, sailing and VFR views. *Realistic terrain* and *Data colours* appear only with relief. *Terrain-adjusted temperature* appears only for the temperature layer. *Waves in 3D* is hidden in the storm watch, the flat Field explorer and the sailing view (which has its own). |
| C | Barb densities that would draw the same barbs are disabled and marked "(same here)". Density is disabled with no barb layer. Level sheets are disabled with no level aloft. |
| D | Disabled with the reason in the label: "none in this run", "none in this area", "no clouds or precipitation in this run", "plan one below the scene", "no sea in this run", "no wind in this run", or *Labels of the GAFOR areas* (enabled with an area file). The run's contents are computed once per run (`runHas`). |
| E | Kept enabled, because other frames have them, with "· none at this time" in the label (`frameHas`; turbulence from a sample of the winds aloft; route clouds from the route corridor). |
| F | One colour scale for the three dust scenarios (the lowest of low to the highest of high), so the scenario shows on the map. |
| G | Relabelled: *Chart window (charts below)*; *Scenario in focus (metrics and charts below; the map shows …)*; *Next CSV holds*; the storm ground's *Automatic (…)* names what it resolves to. |

## 4. Result after the changes

**Before the changes:** the first full audit flagged 607 of 1 657 control changes in the same 79 example/view pairs as
changing nothing, not counting the list filter. 179 of them were field layers picked outside the Field explorer (finding A, three per view tried).
That audit used an earlier fingerprint, which missed changes made only to uniforms on `material.userData` and paused the
per-frame updates. Its count is therefore an upper bound, and every finding in §2 was confirmed by hand or by the later runs.

**After the changes,** everything that still changes nothing is one of:

- *Only layers with data* (list filter), 79 cases, by design;
- the loading option *Next CSV holds*, by design;
- controls that act on charts and tables below the scene, relabelled;
- options equal to the automatic choice, or with nothing there now (storm ground colour);
- layers with nothing in the frame shown, labelled "· none at this time".

### 4.1 Final run

Final run: **1386 control changes in 79 example/view pairs of 17 examples; 1237 changed the view, 149 did not**, all for one of these reasons:

| Reason | Cases | Controls |
| --- | --- | --- |
| Nothing to show in the frame shown (the label says "none at this time") | 46 | Cloud cover by genus, Cloud layers, base to top, Cloud volumes, Cloud volumes over the whole map, Clouds along the route, Corrosive water, Currents, Debris runout paths, Deep-water formation ⇩, Flares ⚡, Icing, Labels, Landslides ▲, Main cloud deck, Other clouds, Precipitation rate shading, Precipitation symbols, Storm motion, Supercells, Thunderstorm columns, Thunderstorm ⚡ and hail ◆ markers, Tornado possible, Tornado risk, Tornado watch areas, Turbulence, Unstable slopes |
| List filter *Only layers with data* (acts on the list) | 79 | Only layers with data |
| Loading option *Next CSV holds* (acts on the next file loaded) | 14 | Next CSV holds |
| Acts on the charts and tables below the scene (relabelled) | 7 | Chart window, Scenario in focus (polar, coast) |
| Storm ground colour equal to the automatic choice, or nothing there now | 3 | Ground colour Automatic |

No control is left that changes nothing without saying why.

## 5. Limits of the audit

- *Fingerprint:* it can miss a change drawn only into a texture's pixels. Suspect cases were checked with screenshots (for example the storm watch ground colour and the Field explorer height scale).
- *One frame:* 60 % into each run, so "none at this time" depends on the frame chosen. The tool can be run at other frames.
- *Out of scope:* the report sections below the scene are not part of the audit; their table rows and strip bands seek the run's frame.
