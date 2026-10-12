"""Sweep cache configurations and create a CSV plus a standalone HTML report."""
import argparse
import csv
import html
import subprocess
from pathlib import Path

from generate_traces import generate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--out", default="results")
    parser.add_argument("--n", type=int, default=32)
    parser.add_argument("--tile", type=int, default=8)
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())
    output = Path(args.out)
    output.mkdir(parents=True, exist_ok=True)
    traces = output / "traces"
    generate(traces, args.n, args.tile)
    rows = []
    for trace in sorted(traces.glob("*.trace")):
        for capacity in (1024, 4096, 16384):
            for ways in (1, 4):
                for policy in ("lru", "fifo"):
                    csv_path = output / "last_run.csv"
                    subprocess.run([binary, "--trace", str(trace), "--size", str(capacity),
                        "--line", "64", "--ways", str(ways), "--policy", policy, "--csv", str(csv_path)],
                        check=True, stdout=subprocess.DEVNULL)
                    with csv_path.open(newline="", encoding="utf-8") as stream:
                        row = next(csv.DictReader(stream))
                    rows.append({"workload": trace.stem, **row})
    with (output / "comparison.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    table = []
    for row in rows:
        rate = float(row["hit_rate"]) * 100
        table.append("<tr>" + "".join(f"<td>{html.escape(row[key])}</td>" for key in
            ("workload", "capacity_bytes", "ways", "replacement", "accesses", "misses")) +
            f'<td><meter min="0" max="100" value="{rate:.4f}"></meter> {rate:.2f}%</td></tr>')
    report = f"""<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Cache Simulator Results</title>
<style>body{{font:15px system-ui,sans-serif;margin:32px;color:#172a3a;background:#f6f8fa}}
main{{max-width:1100px;margin:auto}}h1{{margin-bottom:8px}}p{{max-width:850px;line-height:1.6}}
table{{width:100%;border-collapse:collapse;background:white}}th,td{{padding:10px;text-align:left;border-bottom:1px solid #ddd}}
th{{background:#173c50;color:white;position:sticky;top:0}}meter{{width:80px}}.scroll{{overflow:auto}}</style>
<main><h1>Cache Simulator: Locality Experiments</h1>
<p>{len(rows)} configurations from five synthetic workloads. Matrix size: {args.n} x {args.n}; tile: {args.tile}.
All runs start with an empty cache, use 64-byte lines, write-back and write-allocate.</p>
<p>These are simulated block hits and misses, not hardware performance measurements.
Matrix traces model scalar reads and writes without compiler register reuse or hardware prefetching.
Tiling changes loop order as well as grouping; it is not guaranteed to win for every configuration.</p>
<div class="scroll"><table><thead><tr><th>Workload</th><th>Cache bytes</th><th>Ways</th><th>Policy</th>
<th>Accesses</th><th>Misses</th><th>Hit rate</th></tr></thead><tbody>{''.join(table)}</tbody></table></div></main></html>"""
    (output / "report.html").write_text(report, encoding="utf-8")
    print(f"Completed {len(rows)} runs. Open {output / 'report.html'}")


if __name__ == "__main__":
    main()
