#include "AnalyticsRepository.h"

void AnalyticsRepository::recordClick(const ClickEvent &event) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO click_events (url_id, referrer, user_agent, ip_hash) "
        "VALUES ($1, $2, $3, $4)",
        [](const drogon::orm::Result &) {},
        [urlId = event.urlId](const drogon::orm::DrogonDbException &e)
        {
            // A lost analytics row must never affect the user's redirect.
            LOG_WARN << "click not recorded for url " << urlId << ": "
                     << e.base().what();
        },
        event.urlId, event.referrer, event.userAgent, event.ipHash);
}

void AnalyticsRepository::recordClickAwait(const ClickEvent &event,
                                           std::function<void()> onDone) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO click_events (url_id, referrer, user_agent, ip_hash) "
        "VALUES ($1, $2, $3, $4)",
        [onDone](const drogon::orm::Result &) { onDone(); },
        [onDone, urlId = event.urlId](const drogon::orm::DrogonDbException &e)
        {
            LOG_WARN << "click not recorded for url " << urlId << ": "
                     << e.base().what();
            onDone();  // still send the redirect
        },
        event.urlId, event.referrer, event.userAgent, event.ipHash);
}

void AnalyticsRepository::statsFor(long long urlId,
                                   std::function<void(UrlStats)> onSuccess,
                                   ErrorCb onError) const
{
    // One round trip for every figure. Running three separate queries would
    // mean three index scans over the same rows.
    //
    // FILTER is the SQL-standard way to do a conditional aggregate; it reads
    // better than SUM(CASE WHEN ... THEN 1 ELSE 0 END) and Postgres optimises
    // it the same way.
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT "
        "  COUNT(*)                                              AS total, "
        "  COUNT(*) FILTER (WHERE clicked_at >= date_trunc('day', NOW())) AS today, "
        "  COUNT(DISTINCT ip_hash)                               AS uniques, "
        "  MAX(clicked_at)                                       AS last_click "
        "FROM click_events WHERE url_id = $1",
        [onSuccess](const drogon::orm::Result &r)
        {
            UrlStats s;
            if (!r.empty())
            {
                s.totalClicks    = r[0]["total"].as<long long>();
                s.todayClicks    = r[0]["today"].as<long long>();
                s.uniqueVisitors = r[0]["uniques"].as<long long>();
                s.lastClickedAt  = r[0]["last_click"].isNull()
                                       ? ""
                                       : r[0]["last_click"].as<std::string>();
            }
            onSuccess(s);
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        urlId);
}

void AnalyticsRepository::topReferrers(
    long long urlId,
    int limit,
    std::function<void(std::vector<std::pair<std::string, long long>>)> onSuccess,
    ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT COALESCE(NULLIF(referrer, ''), 'direct') AS source, COUNT(*) AS n "
        "FROM click_events WHERE url_id = $1 "
        "GROUP BY source ORDER BY n DESC LIMIT $2::bigint",
        [onSuccess](const drogon::orm::Result &r)
        {
            std::vector<std::pair<std::string, long long>> out;
            out.reserve(r.size());
            for (const auto &row : r)
                out.emplace_back(row["source"].as<std::string>(),
                                 row["n"].as<long long>());
            onSuccess(std::move(out));
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        urlId, static_cast<long long>(limit));
}
