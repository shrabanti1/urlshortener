#include "AnalyticsController.h"

#include "repositories/AnalyticsRepository.h"
#include "repositories/UrlRepository.h"
#include <algorithm>

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

    // ?days=N controls the chart window.
    int days = 30;
    const auto daysParam = req->getParameter("days");
    if (!daysParam.empty()) { try { days = std::stoi(daysParam); } catch (...) {} }
    days = std::max(1, std::min(days, 90));

    urls.findByShortCode(
        shortCode,
        [callback, shortCode, userId, days](std::optional<UrlRecord> url)
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
                [callback, shortCode, days, urlId = url->id,
                 originalUrl = url->originalUrl,
                 expiresAt = url->expiresAt,
                 expired = url->expired](UrlStats s)
                {
                    static const AnalyticsRepository analytics;
                    analytics.topReferrers(
                        urlId, 5,
                        [callback, shortCode, originalUrl, s, days, urlId,
                         expiresAt, expired](
                            std::vector<std::pair<std::string, long long>> refs)
                        {
                            Json::Value body;
                            body["shortCode"]      = shortCode;
                            body["originalUrl"]    = originalUrl;
                            body["totalClicks"]    = static_cast<Json::Int64>(s.totalClicks);
                            body["todayClicks"]    = static_cast<Json::Int64>(s.todayClicks);
                            body["uniqueVisitors"] = static_cast<Json::Int64>(s.uniqueVisitors);
                            body["lastClickedAt"]  = s.lastClickedAt;
                            body["expiresAt"]      = expiresAt;   // "" = never
                            body["expired"]        = expired;

                            Json::Value list(Json::arrayValue);
                            for (const auto &[source, n] : refs)
                            {
                                Json::Value item;
                                item["source"] = source;
                                item["clicks"] = static_cast<Json::Int64>(n);
                                list.append(item);
                            }
                            body["topReferrers"] = list;

                            // Daily series for the chart, fetched last so the
                            // response is assembled in one place.
                            static const UrlRepository urls;
                            urls.dailyClicks(
                                urlId, days,
                                [callback, body](
                                    std::vector<std::pair<std::string, long long>> series) mutable
                                {
                                    Json::Value points(Json::arrayValue);
                                    for (const auto &[day, n] : series)
                                    {
                                        Json::Value point;
                                        point["date"]   = day;
                                        point["clicks"] = static_cast<Json::Int64>(n);
                                        points.append(point);
                                    }
                                    body["dailyClicks"] = points;
                                    callback(drogon::HttpResponse::newHttpJsonResponse(body));
                                },
                                [callback, body](const std::string &err) mutable
                                {
                                    // The chart is a nice-to-have; the rest of
                                    // the stats are still worth returning.
                                    LOG_WARN << "daily clicks failed: " << err;
                                    body["dailyClicks"] = Json::Value(Json::arrayValue);
                                    callback(drogon::HttpResponse::newHttpJsonResponse(body));
                                });
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
