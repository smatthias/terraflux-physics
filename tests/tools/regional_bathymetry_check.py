"""Run the ocean CLI with a committed real GEBCO terrain sample; check provenance and mask."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
request = json.loads((root / 'examples/gebco-regional-ocean-request.json').read_text())
bathymetry = json.loads((root / 'examples/gebco-regional-bathymetry.json').read_text())
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp) / 'request.json'; output = Path(temp) / 'timeline.json'
    # Include land in this regression so mask construction is tested as well as offshore depths.
    request = copy.deepcopy(request)
    request['basin']['terrain']['values'][0] = 25
    path.write_text(json.dumps(request))
    result = subprocess.run([sys.argv[1], str(path), str(output)], capture_output=True, text=True, cwd=root)
    assert result.returncode == 0, result.stderr
    doc = json.loads(output.read_text())
    assert doc['terrain']['values'] == request['basin']['terrain']['values']
    assert doc['terrain']['vertical_reference'] == 'mean_sea_level'
    assert doc['terrain']['source_id'] == bathymetry['source']
    assert doc['ocean']['basin_source']['projection'] == 'ocean_local_linear'
    assert doc['ocean']['ocean_mask'] == [v <= 0 for v in request['basin']['terrain']['values']]
    assert len(doc['ocean']['frames']) == 2
    assert all(v == 1 for v in bathymetry['coverage_mask'])
