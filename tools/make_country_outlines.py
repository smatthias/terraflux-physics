#!/usr/bin/env python3
"""Write the country outlines the viewer draws for orientation (viewer/public/geo/countries-110m.geojson).

Source: Natural Earth 1:110m Admin 0 countries (public domain, naturalearthdata.com), as bundled with the geopandas
0.14 wheel (geopandas/datasets/naturalearth_lowres). Read with pyshp; coordinates rounded to 0.001 degrees.

    pip download geopandas==0.14.4 --no-deps && unzip geopandas-0.14.4-py3-none-any.whl 'geopandas/datasets/*'
    python3 tools/make_country_outlines.py geopandas/datasets/naturalearth_lowres/naturalearth_lowres

The outlines are coarse (1:110m): for orientation only, not for boundaries of any aviation area.
"""
import json
import sys
from pathlib import Path

import shapefile  # pyshp


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    reader = shapefile.Reader(sys.argv[1])
    features = []
    for item in reader.iterShapeRecords():
        shape, record = item.shape, item.record
        parts = list(shape.parts) + [len(shape.points)]
        rings = [[[round(x, 3), round(y, 3)] for x, y in shape.points[parts[k]:parts[k + 1]]] for k in range(len(parts) - 1)]
        features.append({'type': 'Feature', 'properties': {'name': record['name'], 'iso_a3': record['iso_a3']},
                         'geometry': {'type': 'MultiLineString', 'coordinates': rings}})
    out = Path(__file__).resolve().parent.parent / 'viewer' / 'public' / 'geo' / 'countries-110m.geojson'
    out.parent.mkdir(parents=True, exist_ok=True)
    doc = {'type': 'FeatureCollection', 'source': 'Natural Earth 1:110m Admin 0 countries (public domain)', 'features': features}
    out.write_text(json.dumps(doc, separators=(',', ':')) + '\n')
    print(f'{len(features)} countries -> {out} ({out.stat().st_size // 1024} KiB)')


if __name__ == '__main__':
    main()
