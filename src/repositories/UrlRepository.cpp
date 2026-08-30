#include "UrlRepository.h"

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
                           std::function<void()> onSuccess,
                           ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO urls (id, original_url, short_code) VALUES ($1, $2, $3)",
        [onSuccess](const drogon::orm::Result &) { onSuccess(); },
        [onError](const drogon::orm::DrogonDbException &e)
        {
            onError(e.base().what());
        },
        id, originalUrl, shortCode);
}

void UrlRepository::findByShortCode(
    const std::string &shortCode,
    std::function<void(std::optional<UrlRecord>)> onSuccess,
    ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT id, original_url, short_code FROM urls WHERE short_code = $1",
        [onSuccess](const drogon::orm::Result &r)
        {
            if (r.empty())
            {
                onSuccess(std::nullopt);
                return;
            }
            UrlRecord rec;
            rec.id          = r[0]["id"].as<long long>();
            rec.originalUrl = r[0]["original_url"].as<std::string>();
            rec.shortCode   = r[0]["short_code"].as<std::string>();
            onSuccess(rec);
        },
        [onError](const drogon::orm::DrogonDbException &e)
        {
            onError(e.base().what());
        },
        shortCode);
}
