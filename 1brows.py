import random
import string
import sys
import operator
import os
from multiprocessing import Pool

MIN_STATIONS: int = 5000
MAX_STATIONS: int = 10000
MAX_NAME_LENGTH: int = 16
SEED: int = 42
FILE_NAME = "input.txt"
CHUNK_ROWS = 1 << 21
NUM_ROWS = 1_000_000_000

DATA_VALUES: list[bytes] = [b"%.1f\n" % (i / 10) for i in range(-999, 1000)]

_STATIONS: list[bytes] = []


def make_stations(num_stations: int, rng: random.Random) -> list[bytes]:
    # deterministic name generation across process, order shuffled after generation
    seen = set()
    while len(seen) < num_stations:
        k = rng.randint(1, MAX_NAME_LENGTH)
        seen.add(
            ("".join(rng.choices(string.ascii_letters, k=k)) + ";").encode()
        )  # add delimiter too

    return sorted(seen)


def _init(stations: list[bytes]):
    global _STATIONS
    _STATIONS = stations


def _chunk(job: tuple[int, int]) -> bytes:
    seed, num_rows = job
    rng = random.Random(seed)

    names = rng.choices(_STATIONS, k=num_rows)
    values = rng.choices(DATA_VALUES, k=num_rows)

    return b"".join(map(operator.add, names, values))


def generate_file(seed: int = SEED) -> int:
    rng = random.Random(seed)
    num_stations = rng.randint(MIN_STATIONS, MAX_STATIONS)
    stations = make_stations(num_stations=num_stations, rng=rng)

    # assumption: with 1b samples, each station SHOULD appear at least once.
    # probability of one missing ~= 10_000 * e^{-100_000}

    # random seeds for each chunk of the written file
    jobs = [
        (rng.randrange(1 << 32), min(CHUNK_ROWS, NUM_ROWS - i))
        for i in range(0, NUM_ROWS, CHUNK_ROWS)
    ]

    written = 0
    with open(FILE_NAME, "wb") as f, Pool(
        initializer=_init, initargs=(stations,)
    ) as pool:
        for buf in pool.imap_unordered(func=_chunk, iterable=jobs):
            f.write(buf)
            written += buf.count(b"\n")

    return written


if __name__ == "__main__":
    written = generate_file()
    if written != NUM_ROWS:
        raise SystemExit(f"row count mismatch: wrote {written}, expected{NUM_ROWS}")

    print(f"Wrote {FILE_NAME}: {written} rows")
