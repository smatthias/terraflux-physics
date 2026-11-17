"""tools/audit_view_controls.py: the summary of an audit file lists the controls that changed nothing (the audit itself
needs a browser and a development build of the viewer, see the tool's docstring)."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('audit_view_controls', ROOT / 'tools' / 'audit_view_controls.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class AuditSummaryTest(unittest.TestCase):
    def test_summary_lists_no_change_and_errors(self):
        rows = [
            {'example': 'loadX', 'view': 'Weather map', 'section': '01b / WEATHER MAP LAYERS', 'label': 'Fronts', 'type': 'checkbox', 'what': 'toggle', 'changed': False, 'restored': True},
            {'example': 'loadX', 'view': 'Weather map', 'section': '01b / WEATHER MAP LAYERS', 'label': 'Isobars', 'type': 'checkbox', 'what': 'toggle', 'changed': True, 'restored': True},
            {'example': 'loadX', 'view': 'Flight weather', 'label': 'Level sheets', 'error': 'timeout'},
        ]
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'audit.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                tool.summary(path)
        lines = out.getvalue().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertTrue(lines[0].startswith('NO CHANGE | loadX | Weather map | 01b / WEATHER MAP LAYERS | Fronts | toggle'))
        self.assertTrue(lines[1].startswith('ERROR | loadX | Flight weather | Level sheets | timeout'))

    def test_fingerprint_and_controls_scripts_are_present(self):
        self.assertIn('__terrafluxScene', tool.FP)
        self.assertIn("aside section", tool.CONTROLS_JS)


if __name__ == '__main__':
    unittest.main()
