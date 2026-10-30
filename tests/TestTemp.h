#pragma once

// A temporary directory of its own for each run of mirage_tests, so that two
// runs at the same time never share a file name (DECISIONS.md D-032). Every
// test that writes a file writes it here. The directory is created on first
// use under the system's temporary directory and removed, with what is in
// it, when the process exits normally.

#include <cstdlib>       // mkdtemp() on Linux
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>      // mkdtemp() on macOS

namespace testtemp {

// The directory: <temporary directory>/mirage_tests.XXXXXX, made by mkdtemp().
inline const std::filesystem::path& dir()
{
    struct Dir {
        std::filesystem::path path;
        Dir()
        {
            const std::filesystem::path pattern =
                std::filesystem::temp_directory_path() / "mirage_tests.XXXXXX";
            std::string name = pattern.string();
            if (!mkdtemp(name.data()))
                throw std::runtime_error("cannot create a temporary directory from " + name);
            path = name;
        }
        ~Dir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
    static const Dir d;
    return d.path;
}

// `name` inside dir(); nothing is created.
inline std::filesystem::path path(const std::string& name) { return dir() / name; }

} // namespace testtemp
