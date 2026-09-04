#include "UrlRepository.h"

#include "cache/UrlCache.h"

void UrlRepository::nextId(std::function<void(long long)> onSuccess,
                           ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT nextval('urls_id_seq')",
        [onSuccess](const drogon::orm::Result &r)
        {
            onSuccess(r[0][0].as<long long>());
        },
        [onError](const drogon::orm::DrogonDbException &e)
        {
            onError(e.base().what());
        });
}

void UrlRepository::insert(long long id,
                           const std::string &originalUrl,
                           const std::string &shortCode,
                           long long userId,
                           bool isCustom,
                           int expiresInDays,
                           std::function<void(bool)> onSuccess,
                           ErrorCb onError) const
{
    // ON CONFLICT DO NOTHING turns a duplicate short_code into "no rows
    // returned" rather than an exception, so a taken alias is ordinary control
    // flow instead of an error path. The UNIQUE constraint is still what makes
    // this race-free between concurrent requests.
    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO urls (id, original_url, short_code, user_id, is_custom, expires_at) "
        "VALUES ($1, $2, $3, NULLIF($4::bigint, 0), $5, "
        "        CASE WHEN $6::int > 0 THEN NOW() + ($6::int * INTERVAL '1 day') END) "
        "ON CONFLICT (short_code) DO NOTHING "
        "RETURNING id",
        [onSuccess](const drogon::orm::Result &r) { onSuccess(!r.empty()); },
        [onError](const drogon::orm::DrogonDbException &e)
        {
            onError(e.base().what());
        },
        id, originalUrl, shortCode, userId, isCustom, expiresInDays);
}

void UrlRepository::dailyClicks(
    long long urlId,
    int days,
    std::function<void(std::vector<std::pair<std::string, long long>>)> onSuccess,
    ErrorCb onError) const
{
    // generate_series produces every day in the window; the LEFT JOIN attaches
    // counts where they exist. Grouping click_events alone would omit quiet
    // days entirely and make a chart lie about the shape of the traffic.
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT to_char(d.day, 'YYYY-MM-DD') AS day, "
        "       COALESCE(c.n, 0) AS clicks "
        "FROM generate_series("
        "       date_trunc('day', NOW()) - (($1::int - 1) * INTERVAL '1 day'), "
        "       date_trunc('day', NOW()), "
        "       INTERVAL '1 day') AS d(day) "
        "LEFT JOIN ("
        "       SELECT date_trunc('day', clicked_at) AS day, COUNT(*) AS n "
        "       FROM click_events WHERE url_id = $2 "
        "       GROUP BY 1"
        ") c ON c.day = d.day "
        "ORDER BY d.day",
        [onSuccess](const drogon::orm::Result &r)
        {
            std::vector<std::pair<std::string, long long>> out;
            out.reserve(r.size());
            for (const auto &row : r)
                out.emplace_back(row["day"].as<std::string>(),
                                 row["clicks"].as<long long>());
            onSuccess(std::move(out));
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        days, urlId);
}

