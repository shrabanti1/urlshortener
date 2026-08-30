#include "AuthController.h"

#include "repositories/RefreshTokenRepository.h"
#include "repositories/UserRepository.h"
#include "utils/Config.h"
#include "utils/Http.h"
#include "utils/Jwt.h"
#include "utils/Password.h"

namespace {

constexpr size_t kMinPasswordLength = 8;
constexpr size_t kMaxPasswordLength = 256;  // bound the work an attacker can force

// Deliberately permissive. Over-strict email regexes reject valid addresses;
// the real proof that an address works is sending mail to it.
bool looksLikeEmail(const std::string &e)
{
    const auto at = e.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= e.size()) return false;
    if (e.find(' ') != std::string::npos) return false;
    return e.find('.', at) != std::string::npos;
}

// Extracts and validates {email, password}. Returns an error response, or
// nullptr when the input is good.
drogon::HttpResponsePtr readCredentials(const drogon::HttpRequestPtr &req,
                                        std::string &email,
                                        std::string &pw)
{
    const auto json = req->getJsonObject();
    if (!json)
        return http_util::jsonError(drogon::k400BadRequest,
                                    "request body must be valid JSON");

    if (!json->isMember("email") || !(*json)["email"].isString() ||
        !json->isMember("password") || !(*json)["password"].isString())
        return http_util::jsonError(
            drogon::k400BadRequest,
            "fields 'email' and 'password' are required and must be strings");

    email = (*json)["email"].asString();
    pw    = (*json)["password"].asString();

    if (!looksLikeEmail(email))
        return http_util::jsonError(drogon::k400BadRequest, "invalid email address");

    if (pw.size() < kMinPasswordLength)
        return http_util::jsonError(drogon::k400BadRequest,
                                    "password must be at least 8 characters");

    if (pw.size() > kMaxPasswordLength)
        return http_util::jsonError(drogon::k400BadRequest,
                                    "password must be at most 256 characters");
    return nullptr;
}

// Issues an access token plus a freshly stored refresh token, then answers.
void respondWithTokens(long long userId,
                       const std::string &email,
                       drogon::HttpStatusCode status,
                       std::function<void(const drogon::HttpResponsePtr &)> callback)
{
    static const RefreshTokenRepository tokens;

    const auto issued = refresh_token::mint();

    tokens.store(
        userId, issued.tokenHash, refresh_token::lifetimeDays(),
        [callback, userId, email, status, plain = issued.token](long long)
        {
            Json::Value body;
            body["accessToken"]  = jwt_util::issueAccessToken(userId, email);
            body["tokenType"]    = "Bearer";
            body["expiresIn"]    = config::getInt("JWT_EXPIRY_MINUTES", 60) * 60;
            // Only ever sent here; the server keeps a SHA-256 of it.
            body["refreshToken"] = plain;
            body["refreshExpiresIn"] =
                refresh_token::lifetimeDays() * 24 * 60 * 60;

            Json::Value user;
            user["id"]    = static_cast<Json::Int64>(userId);
            user["email"] = email;
            body["user"]  = user;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
            resp->setStatusCode(status);
            callback(resp);
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "could not store refresh token: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not complete sign in"));
        });
}

// Reads {"refreshToken": "..."} from the body.
std::string readRefreshToken(const drogon::HttpRequestPtr &req)
{
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("refreshToken") ||
        !(*json)["refreshToken"].isString())
        return "";
    return (*json)["refreshToken"].asString();
}

}  // namespace

void AuthController::_register(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string email, pw;
    if (auto bad = readCredentials(req, email, pw)) { callback(bad); return; }

    const std::string hash = password::hash(pw);
    if (hash.empty())
    {
        LOG_ERROR << "password hashing failed";
        callback(http_util::jsonError(drogon::k500InternalServerError,
                                      "could not create account"));
        return;
    }

    static const UserRepository users;
    users.create(
        email, hash,
        [callback, email](std::optional<User> created)
        {
            if (!created)
            {
                // 409 Conflict. This does reveal that the email is registered
                // -- see README for the enumeration tradeoff.
                callback(http_util::jsonError(drogon::k409Conflict,
                                              "email already registered"));
                return;
            }
            respondWithTokens(created->id, created->email, drogon::k201Created,
                              callback);
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "register failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not create account"));
        });
}

void AuthController::login(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    std::string email, pw;
    if (auto bad = readCredentials(req, email, pw)) { callback(bad); return; }

    static const UserRepository users;
    users.findByEmail(
        email,
        [callback, pw](std::optional<User> user)
        {
            // One message for both "no such user" and "wrong password", so the
            // response cannot be used to discover which emails are registered.
            static const auto reject = []
            {
                return http_util::jsonError(drogon::k401Unauthorized,
                                            "invalid email or password");
            };

            if (!user)
            {
                // Hash anyway so a missing account does not return noticeably
                // faster than a wrong password (timing side channel).
                (void)password::verify(
                    pw,
                    "$argon2id$v=19$m=65536,t=2,p=1$"
                    "AAAAAAAAAAAAAAAAAAAAAA$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
                callback(reject());
                return;
            }

            if (!password::verify(pw, user->passwordHash))
            {
                callback(reject());
                return;
            }

            respondWithTokens(user->id, user->email, drogon::k200OK, callback);
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "login failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not sign in"));
        });
}


