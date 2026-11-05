"""The fixture comparison of tools/build_ocean_example.py: exact on the machine that wrote the fixture,
tolerant of last-bit and last-digit differences between compilers and C libraries, strict about anything else."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('build_ocean_example', Path(__file__).resolve().parents[2] / 'tools/build_ocean_example.py')
example = importlib.util.module_from_spec(spec)
spec.loader.exec_module(example)


def timeline(**changes):
    frames = [{'elapsed_seconds': 3600.0 * k, 'temperature_k': [280.0 + 0.1 * i + k for i in range(50)], 'heat_content_j': 8.632636891879183e+22 + k,
               'surface_elevation_m': [54.28, 140.27, 1.5], 'front': [i % 7 == 0 for i in range(50)], 'stream_id': [i % 3 - 1 for i in range(50)],
               'volume_balance_error_m3': 0.06, 'name': 'basin'} for k in range(4)]
    document = {'ocean': {'frames': frames, 'processes': ['air_sea']}}
    for path, value in changes.items():
        frame, key, index = path.split(':')
        target = frames[int(frame)]
        if index == '':
            target[key] = value
        else:
            target[key][int(index)] = value
    return document


class FixtureComparisonTest(unittest.TestCase):
    def check(self, generated, field_tolerance=1e-4):
        return example.check(timeline(), generated, 1e-6, field_tolerance)

    def test_identical_documents_match_exactly(self):
        self.assertEqual(self.check(timeline()), (True, 'matches regeneration'))

    def test_last_bit_and_last_printed_digit_differences_are_accepted(self):
        ok, message = self.check(timeline(**{'0:heat_content_j:': 8.632636481784696e+22, '2:surface_elevation_m:0': 54.27, '3:surface_elevation_m:1': 140.28}))
        self.assertTrue(ok, message)
        self.assertIn('printed digits', message)

    def test_residuals_are_noise(self):
        ok, _ = self.check(timeline(**{'1:volume_balance_error_m3:': -0.06}))
        self.assertTrue(ok)

    def test_structure_must_match(self):
        for generated in (timeline(**{'0:name:': 'other'}), timeline(**{'0:surface_elevation_m:': [54.28, 140.27]}), timeline(**{'1:front:3': 1.0})):
            ok, message = self.check(generated)
            self.assertFalse(ok, message)

    def test_a_real_change_is_rejected(self):
        ok, message = self.check(timeline(**{'2:surface_elevation_m:0': 54.5}))
        self.assertFalse(ok)
        self.assertIn('/ocean/frames[2]/surface_elevation_m[0]', message)
        ok, message = self.check(timeline(**{f'{k}:temperature_k:{i}': 281.0 + 0.1 * i + k for k in range(4) for i in range(50)}))
        self.assertFalse(ok, 'every temperature one kelvin warmer')

    def test_weather_noise_in_a_long_run_passes_with_a_field_tolerance(self):
        # A few cells of a field differ (a threshold flipped): rejected by default, accepted by a chaotic run's tolerance.
        noisy = timeline(**{'1:temperature_k:7': 281.7 + 7.0, '3:temperature_k:30': 286.0 - 5.0})
        self.assertFalse(self.check(noisy)[0])
        ok, message = self.check(noisy, field_tolerance=0.01)
        self.assertTrue(ok, message)
        self.assertIn('cross-platform tolerance', message)

    def test_threshold_diagnostics_get_ten_times_the_tolerance(self):
        flipped = timeline(**{'1:front:7': True, '2:front:14': False, '0:stream_id:4': -1})
        self.assertFalse(self.check(flipped, field_tolerance=0.01)[0])
        self.assertTrue(self.check(flipped, field_tolerance=0.05)[0])

    def test_quantum_of_printed_numbers(self):
        self.assertAlmostEqual(example.quantum(54.28), 0.01)
        self.assertAlmostEqual(example.quantum(1.5e-05), 1e-06)
        self.assertAlmostEqual(example.quantum(1296.0), 0.1)
        self.assertEqual(example.quantum(8.632636891879183e+22), 0.0)
        self.assertEqual(example.quantum(3.0720000000000004e-05), 0.0)
        self.assertEqual(example.quantum(7), 1.0)
        self.assertEqual(example.quantum(True), 0.0)


if __name__ == '__main__':
    unittest.main()
