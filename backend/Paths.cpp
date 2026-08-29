#include "Paths.h"

#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
    #include <direct.h>
    #define CEREVIA_MKDIR(path) _mkdir(path)
    static const char kSeparator = '\\';
#else
    #include <unistd.h>
    #define CEREVIA_MKDIR(path) mkdir(path, 0755)
    static const char kSeparator = '/';
#endif

namespace paths {
namespace {

std::string g_root;
std::string g_executableDir;

std::string parentOf(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

bool looksLikeProjectRoot(const std::string &candidate)
{
    return fileExists(candidate + "/frontend/index.html") &&
           fileExists(candidate + "/backend/suggestion.json");
}

std::string currentWorkingDirectory()
{
    char buffer[4096];
#if defined(_WIN32)
    if (_getcwd(buffer, sizeof(buffer))) return buffer;
#else
    if (getcwd(buffer, sizeof(buffer))) return buffer;
#endif
    return ".";
}

// Walks up at most `levels` directories from `start` looking for the marker
// layout. Returns an empty string when nothing matches.
std::string searchUpwards(const std::string &start, int levels = 6)
{
    std::string current = start;
    for (int i = 0; i <= levels; ++i) {
        if (looksLikeProjectRoot(current)) return current;
        const std::string parent = parentOf(current);
        if (parent == current) break;
        current = parent;
    }
    return {};
}

} // namespace

bool fileExists(const std::string &path)
{
    struct stat info{};
    return stat(path.c_str(), &info) == 0;
}

bool ensureDirectory(const std::string &path)
{
    if (fileExists(path)) return true;
    return CEREVIA_MKDIR(path.c_str()) == 0;
}

void init(const char *argv0)
{
    if (argv0 && *argv0) {
        g_executableDir = parentOf(argv0);
    }

    if (const char *fromEnv = std::getenv("CEREVIA_ROOT")) {
        if (*fromEnv && looksLikeProjectRoot(fromEnv)) {
            g_root = fromEnv;
        } else if (*fromEnv) {
            std::cerr << "[paths] CEREVIA_ROOT=" << fromEnv
                      << " does not look like a CEREVIA checkout; falling back to auto-detect."
                      << std::endl;
        }
    }

    if (g_root.empty() && !g_executableDir.empty()) g_root = searchUpwards(g_executableDir);
    if (g_root.empty()) g_root = searchUpwards(currentWorkingDirectory());
    if (g_root.empty()) g_root = currentWorkingDirectory();
}

const std::string &projectRoot()
{
    if (g_root.empty()) init(nullptr);
    return g_root;
}

std::string resolve(const std::string &relative)
{
    return projectRoot() + kSeparator + relative;
}

std::string frontendDir()  { return resolve("frontend"); }
std::string backendDir()   { return resolve("backend"); }
std::string databaseDir()  { return resolve("database"); }

std::string databaseFile()
{
    if (const char *override = std::getenv("CEREVIA_DB")) {
        if (*override) return override;
    }
    return databaseDir() + kSeparator + "mental_health.db";
}

} // namespace paths
