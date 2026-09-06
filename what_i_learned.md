I learned a lot throughout the process of this project, especially the following
points that are important to consider when designing high-throughput programs.

1. _Profiling_

perf was my go-to throughout this process for diagnosing not only latencies in
my hot path, but also as a wall-clock harness for measuring end-to-end
throughput. Beyond evaluating the hot path, perf was how I came to understand
the layer that was responsible for my program stalling: the page cache and
readahead on the OS side, or L1d/L2 and the hardware prefetchers on the CPU
side.

I also learned to take these metrics with a grain of salt, particularly the wall
clock timings and cycle counters. Certain methods may seem to consume a lot of
cycles when in reality they're stalled. This isn't to say that the cycle counter
is wrong, but these counters measure elapsed execution rather than useful work.
A minor fault that installs a page-table entry (PTE) or a simple data miss burns
cycles all the same. Being able to discern what is causing the influx in clock
cycles is the important distinction to make.

Working with a large mapped file (size nearing the amount of available RAM I
had), I experienced high page cache pressure, where the kernel could not keep
the file mapping resident. This led to clean pages being reclaimed with the next
access to that region faulting back to disk. My program was inherently I/O
bound, regardless of how tight my parse loop was. This rendered any wall clock
measurement essentially useless for measuring parsing optimizations and led to a
lot of confusion until I considered the number of major faults triggered by my
program. My workaround for this was just to use a smaller file, which worked
well given my hardware limitations.

2. SIMD / SWAR

TODO

3. `mmap`

I think it's easy to fall into the 'oh, just mmap the file' spell without really
understanding how it works underneath. Essentially, mmap reserves virtual
address space where the file will live within the process, doing 0 work upfront.
However, the first touch of an address leads to a fault, and only then does the
kernel attach a page frame, pull the data in, and install a PTE. One thing that
I didn't know beforehand was that with memory-mapped files, Linux amortizes this
action with the `fault_around_bytes` variable that determines how many
surrounding PTEs are installed on each minor fault.

`mmap` completely bypasses the typical kernel-to-user space copy that is
associated with the `read()` syscall. A mapped page is exactly the page cache
page, meaning that the process' PTE just points to the frame that the page cache
already owns. Either way, the page cache is present in the path, but the
file-backed mapping prevents the data copy into a private user-space buffer.

One strategy that I employed came from the
[simdjson repo](https://github.com/simdjson/simdjson/blob/master/doc/performance.md#free-padding)
that allowed me to completely drop the tail boundary check on my SIMD loads. The
problem I had to account for was the final register load reading past the last
byte of the file: if the size of the file happens to land on a page boundary,
that read touches a page entirely outside the mapping and the kernel raises a
`SIGBUS`. The fix is to map two regions on top of each other, with one anonymous
mapping that covers the entire file plus a buffer that stretches a register
read, then map the actual file over the front of it. This allowed the tail of
the last read to be readable, zero-filled, and not affect the current
implementation I had in any way. The exact method I took inspiration from can be
found
[here](https://github.com/simdjson/simdjson/blob/master/include/simdjson/padded_string-inl.h#L397).
