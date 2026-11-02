# Configured surface outlets

`apply_surface_outlets` provides an optional prescribed boundary sink after routing, infiltration, evaporation and soil drainage. Each outlet identifies a unique active boundary cell by zero-based x/y, an absolute `crest_elevation_m` in the terrain datum, and nonnegative `max_rate_m_s` capacity. Interior, duplicate, out-of-grid or masked outlets are rejected before the example creates outputs.

Removed depth is the minimum of available surface storage, positive free-surface head above crest, and capacity × timestep. A crest below the terrain bed can remove available surface water but never ground material or negative storage. The outlet cannot introduce external water or affect soil storage. Per-cell discharged depths and total discharged volume are returned separately. This is a capacity-limited boundary sink, not a weir law, open-boundary momentum condition or calibrated river discharge model.

Scenario files with nonempty `outlets` use boundary `configured_outlets` and process_order `rain_route_infiltrate_evaporate_drain_outlet`. Files without outlets keep the existing closed mode. Outlet entries require exactly x, y, crest_elevation_m and max_rate_m_s. The serialized water timeline records these entries and `cumulative_discharge_volume_m3` for every snapshot.

The zero-initial-storage budget includes discharge:

`surface + soil + evaporation + drainage + surface discharge = rainfall + residual`.

The viewer validates outlet positions, coverage, uniqueness, scenario provenance, cumulative discharge and capacity limits before showing discharge in budgets, comparisons and CSV. Closed-mode data expose zero discharge to comparison/export code without changing existing fixture files.

Example:

```sh
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-outlet-scenario.json --output /tmp/outlet-example/engine-weather.json
```

Open `/tmp/outlet-example/engine-timeline.json` in the viewer or as a comparison against the bundled baseline. This isolated example disables infiltration and other losses and uses a corner outlet to demonstrate positive discharge. Baseline precipitation, terrain and atmosphere are unchanged. Discharge leaves the modeled domain; it is not silently classified as evaporation or soil drainage.
