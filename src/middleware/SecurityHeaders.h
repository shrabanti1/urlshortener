#pragma once
#include <drogon/drogon.h>

namespace security_headers {

// Adds the response headers nginx sets in the Compose deployment. Registered
// globally as a pre-sending advice, so it covers every route including static
// files and error responses.
void apply(const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &resp);

}  // namespace security_headers
