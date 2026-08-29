#include "Http.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace http {
namespace {

// A single request may not exceed this. Keeps a hostile or buggy client from
// making the server allocate without bound.
constexpr size_t kMaxHeaderBytes = 64 * 1024;
constexpr size_t kMaxBodyBytes   = 4 * 1024 * 1024;

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

// Reads from the socket until `terminator` shows up in the accumulated buffer.
bool readUntil(net::Socket client, std::string &buffer, const std::string &terminator, size_t limit)
{
    char chunk[4096];
    while (buffer.find(terminator) == std::string::npos) {
        if (buffer.size() > limit) return false;
        const int got = recv(client, chunk, sizeof(chunk), 0);
        if (got <= 0) return false;
        buffer.append(chunk, static_cast<size_t>(got));
    }
    return true;
}

void parseQueryString(const std::string &raw, std::map<std::string, std::string> &out)
{
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t amp = raw.find('&', pos);
        if (amp == std::string::npos) amp = raw.size();
        const std::string pair = raw.substr(pos, amp - pos);
        const size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            if (!pair.empty()) out[urlDecode(pair)] = "";
        } else {
            out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
        pos = amp + 1;
    }
}

const char *kCorsHeaders =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
    "Access-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n"
    "Access-Control-Max-Age: 86400\r\n";

void sendAll(net::Socket client, const std::string &payload)
{
    size_t sent = 0;
    while (sent < payload.size()) {
        const int wrote = send(client,
                               payload.data() + sent,
                               static_cast<int>(payload.size() - sent),
                               0);
        if (wrote <= 0) return; // peer gone; nothing useful left to do
        sent += static_cast<size_t>(wrote);
    }
}

} // namespace

std::string Request::header(const std::string &name, const std::string &fallback) const
{
    const auto it = headers.find(toLower(name));
    return it == headers.end() ? fallback : it->second;
}

std::string Request::param(const std::string &name, const std::string &fallback) const
{
    const auto it = query.find(name);
    return it == query.end() ? fallback : it->second;
}

std::string urlDecode(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            char *end = nullptr;
            const long code = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0') {
                out.push_back(static_cast<char>(code));
                i += 2;
                continue;
            }
            out.push_back(value[i]);
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

bool readRequest(net::Socket client, Request &out)
{
    std::string buffer;
    if (!readUntil(client, buffer, "\r\n\r\n", kMaxHeaderBytes)) return false;

    const size_t headerEnd = buffer.find("\r\n\r\n");
    const std::string headerBlock = buffer.substr(0, headerEnd);
    std::string body = buffer.substr(headerEnd + 4);

    std::istringstream stream(headerBlock);
    std::string requestLine;
    if (!std::getline(stream, requestLine)) return false;
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

    std::istringstream lineStream(requestLine);
    std::string target;
    if (!(lineStream >> out.method >> target)) return false;
    lineStream >> out.version;

    const size_t queryStart = target.find('?');
    if (queryStart == std::string::npos) {
        out.path = urlDecode(target);
    } else {
        out.path = urlDecode(target.substr(0, queryStart));
        parseQueryString(target.substr(queryStart + 1), out.query);
    }

    std::string headerLine;
    while (std::getline(stream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
        if (headerLine.empty()) continue;
        const size_t colon = headerLine.find(':');
        if (colon == std::string::npos) continue;
        out.headers[toLower(trim(headerLine.substr(0, colon)))] = trim(headerLine.substr(colon + 1));
    }

    // Pull in the rest of the body when the client sent it in later packets.
    const std::string lengthHeader = out.header("content-length");
    if (!lengthHeader.empty()) {
        long declared = 0;
        try {
            declared = std::stol(lengthHeader);
        } catch (const std::exception &) {
            return false;
        }
        if (declared < 0 || static_cast<size_t>(declared) > kMaxBodyBytes) return false;

        char chunk[4096];
        while (body.size() < static_cast<size_t>(declared)) {
            const int got = recv(client, chunk, sizeof(chunk), 0);
            if (got <= 0) break;
            body.append(chunk, static_cast<size_t>(got));
        }
        if (body.size() > static_cast<size_t>(declared)) body.resize(static_cast<size_t>(declared));
    }

    out.body = body;
    return !out.method.empty() && !out.path.empty();
}

std::string statusText(int status)
{
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

void sendResponse(net::Socket client,
                  int status,
                  const std::string &contentType,
                  const std::string &body,
                  const std::string &extraHeaders)
{
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << statusText(status) << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << kCorsHeaders
             << extraHeaders
             << "Connection: close\r\n\r\n"
             << body;
    sendAll(client, response.str());
}

void sendJson(net::Socket client, int status, const std::string &json)
{
    sendResponse(client, status, "application/json; charset=utf-8", json,
                 "Cache-Control: no-store\r\n");
}

void sendJsonError(net::Socket client, int status, const std::string &message)
{
    std::string escaped;
    for (char c : message) {
        if (c == '"' || c == '\\') escaped.push_back('\\');
        escaped.push_back(c);
    }
    sendJson(client, status, "{\"error\":\"" + escaped + "\"}");
}

void sendNoContent(net::Socket client)
{
    std::ostringstream response;
    response << "HTTP/1.1 204 No Content\r\n"
             << kCorsHeaders
             << "Connection: close\r\n\r\n";
    sendAll(client, response.str());
}

std::string mimeTypeFor(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    const std::string ext = toLower(path.substr(dot + 1));

    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")  return "text/css; charset=utf-8";
    if (ext == "js" || ext == "mjs") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "ico")  return "image/x-icon";
    if (ext == "webp") return "image/webp";
    if (ext == "woff") return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf")  return "font/ttf";
    if (ext == "txt")  return "text/plain; charset=utf-8";
    if (ext == "map")  return "application/json; charset=utf-8";
    return "application/octet-stream";
}

bool sendStaticFile(net::Socket client, const std::string &rootDir, const std::string &urlPath)
{
    // Reject anything that could climb out of the web root before touching disk.
    if (urlPath.find("..") != std::string::npos) return false;
    if (urlPath.find('\0') != std::string::npos) return false;

    std::string relative = urlPath;
    if (relative.empty() || relative == "/") relative = "/index.html";
    if (relative.front() == '/') relative.erase(0, 1);

    const std::string fullPath = rootDir + "/" + relative;
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return false;

    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string body = contents.str();

    // Assets change while the app is being worked on, so never let the browser
    // cache a stale copy of the UI.
    sendResponse(client, 200, mimeTypeFor(relative), body, "Cache-Control: no-cache\r\n");
    return true;
}

} // namespace http
