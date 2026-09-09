import re
import subprocess

from pathlib import Path

ROOT = Path(__file__).parent
BENCH_DIR = ROOT / "bench"

ELAPSED = re.compile(r"^\s*(\S+) \+- (\S+) seconds time elapsed", re.M)


def main() -> None:
    for thread_count in range(1, 13):
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
        (BENCH_DIR / f"final_stats_threads_{thread_count}.txt").write_text(report)

        m = ELAPSED.search(report)
        print(
            f"{thread_count:2d} threads: {m.group(1)}s +- {m.group(2)}" if m else report
        )


if __name__ == "__main__":
    main()
