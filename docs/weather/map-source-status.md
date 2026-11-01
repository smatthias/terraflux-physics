# Live map-source status

## Selected candidate

The first provider candidate remains the Bureau of Meteorology's 256 km Melbourne (Broadmeadows) radar. The Bureau's radar guide describes radar reflectivity images, their timestamped PNG naming pattern, and separate .map coordinate files. It also says individual radar images are issued every five minutes.

- Product page: [256 km Melbourne (Broadmeadows) Radar](https://www.bom.gov.au/products/IDR012.shtml)
- File and coordinate guide: [BOM Radar Images User Guide](https://www.bom.gov.au/catalogue/Bureau_of_Meteorology_radar_images_user_guide.pdf)

## Usage status

As checked on 1 November 2026, the Bureau's public anonymous FTP terms say the products may be used for personal use or within an organisation, but not for commercial use or supply to another person. The Bureau's copyright notice also says specialized radar imagery may require a data licence and prohibits automated scraping of its public website. Its real-time data services page identifies radar data as a registered-user service.

- [Anonymous FTP terms](https://www.bom.gov.au/catalogue/anon-ftp.shtml)
- [BOM copyright notice](https://www.bom.gov.au/copyright)
- [Real-time data services](https://www.bom.gov.au/resources/data-services)

Therefore, the anonymous feed is not cleared for TerraFlux commercial use. Do not add automated live retrieval or check real BOM radar images into this repository unless a data licence covers the intended retrieval, storage, processing, derived outputs, and distribution.

## Adapter inputs still needed

Before implementing the BOM provider adapter, obtain a sample covered by the required licence and its source documentation:

1. One timestamped radar PNG and the matching .map coordinate file.
2. The authoritative palette or legend mapping image colors to reflectivity values and missing/quality states.
3. The product's valid-time convention, projection details, and required attribution.
4. Written confirmation that the intended commercial processing and any output distribution are permitted.

The existing series runner accepts caller-supplied sidecar/raster pairs and applies each valid-time update at a model timestep boundary. The committed two-frame example is synthetic; no real BOM product has been downloaded or embedded. After licensing and sample details are available, the adapter can parse the source metadata into the existing v1 sidecar and map-series contracts.
