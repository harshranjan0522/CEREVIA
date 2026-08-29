#include "ApiServer.h"

#include "Paths.h"
#include "Sql.h"
#include "libs/json/json.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

using json = nlohmann::json;

namespace {

constexpr int kMaxFailedLogins = 5;
constexpr int kLockoutSeconds  = 60;

long long nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Parses a request body as JSON. Returns false (and leaves `out` empty) for
// malformed input so handlers can answer 400 instead of throwing.
bool parseBody(const http::Request &request, json &out)
{
    if (request.body.empty()) {
        out = json::object();
        return true;
    }
    try {
        out = json::parse(request.body);
        return out.is_object();
    } catch (const std::exception &) {
        return false;
    }
}

int intParam(const http::Request &request, const std::string &name, int fallback)
{
    const std::string raw = request.param(name);
    if (raw.empty()) return fallback;
    try {
        return std::stoi(raw);
    } catch (const std::exception &) {
        return fallback;
    }
}

// Pulls the trailing numeric id out of a path like "/api/mood/12".
bool trailingId(const std::string &path, const std::string &prefix, int &id)
{
    if (path.rfind(prefix, 0) != 0) return false;
    const std::string tail = path.substr(prefix.size());
    if (tail.empty()) return false;
    for (char c : tail) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        id = std::stoi(tail);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace

ApiServer::ApiServer() : moods_(db_), journal_(db_) {}

bool ApiServer::loginThrottled(int &retryAfterSeconds)
{
    const long long until = lockedUntil_.load();
    if (until <= nowSeconds()) return false;
    retryAfterSeconds = static_cast<int>(until - nowSeconds());
    return true;
}

void ApiServer::recordLoginFailure()
{
    const int failures = failedLogins_.fetch_add(1) + 1;
    if (failures >= kMaxFailedLogins) {
        lockedUntil_.store(nowSeconds() + kLockoutSeconds);
        failedLogins_.store(0);
        std::cout << "[api] Too many failed PIN attempts; pausing logins for "
                  << kLockoutSeconds << "s." << std::endl;
    }
}

void ApiServer::resetLoginFailures()
{
    failedLogins_.store(0);
    lockedUntil_.store(0);
}

bool ApiServer::routeApi(net::Socket client, const http::Request &request, const std::string &path)
{
    const std::string &method = request.method;

    // ---- Meta -------------------------------------------------------------
    if (path == "/health") {
        http::sendJson(client, 200, json{
            {"status", "ok"},
            {"service", "cerevia-backend"},
            {"database", db_.isOpen() ? "connected" : "unavailable"},
        }.dump());
        return true;
    }

    if (path == "/meta") {
        // The launcher may have had to move the companion off its default port
        // (macOS AirPlay squats on 5000), so the frontend is told where it is
        // rather than guessing.
        int companionPort = 5001;
        if (const char *fromEnv = std::getenv("CEREVIA_CHAT_PORT")) {
            try {
                companionPort = std::stoi(fromEnv);
            } catch (const std::exception &) { /* keep the default */ }
        }

        http::sendJson(client, 200, json{
            {"app", "CEREVIA"},
            {"version", "2.0.0"},
            {"companionPort", companionPort},
            {"moods", MoodTracker::vocabulary()},
            {"journalPrompts", JournalManager::prompts()},
        }.dump());
        return true;
    }

    // ---- Authentication ---------------------------------------------------
    if (path == "/auth/login" && method == "POST") {
        int retryAfter = 0;
        if (loginThrottled(retryAfter)) {
            http::sendJson(client, 429, json{
                {"success", false},
                {"error", "Too many attempts. Try again shortly."},
                {"retryAfter", retryAfter},
            }.dump());
            return true;
        }

        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }

        const std::string pin = body.value("pin", std::string());
        const bool ok = db_.checkPIN(pin);
        if (ok) {
            resetLoginFailures();
        } else {
            recordLoginFailure();
        }

        http::sendJson(client, ok ? 200 : 401, json{
            {"success", ok},
            {"displayName", ok ? db_.getDisplayName() : std::string()},
            {"error", ok ? std::string() : std::string("That PIN does not match.")},
        }.dump());
        return true;
    }

    if (path == "/auth/pin" && method == "POST") {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }
        const std::string pin = body.value("pin", std::string());
        if (pin.size() < 4 || pin.size() > 12) {
            http::sendJson(client, 400, json{
                {"success", false},
                {"error", "Choose a PIN between 4 and 12 characters."},
            }.dump());
            return true;
        }
        const bool ok = db_.setPIN(pin);
        http::sendJson(client, ok ? 200 : 500, json{{"success", ok}}.dump());
        return true;
    }

