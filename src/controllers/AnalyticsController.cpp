#include "AnalyticsController.h"

#include "repositories/AnalyticsRepository.h"
#include "repositories/UrlRepository.h"
#include "utils/Http.h"

void AnalyticsController::stats(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string shortCode)
{
    static const UrlRepository urls;
    static const AnalyticsRepository analytics;

    long long userId = 0;
    if (req->attributes()->find("userId"))
        userId = req->attributes()->get<long long>("userId");

    if (userId == 0)
    {
        callback(http_util::jsonError(drogon::k401Unauthorized, "authentication required"));
        return;
    }

    urls.findByShortCode(
        shortCode,
        [callback, shortCode, userId](std::optional<UrlRecord> url)
        {
            // Same response for "no such code" and "not yours", so stats
            // cannot be used to discover which codes exist.
            if (!url || url->userId != userId)
            {
                callback(http_util::jsonError(drogon::k404NotFound, "short url not found"));
                return;
            }

            static const AnalyticsRepository analytics;
            analytics.statsFor(
                url->id,
                [callback, shortCode, urlId = url->id,
                 originalUrl = url->originalUrl](UrlStats s)
                {
                    static const AnalyticsRepository analytics;
                    analytics.topReferrers(
                        urlId, 5,
                        [callback, shortCode, originalUrl, s](
                            std::vector<std::pair<std::string, long long>> refs)
                        {
                            Json::Value body;
                            body["shortCode"]      = shortCode;
                            body["originalUrl"]    = originalUrl;
                            body["totalClicks"]    = static_cast<Json::Int64>(s.totalClicks);
                            body["todayClicks"]    = static_cast<Json::Int64>(s.todayClicks);
                            body["uniqueVisitors"] = static_cast<Json::Int64>(s.uniqueVisitors);
                            body["lastClickedAt"]  = s.lastClickedAt;

                            Json::Value list(Json::arrayValue);
                            for (const auto &[source, n] : refs)
                            {
                                Json::Value item;
                                item["source"] = source;
                                item["clicks"] = static_cast<Json::Int64>(n);
                                list.append(item);
                            }
                            body["topReferrers"] = list;

                            callback(drogon::HttpResponse::newHttpJsonResponse(body));
                        },
                        [callback](const std::string &err)
                        {
                            LOG_ERROR << "top referrers failed: " << err;
                            callback(http_util::jsonError(
                                drogon::k500InternalServerError, "could not load stats"));
                        });
                },
                [callback](const std::string &err)
                {
                    LOG_ERROR << "stats failed: " << err;
                    callback(http_util::jsonError(drogon::k500InternalServerError,
                                                  "could not load stats"));
                });
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "stats lookup failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not load stats"));
        });
}
