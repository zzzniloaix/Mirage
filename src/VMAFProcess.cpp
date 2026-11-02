#include "VMAFProcess.h"

#include <cstddef>

#include <sys/resource.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#endif

namespace vmafproc {

unsigned physical_cores()
{
#if defined(__APPLE__)
    int n = 0;
    std::size_t size = sizeof n;
    if (sysctlbyname("hw.physicalcpu", &n, &size, nullptr, 0) != 0 || n < 1) return 0;
    return static_cast<unsigned>(n);
#else
    namespace fs = std::filesystem;
    std::set<std::pair<std::string, std::string>> cores;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator("/sys/devices/system/cpu", ec)) {
        const std::string name = e.path().filename().string();
        if (name.size() < 4 || name.compare(0, 3, "cpu") != 0 ||
            name.find_first_not_of("0123456789", 3) != std::string::npos)
            continue;
        std::string package, core;
        std::ifstream(e.path() / "topology/physical_package_id") >> package;
        std::ifstream(e.path() / "topology/core_id") >> core;
        if (!package.empty() && !core.empty()) cores.emplace(package, core);
    }
    return static_cast<unsigned>(cores.size());
#endif
}

unsigned default_jobs()
{
    const unsigned cores = physical_cores();
    return cores > 1 ? cores - 1 : 1;
}

std::uint64_t resident_bytes()
{
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return static_cast<std::uint64_t>(info.resident_size);
#else
    std::uint64_t size = 0, resident = 0;
    if (!(std::ifstream("/proc/self/statm") >> size >> resident)) return 0;
    const long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? resident * static_cast<std::uint64_t>(page) : 0;
#endif
}

std::uint64_t peak_resident_bytes()
{
    rusage u{};
    if (getrusage(RUSAGE_SELF, &u) != 0 || u.ru_maxrss < 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(u.ru_maxrss);
#else
    return static_cast<std::uint64_t>(u.ru_maxrss) * 1024u;
#endif
}

} // namespace vmafproc
