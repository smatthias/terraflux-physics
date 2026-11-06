"""tools/silso_to_request.py: reading the SILSO monthly file and turning it into a monthly solar-cycle record."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('silso_to_request', ROOT / 'tools/silso_to_request.py')
silso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(silso)


def csv(months, start=(1849, 11), missing=(), provisional_from=None):
    """A file in the SILSO layout with made-up values (k / 10)."""
    lines = []
    year, month = start
    for k in range(months):
        value = -1 if k in missing else k / 10.0
        definitive = 0 if provisional_from is not None and k >= provisional_from else 1
        lines.append(f'{year};{month:2d};{year + (month - 0.5) / 12:.3f};{value:6.1f};{1.2:5.1f};{30:5d};{definitive}')
        month += 1
        if month == 13:
            year, month = year + 1, 1
    return '\n'.join(lines) + '\n'


class SilsoTest(unittest.TestCase):
    def test_reads_values_missing_months_and_the_definitive_flag(self):
        rows = silso.read_silso(csv(5, missing={2}, provisional_from=4))
        self.assertEqual(rows[0], (1849, 11, 0.0, True))
        self.assertIsNone(rows[2][2])
        self.assertFalse(rows[4][3])

    def test_months_must_be_consecutive(self):
        text = csv(3).splitlines()
        with self.assertRaises(ValueError):
            silso.read_silso('\n'.join([text[0], text[2]]))

    def test_record_starts_at_the_requested_month(self):
        record = silso.monthly_record(silso.read_silso(csv(30)), '1850-01', 10)
        self.assertEqual((record['kind'], record['first_year'], record['first_month'], record['first_cycle_number']), ('monthly', 1850, 1, 10))
        self.assertEqual(record['values'][:3], [0.2, 0.3, 0.4])
        self.assertIn('SILSO', record['data_origin'])

    def test_gaps_are_refused_unless_filled(self):
        rows = silso.read_silso(csv(30, missing={5, 6}))
        with self.assertRaises(ValueError):
            silso.monthly_record(rows, '1850-01', 10)
        record = silso.monthly_record(rows, '1850-01', 10, fill_gaps=True)
        # Months 5 and 6 of the file (values 0.5, 0.6) are interpolated between 0.4 and 0.7.
        self.assertEqual(record['values'][3:5], [0.5, 0.6])
        self.assertIn('2 missing months interpolated', record['data_origin'])

    def test_command_line_writes_a_request_from_the_template(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'SN_m_tot_V2.0.csv'
            source.write_text(csv(12 * 60))
            output = Path(directory) / 'request.json'
            result = subprocess.run([sys.executable, str(ROOT / 'tools/silso_to_request.py'), str(source), '--start', '1850-01', '--output', str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            request = json.loads(output.read_text())
            self.assertEqual(request['schema_version'], 'terraflux.solar.cycle-run/v1')
            self.assertEqual(request['record']['kind'], 'monthly')
            # The file ends in 1909-10: the issue year is capped at its end.
            self.assertAlmostEqual(request['forecast']['issue_year'], 1909 + 10 / 12)
            self.assertGreaterEqual(request['forecast']['horizon_year'], request['forecast']['issue_year'] + 2)
            late = subprocess.run([sys.executable, str(ROOT / 'tools/silso_to_request.py'), str(source), '--issue', '1950', '--output', str(output)], capture_output=True, text=True)
            self.assertNotEqual(late.returncode, 0)


if __name__ == '__main__':
    unittest.main()
