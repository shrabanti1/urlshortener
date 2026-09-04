#include "UrlController.h"

#include "cache/UrlCache.h"
#include "repositories/UrlRepository.h"
#include "utils/Config.h"
#include "utils/Http.h"
#include "utils/AliasValidator.h"
#include "utils/ShortCode.h"
#include "utils/UrlValidator.h"

namespace {

// Set by JwtAuthFilter. Absent means the route was misconfigured without the
// filter, which is a programming error rather than a client error.
long long currentUserId(const drogon::HttpRequestPtr &req)
{
    if (!req->attributes()->find("userId")) return 0;
    return req->attributes()->get<long long>("userId");
}

std::string shortUrlFor(const std::string &code)
{
    return config::get("BASE_URL", "http://localhost:8080") + "/" + code;
}

}  // namespace

namespace {

// A generated code can collide with a custom alias someone already claimed.
// Rather than fail the request, take the next id and try again. Bounded so a
// genuine bug cannot spin forever.
constexpr int kMaxCodeAttempts = 5;

void insertGenerated(const UrlRepository &repo,
                     const std::string &originalUrl,
                     long long userId,
                     int expiresInDays,
                     int attempt,
                     std::function<void(const std::string &)> onCreated,
                     std::function<void(const std::string &)> onError)
{
    if (attempt >= kMaxCodeAttempts)
    {
        onError("could not allocate an unused short code");
        return;
    }

    repo.nextId(
        [&repo, originalUrl, userId, expiresInDays, attempt, onCreated,
         onError](long long id)
        {
            static const UrlRepository r;
            const std::string code = shortcode::generate(id);
            r.insert(
                id, originalUrl, code, userId, /*isCustom=*/false, expiresInDays,
                [code, originalUrl, userId, expiresInDays, attempt, onCreated,
                 onError](bool stored)
                {
                    if (stored) { onCreated(code); return; }
                    // Taken by an alias: skip this id and try the next.
                    static const UrlRepository r2;
                    LOG_WARN << "generated code " << code
                             << " already taken, retrying";
                    insertGenerated(r2, originalUrl, userId, expiresInDays,
                                    attempt + 1, onCreated, onError);
                },
                onError);
        },
        onError);
}

}  // namespace

void UrlController::createUrl(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    static const UrlRepository repo;

    const long long userId = currentUserId(req);
    if (userId == 0)
    {
        callback(http_util::jsonError(drogon::k401Unauthorized, "authentication required"));
        return;
    }

    const auto json = req->getJsonObject();
    if (!json)
    {
        callback(http_util::jsonError(drogon::k400BadRequest,
                                      "request body must be valid JSON"));
        return;
    }
    if (!json->isMember("url") || !(*json)["url"].isString())
    {
        callback(http_util::jsonError(
            drogon::k400BadRequest, "field 'url' is required and must be a string"));
        return;
    }

    const std::string originalUrl = (*json)["url"].asString();
    const std::string problem = urlvalidator::validate(originalUrl);
    if (!problem.empty())
    {
        callback(http_util::jsonError(drogon::k400BadRequest, problem));
        return;
    }

    // ---- optional expiry ---------------------------------------------------
    int expiresInDays = 0;   // 0 means never
    if (json->isMember("expiresInDays") && !(*json)["expiresInDays"].isNull())
    {
        if (!(*json)["expiresInDays"].isIntegral())
        {
            callback(http_util::jsonError(drogon::k400BadRequest,
                                          "expiresInDays must be a whole number"));
            return;
        }
        expiresInDays = (*json)["expiresInDays"].asInt();
        if (expiresInDays < 0 || expiresInDays > 3650)
        {
            callback(http_util::jsonError(
                drogon::k400BadRequest, "expiresInDays must be between 0 and 3650"));
            return;
        }
    }

    auto onDbError = [callback](const std::string &err)
    {
        LOG_ERROR << "create url failed: " << err;
        callback(http_util::jsonError(drogon::k500InternalServerError,
                                      "could not save url"));
    };

    auto respond = [callback, expiresInDays](const std::string &code)
    {
        Json::Value body;
        body["shortCode"] = code;
        body["shortUrl"]  = shortUrlFor(code);
        if (expiresInDays > 0) body["expiresInDays"] = expiresInDays;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(drogon::k201Created);
        callback(resp);
    };

    // ---- custom alias ------------------------------------------------------
    if (json->isMember("alias") && !(*json)["alias"].isNull() &&
        !(*json)["alias"].asString().empty())
    {
        if (!(*json)["alias"].isString())
        {
            callback(http_util::jsonError(drogon::k400BadRequest,
                                          "alias must be a string"));
            return;
        }
        const std::string alias = (*json)["alias"].asString();
        const std::string aliasProblem = alias_validator::validate(alias);
        if (!aliasProblem.empty())
        {
            callback(http_util::jsonError(drogon::k400BadRequest, aliasProblem));
            return;
        }

        // The alias still needs an id, because analytics reference urls.id.
        repo.nextId(
            [alias, originalUrl, userId, expiresInDays, respond, callback,
             onDbError](long long id)
            {
                static const UrlRepository r;
                r.insert(
                    id, originalUrl, alias, userId, /*isCustom=*/true, expiresInDays,
                    [alias, respond, callback](bool stored)
                    {
                        if (!stored)
                        {
                            // Unlike a generated code, the user asked for THIS
                            // one, so tell them it is gone rather than retrying.
                            callback(http_util::jsonError(
                                drogon::k409Conflict, "that alias is already taken"));
                            return;
                        }
                        respond(alias);
                    },
                    onDbError);
            },
            onDbError);
        return;
    }

    // ---- generated code ----------------------------------------------------
    insertGenerated(repo, originalUrl, userId, expiresInDays, 0, respond, onDbError);
}

