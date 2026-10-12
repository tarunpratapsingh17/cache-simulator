"""CLI checks and an independently structured OrderedDict reference model."""
import argparse
import csv
import random
import subprocess
import sys
import tempfile
import unittest
from collections import Counter, OrderedDict
from pathlib import Path

PARSER = argparse.ArgumentParser()
PARSER.add_argument("--binary", required=True)
ARGS, REMAINING = PARSER.parse_known_args()
BINARY = str(Path(ARGS.binary).resolve())


def reference(records, capacity, line_size, ways, policy, write_policy, allocate):
    sets = [OrderedDict() for _ in range(capacity // line_size // ways)]
    totals = dict.fromkeys([
        "records", "accesses", "reads", "writes", "hits", "misses", "evictions",
        "writebacks", "memory_read_bytes", "memory_write_bytes", "dirty_lines",
    ], 0)
    for operation, address, size in records:
        totals["records"] += 1
        for block in range(address // line_size, (address + size - 1) // line_size + 1):
            count = min(address + size, (block + 1) * line_size) - max(address, block * line_size)
            bucket = sets[block % len(sets)]
            totals["accesses"] += 1
            totals["writes" if operation == "W" else "reads"] += 1
            if block in bucket:
                totals["hits"] += 1
                if policy == "lru":
                    bucket.move_to_end(block)
            else:
                totals["misses"] += 1
                if operation == "W" and not allocate:
                    totals["memory_write_bytes"] += count
                    continue
                if len(bucket) == ways:
                    _, dirty = bucket.popitem(last=False)
                    totals["evictions"] += 1
                    if dirty:
                        totals["writebacks"] += 1
                        totals["memory_write_bytes"] += line_size
                bucket[block] = False
                totals["memory_read_bytes"] += line_size
            if operation == "W":
                if write_policy == "write-back":
                    bucket[block] = True
                else:
                    totals["memory_write_bytes"] += count
    totals["dirty_lines"] = sum(sum(bucket.values()) for bucket in sets)
    return totals


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[1] / "build" / "test_tmp"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_trace(self, text, *options):
        trace = self.root / "input.trace"
        output = self.root / "output.csv"
        trace.write_text(text, encoding="utf-8")
        result = subprocess.run([BINARY, "--trace", str(trace), "--csv", str(output), *options],
                                capture_output=True, text=True)
        rows = []
        if result.returncode == 0:
            with output.open(encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))
        return result, rows

    def test_randomized_against_reference(self):
        rng = random.Random(2026)
        records = [(rng.choice("RW"), rng.randrange(256), rng.randrange(1, 10)) for _ in range(500)]
        text = "".join(f"{op} 0x{addr:x} {size}\n" for op, addr, size in records)
        for ways in (1, 2, 8):
            for policy in ("lru", "fifo"):
                for write_policy in ("write-back", "write-through"):
                    for allocate in (False, True):
                        with self.subTest(ways=ways, policy=policy, write_policy=write_policy, allocate=allocate):
                            result, rows = self.run_trace(text, "--size", "32", "--line", "4", "--ways", str(ways),
                                "--policy", policy, "--write-policy", write_policy,
                                "--write-allocate", "yes" if allocate else "no")
                            self.assertEqual(result.returncode, 0, result.stderr)
                            expected = reference(records, 32, 4, ways, policy, write_policy, allocate)
                            for key, value in expected.items():
                                self.assertEqual(int(rows[0][key]), value, key)
                            self.assertAlmostEqual(float(rows[0]["hit_rate"]), expected["hits"] / expected["accesses"], places=8)

    def test_example_has_known_counts(self):
        example = Path(__file__).resolve().parents[1] / "traces" / "example.trace"
        result, rows = self.run_trace(example.read_text(), "--size", "16", "--line", "4", "--ways", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        for key, value in {"records": 8, "hits": 4, "misses": 4, "evictions": 1, "writebacks": 1}.items():
            self.assertEqual(int(rows[0][key]), value)

    def test_empty_trace(self):
        result, rows = self.run_trace("# empty\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(float(rows[0]["hit_rate"]), 0)
        self.assertEqual(int(rows[0]["accesses"]), 0)

    def test_error_includes_line_number(self):
        result, _ = self.run_trace("R 0\n\nW nonsense 4\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("trace line 3", result.stderr)

    def test_invalid_cli_and_configuration(self):
        for args in (["--size"], ["--trace", "missing.trace"], ["--unknown", "1"]):
            result = subprocess.run([BINARY, *args], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
        result, _ = self.run_trace("R 0\n", "--size", "3")
        self.assertEqual(result.returncode, 2)

    def test_trace_cannot_be_overwritten_by_csv(self):
        trace = self.root / "input.trace"
        trace.write_text("R 0\n")
        for destination in (str(trace), str(self.root) + "/./input.trace"):
            result = subprocess.run([BINARY, "--trace", str(trace), "--csv", destination], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(trace.read_text(), "R 0\n")

    def test_generated_matmul_traces_preserve_work(self):
        generator = Path(__file__).resolve().parents[1] / "scripts" / "generate_traces.py"
        output = self.root / "generated"
        subprocess.run([sys.executable, str(generator), "--out", str(output), "--n", "5", "--tile", "2"],
                       check=True, capture_output=True, text=True)
        def records(name):
            return [line for line in (output / name).read_text().splitlines() if not line.startswith("#")]
        naive = records("matmul_naive.trace")
        tiled = records("matmul_tiled.trace")
        self.assertEqual(len(naive), 4 * 5 ** 3)
        self.assertEqual(Counter(naive), Counter(tiled))
        self.assertNotEqual(naive, tiled)
        self.assertEqual(len(list(output.glob("*.trace"))), 5)

    def test_generated_traces_are_reproducible(self):
        generator = Path(__file__).resolve().parents[1] / "scripts" / "generate_traces.py"
        output = self.root / "generated"
        command = [sys.executable, str(generator), "--out", str(output), "--n", "4", "--tile", "2"]
        subprocess.run(command, check=True, capture_output=True)
        before = {path.name: path.read_bytes() for path in output.glob("*.trace")}
        subprocess.run(command, check=True, capture_output=True)
        self.assertEqual(before, {path.name: path.read_bytes() for path in output.glob("*.trace")})


if __name__ == "__main__":
    unittest.main(argv=[__file__, *REMAINING])
