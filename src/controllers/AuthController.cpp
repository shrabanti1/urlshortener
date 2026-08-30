#include "AuthController.h"

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

drogon::HttpResponsePtr tokenResponse(long long userId,
                                      const std::string &email,
                                      drogon::HttpStatusCode status)
{
    Json::Value body;
    body["accessToken"] = jwt_util::issueAccessToken(userId, email);
    body["tokenType"]   = "Bearer";
    body["expiresIn"]   = config::getInt("JWT_EXPIRY_MINUTES", 60) * 60;

    Json::Value user;
    user["id"]    = static_cast<Json::Int64>(userId);
    user["email"] = email;
    body["user"]  = user;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
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
            callback(tokenResponse(created->id, created->email, drogon::k201Created));
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

            callback(tokenResponse(user->id, user->email, drogon::k200OK));
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "login failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not sign in"));
        });
}
