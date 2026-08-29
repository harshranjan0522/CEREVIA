#ifndef CEREVIA_PATHS_H
#define CEREVIA_PATHS_H

#include <string>

// ---------------------------------------------------------------------------
// Locates the project's files no matter which directory the binary is started
// from. The old code guessed with a hard-coded list of "../.." prefixes, which
// broke as soon as the build directory moved.
//
// Resolution order: $CEREVIA_ROOT, then a walk up from the executable's own
// directory, then a walk up from the current working directory, looking for the
// marker layout (a `frontend/` and a `database/` directory side by side).
// ---------------------------------------------------------------------------
namespace paths {

// Remembers argv[0] so the executable's directory can be used as a starting
// point. Call once from main() before anything else.
void init(const char *argv0);

const std::string &projectRoot();
std::string frontendDir();
std::string databaseDir();
std::string databaseFile();
std::string backendDir();
std::string resolve(const std::string &relative);

bool fileExists(const std::string &path);
bool ensureDirectory(const std::string &path);

} // namespace paths

#endif // CEREVIA_PATHS_H
