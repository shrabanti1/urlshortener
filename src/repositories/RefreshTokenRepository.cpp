#include "RefreshTokenRepository.h"

void RefreshTokenRepository::store(long long userId,
                                   const std::string &tokenHash,
                                   int lifetimeDays,
                                   std::function<void(long long)> onSuccess,
                                   ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO refresh_tokens (user_id, token_hash, expires_at) "
        "VALUES ($1, $2, NOW() + ($3::int * INTERVAL '1 day')) RETURNING id",
        [onSuccess](const drogon::orm::Result &r)
        { onSuccess(r[0]["id"].as<long long>()); },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        userId, tokenHash, lifetimeDays);
}

void RefreshTokenRepository::findByHash(
    const std::string &tokenHash,
    std::function<void(std::optional<RefreshTokenRecord>)> onSuccess,
    ErrorCb onError) const
{
    // Expiry is evaluated by the database, not the app: server clocks are the
    // single source of truth for time here.
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT id, user_id, (revoked_at IS NOT NULL) AS revoked, "
        "       (expires_at <= NOW()) AS expired "
        "FROM refresh_tokens WHERE token_hash = $1",
        [onSuccess](const drogon::orm::Result &r)
        {
            if (r.empty()) { onSuccess(std::nullopt); return; }
            RefreshTokenRecord rec;
            rec.id      = r[0]["id"].as<long long>();
            rec.userId  = r[0]["user_id"].as<long long>();
            rec.revoked = r[0]["revoked"].as<bool>();
            rec.expired = r[0]["expired"].as<bool>();
            onSuccess(rec);
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        tokenHash);
}

void RefreshTokenRepository::revoke(long long id,
                                    long long replacedBy,
                                    std::function<void()> onSuccess,
                                    ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "UPDATE refresh_tokens SET revoked_at = NOW(), "
        "       replaced_by = NULLIF($2::bigint, 0) "
        "WHERE id = $1 AND revoked_at IS NULL",
        [onSuccess](const drogon::orm::Result &) { onSuccess(); },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        id, replacedBy);
}

void RefreshTokenRepository::revokeAllForUser(long long userId,
                                              std::function<void(long long)> onSuccess,
                                              ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "UPDATE refresh_tokens SET revoked_at = NOW() "
        "WHERE user_id = $1 AND revoked_at IS NULL",
        [onSuccess](const drogon::orm::Result &r)
        { onSuccess(static_cast<long long>(r.affectedRows())); },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        userId);
}

void RefreshTokenRepository::deleteExpired(std::function<void(long long)> onSuccess,
                                           ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "DELETE FROM refresh_tokens WHERE expires_at <= NOW() - INTERVAL '7 days'",
        [onSuccess](const drogon::orm::Result &r)
        { onSuccess(static_cast<long long>(r.affectedRows())); },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); });
}