void UrlRepository::deleteExpired(
    int graceDays,
    std::function<void(std::vector<std::string>)> onSuccess,
    ErrorCb onError) const
{
    // RETURNING gives the codes back so their cache entries can be dropped
    // too. Without that, Redis could keep serving a deleted link until its TTL.
    drogon::app().getDbClient()->execSqlAsync(
        "DELETE FROM urls "
        "WHERE expires_at IS NOT NULL "
        "  AND expires_at < NOW() - ($1::int * INTERVAL '1 day') "
        "RETURNING short_code",
        [onSuccess](const drogon::orm::Result &r)
        {
            std::vector<std::string> codes;
            codes.reserve(r.size());
            for (const auto &row : r) codes.push_back(row["short_code"].as<std::string>());
            onSuccess(std::move(codes));
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        graceDays);
}

void UrlRepository::listByUser(long long userId,
                               long long limit,
                               long long offset,
                               std::function<void(std::vector<UrlRecord>)> onSuccess,
                               ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT id, original_url, short_code, user_id, created_at, is_custom, "
        "       expires_at, "
        "       (expires_at IS NOT NULL AND expires_at <= NOW()) AS expired "
        "FROM urls WHERE user_id = $1 "
        "ORDER BY created_at DESC LIMIT $2::bigint OFFSET $3::bigint",
        [onSuccess](const drogon::orm::Result &r)
        {
            std::vector<UrlRecord> out;
            out.reserve(r.size());
            for (const auto &row : r)
            {
                UrlRecord rec;
                rec.id          = row["id"].as<long long>();
                rec.originalUrl = row["original_url"].as<std::string>();
                rec.shortCode   = row["short_code"].as<std::string>();
                rec.userId      = row["user_id"].isNull()
                                      ? 0
                                      : row["user_id"].as<long long>();
                rec.createdAt   = row["created_at"].as<std::string>();
                rec.isCustom    = row["is_custom"].as<bool>();
                rec.expiresAt   = row["expires_at"].isNull()
                                      ? ""
                                      : row["expires_at"].as<std::string>();
                rec.expired     = row["expired"].as<bool>();
                out.push_back(std::move(rec));
            }
            onSuccess(std::move(out));
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        userId, limit, offset);
}

void UrlRepository::deleteOwned(const std::string &shortCode,
                                long long userId,
                                std::function<void(bool)> onSuccess,
                                ErrorCb onError) const
{
    // The ownership check is part of the WHERE clause, not a separate read.
    // A read-then-delete would leave a window where ownership could change.
    drogon::app().getDbClient()->execSqlAsync(
        "DELETE FROM urls WHERE short_code = $1 AND user_id = $2",
        [onSuccess](const drogon::orm::Result &r)
        { onSuccess(r.affectedRows() > 0); },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        shortCode, userId);
}

void UrlRepository::findByShortCode(
    const std::string &shortCode,
    std::function<void(std::optional<UrlRecord>)> onSuccess,
    ErrorCb onError) const
{
    static const UrlCache cache;

    // ---- 1. Ask the cache first -------------------------------------------
    cache.get(
        shortCode,
        [shortCode, onSuccess, onError](std::optional<UrlRecord> cached)
        {
            if (cached)
            {
                LOG_DEBUG << "cache HIT for " << shortCode;
                onSuccess(std::move(cached));
                return;
            }

            LOG_DEBUG << "cache MISS for " << shortCode;

            // ---- 2. Miss: go to the source of truth ------------------------
            drogon::app().getDbClient()->execSqlAsync(
                "SELECT id, original_url, short_code, user_id, is_custom, "
                "       expires_at, "
                "       (expires_at IS NOT NULL AND expires_at <= NOW()) AS expired "
                "FROM urls WHERE short_code = $1",
                [shortCode, onSuccess](const drogon::orm::Result &r)
                {
                    if (r.empty())
                    {
                        // Deliberately NOT cached; see README on negative caching.
                        onSuccess(std::nullopt);
                        return;
                    }

                    UrlRecord rec;
                    rec.id          = r[0]["id"].as<long long>();
                    rec.originalUrl = r[0]["original_url"].as<std::string>();
                    rec.shortCode   = r[0]["short_code"].as<std::string>();
                    rec.userId      = r[0]["user_id"].isNull()
                                          ? 0
                                          : r[0]["user_id"].as<long long>();
                    rec.isCustom    = r[0]["is_custom"].as<bool>();
                    rec.expiresAt   = r[0]["expires_at"].isNull()
                                          ? ""
                                          : r[0]["expires_at"].as<std::string>();
                    rec.expired     = r[0]["expired"].as<bool>();

                    // ---- 3. Populate the cache for next time ---------------
                    static const UrlCache cache;
                    cache.put(rec);

                    onSuccess(rec);
                },
                [onError](const drogon::orm::DrogonDbException &e)
                {
                    onError(e.base().what());
                },
                shortCode);
        });
}
