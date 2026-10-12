# Validation record

Date: 2026-10-12.

## Environment

- Windows x64, Visual Studio 2026 Build Tools developer environment (18.9.2).
- MSVC, C++17, `/W4 /O2 /EHsc`.
- Python 3.13.15; only standard-library modules used.

## Commands

```powershell
.\scripts\build_windows.cmd
python scripts\benchmark.py --binary build\cache_sim.exe
```

The final test run passed all 14 C++ test groups and all 8 Python tests.
The latter include 24 randomized reference comparisons, each replaying the same 500
seeded read/write records. Records can span multiple lines.

The benchmark ran 60 combinations: 5 generated workloads x 3 capacities x 2
associativities x 2 replacement policies. The checked-in `example-comparison.csv`
contains that run's configurations and counters. Regenerate the HTML report with
the benchmark command; generated traces and binaries are ignored by Git.

## What is and is not established

The local build and reference comparisons check functional behavior under the
tested configurations. They do not constitute a formal proof, host-cache
measurement, or FPGA/ASIC implementation. Timing and hardware speedup claims
are intentionally absent. Linux builds and remote CI have not been run locally.

The work was implemented with AI assistance and checked with executable tests.
