import importlib.util
from pathlib import Path
import unittest
import contextlib
import io
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('verify',Path(__file__).resolve().parents[2]/'tools/verify_pipeline.py')
verify=importlib.util.module_from_spec(spec);spec.loader.exec_module(verify)

class PipelineTest(unittest.TestCase):
    def test_complete_plan_builds_tests_and_installs_locked_viewer(self):
        args=verify.parse_args(['--build-dir','/tmp/terraflux test build','--cmake-prefix-path','/tmp/deps with spaces'])
        plan=verify.build_plan(args)
        self.assertEqual(len(plan),7)
        self.assertIn('-DBUILD_TESTING=ON',plan[0][1])
        self.assertIn('-DCMAKE_PREFIX_PATH=/tmp/deps with spaces',plan[0][1])
        self.assertIn('/tmp/terraflux test build',plan[0][1])
        self.assertEqual(plan[4][1][0:2],['npm','ci'])
        self.assertEqual(plan[-1][1],['npm','run','build'])
    def test_ci_modes_include_only_their_scope(self):
        cpp=verify.build_plan(verify.parse_args(['--cpp-only','--skip-configure','--skip-build']))
        self.assertEqual(len(cpp),2)
        self.assertTrue(all(command[0]!='npm' for _,command,_ in cpp))
        viewer=verify.build_plan(verify.parse_args(['--viewer-only','--skip-install']))
        self.assertEqual([command for _,command,_ in viewer],[['npm','test'],['npm','run','build']])
    def test_failed_stage_stops_following_commands(self):
        import subprocess
        with contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()),patch.object(verify.shutil,'which',return_value='/tool'),patch.object(Path,'is_dir',return_value=True),patch.object(verify.subprocess,'run',side_effect=subprocess.CalledProcessError(1,['npm','test'])) as run:
            self.assertEqual(verify.main(['--viewer-only','--skip-install']),1)
            self.assertEqual(run.call_count,1)
    def test_dry_run_never_executes(self):
        with contextlib.redirect_stdout(io.StringIO()),patch.object(verify.subprocess,'run') as run:
            self.assertEqual(verify.main(['--dry-run']),0)
            run.assert_not_called()

if __name__=='__main__':unittest.main()
