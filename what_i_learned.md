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

2. _SIMD / SWAR_

I had no previous experience writing any vectorized code, so this was a great
exercise to familiarize myself with SIMD programming. Most CPU cores come with
SIMD registers, that allow the processor to perform the same instruction across
multiple data points at a time. These registers vary in sizes, with 256-bit /
512-bit being the most common sizes present on most production machines. The
processor on my workbench has 16 256-bit SIMD registers, which I used to greatly
increase the throughput of my input parsing. Originally, I operated by
processing one line at a time, copying the data from the mapped pointer given by
memory mapping my file (more on that later), into a local buffer within the
process. This proved to be rather inefficient as it took time, and most
importantly, branches, to find the position of the semicolon and newline in each
line using `std::ranges::find`.

My revised approach read 64 bytes of input at a time, copying the two 32-byte
halves into SIMD registers before utilizing bitmask operations to determine the
location of the semicolon and newline of each line (or fraction thereof)
contained in the bytes. I will gloss over the details of the SIMD operations to
establish my methodology. Since each line was between 6 and 23 bytes, I could
find the boundaries for anywhere from 2 to 10 lines at a time. This beat the
pointer chasing established with the naive method, where I operated one line at
a time and derived the beginning of the subsequent line during the parsing.
Extracting all of the delimiter positions up front broke that chain and allowed
the per-line work to be truly independent.

> **Aside:** Here's exactly how I utilized SIMD to find the positions of the
> delimiters in a 64-byte chunk of input:
>
> 1. Copy the two 32-byte halves into two SIMD registers as well as the
>    delimiters, broadcasting their 8-bit value across all 32 vector lanes of
>    their respective register.
> 2. Compare each half against each delimiter byte-wise, resulting in a vector
>    whose lanes are `0xFF` where the byte matched and `0x00` otherwise.
> 3. Reduce each comparison to 32 bits by taking the MSB of each element. The
>    position of each set bit is the index in which there is a delimiter present
>    in that half.

When it came to parsing the temperature, a value within $[-99.9, 99.9]$
formatted to the tenths place, I actually endead up going a different path from
SIMD, keeping the work in a general-purpose register. Each temperature reading
fits into a single 8-byte word, and the value could be extracted via bit
manipulation and multiplication. Credit to
[merrykitty](https://github.com/merykitty/1brc/blob/1a4ac0d2496e9329534370eaf01b28e77658b073/src/main/java/dev/morling/onebrc/CalculateAverage_merykitty.java)
who pioneered the method. I attempted to leave some comments in my code
explaining my interpretation of the madness. This parse is completely
branchless, even with regards to the sign and number of integer digits. I did
attempt to vectorize this across the lines present within the SIMD register, but
the temperature fields sit at data-dependent offsets, and AVX2 provides no easy
way to align values in lanes that vary per line.

3. `mmap`

I think it's easy to fall into the 'oh, just mmap the file' spell without really
understanding how it works underneath. Essentially, mmap reserves virtual
address space where the file will live within the process, doing 0 work upfront.
However, the first touch of an address leads to a fault, and only then does the
kernel attach a page frame, pull the data in, and install a PTE. One thing that
I didn't know beforehand was that with memory-mapped files, Linux amortizes this
action with the `fault_around_bytes` variable that determines how many
surrounding PTEs are installed on each minor fault.

> **Aside:** I actually tested my program across different values of
> `fault_around_bytes`, particularly measuring throughput vs. page faults. Any
> value past the system default actually performed approximately the same with
> respect to the wall clock on my machine, the only difference being the
> decreased number of page faults. This informed me that my read path was
> actually bandwidth bound and not fault bound.

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
