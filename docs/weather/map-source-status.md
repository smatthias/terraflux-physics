# Live map-source status

## Temporary prototype source: NOAA MRMS

Use NOAA's Multi-Radar/Multi-Sensor System (MRMS) merged reflectivity composite as the temporary live radar source for development. The selected product is the current `MergedReflectivityComposite` GRIB2 file, published over HTTPS by NCEP. It combines radar-derived reflectivity into a gridded composite, so it is a better fit for the map/grid ingestion work than individual raw NEXRAD radar volumes.

- Product feed: [MRMS merged reflectivity composite](https://mrms.ncep.noaa.gov/2D/MergedReflectivityComposite/)
- Access documentation: [NOAA/NSSL MRMS data access](https://www.nssl.noaa.gov/projects/mrms/MRMS_data.php)
- Product and GRIB2 field tables: [Operational MRMS GRIB2 tables](https://www.nssl.noaa.gov/projects/mrms/operational/tables.php)
- Data terms: [NOAA MRMS on AWS Open Data](https://registry.opendata.aws/noaa-mrms-pds/)

NOAA NODD says its MRMS data are open for public use and requests attribution for unaltered data. Do not imply NOAA endorsement; identify transformed data as modified. This temporary feed is U.S.-coverage data and does not replace the planned Victoria/Australia source.

The repository includes `tools/fetch_noaa_mrms.py` to retrieve the current compressed GRIB2 file and write a provenance record, plus `tools/decode_noaa_mrms.py` to use `wgrib2` to decode a bounded area into canonical radar-reflectivity observations. The decoder reads valid time and geographic coordinates from the GRIB2 field and preserves explicit missing/no-coverage codes. Retrieval time remains separate from product valid time. The decoder writes source observations; the dry air-temperature model does not interpret dBZ as temperature. Follow [the MRMS ingestion guide](noaa-mrms.md) for the workflow and limits.

## Planned licensed source: BOM Victoria radar

The Bureau of Meteorology's 256 km Melbourne (Broadmeadows) radar remains the target source for the Victoria use case. The Bureau documents timestamped radar PNGs and separate `.map` coordinate files.

- Product page: [256 km Melbourne (Broadmeadows) Radar](https://www.bom.gov.au/products/IDR012.shtml)
- File and coordinate guide: [BOM Radar Images User Guide](https://www.bom.gov.au/catalogue/Bureau_of_Meteorology_radar_images_user_guide.pdf)

As checked on 1 November 2026, the Bureau's public anonymous FTP terms say the products may be used for personal use or within an organisation, but not for commercial use or supply to another person. Its real-time data service provides the licensing route. Do not add automated retrieval of real BOM imagery until the licence covers retrieval, storage, processing, derived outputs, and distribution.

Before that adapter is implemented, obtain a licensed sample PNG, matching `.map` file, authoritative legend/palette, projection details, valid-time convention, and required attribution.

## Adapter boundary

The existing series runner accepts caller-supplied sidecar/raster pairs and applies each valid-time update at a model timestep boundary. Synthetic fixtures remain the test input. NOAA acquisition is kept outside the C++ physics kernels; its GRIB2 decoder must normalize the product grid, field values, timestamps, and quality codes into documented TerraFlux inputs.