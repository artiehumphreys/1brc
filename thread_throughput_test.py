import re
import subprocess

from pathlib import Path
import matplotlib.pyplot as plt

ROOT = Path(__file__).parent
BENCH_DIR = ROOT / "bench"

PERF_OUTPUT_PREFIX = "final_stats_threads"

_ELAPSED = re.compile(r"([\d.]+)\s*\+-\s*([\d.]+)\s+seconds time elapsed")
_LLC_MISS = re.compile(r"([\d,]+)\s+LLC-load-misses(?=\s)")


def parse_perf_stat(path: Path) -> dict[str, float | int]:
    output = path.read_text()
    avg_elapsed_time = _ELAPSED.search(output)
    llc_misses = _LLC_MISS.search(output)

    if avg_elapsed_time is None or llc_misses is None:
        raise ValueError(f"bad output for {path}")

    return {
        "elapsed_s": float(avg_elapsed_time.group(1)),
        "elapsed_stderr_s": float(avg_elapsed_time.group(2)),
        "llc_load_misses": int(llc_misses.group(1).replace(",", "")),
    }


def run_perf(max_thread_count: int = 12) -> None:
    for thread_count in range(1, (max_thread_count + 1)):
        subprocess.run(
            ["make", "-B", f"CPPFLAGS=-DTHREAD_COUNT={thread_count}"],
            cwd=ROOT,
            check=True,
        )
        res = subprocess.run(
            ["perf", "stat", "-r", "5", "-d", "./1brc"],
            cwd=BENCH_DIR,
            capture_output=True,
            text=True,
            check=True,
        )

        report = res.stderr
        (BENCH_DIR / f"{PERF_OUTPUT_PREFIX}_{thread_count}.txt").write_text(report)


def graph_results(stats: list[dict[str, int | float]]) -> None:
    threads = range(1, len(stats) + 1)

    fig, ax = plt.subplots()

    ax.errorbar(
        threads,
        [s["elapsed_s"] for s in stats],
        yerr=[s["elapsed_stderr_s"] for s in stats],
        fmt="o-",
        capsize=4,
        elinewidth=1,
    )
    ax.set_xlabel("threads")
    ax.set_ylabel("elapsed (s)")
    fig.savefig(BENCH_DIR / "elapsed.png", dpi=150)

    fig, ax = plt.subplots()
    ax.plot(threads, [s["llc_load_misses"] for s in stats], "o-")
    ax.set_xlabel("threads")
    ax.set_ylabel("LLC load misses")
    fig.savefig(BENCH_DIR / "llc.png", dpi=150)


def _collect() -> list[Path]:
    return sorted(
        BENCH_DIR.glob(f"{PERF_OUTPUT_PREFIX}_*.txt"),
        key=lambda p: int(p.stem.rsplit("_", 1)[1]),
    )


def main() -> None:
    max_thread_count = 12
    files = _collect()
    if len(files) != max_thread_count:
        run_perf(max_thread_count=max_thread_count)
        files = _collect()

    stats = [parse_perf_stat(f) for f in files]
    graph_results(stats)


if __name__ == "__main__":
    main()
