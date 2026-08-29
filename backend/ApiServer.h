#ifndef CEREVIA_APISERVER_H
#define CEREVIA_APISERVER_H

#include "Database.h"
#include "Http.h"
#include "JournalManager.h"
#include "MoodTracker.h"

#include <atomic>
#include <string>

// ---------------------------------------------------------------------------
// The HTTP layer: routes JSON requests to the domain objects and serves the
// frontend as static files from the same origin, so the whole app is reachable
// from one URL with no CORS surprises.
// ---------------------------------------------------------------------------
class ApiServer {
public:
    ApiServer();

    // Blocks until the process is stopped. Returns false if the port could not
    // be bound, which the launcher reports as a real failure.
    bool start(int port);

private:
    void handleConnection(net::Socket client);
    bool route(net::Socket client, const http::Request &request);
    bool routeApi(net::Socket client, const http::Request &request, const std::string &path);

    // Login throttling — a 4-digit PIN is guessable, so repeated failures back off.
    bool loginThrottled(int &retryAfterSeconds);
    void recordLoginFailure();
    void resetLoginFailures();

    Database db_;
    MoodTracker moods_;
    JournalManager journal_;

    std::atomic<int> failedLogins_{0};
    std::atomic<long long> lockedUntil_{0};
};

#endif // CEREVIA_APISERVER_H
