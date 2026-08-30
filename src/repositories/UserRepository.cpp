#include "UserRepository.h"

void UserRepository::create(const std::string &email,
                            const std::string &passwordHash,
                            std::function<void(std::optional<User>)> onSuccess,
                            ErrorCb onError) const
{
    // ON CONFLICT DO NOTHING makes this a single atomic statement: no
    // check-then-insert race where two requests both see "email is free".
    // The conflict target is the LOWER(email) expression index.
    drogon::app().getDbClient()->execSqlAsync(
        "INSERT INTO users (email, password_hash) VALUES ($1, $2) "
        "ON CONFLICT (LOWER(email)) DO NOTHING "
        "RETURNING id, email, password_hash",
        [onSuccess](const drogon::orm::Result &r)
        {
            if (r.empty())
            {
                onSuccess(std::nullopt);  // email already taken
                return;
            }
            User u;
            u.id           = r[0]["id"].as<long long>();
            u.email        = r[0]["email"].as<std::string>();
            u.passwordHash = r[0]["password_hash"].as<std::string>();
            onSuccess(u);
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        email, passwordHash);
}

void UserRepository::findByEmail(const std::string &email,
                                 std::function<void(std::optional<User>)> onSuccess,
                                 ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT id, email, password_hash FROM users WHERE LOWER(email) = LOWER($1)",
        [onSuccess](const drogon::orm::Result &r)
        {
            if (r.empty())
            {
                onSuccess(std::nullopt);
                return;
            }
            User u;
            u.id           = r[0]["id"].as<long long>();
            u.email        = r[0]["email"].as<std::string>();
            u.passwordHash = r[0]["password_hash"].as<std::string>();
            onSuccess(u);
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        email);
}

void UserRepository::findById(long long id,
                              std::function<void(std::optional<User>)> onSuccess,
                              ErrorCb onError) const
{
    drogon::app().getDbClient()->execSqlAsync(
        "SELECT id, email, password_hash FROM users WHERE id = $1",
        [onSuccess](const drogon::orm::Result &r)
        {
            if (r.empty()) { onSuccess(std::nullopt); return; }
            User u;
            u.id           = r[0]["id"].as<long long>();
            u.email        = r[0]["email"].as<std::string>();
            u.passwordHash = r[0]["password_hash"].as<std::string>();
            onSuccess(u);
        },
        [onError](const drogon::orm::DrogonDbException &e)
        { onError(e.base().what()); },
        id);
}
