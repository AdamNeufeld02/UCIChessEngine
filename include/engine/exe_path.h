#pragma once
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace engine {

inline std::filesystem::path executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, len)).parent_path();
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(buf, static_cast<size_t>(len))).parent_path();
#endif
}

// Walks up from the executable's own location looking for a "data" directory
// containing `relativePath`, since the working directory at launch depends
// on how the program gets started (terminal, GUI, script, an exe copied
// somewhere else entirely) and isn't something to rely on. Falls back to a
// plain CWD-relative "data/<relativePath>" if nothing is found within a few
// levels, or if `relativePath` is already absolute (used as-is then).
inline std::filesystem::path findNearExecutable(const std::filesystem::path& relativePath) {
    if (relativePath.is_absolute()) return relativePath;

    std::filesystem::path dir = executableDir();
    for (int i = 0; i < 6; ++i) {
        std::filesystem::path candidate = dir / "data" / relativePath;
        if (std::filesystem::exists(candidate)) return candidate;
        std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::filesystem::path("data") / relativePath;
}

// Same "don't trust CWD at launch" reasoning as findNearExecutable, but for
// locating the top of the source tree (identified by CMakeLists.txt) rather
// than a specific data file - for output locations like tools/tune_output
// that should land in one consistent place in the repo regardless of which
// directory a tool happens to be run from. Falls back to the current
// working directory if the repo root can't be found near the executable.
inline std::filesystem::path findRepoRoot() {
    std::filesystem::path dir = executableDir();
    for (int i = 0; i < 6; ++i) {
        if (std::filesystem::exists(dir / "CMakeLists.txt")) return dir;
        std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::filesystem::current_path();
}

}
