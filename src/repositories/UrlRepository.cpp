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
                           std::function<void()> onSuccess,
                           ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        // NULLIF maps the "no owner" sentinel 0 to SQL NULL. Inserting a
        // literal 0 would violate the users(id) foreign key.
        "INSERT INTO urls (id, original_url, short_code, user_id) "
        "VALUES ($1, $2, $3, NULLIF($4::bigint, 0))",
        [onSuccess](const drogon::orm::Result &) { onSuccess(); },
        [onError](const drogon::orm::DrogonDbException &e)
        {
            onError(e.base().what());
        },
        id, originalUrl, shortCode, userId);
}

void UrlRepository::listByUser(long long userId,
                               long long limit,
                               long long offset,
                               std::function<void(std::vector<UrlRecord>)> onSuccess,
                               ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT id, original_url, short_code, user_id, created_at "
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
                "SELECT id, original_url, short_code, user_id FROM urls "
                "WHERE short_code = $1",
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
