// ---------------------------------------------------------------------------
// CEREVIA backend entry point.
//
// Resolves the project layout, opens the database, and serves both the JSON API
// and the frontend on one local port. Override the port with CEREVIA_PORT.
// ---------------------------------------------------------------------------
#include "ApiServer.h"
#include "Paths.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv)
{
    paths::init(argc > 0 ? argv[0] : nullptr);

    int port = 5000;
    if (const char *fromEnv = std::getenv("CEREVIA_PORT")) {
        try {
            port = std::stoi(fromEnv);
        } catch (const std::exception &) {
            std::cerr << "[api] Ignoring invalid CEREVIA_PORT=" << fromEnv << std::endl;
        }
    }
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (const std::exception &) {
            std::cerr << "[api] Ignoring invalid port argument: " << argv[1] << std::endl;
        }
    }

    std::cout << "[api] Project root: " << paths::projectRoot() << std::endl;

    ApiServer server;
    if (!server.start(port)) {
        return 1;
    }
    return 0;
}