    if (path == "/auth/question" && method == "GET") {
        http::sendJson(client, 200, json{{"question", db_.getSecurityQuestion()}}.dump());
        return true;
    }

    if (path == "/auth/verify" && method == "POST") {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }
        const bool ok = db_.verifySecurityAnswer(body.value("answer", std::string()));
        http::sendJson(client, ok ? 200 : 401, json{{"verified", ok}}.dump());
        return true;
    }

    if (path == "/auth/reset" && method == "POST") {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }
        // The recovery answer has to be re-supplied here so knowing the reset
        // URL alone is not enough to take over the account.
        const std::string answer = body.value("answer", std::string());
        const std::string newPin = body.value("newPin", std::string());
        if (!answer.empty() && !db_.verifySecurityAnswer(answer)) {
            http::sendJson(client, 401, json{
                {"success", false},
                {"error", "That answer does not match."},
            }.dump());
            return true;
        }
        if (newPin.size() < 4 || newPin.size() > 12) {
            http::sendJson(client, 400, json{
                {"success", false},
                {"error", "Choose a PIN between 4 and 12 characters."},
            }.dump());
            return true;
        }
        const bool ok = db_.setPIN(newPin);
        if (ok) resetLoginFailures();
        http::sendJson(client, ok ? 200 : 500, json{{"success", ok}}.dump());
        return true;
    }

    // ---- Profile ----------------------------------------------------------
    if (path == "/profile" && method == "GET") {
        http::sendJson(client, 200, json{
            {"displayName", db_.getDisplayName()},
            {"emergencyContact", db_.getEmergencyContact()},
            {"securityQuestion", db_.getSecurityQuestion()},
            {"reminderTime", db_.getSetting("reminderTime", "")},
            {"theme", db_.getSetting("theme", "system")},
        }.dump());
        return true;
    }

    if (path == "/profile" && (method == "POST" || method == "PATCH")) {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }

        if (body.contains("displayName") && body["displayName"].is_string()) {
            db_.setDisplayName(body["displayName"].get<std::string>());
        }
        if (body.contains("emergencyContact") && body["emergencyContact"].is_string()) {
            const std::string contact = body["emergencyContact"].get<std::string>();
            if (!contact.empty()) db_.setEmergencyContact(contact);
        }
        if (body.contains("securityQuestion") && body.contains("securityAnswer") &&
            body["securityQuestion"].is_string() && body["securityAnswer"].is_string()) {
            db_.setSecurityQuestion(body["securityQuestion"].get<std::string>(),
                                    body["securityAnswer"].get<std::string>());
        }
        if (body.contains("reminderTime") && body["reminderTime"].is_string()) {
            db_.setSetting("reminderTime", body["reminderTime"].get<std::string>());
        }
        if (body.contains("theme") && body["theme"].is_string()) {
            db_.setSetting("theme", body["theme"].get<std::string>());
        }

        http::sendJson(client, 200, json{
            {"success", true},
            {"displayName", db_.getDisplayName()},
            {"emergencyContact", db_.getEmergencyContact()},
            {"securityQuestion", db_.getSecurityQuestion()},
            {"reminderTime", db_.getSetting("reminderTime", "")},
            {"theme", db_.getSetting("theme", "system")},
        }.dump());
        return true;
    }

    // ---- Moods ------------------------------------------------------------
    if (path == "/mood" && method == "GET") {
        http::sendJson(client, 200,
                       moods_.list(intParam(request, "days", 0), intParam(request, "limit", 200)).dump());
        return true;
    }

    if (path == "/mood" && method == "POST") {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }
        const json saved = moods_.add(body);
        if (saved.contains("error")) {
            http::sendJson(client, 400, saved.dump());
            return true;
        }
        http::sendJson(client, 201, saved.dump());
        return true;
    }

    if (path == "/mood/reset" && method == "POST") {
        http::sendJson(client, 200, json{{"success", true}, {"deleted", moods_.clear()}}.dump());
        return true;
    }

    int id = 0;
    if (trailingId(path, "/mood/", id) && method == "DELETE") {
        const bool removed = moods_.remove(id);
        http::sendJson(client, removed ? 200 : 404, json{{"success", removed}}.dump());
        return true;
    }

    // ---- Journal ----------------------------------------------------------
    if (path == "/journal" && method == "GET") {
        http::sendJson(client, 200,
                       journal_.list(request.param("q"), intParam(request, "limit", 200)).dump());
        return true;
    }

    if (path == "/journal" && method == "POST") {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }
        const json saved = journal_.add(body);
        if (saved.contains("error")) {
            http::sendJson(client, 400, saved.dump());
            return true;
        }
        http::sendJson(client, 201, saved.dump());
        return true;
    }

    if (path == "/journal/stats" && method == "GET") {
        http::sendJson(client, 200, journal_.stats().dump());
        return true;
    }

    if (path == "/journal/prompt" && method == "GET") {
        http::sendJson(client, 200, json{{"prompt", JournalManager::randomPrompt()}}.dump());
        return true;
    }

    if (path == "/journal/reset" && method == "POST") {
        http::sendJson(client, 200, json{{"success", true}, {"deleted", journal_.clear()}}.dump());
        return true;
    }

    if (trailingId(path, "/journal/", id) && method == "DELETE") {
        const bool removed = journal_.remove(id);
        http::sendJson(client, removed ? 200 : 404, json{{"success", removed}}.dump());
        return true;
    }

    // ---- Insights ---------------------------------------------------------
    if (path == "/stats/summary" && method == "GET") {
        http::sendJson(client, 200, moods_.summary(intParam(request, "days", 14)).dump());
        return true;
    }

    if (path == "/suggestion" && method == "GET") {
        http::sendJson(client, 200, moods_.suggestion().dump());
        return true;
    }

    if (path == "/eq" && method == "GET") {
        http::sendJson(client, 200, moods_.eqResources().dump());
        return true;
    }

    if (path == "/crisis" && method == "GET") {
        http::sendJson(client, 200, moods_.crisisStatus().dump());
        return true;
    }

    // ---- Breathing --------------------------------------------------------
    if (path == "/breathing" && method == "GET") {
        std::lock_guard<std::mutex> guard(db_.mutex());
        sql::Stmt stmt(db_.getHandle(),
                       "SELECT COUNT(*), COALESCE(SUM(cycles), 0), COALESCE(SUM(seconds), 0) "
                       "FROM breathing_sessions");
        json out{{"sessions", 0}, {"cycles", 0}, {"seconds", 0}};
        if (stmt && stmt.step()) {
            out["sessions"] = stmt.integer(0);
            out["cycles"] = stmt.integer(1);
            out["seconds"] = stmt.integer(2);
        }
        http::sendJson(client, 200, out.dump());
        return true;
    }

    if (path == "/breathing" && method == "POST") {
        json body;
        if (!parseBody(request, body)) {
            http::sendJsonError(client, 400, "Invalid JSON body.");
            return true;
        }
        const std::string technique = body.value("technique", std::string("box"));
        const int cycles = body.contains("cycles") && body["cycles"].is_number() ? body["cycles"].get<int>() : 0;
        const int seconds = body.contains("seconds") && body["seconds"].is_number() ? body["seconds"].get<int>() : 0;
        if (cycles <= 0) {
            http::sendJson(client, 400, json{{"error", "Nothing to log."}}.dump());
            return true;
        }

        std::lock_guard<std::mutex> guard(db_.mutex());
        sql::Stmt stmt(db_.getHandle(),
                       "INSERT INTO breathing_sessions(technique, cycles, seconds, created_at) "
                       "VALUES (?, ?, ?, datetime('now', 'localtime'))");
        stmt.bind(1, technique).bind(2, cycles).bind(3, seconds);
        const bool ok = stmt.run();
        http::sendJson(client, ok ? 201 : 500, json{{"success", ok}}.dump());
        return true;
    }

    if (path == "/breathing/reset" && method == "POST") {
        std::lock_guard<std::mutex> guard(db_.mutex());
        sql::Stmt stmt(db_.getHandle(), "DELETE FROM breathing_sessions");
        stmt.run();
        http::sendJson(client, 200, json{{"success", true}}.dump());
        return true;
    }

    return false;
}

