#!/usr/bin/env python3
"""Build and verify the synthetic-input TerraFlux physics and viewer pipeline."""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def build_plan(args):
    plan=[]
    build=args.build_dir.resolve()
    if not args.viewer_only:
        if not args.skip_configure:
            command=['cmake','-S',str(ROOT),'-B',str(build),'-DBUILD_TESTING=ON','-DCMAKE_BUILD_TYPE=Release']
            if args.cmake_prefix_path:command.append(f'-DCMAKE_PREFIX_PATH={args.cmake_prefix_path}')
            plan.append(('Configure C++',command,ROOT))
        if not args.skip_build:plan.append(('Build C++',['cmake','--build',str(build),'--parallel',str(args.jobs)],ROOT))
        plan.append(('C++ tests and fixture regeneration',['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error'],ROOT))
        plan.append(('Python composer tests',[sys.executable,'-m','unittest','discover','-s','tests/tools','-v'],ROOT))
    if not args.cpp_only:
        if not args.skip_install:plan.append(('Install locked viewer dependencies',['npm','ci','--no-audit','--no-fund'],ROOT/'viewer'))
        plan.append(('Viewer tests',['npm','test'],ROOT/'viewer'))
        plan.append(('Viewer production build',['npm','run','build'],ROOT/'viewer'))
    return plan

def parse_args(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build')
    parser.add_argument('--cmake-prefix-path',help='installed dependency prefix or CMake semicolon-separated prefixes')
    parser.add_argument('--jobs',type=int,default=2)
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--cpp-only',action='store_true',help='verify only C++/Python, not the viewer')
    mode.add_argument('--viewer-only',action='store_true',help='verify only the viewer')
    parser.add_argument('--skip-configure',action='store_true',help='reuse an existing CMake configuration')
    parser.add_argument('--skip-build',action='store_true',help='reuse already-built C++ executables')
    parser.add_argument('--skip-install',action='store_true',help='reuse installed viewer dependencies')
    parser.add_argument('--dry-run',action='store_true',help='print commands without executing or claiming verification')
    args=parser.parse_args(argv)
    if args.jobs<1:parser.error('--jobs must be positive')
    if args.build_dir.resolve()==ROOT:parser.error('use a separate build directory')
    if args.viewer_only and (args.skip_configure or args.skip_build or args.cmake_prefix_path):parser.error('C++ options do not apply to --viewer-only')
    if args.cpp_only and args.skip_install:parser.error('--skip-install does not apply to --cpp-only')
    if args.skip_build and not args.skip_configure:parser.error('--skip-build requires --skip-configure')
    return args

def main(argv=None):
    args=parse_args(argv);plan=build_plan(args)
    if not args.dry_run:
        missing=sorted({command[0] for _,command,_ in plan if not shutil.which(command[0])})
        if missing:
            print('Missing required tools: '+', '.join(missing),file=sys.stderr);return 2
        if args.skip_configure and not (args.build_dir/'CMakeCache.txt').is_file():
            print('Cannot reuse configuration: CMakeCache.txt is missing.',file=sys.stderr);return 2
        if args.skip_install and not (ROOT/'viewer/node_modules').is_dir():
            print('Cannot reuse viewer dependencies: node_modules is missing.',file=sys.stderr);return 2
    for index,(label,command,cwd) in enumerate(plan,1):
        print(f'[{index}/{len(plan)}] {label}\n  {shlex.join(command)}',flush=True)
        if args.dry_run:continue
        try:subprocess.run(command,cwd=cwd,check=True)
        except (OSError,subprocess.CalledProcessError) as error:
            print(f'FAILED: {label}: {error}',file=sys.stderr);return 1
    if args.dry_run:print('Dry run only: no checks executed.')
    else:
        scope='C++/Python' if args.cpp_only else 'viewer' if args.viewer_only else 'C++/Python and viewer'
        print(f'PASS: {scope} checks. Synthetic inputs only; browser/WebGL and live-provider verification are separate.')
    return 0

if __name__=='__main__':sys.exit(main())
