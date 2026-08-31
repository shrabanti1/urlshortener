#pragma once
#include <drogon/drogon.h>

namespace rate_limit {

// Per-client-IP rate limiting, replacing nginx's limit_req when the app runs
// without a reverse proxy.
//
// Returns nullptr when the request is allowed, or a 429 response to send
// instead. Registered as a synchronous advice so it runs before routing and
// costs nothing but a map lookup.
drogon::HttpResponsePtr check(const drogon::HttpRequestPtr &req);

}  // namespace rate_limit
