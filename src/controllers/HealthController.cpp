#include "HealthController.h"

#include <filesystem>

#include "utils/Config.h"

void HealthController::root(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    // With nginx in front, nginx serves the UI and this stays the plain-text
    // service name. Standalone (Render, Fly) the app serves the page itself.
    if (config::get("STANDALONE", "false") == "true")
    {
        const std::string index =
            config::get("STATIC_ROOT", "web") + "/index.html";
        if (std::filesystem::exists(index))
        {
            callback(drogon::HttpResponse::newFileResponse(
                index, "", drogon::CT_TEXT_HTML));
            return;
        }
    }

    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->setBody("URL Shortener API");
    callback(resp);
}

void HealthController::health(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto db = drogon::app().getDbClient();

    db->execSqlAsync(
        "SELECT 1",
        [callback](const drogon::orm::Result &)
        {
            Json::Value json;
            json["status"]   = "ok";
            json["database"] = "connected";
            callback(drogon::HttpResponse::newHttpJsonResponse(json));
        },
        [callback](const drogon::orm::DrogonDbException &e)
        {
            LOG_ERROR << "Health check DB failure: " << e.base().what();
            Json::Value json;
            json["status"]   = "error";
            json["database"] = "unreachable";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k503ServiceUnavailable);
            callback(resp);
        });
}
