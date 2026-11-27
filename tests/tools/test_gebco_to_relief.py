import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

AVAILABLE = importlib.util.find_spec('netCDF4') is not None
if AVAILABLE:
    import numpy as np
    from netCDF4 import Dataset
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
    from gebco_to_relief import convert


@unittest.skipUnless(AVAILABLE, 'optional netCDF4 dependency not installed')
class ReliefTest(unittest.TestCase):
    def fixture(self, path, descending=False, shifted=False):
        with Dataset(path, 'w') as ds:
            ds.createDimension('lat', 2); ds.createDimension('lon', 4)
            lat = ds.createVariable('lat', 'f8', ('lat',)); lat.units = 'degrees_north'
            lon = ds.createVariable('lon', 'f8', ('lon',)); lon.units = 'degrees_east'
            z = ds.createVariable('elevation', 'f8', ('lat', 'lon'), fill_value=-32768); z.units = 'm'
            lat[:] = [45, -45] if descending else [-45, 45]
            lon[:] = [45, 135, 225, 315] if shifted else [-135, -45, 45, 135]
            values = np.array([[-4000, -32768, 0, 10], [1000, 2000, 3000, 4000]])
            if shifted: values = values[:, [2, 3, 0, 1]]
            z[:] = values[::-1] if descending else values

    def test_orientation_wrap_and_nodata(self):
        with tempfile.TemporaryDirectory() as tmp:
            for descending, shifted in [(False, False), (True, False), (True, True)]:
                path = Path(tmp) / 'grid.nc'; self.fixture(path, descending, shifted)
                self.assertEqual(convert(path, 4, 2, 'Synthetic')['elevation_m'], [-4000, None, 0, 10, 1000, 2000, 3000, 4000])

    def test_cli_downsampling(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'grid.nc'; out = Path(tmp) / 'out.json'; self.fixture(path)
            subprocess.run([sys.executable, 'tools/gebco_to_relief.py', str(path), str(out), '--nx', '2', '--ny', '2', '--source', 'Synthetic fixture'], check=True)
            doc = json.loads(out.read_text())
            self.assertEqual(doc['elevation_m'], [None, 10, 2000, 4000])
            self.assertEqual(doc['source'], 'Synthetic fixture')

    def test_rejects_subset_units_and_output_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'grid.nc'; self.fixture(path)
            with self.assertRaises(ValueError): convert(path, 2000, 2000, 'Synthetic')
            with Dataset(path, 'a') as ds: ds.variables['lat'][:] = [-20, 20]
            with self.assertRaises(ValueError): convert(path, 4, 2, 'Synthetic')
            self.fixture(path)
            with Dataset(path, 'a') as ds: ds.variables['elevation'].units = 'feet'
            with self.assertRaises(ValueError): convert(path, 4, 2, 'Synthetic')
