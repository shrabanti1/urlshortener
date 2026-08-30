#include "RedirectController.h"

#include "repositories/AnalyticsRepository.h"
#include "repositories/UrlRepository.h"
#include "utils/Config.h"
#include "utils/IpHash.h"

namespace {

drogon::HttpResponsePtr notFound()
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k404NotFound);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->setBody("Short URL not found");
    return resp;
}

// Bound what we store: a hostile client can send enormous headers.
std::string clip(std::string s, size_t max)
{
    if (s.size() > max) s.resize(max);
    return s;
}

// Behind Nginx (Phase 7/8) the socket peer is the proxy, so the real client
// address arrives in X-Forwarded-For. Only trust it when we know we are
// behind a proxy we control.
std::string clientIp(const drogon::HttpRequestPtr &req)
{
    if (config::get("TRUST_PROXY_HEADERS", "false") == "true")
    {
        const std::string xff = req->getHeader("x-forwarded-for");
        if (!xff.empty())
        {
            // Left-most entry is the original client.
            const auto comma = xff.find(',');
            return comma == std::string::npos ? xff : xff.substr(0, comma);
        }
    }
    return req->getPeerAddr().toIp();
}

}  // namespace

void RedirectController::redirect(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string shortCode)
{
    static const UrlRepository repo;

    // Captured now: req must not be touched after the redirect is sent.
    const std::string referrer  = clip(req->getHeader("referer"), 512);
    const std::string userAgent = clip(req->getHeader("user-agent"), 512);
    const std::string ip        = clientIp(req);

    repo.findByShortCode(
        shortCode,
        [callback, referrer, userAgent, ip](std::optional<UrlRecord> found)
        {
            if (!found)
            {
                callback(notFound());
                return;
            }

            auto redirectResp = drogon::HttpResponse::newRedirectionResponse(
                found->originalUrl, drogon::k302Found);

            if (config::get("ANALYTICS_ENABLED", "true") != "true")
            {
                callback(redirectResp);
                return;
            }

            static const AnalyticsRepository analytics;
            ClickEvent ev;
            ev.urlId     = found->id;   // came free from the cache entry
            ev.referrer  = referrer;
            ev.userAgent = userAgent;
            ev.ipHash    = iphash::anonymize(ip);

            if (config::get("ANALYTICS_MODE", "async") == "sync")
            {
                // The user waits for the INSERT to commit before being
                // redirected. Correct, but the write is on the critical path.
                analytics.recordClickAwait(
                    ev, [callback, redirectResp]() { callback(redirectResp); });
                return;
            }

            // ---- async (default) ---------------------------------------
            // Send the redirect FIRST, then write. The user never waits for
            // an analytics row.
            callback(redirectResp);
            analytics.recordClick(ev);
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "lookup failed: " << err;
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k500InternalServerError);
            resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            resp->setBody("Internal error");
            callback(resp);
        });
}
