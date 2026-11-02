#!/usr/bin/env python3
"""Decode a cropped NOAA MRMS GRIB2 reflectivity field into TerraFlux JSON.

Requires wgrib2 with MRMS GRIB2 tables installed. A geographic bbox is
mandatory so the full continental grid is never expanded into a huge CSV.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

PRODUCT_NAME = "MergedReflectivityComposite"
SOURCE_URL = (
    "https://mrms.ncep.noaa.gov/2D/MergedReflectivityComposite/"
    "MRMS_MergedReflectivityComposite.latest.grib2.gz"
)
MISSING_CODES = {
    -99.0: "MRMS missing value code -99",
    -999.0: "MRMS no-coverage value code -999",
    -3.0: "MRMS no-coverage value code -3",
}


def parse_bbox(text: str) -> tuple[float, float, float, float]:
    try:
        values = tuple(float(part.strip()) for part in text.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("bbox must be west,south,east,north") from error
    if len(values) != 4:
        raise argparse.ArgumentTypeError("bbox must be west,south,east,north")
    west, south, east, north = values
    if not all(math.isfinite(value) for value in values):
        raise argparse.ArgumentTypeError("bbox coordinates must be finite")
    if not (-180.0 <= west < east <= 180.0):
        raise argparse.ArgumentTypeError("bbox longitude must satisfy -180 <= west < east <= 180")
    if not (-90.0 <= south < north <= 90.0):
        raise argparse.ArgumentTypeError("bbox latitude must satisfy -90 <= south < north <= 90")
    return west, south, east, north


def parse_time(value: str) -> str:
    parsed = dt.datetime.strptime(value, "%Y-%m-%d %H:%M:%S").replace(tzinfo=dt.timezone.utc)
    return parsed.isoformat(timespec="seconds").replace("+00:00", "Z")


def observation(valid_time: str, longitude: float, latitude: float, value: float) -> dict:
    record = {
        "schema_version": "terraflux.weather.observation/v1",
        "source": {
            "source_id": "noaa_mrms",
            "source_kind": "public_dataset",
            "product_id": PRODUCT_NAME,
            "interpretation": "structured_grid_value",
        },
        "valid_time_utc": valid_time,
        "variable": "radar_reflectivity",
        "value": None,
        "units": "dBZ",
        "location": {"crs": "EPSG:4326", "coordinates": [longitude, latitude]},
        "vertical_reference": "composite",
        "quality": {},
    }
    if value in MISSING_CODES:
        record["quality"] = {"status": "no_data", "reason": MISSING_CODES[value]}
    else:
        record["value"] = value
        record["quality"] = {"status": "decoded"}
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("grib2_file", type=Path, help="expanded MRMS GRIB2 file")
    parser.add_argument("output_json", type=Path, help="destination canonical observation JSON")
    parser.add_argument(
        "--bbox", required=True, type=parse_bbox, metavar="W,S,E,N",
        help="required geographic crop in decimal degrees (west,south,east,north)",
    )
    parser.add_argument("--wgrib2", default="wgrib2", help="wgrib2 executable (default: wgrib2)")
    parser.add_argument(
        "--max-points", type=int, default=1_000_000,
        help="maximum CSV grid points to emit (default: 1000000)",
    )
    args = parser.parse_args()

    if not args.grib2_file.is_file():
        parser.error(f"GRIB2 file does not exist: {args.grib2_file}")
    if args.max_points < 1:
        parser.error("--max-points must be positive")

    west, south, east, north = args.bbox
    output_path = args.output_json
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="terraflux-mrms-") as temporary:
        csv_path = Path(temporary) / "mrms-crop.csv"
        command = [
            args.wgrib2,
            str(args.grib2_file),
            "-match",
            PRODUCT_NAME,
            "-undefine",
            "out-box",
            f"{west}:{east}",
            f"{south}:{north}",
            "-csv",
            str(csv_path),
        ]
        try:
            result = subprocess.run(command, check=False, text=True, capture_output=True)
        except OSError as error:
            print(f"could not execute wgrib2: {error}", file=sys.stderr)
            return 1
        if result.returncode != 0:
            print(result.stderr.strip() or result.stdout.strip(), file=sys.stderr)
            return result.returncode or 1
        if not csv_path.is_file():
            print("wgrib2 completed without creating the CSV output", file=sys.stderr)
            return 1

        records = []
        valid_times = set()
        fields = set()
        try:
            with csv_path.open(newline="", encoding="utf-8") as source:
                for row_number, row in enumerate(csv.reader(source), start=1):
                    if len(row) != 7:
                        raise ValueError(f"CSV row {row_number} has {len(row)} columns; expected 7")
                    _reference_time, verification_time, field, _level, lon_text, lat_text, value_text = row
                    if PRODUCT_NAME not in field:
                        raise ValueError(f"unexpected GRIB2 field in CSV: {field}")
                    valid_time = parse_time(verification_time)
                    longitude = float(lon_text)
                    latitude = float(lat_text)
                    value = float(value_text)
                    if not all(math.isfinite(number) for number in (longitude, latitude, value)):
                        raise ValueError(f"CSV row {row_number} contains a non-finite number")
                    if not (west <= longitude <= east and south <= latitude <= north):
                        raise ValueError(f"CSV row {row_number} is outside the requested bbox")
                    valid_times.add(valid_time)
                    fields.add(field)
                    records.append(observation(valid_time, longitude, latitude, value))
                    if len(records) > args.max_points:
                        raise ValueError(
                            f"crop exceeds --max-points={args.max_points}; retry with a smaller bbox"
                        )
        except (OSError, csv.Error, ValueError) as error:
            print(f"failed to decode NOAA MRMS CSV: {error}", file=sys.stderr)
            return 1

    if not records:
        print("wgrib2 returned no grid points for this product and bbox", file=sys.stderr)
        return 1
    if len(valid_times) != 1 or len(fields) != 1:
        print("expected exactly one product field and one valid time", file=sys.stderr)
        return 1

    temporary_output = output_path.with_name(output_path.name + ".tmp")
    try:
        with temporary_output.open("w", encoding="utf-8", newline="\n") as target:
            json.dump(records, target, indent=2, allow_nan=False)
            target.write("\n")
        os.replace(temporary_output, output_path)
    except OSError as error:
        temporary_output.unlink(missing_ok=True)
        print(f"could not write observations: {error}", file=sys.stderr)
        return 1

    print(f"Wrote {len(records)} NOAA MRMS observations for {next(iter(valid_times))}")
    print(f"Source: {SOURCE_URL}")
    print(f"Output: {output_path}")
    print("This is a derived radar composite, not an individual radar scan.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
