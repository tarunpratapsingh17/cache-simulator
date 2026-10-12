"""Generate synthetic, reproducible access patterns. No actual program tracing."""
import argparse
import random
from pathlib import Path


def generate(output, n=32, tile=8, seed=2026):
    if not 1 <= n <= 128 or not 1 <= tile <= n:
        raise ValueError("require 1 <= tile <= n <= 128")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    def save(name, records):
        with (output / f"{name}.trace").open("w", encoding="ascii") as stream:
            stream.write(f"# Synthetic workload: {name}; n={n}, tile={tile}, seed={seed}\n")
            for op, address, size in records:
                stream.write(f"{op} 0x{address:x} {size}\n")

    save("sequential", (("R", index * 4, 4) for _ in range(4) for index in range(n * n)))
    save("column_scan", (("R", (row * n + col) * 4, 4) for _ in range(4) for col in range(n) for row in range(n)))
    rng = random.Random(seed)
    save("random", (("R", rng.randrange(n * n) * 4, 4) for _ in range(4 * n * n)))

    # Separate 64-byte-aligned arrays. Both variants use identical scalar access
    # counts and perform the same logical updates; only loop order changes.
    span = ((n * n * 4 + 63) // 64) * 64
    def update(i, j, k):
        yield "R", (i * n + k) * 4, 4
        yield "R", span + (k * n + j) * 4, 4
        yield "R", 2 * span + (i * n + j) * 4, 4
        yield "W", 2 * span + (i * n + j) * 4, 4
    def naive():
        for i in range(n):
            for j in range(n):
                for k in range(n):
                    yield from update(i, j, k)
    def tiled():
        for ii in range(0, n, tile):
            for jj in range(0, n, tile):
                for kk in range(0, n, tile):
                    for i in range(ii, min(ii + tile, n)):
                        for k in range(kk, min(kk + tile, n)):
                            for j in range(jj, min(jj + tile, n)):
                                yield from update(i, j, k)
    save("matmul_naive", naive())
    save("matmul_tiled", tiled())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="traces/generated")
    parser.add_argument("--n", type=int, default=32)
    parser.add_argument("--tile", type=int, default=8)
    parser.add_argument("--seed", type=int, default=2026)
    args = parser.parse_args()
    generate(args.out, args.n, args.tile, args.seed)
    print(f"Generated 5 synthetic traces in {args.out}")
