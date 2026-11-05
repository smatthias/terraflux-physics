# Reproducible verification

From the repository checkout:

```sh
python3 tools/verify_pipeline.py
```

The default command performs, in order:

1. CMake configuration with BUILD_TESTING enabled and Release mode.
2. C++ build.
3. CTest, including kernel tests, gridding/composer integration, scenario validation, baseline/variant/outlet fixture regeneration, timestep sensitivity and basin checks. The ocean and polar fixture checks regenerate multi-year runs; the two ocean-carbon fixture checks take about a minute each. `weather.viewer_polar_ocean` alone takes about 5 minutes; it carries the CTest label `slow`, so `ctest --test-dir build -LE slow` skips it in quick local loops.
4. Python composer and verification-runner tests.
5. Locked viewer installation with npm ci.
6. Viewer data, comparison and CSV tests.
7. Viewer production build.

It checks for required command-line tools before execution, uses argument lists without shell interpolation, stops at the first failed stage and returns a nonzero status. It does not rewrite committed generated examples: CTest regeneration checks use --check. npm ci may replace node_modules and the build steps update ignored build output.

## Prerequisites

Python 3.10+, a modern CMake/CTest supporting --test-dir and --no-tests=error, a C++17 toolchain, Node.js 22 with npm, and installed native build dependencies matching CMakeLists.txt (libpng 1.6.59+ and nlohmann/json 3.12.0+). The command does not install native dependencies. The pinned dependency build recipe used by CI is in `.github/workflows/weather-ci.yml`.

If native dependencies are installed under a separate prefix:

```sh
python3 tools/verify_pipeline.py --cmake-prefix-path /path/to/dependencies
```

The existing CMAKE_PREFIX_PATH environment setting is also honored by CMake. Choose a separate build directory and concurrency if needed:

```sh
python3 tools/verify_pipeline.py --build-dir build-review --jobs 2
```

## Explicit partial or reused checks

```sh
# Inspect commands only; performs no verification.
python3 tools/verify_pipeline.py --dry-run
# Existing configured and built C++ outputs, as used by the C++ CI job.
python3 tools/verify_pipeline.py --cpp-only --skip-configure --skip-build
# Existing locked viewer installation, as used by the viewer CI job.
python3 tools/verify_pipeline.py --viewer-only --skip-install
```

Partial modes name their scope in the final result. Reused checks require the expected CMake cache or node_modules folder. By default both components are configured/built/tested. CI uses the same runner in two jobs after its dependency setup rather than duplicating test commands.

All checked examples use synthetic inputs. PASS means the selected automated checks passed, not verified physical forecasting skill, live NOAA integration, successful WebGL rendering or an approved Taiga submission. Browser/WebGL and real-provider checks remain separate. AI and behavior prediction are outside this verification scope.
