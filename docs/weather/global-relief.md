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

## Next integration

A GEBCO converter must read coordinate axes and vertical metadata, subset or downsample before export, reverse north-first rows when necessary, preserve nodata, and include dataset version and attribution in `source`. No GEBCO download, NetCDF converter, or model bathymetry coupling is included yet. The coarse climate grid limits visible detail even with a denser input file.