void UrlController::listUrls(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    static const UrlRepository repo;

    const long long userId = currentUserId(req);
    if (userId == 0)
    {
        callback(http_util::jsonError(drogon::k401Unauthorized, "authentication required"));
        return;
    }

    // Always bound the page size: an unbounded list is a denial-of-service
    // vector once a user has many rows.
    long long limit = 50;
    long long offset = 0;
    const auto limitParam = req->getParameter("limit");
    const auto offsetParam = req->getParameter("offset");
    if (!limitParam.empty())  { try { limit  = std::stoi(limitParam);  } catch (...) {} }
    if (!offsetParam.empty()) { try { offset = std::stoi(offsetParam); } catch (...) {} }
    limit  = std::max(1LL, std::min(limit, 100LL));
    offset = std::max(0LL, offset);

    repo.listByUser(
        userId, limit, offset,
        [callback, limit, offset](std::vector<UrlRecord> rows)
        {
            Json::Value items(Json::arrayValue);
            for (const auto &r : rows)
            {
                Json::Value item;
                item["shortCode"]   = r.shortCode;
                item["shortUrl"]    = shortUrlFor(r.shortCode);
                item["originalUrl"] = r.originalUrl;
                item["createdAt"]   = r.createdAt;
                item["isCustom"]    = r.isCustom;
                item["expiresAt"]   = r.expiresAt;   // "" means never
                item["expired"]     = r.expired;
                items.append(item);
            }
            Json::Value body;
            body["urls"]   = items;
            body["count"]  = static_cast<int>(rows.size());
            body["limit"]  = static_cast<Json::Int64>(limit);
            body["offset"] = static_cast<Json::Int64>(offset);
            callback(drogon::HttpResponse::newHttpJsonResponse(body));
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "list urls failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not list urls"));
        });
}

void UrlController::deleteUrl(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string shortCode)
{
    static const UrlRepository repo;
    static const UrlCache cache;

    const long long userId = currentUserId(req);
    if (userId == 0)
    {
        callback(http_util::jsonError(drogon::k401Unauthorized, "authentication required"));
        return;
    }

    repo.deleteOwned(
        shortCode, userId,
        [callback, shortCode](bool deleted)
        {
            if (!deleted)
            {
                // 404 for both "does not exist" and "belongs to someone else",
                // so this endpoint cannot be used to probe which codes exist.
                callback(http_util::jsonError(drogon::k404NotFound, "short url not found"));
                return;
            }

            // Invalidate AFTER the database commit: the source of truth changes
            // first, then the copy is dropped.
            static const UrlCache cache;
            cache.invalidate(shortCode);

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k204NoContent);
            callback(resp);
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "delete url failed: " << err;
            callback(http_util::jsonError(drogon::k500InternalServerError,
                                          "could not delete url"));
        });
}
