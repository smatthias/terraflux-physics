#!/usr/bin/env python3
"""Download one current NOAA MRMS merged reflectivity composite.

This acquires source data only. It does not decode GRIB2 or create a TerraFlux
map sidecar. Do not treat retrieval time as the observation valid time.
"""
from __future__ import annotations

import argparse
import datetime as dt
import gzip
import json
import os
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

SOURCE_URL = (
    "https://mrms.ncep.noaa.gov/2D/MergedReflectivityComposite/"
    "MRMS_MergedReflectivityComposite.latest.grib2.gz"
)
MAX_COMPRESSED_BYTES = 100 * 1024 * 1024
MAX_EXPANDED_BYTES = 512 * 1024 * 1024
USER_AGENT = "TerraFlux-Physics/0.1 (NOAA MRMS prototype acquisition)"


def download(url: str, destination: Path, max_bytes: int) -> tuple[int, str | None, str | None]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        etag = response.headers.get("ETag")
        last_modified = response.headers.get("Last-Modified")
        total = 0
        with destination.open("wb") as output:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > max_bytes:
                    raise ValueError(f"download exceeds the {max_bytes}-byte safety limit")
                output.write(chunk)
        return total, etag, last_modified


def expand_gzip(source: Path, destination: Path) -> int:
    total = 0
    with gzip.open(source, "rb") as compressed, destination.open("wb") as output:
        while True:
            chunk = compressed.read(1024 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > MAX_EXPANDED_BYTES:
                raise ValueError(
                    f"expanded GRIB2 exceeds the {MAX_EXPANDED_BYTES}-byte safety limit"
                )
            output.write(chunk)
    return total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output_dir", type=Path,
        help="directory for the downloaded .grib2.gz, expanded .grib2, and provenance JSON",
    )
    parser.add_argument(
        "--url", default=SOURCE_URL,
        help="HTTPS URL for an NOAA MRMS merged reflectivity GRIB2 gzip file",
    )
    parser.add_argument(
        "--overwrite", action="store_true",
        help="replace output files if they already exist",
    )
    args = parser.parse_args()

    if not args.url.startswith("https://mrms.ncep.noaa.gov/"):
        parser.error("--url must use the official mrms.ncep.noaa.gov HTTPS host")

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    compressed_path = output_dir / "MRMS_MergedReflectivityComposite.latest.grib2.gz"
    grib_path = output_dir / "MRMS_MergedReflectivityComposite.latest.grib2"
    provenance_path = output_dir / "MRMS_MergedReflectivityComposite.latest.source.json"
    destinations = (compressed_path, grib_path, provenance_path)
    if not args.overwrite and any(path.exists() for path in destinations):
        parser.error("an output file already exists; choose another directory or pass --overwrite")

    with tempfile.TemporaryDirectory(prefix="terraflux-mrms-", dir=output_dir) as temporary:
        temporary_dir = Path(temporary)
        compressed_temp = temporary_dir / compressed_path.name
        grib_temp = temporary_dir / grib_path.name

        try:
            compressed_bytes, etag, last_modified = download(
                args.url, compressed_temp, MAX_COMPRESSED_BYTES
            )
            expanded_bytes = expand_gzip(compressed_temp, grib_temp)
            fetched_at = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
            provenance = {
                "schema": "terraflux.weather.source-acquisition/v1",
                "provider": "NOAA MRMS",
                "product": "MergedReflectivityComposite",
                "source_url": args.url,
                "source_terms": (
                    "NOAA NODD open data; attribution requested for unaltered data. "
                    "Do not imply NOAA endorsement. Describe transformed data as modified."
                ),
                "source_terms_url": "https://registry.opendata.aws/noaa-mrms-pds/",
                "retrieved_at_utc": fetched_at,
                "http_last_modified": last_modified,
                "http_etag": etag,
                "compressed_file": compressed_path.name,
                "compressed_bytes": compressed_bytes,
                "grib2_file": grib_path.name,
                "grib2_bytes": expanded_bytes,
                "valid_time_utc": None,
                "valid_time_note": (
                    "Not inferred from retrieval time or the 'latest' filename. "
                    "Read the product timestamp from GRIB2 metadata before modeling."
                ),
                "processing_status": "downloaded_only_grib2_not_decoded",
            }
            provenance_temp = temporary_dir / provenance_path.name
            provenance_temp.write_text(
                json.dumps(provenance, indent=2) + "\n", encoding="utf-8"
            )

            for path in destinations:
                if args.overwrite:
                    path.unlink(missing_ok=True)
            os.replace(compressed_temp, compressed_path)
            os.replace(grib_temp, grib_path)
            os.replace(provenance_temp, provenance_path)
        except (urllib.error.URLError, TimeoutError, OSError, EOFError, gzip.BadGzipFile, ValueError) as error:
            print(f"NOAA MRMS acquisition failed: {error}", file=sys.stderr)
            return 1

    print(f"Downloaded NOAA MRMS source: {grib_path}")
    print(f"Provenance recorded: {provenance_path}")
    print("GRIB2 has not been decoded; read its valid time and grid metadata before use.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
