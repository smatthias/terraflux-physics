# Global relief import

The Coupled Earth climate and CO₂ maps already render a globe. Their **Load globe relief JSON** control now accepts an offline global cell-centred grid. The imported relief changes only display geometry; it does not change the climate land mask, model state, or forecast terrain. Missing cells receive no elevation displacement. Field-height overlays remain independent of geographic relief.

Contract:

```json
{
  "schema": "terraflux.global-relief/v1",
  "nx": 4,
  "ny": 2,
  "units": "m",
  "datum": "mean_sea_level",
  "row_order": "south_to_north",
  "source": "Synthetic demonstration; not GEBCO",
  "elevation_m": [-4000, -2000, null, -1000, 1000, 2000, 0, -3000]
}
```

The grid covers the entire world, longitude −180° to +180° and latitude −90° to +90°. Values are flattened by row, west to east, with the southernmost row first. Cell centres lie half a cell inside the bounds. Null is nodata, zero is sea level. Grids are limited to one million cells, files to 32 MB, elevations to −12,000…10,000 m. A nearest-cell resample maps the imported grid to the climate grid and preserves nulls. The display uses 20× vertical exaggeration, capped at 4% of globe radius. The existing Relief switch disables the geographic displacement together with the field relief.

Use `examples/global-relief-synthetic.json` to exercise loading and clearing. It is deliberately coarse synthetic data, not geographic terrain.

## GEBCO NetCDF conversion

Install the optional converter dependencies and convert an already downloaded **global elevation grid** (not the TID grid):

```sh
python3 -m pip install -r tools/requirements-relief.txt
python3 tools/gebco_to_relief.py GEBCO_2026.nc relief.json --nx 360 --ny 180 \
  --source "GEBCO Bathymetric Compilation Group 2026 (2026), GEBCO_2026 Grid, doi:10.5285/4f68d5c7-45eb-f999-e063-7086abc036fa; nearest-cell sample"
```

Open a climate or CO₂ map and load `relief.json` using **Load globe relief JSON**.

The converter expects `elevation(lat, lon)`, metres positive upward, with regular global pixel-centre coordinates. It accepts ascending or descending latitude and longitude axes, including 0–360° longitude, and emits south-to-north, west-to-east rows. It reads selected positions from each sampled row rather than loading the entire elevation raster. Masked and nonfinite cells become JSON null. Sampling selects the nearest source cell; it does not average depth or retain small coast features. Regional subsets and non-global axes are rejected rather than stretched over the planet. The climate grid still limits display detail.

[GEBCO's 2026 documentation](https://www.gebco.net/data-products-gridded-bathymetry-data/gebco2026-grid) describes global cell-centred elevation in metres and an assumed mean-sea-level datum, with exceptions in some shallow-water source data. The output uses that assumption; it performs no vertical datum transformation. Preserve the release's attribution in `--source` and consult its terms. The converter does not download data automatically.

Synthetic NetCDF tests cover coordinate reversal, longitude wrapping, nodata, CLI downsampling and rejected inputs. Actual GEBCO-file conversion, visual browser verification and model bathymetry coupling remain pending.
