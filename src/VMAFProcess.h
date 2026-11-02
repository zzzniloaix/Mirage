#pragma once

// What the ladder's pool reads about the machine and the process (work order
// section 2; DECISIONS.md D-044, D-045): the physical cores, for the pool's
// default size, and the process's resident memory, for the memory limit. No
// FFmpeg / libvmaf here so it can be unit-tested in mirage_tests.

#include <cstdint>

namespace vmafproc {

// The machine's physical cores: hw.physicalcpu on macOS; on Linux the
// distinct (package, core) pairs of /sys/devices/system/cpu/cpu*/topology.
// 0 when they can't be read.
unsigned physical_cores();

// The pool's default size (D-044): physical_cores() minus one, at least 1.
unsigned default_jobs();

// The process's resident memory now, in bytes: the task's resident size on
// macOS, /proc/self/statm's resident pages on Linux. 0 when it can't be read.
std::uint64_t resident_bytes();

// The most resident memory the process has had so far, in bytes
// (getrusage(RUSAGE_SELF).ru_maxrss: bytes on macOS, KiB on Linux). 0 when
// it can't be read.
std::uint64_t peak_resident_bytes();

} // namespace vmafproc
