#ifndef CEREVIA_HTTP_H
#define CEREVIA_HTTP_H

#include "Net.h"

#include <map>
#include <string>

namespace http {

// A parsed HTTP request. `path` is the decoded path with no query string,
// `query` holds the decoded ?a=b&c=d pairs and `body` is the raw payload.
struct Request {
    std::string method;
    std::string path;
    std::string version;
    std::string body;
    std::map<std::string, std::string> headers; // keys lower-cased
    std::map<std::string, std::string> query;

    std::string header(const std::string &name, const std::string &fallback = "") const;
    std::string param(const std::string &name, const std::string &fallback = "") const;
};

// Reads a full request off `client`: request line + headers, then exactly
// Content-Length bytes of body. Returns false if the peer hung up or the
// request was malformed / oversized.
bool readRequest(net::Socket client, Request &out);

// Writes a complete response. `contentType` is sent verbatim; CORS headers are
// always attached so the frontend works from any origin (including file://).
void sendResponse(net::Socket client,
                  int status,
                  const std::string &contentType,
                  const std::string &body,
                  const std::string &extraHeaders = "");

void sendJson(net::Socket client, int status, const std::string &json);
void sendJsonError(net::Socket client, int status, const std::string &message);
void sendNoContent(net::Socket client);

// Serves a file from `rootDir` for `urlPath`. Returns false when the file does
// not exist or the path tries to escape the root, so the caller can 404.
bool sendStaticFile(net::Socket client, const std::string &rootDir, const std::string &urlPath);

std::string urlDecode(const std::string &value);
std::string mimeTypeFor(const std::string &path);
std::string statusText(int status);

} // namespace http

#endif // CEREVIA_HTTP_H