bool ApiServer::route(net::Socket client, const http::Request &request)
{
    // CORS preflight — answer before anything else touches the database.
    if (request.method == "OPTIONS") {
        http::sendNoContent(client);
        return true;
    }

    const std::string &path = request.path;

    if (path.rfind("/api/", 0) == 0) {
        const std::string apiPath = path.substr(4); // keep the leading slash
        if (routeApi(client, request, apiPath)) return true;
        http::sendJsonError(client, 404, "No such endpoint: " + request.method + " " + path);
        return true;
    }

    // ---- Legacy v1 routes -------------------------------------------------
    // Kept so anything still pointing at the old API keeps working.
    static const std::map<std::string, std::string> kLegacy = {
        {"POST /login",              "/auth/login"},
        {"POST /register",           "/auth/pin"},
        {"GET /auth/question",       "/auth/question"},
        {"POST /auth/verify",        "/auth/verify"},
        {"POST /auth/reset",         "/auth/reset"},
        {"GET /mood/all",            "/mood"},
        {"POST /mood/add",           "/mood"},
        {"POST /mood/reset",         "/mood/reset"},
        {"GET /journal/all",         "/journal"},
        {"POST /journal/add",        "/journal"},
        {"GET /journal/count",       "/journal/stats"},
        {"POST /journal/reset",      "/journal/reset"},
        {"GET /stats/crisis",        "/crisis"},
        {"GET /suggestion/today",    "/suggestion"},
        {"GET /eq/resources",        "/eq"},
        {"GET /emergency/contact",   "/profile"},
        {"GET /health",              "/health"},
    };

    // The one legacy route whose response shape differs from its modern
    // counterpart, so it gets its own handler rather than an alias.
    if (request.method == "GET" && path == "/emergency/contact") {
        http::sendJson(client, 200, nlohmann::json{{"contact", db_.getEmergencyContact()}}.dump());
        return true;
    }

    const auto legacy = kLegacy.find(request.method + " " + path);
    if (legacy != kLegacy.end()) {
        if (routeApi(client, request, legacy->second)) return true;
    }

    // ---- Static frontend --------------------------------------------------
    if (request.method == "GET") {
        const std::string root = paths::frontendDir();
        if (http::sendStaticFile(client, root, path)) return true;

        // Extension-less URLs get an .html fallback so /dashboard works.
        if (path.find('.') == std::string::npos &&
            http::sendStaticFile(client, root, path + ".html")) {
            return true;
        }
    }

    return false;
}

