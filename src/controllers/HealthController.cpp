#include "HealthController.h"

void HealthController::root(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
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
