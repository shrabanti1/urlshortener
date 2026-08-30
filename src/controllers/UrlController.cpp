#include "UrlController.h"

#include "repositories/UrlRepository.h"
#include "utils/Config.h"
#include "utils/Http.h"
#include "utils/ShortCode.h"
#include "utils/UrlValidator.h"

void UrlController::createUrl(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    static const UrlRepository repo;

    // ---- 1. Parse ----------------------------------------------------------
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

    // ---- 2. Validate -------------------------------------------------------
    const std::string problem = urlvalidator::validate(originalUrl);
    if (!problem.empty())
    {
        callback(http_util::jsonError(drogon::k400BadRequest, problem));
        return;
    }

    // ---- 3. Persist --------------------------------------------------------
    auto onDbError = [callback](const std::string &err)
    {
        LOG_ERROR << "create url failed: " << err;
        callback(http_util::jsonError(drogon::k500InternalServerError,
                                      "could not save url"));
    };

    repo.nextId(
        [originalUrl, callback, onDbError](long long id)
        {
            static const UrlRepository repo;
            const std::string code = shortcode::generate(id);

            repo.insert(
                id, originalUrl, code,
                [code, callback]()
                {
                    Json::Value body;
                    body["shortCode"] = code;
                    body["shortUrl"] =
                        config::get("BASE_URL", "http://localhost:8080") + "/" + code;

                    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
                    resp->setStatusCode(drogon::k201Created);
                    callback(resp);
                },
                onDbError);
        },
        onDbError);
}