void ApiServer::handleConnection(net::Socket client)
{
    http::Request request;
    if (!http::readRequest(client, request)) {
        net::closeSocket(client);
        return;
    }

    try {
        if (!route(client, request)) {
            http::sendJsonError(client, 404, "Not found: " + request.method + " " + request.path);
        }
    } catch (const std::exception &error) {
        std::cerr << "[api] " << request.method << ' ' << request.path
                  << " failed: " << error.what() << std::endl;
        http::sendJsonError(client, 500, "The request could not be completed.");
    }

    net::closeSocket(client);
}

bool ApiServer::start(int port)
{
    if (!net::startup()) {
        std::cerr << "[api] Socket layer failed to initialise." << std::endl;
        return false;
    }

    const net::Socket server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == net::kInvalidSocket) {
        std::cerr << "[api] socket() failed: " << net::lastError() << std::endl;
        net::shutdownLib();
        return false;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<net::OptVal>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // local machine only, by design
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "[api] Could not bind port " << port
                  << " (error " << net::lastError() << "). Is CEREVIA already running?" << std::endl;
        net::closeSocket(server);
        net::shutdownLib();
        return false;
    }

    if (::listen(server, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[api] listen() failed: " << net::lastError() << std::endl;
        net::closeSocket(server);
        net::shutdownLib();
        return false;
    }

    std::cout << "[api] CEREVIA is listening on http://127.0.0.1:" << port << std::endl;
    std::cout << "[api] Serving the app from " << paths::frontendDir() << std::endl;

    while (true) {
        const net::Socket client = ::accept(server, nullptr, nullptr);
        if (client == net::kInvalidSocket) {
            std::cerr << "[api] accept() failed: " << net::lastError() << std::endl;
            continue;
        }
        // One thread per connection: a slow client can no longer stall the
        // whole server the way the old single-threaded loop allowed.
        std::thread(&ApiServer::handleConnection, this, client).detach();
    }

    net::closeSocket(server);
    net::shutdownLib();
    return true;
}
