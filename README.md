# Cache Simulator & Memory-Access Analyzer

A small C++17 cache simulator for exploring how memory-access order, cache geometry,
replacement policy, and write policy change simulated hits, misses, and memory traffic.
Runs on a laptop CPU. No GPU, FPGA, paid EDA tool, or Python package installation is required.

## What it does

- Configurable capacity, line size, and associativity: direct-mapped through fully associative.
- LRU and FIFO replacement policies.
- Write-back or write-through, with optional write allocation.
- Streaming trace replay with hexadecimal 64-bit addresses and byte-sized accesses.
- Correct splitting of accesses that cross cache-line boundaries.
- Hits, misses, evictions, dirty writebacks, memory traffic, and remaining dirty lines.
- Synthetic sequential, column-scan, random, naive-matmul, and tiled-matmul workloads.
- A reproducible 60-run comparison with CSV output and a standalone HTML report.
- Unit tests, randomized reference-model checking, and a GitHub Actions workflow.

## Quick start

### Windows

Requirements: Python 3.9+ and Visual Studio Build Tools with the **Desktop development
with C++** workload. If these are installed, open PowerShell in this folder:

```powershell
.\scripts\build_windows.cmd
.\build\cache_sim.exe --trace traces\example.trace --size 16 --line 4 --ways 2
python scripts\benchmark.py --binary build\cache_sim.exe
Start-Process results\report.html
```

The build script locates Visual Studio, compiles both executables, and runs the C++
and Python tests. It does not install software or change global settings.

### Linux / WSL

Requirements: a C++17 compiler with standard filesystem support (GCC 9+ is a convenient
choice), Make, and Python 3.9+.

```sh
make
make test
./build/cache_sim --trace traces/example.trace --size 16 --line 4 --ways 2
python3 scripts/benchmark.py --binary build/cache_sim
```

Open `results/report.html` in a browser. No web server or external assets are needed.

### CMake alternative

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run Python integration checks with `python tests/test_integration.py --binary PATH`.
The binary is normally `build/cache_sim` on Linux and `build/Release/cache_sim.exe`
with the Visual Studio generator. CTest runs the C++ test executable.

## Trace format

```text
# Operation, hexadecimal address, optional decimal byte count (defaults to 1)
R 0x1000 4
W 0x1004 4
R ffff 1  # an address may omit the 0x prefix
```

Only `R` and `W` are accepted. Blank lines and `#` comments are ignored. Sizes must
be between 1 and 1,048,576 bytes; overflowing 64-bit address ranges are rejected.
Invalid input reports its line number and exits with status 2.

Capacity, line size, and ways must be positive powers of two. A cache must contain
at least one complete set, and the model limits allocation to 1,048,576 cache lines.
An empty trace has a defined hit rate of zero.

## Example

```sh
./build/cache_sim --trace traces/example.trace --size 16 --line 4 --ways 2 --policy lru
```

Expected result: **8 records, 4 hits, 4 misses, 1 eviction, and 1 dirty writeback**.
The example is deliberately small enough to verify by hand.

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `--size` | `32768` | Cache data capacity in bytes |
| `--line` | `64` | Cache-line size in bytes |
| `--ways` | `4` | Lines per set |
| `--policy` | `lru` | `lru` or `fifo` |
| `--write-policy` | `write-back` | `write-back` or `write-through` |
| `--write-allocate` | `yes` | Fill a cache line on a write miss |
| `--csv` | none | Export configuration and statistics to a file |

## Locality experiment

```sh
python3 scripts/generate_traces.py --out traces/generated --n 32 --tile 8
python3 scripts/benchmark.py --binary build/cache_sim --n 32 --tile 8
```

The benchmark sweeps five workloads, three capacities (1/4/16 KiB), two
associativities (1/4), and two replacement policies (LRU/FIFO). Each run starts cold,
uses 64-byte lines, and uses write-back with write allocation. The default workload
seed is 2026.

Example reproduced on 2026-10-12 with the local Windows build:

| Synthetic 32 x 32 matrix workload | Cache | Block accesses | Misses | Hit rate |
|---|---|---:|---:|---:|
| Naive `i,j,k` traversal | 4 KiB, 4-way, LRU | 131,072 | 4,652 | 96.45% |
| Tiled traversal, tile 8 | 4 KiB, 4-way, LRU | 131,072 | 764 | 99.42% |

These numbers describe this model and workload, **not execution-time speedups or
measurements of the host CPU's cache**. Both matrix traces perform the same number
of logical accesses. Tiling also changes the inner loop order, so this comparison
does not isolate blocking alone. Other cache configurations can behave differently.

Checked-in results: [comparison CSV](docs/example-comparison.csv).

## Model semantics

For an address `a`, line size `B`, and set count `S`:

```text
block  = a / B
set    = block % S
tag    = block / S
offset = a % B
S      = capacity / (B * ways)
```

- A fill reads a complete line from backing memory, including on an allocating write miss.
- Write-through stores write only the bytes requested by that access.
- A dirty write-back eviction writes a complete line.
- A no-write-allocate miss writes directly to memory and leaves existing cache lines untouched.
- Hits update LRU recency but do not change FIFO insertion order.
- Multi-line records count as one trace record and multiple block accesses.
- Dirty resident lines are **not flushed** at end of trace; they are reported separately.

## Verification

- 14 C++ test groups cover conflicts, replacement, write policies, line crossings,
  set independence, high addresses, parsing, and rejected inputs.
- 8 Python test cases cover the CLI, expected example counts, empty traces, error
  reporting, output-path protection, and workload generation.
- The randomized test replays 500 seeded records across 24 configurations and
  compares every integer statistic against an independent Python `OrderedDict` model.
- Workload tests verify deterministic generation and identical logical matrix-access
  multisets even when matrix dimensions are not divisible by the tile size.
- GitHub Actions is configured for Linux and Windows; remote CI status is only
  established after the workflow actually runs.

See [validation notes](docs/VALIDATION.md) for the local run and limitations.

## Scope and limitations

This is a functional, single-level cache model. It does not simulate L2/L3,
prefetching, coherence, TLBs, MSHRs, timing, or instruction execution. It does not
classify compulsory/conflict/capacity misses or report IPC/MPKI. Input addresses
are interpreted directly without virtual-to-physical translation.

Synthetic matrix traces model scalar array accesses, including each update to C.
They omit compiler optimizations and register reuse. They are useful for controlled
locality comparisons, not predictions of a real CPU or GPU.

## Layout

```text
include/cache.hpp            Cache model and counters
include/trace.hpp            Trace parser
src/main.cpp                 Command-line interface and CSV export
tests/test_cache.cpp         C++ unit tests
tests/test_integration.py    Python reference model and integration checks
scripts/generate_traces.py   Reproducible synthetic workloads
scripts/benchmark.py         Configuration sweep and HTML report
scripts/build_windows.cmd   Windows build and test entry point
traces/example.trace        Hand-checkable sample
docs/                       Recorded results and validation notes
```

## Development

Created for Tarun Pratap Singh with AI-assisted implementation and local validation.
This repository is an original implementation, not a renamed copy of the reference
repositories discussed during project planning. See [LICENSE](LICENSE).