void AuthController::refresh(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    static const RefreshTokenRepository tokens;
    static const UserRepository users;

    const std::string presented = readRefreshToken(req);
    if (presented.empty())
    {
        callback(http_util::jsonError(drogon::k400BadRequest,
                                      "field 'refreshToken' is required"));
        return;
    }

    const std::string hash = refresh_token::hash(presented);

    tokens.findByHash(
        hash,
        [callback, hash](std::optional<RefreshTokenRecord> record)
        {
            static const RefreshTokenRepository tokens;
            static const UserRepository users;

            // One message for unknown, expired and revoked, so a caller cannot
            // probe which tokens ever existed.
            auto reject = []
            {
                return http_util::jsonError(drogon::k401Unauthorized,
                                            "invalid or expired refresh token");
            };

            if (!record || record->expired)
            {
                callback(reject());
                return;
            }

            if (record->revoked)
            {
                // A token that was already rotated is being replayed. Either it
                // leaked, or a client is retrying. Safest response is to revoke
                // the whole family and force a fresh login.
                LOG_WARN << "refresh token reuse detected for user "
                         << record->userId << "; revoking all sessions";
                tokens.revokeAllForUser(
                    record->userId,
                    [callback, reject](long long n)
                    {
                        LOG_WARN << "revoked " << n << " refresh tokens";
                        callback(reject());
                    },
                    [callback, reject](const std::string &) { callback(reject()); });
                return;
            }

            const long long userId = record->userId;
            const long long oldId = record->id;

            users.findById(
                userId,
                [callback, userId, oldId](std::optional<User> user)
                {
                    static const RefreshTokenRepository tokens;
                    if (!user)
                    {
                        callback(http_util::jsonError(drogon::k401Unauthorized,
                                                      "invalid or expired refresh token"));
                        return;
                    }

                    // Rotation: mint a new refresh token and retire the old one.
                    const auto issued = refresh_token::mint();
                    tokens.store(
                        userId, issued.tokenHash, refresh_token::lifetimeDays(),
                        [callback, userId, oldId, email = user->email,
                         plain = issued.token](long long newId)
                        {
                            static const RefreshTokenRepository tokens;
                            tokens.revoke(
                                oldId, newId,
                                [callback, userId, email, plain]
                                {
                                    Json::Value body;
                                    body["accessToken"] =
                                        jwt_util::issueAccessToken(userId, email);
                                    body["tokenType"] = "Bearer";
                                    body["expiresIn"] =
                                        config::getInt("JWT_EXPIRY_MINUTES", 60) * 60;
                                    body["refreshToken"] = plain;
                                    body["refreshExpiresIn"] =
                                        refresh_token::lifetimeDays() * 24 * 60 * 60;
                                    callback(
                                        drogon::HttpResponse::newHttpJsonResponse(body));
                                },
                                [callback](const std::string &err)
                                {
                                    LOG_ERROR << "revoke failed: " << err;
                                    callback(http_util::jsonError(
                                        drogon::k500InternalServerError,
                                        "could not refresh session"));
                                });
                        },
                        [callback](const std::string &err)
                        {
                            LOG_ERROR << "store failed: " << err;
                            callback(http_util::jsonError(
                                drogon::k500InternalServerError,
                                "could not refresh session"));
                        });
                },
                [callback](const std::string &err)
                {
                    LOG_ERROR << "user lookup failed: " << err;
                    callback(http_util::jsonError(drogon::k500InternalServerError,
                                                  "could not refresh session"));
                });
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "refresh lookup failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not refresh session"));
        });
}

void AuthController::logout(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    static const RefreshTokenRepository tokens;

    const std::string presented = readRefreshToken(req);
    if (presented.empty())
    {
        callback(http_util::jsonError(drogon::k400BadRequest,
                                      "field 'refreshToken' is required"));
        return;
    }

    // Logout is idempotent and always reports success: telling a caller that a
    // token did not exist would leak information, and a client logging out
    // twice is not an error.
    auto done = [callback]
    {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k204NoContent);
        callback(resp);
    };

    const bool everywhere = [&]
    {
        const auto json = req->getJsonObject();
        return json && (*json)["allDevices"].isBool() &&
               (*json)["allDevices"].asBool();
    }();

    tokens.findByHash(
        refresh_token::hash(presented),
        [done, everywhere](std::optional<RefreshTokenRecord> record)
        {
            static const RefreshTokenRepository tokens;
            if (!record) { done(); return; }

            if (everywhere)
            {
                tokens.revokeAllForUser(
                    record->userId, [done](long long) { done(); },
                    [done](const std::string &) { done(); });
                return;
            }
            tokens.revoke(record->id, 0, done, [done](const std::string &) { done(); });
        },
        [done](const std::string &) { done(); });
}
