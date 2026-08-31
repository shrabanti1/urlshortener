#include "SecurityHeaders.h"

#include "utils/Config.h"

namespace security_headers {

void apply(const drogon::HttpRequestPtr &, const drogon::HttpResponsePtr &resp)
{
    resp->addHeader("X-Content-Type-Options", "nosniff");
    resp->addHeader("Referrer-Policy", "no-referrer-when-downgrade");
    resp->addHeader("X-Frame-Options", "DENY");

    // Only meaningful over HTTPS. The platform terminates TLS, so this is
    // gated on knowing a proxy we trust is in front.
    if (config::get("TRUST_PROXY_HEADERS", "false") == "true")
        resp->addHeader("Strict-Transport-Security", "max-age=31536000");
}

}  // namespace security_headers
