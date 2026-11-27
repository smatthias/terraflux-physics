# Regional GEBCO bathymetry for ocean runs

`tools/gebco_to_ocean.py` samples a local GEBCO NetCDF file or HTTPS OPeNDAP dataset onto the **existing grid of an ocean circulation request**. Dependencies are `tools/requirements-relief.txt`.

```sh
python3 tools/gebco_to_ocean.py GEBCO_2026.nc bathymetry.json \
  --request examples/atlantic-streams-request.json \
  --source "GEBCO Bathymetric Compilation Group 2026 (2026), GEBCO_2026 Grid, doi:10.5285/4f68d5c7-45eb-f999-e063-7086abc036fa; nearest-cell sample" \
  --ocean-output ocean-with-gebco.json
./build/terraflux-run-ocean-circulation ocean-with-gebco.json ocean-timeline.json
```

Use separate paths; the input request is preserved. Local regional subsets and global grids are accepted when axes are regular, monotonic latitude/longitude cell centres. The variable must be `elevation(lat, lon)` with metres positive upward. The importer reads only sampled positions grouped by latitude row, not the full elevation grid. The output is bounded to 100,000 cells.

## Coordinates and coverage

Cell centres use `origin + (index + 0.5) * spacing`. Geographic coordinates match the ocean CLI's air/sea forcing approximation:

- latitude = anchor latitude + north / 111195;
- longitude = anchor longitude + east / (111195 × cos(anchor latitude)), wrapped to −180…180°.

This is the ocean's local linear approximation, **not** the forecast workspace's azimuthal projection. Near-polar anchors (absolute latitude ≥85°), invalid spacing and pole-crossing grids are rejected. Large domains inherit the approximation's distortions.

`terraflux.regional-bathymetry/v1` records the original grid, projection, source, datum, sampled coordinates, elevations, positive depths, a coverage mask and an ocean mask. Missing input or out-of-coverage cells have null elevation, depth and ocean classification, with coverage zero. Zero elevation is a covered sea-level value, following the CLI's `elevation <= sea_level` convention. Longitude seams and reversed axes are handled explicitly.

With incomplete coverage the bathymetry document is still written for inspection, but `--ocean-output` fails and does not write a runnable request. Unknown cells are never silently converted to land or water. With complete coverage the importer replaces the basin's terrain or explicit mask with sampled elevations and sea level zero, retaining other basin settings. The CLI preserves source, vertical-reference and projection metadata in basin provenance, and source/datum in the terrain output.

## What the model uses

Terrain elevations determine the ocean mask. When ocean carbon/biogeochemistry is enabled, negative elevations also supply seafloor depth (subject to that module's configured minimum depth). The reduced-gravity circulation still has its own prescribed active-layer thickness: importing bathymetry does **not** add bottom-following momentum, tides, depth-limited active-layer dynamics or calibration. Do not equate seabed depth with the active-layer thickness.

## Verified examples

`examples/gebco-regional-bathymetry.json` contains 48 real GEBCO_2026 samples from CEDA OPeNDAP on an 8 × 6 Atlantic grid. `examples/gebco-regional-ocean-request.json` is a one-step ocean run with those elevations and synthetic forcing. It is a smoke example, not a forecast or validated circulation reconstruction. Provenance is embedded in both files.

Python tests exercise coordinate alignment, reversed axes, depth signs, nodata, out-of-domain coverage, wrapping, invalid grids and request generation. `weather.regional_bathymetry_cli` checks elevation preservation, mask construction, source/datum propagation and a complete engine run using the committed sample. The full GEBCO dataset is not committed.
