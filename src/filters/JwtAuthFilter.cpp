#include "JwtAuthFilter.h"

#include "utils/Http.h"
#include "utils/Jwt.h"

void JwtAuthFilter::doFilter(const drogon::HttpRequestPtr &req,
                             drogon::FilterCallback &&fcb,
                             drogon::FilterChainCallback &&fccb)
{
    const std::string header = req->getHeader("Authorization");

    // Expected form: "Bearer <token>"
    constexpr const char *kPrefix = "Bearer ";
    constexpr size_t kPrefixLen = 7;

    if (header.rfind(kPrefix, 0) != 0 || header.size() <= kPrefixLen)
    {
        fcb(http_util::jsonError(drogon::k401Unauthorized,
                                 "missing or malformed Authorization header"));
        return;
    }

    const auto claims = jwt_util::verifyAccessToken(header.substr(kPrefixLen));
    if (!claims)
    {
        fcb(http_util::jsonError(drogon::k401Unauthorized, "invalid or expired token"));
        return;
    }

    // Hand the identity to the controller. Attributes are per-request storage,
    // so there is no shared state between concurrent requests.
    req->attributes()->insert("userId", claims->userId);
    req->attributes()->insert("userEmail", claims->email);

    fccb();  // continue to the controller
}
