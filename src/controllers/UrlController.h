#pragma once
#include <drogon/HttpController.h>

class UrlController : public drogon::HttpController<UrlController>
{
  public:
    METHOD_LIST_BEGIN
    // The third argument is the filter chain. JwtAuthFilter runs first and
    // rejects the request before these methods are ever entered.
    ADD_METHOD_TO(UrlController::createUrl, "/api/urls", drogon::Post, "JwtAuthFilter");
    ADD_METHOD_TO(UrlController::listUrls,  "/api/urls", drogon::Get,  "JwtAuthFilter");
    ADD_METHOD_VIA_REGEX(UrlController::deleteUrl,
                         "^/api/urls/([0-9A-Za-z]{1,16})$",
                         drogon::Delete, "JwtAuthFilter");
    METHOD_LIST_END

    void createUrl(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void listUrls(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void deleteUrl(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                   std::string shortCode);
};
