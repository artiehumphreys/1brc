import subprocess
import os

from pathlib import Path

BENCH_DIR = Path("/bench")


def main() -> None:
    outputs = []
    for thread_count in range(1, 12):
        subprocess.run(["make", f'CPPFLAGS="-THREAD_COUNT={thread_count}"'])
        os.chdir(BENCH_DIR)
        res = subprocess.run(
            ["perf", "stat", "-r", "5" "-d" "./1brc"], capture_output=True, text=True
        )
        outputs.append(res)

    print(outputs)


if __name__ == "__main__":
    main()
