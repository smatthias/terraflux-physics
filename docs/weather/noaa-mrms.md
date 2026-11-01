# NOAA MRMS prototype ingestion

The prototype uses NOAA's MRMS `MergedReflectivityComposite` product. NOAA documents the field as composite reflectivity in dBZ, updated every two minutes, with `-99` missing and `-999` no-coverage values. The data are a derived mosaic of radar inputs, not a raw NEXRAD radar volume or an individual site scan.

NOAA's MRMS data are open for public use; NOAA requests attribution for unaltered data, prohibits implying endorsement, and asks that modified data not be represented as original unaltered data. This composite has U.S. coverage and does not replace the planned Victoria/BOM source.

## Acquire one current product

Python's standard library is sufficient for acquisition:

~~~sh
python3 tools/fetch_noaa_mrms.py /tmp/terraflux-mrms
~~~

The helper saves the compressed GRIB2, expanded GRIB2, and provenance JSON. The provider's `latest` URL is mutable; keep the fetched timestamp, HTTP metadata, and local file together. The helper does not infer product valid time from retrieval time.

## Decode a bounded area

Install `wgrib2` with the MRMS GRIB2 table definitions and choose a small U.S. bounding box. The decoder requires a crop and defaults to at most one million points so it does not expand the full continental grid into a huge JSON document.

~~~sh
python3 tools/decode_noaa_mrms.py \
  /tmp/terraflux-mrms/MRMS_MergedReflectivityComposite.latest.grib2 \
  /tmp/mrms-observations.json \
  --bbox=-123,37,-121,39
~~~

The decoder asks `wgrib2` to select `MergedReflectivityComposite`, crop to the requested longitude/latitude bounds, and emit CSV. It reads the GRIB verification time and each point's longitude, latitude, and dBZ value, then writes canonical `terraflux.weather.observation/v1` records. Explicit `-99`, `-999`, and `-3` missing/no-coverage codes become `no_data`; zero remains numeric reflectivity. Undefined points omitted by wgrib2 remain absent rather than being fabricated.

The output is a radar observation field. It is not air temperature, precipitation rate, or a forecast. The current dry temperature transport model does not consume radar reflectivity as temperature. A later radar/precipitation step must define how to use this field.