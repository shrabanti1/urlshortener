#pragma once
#include <drogon/HttpController.h>

class AnalyticsController : public drogon::HttpController<AnalyticsController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_VIA_REGEX(AnalyticsController::stats,
                         "^/api/urls/([0-9A-Za-z]{1,16})/stats$",
                         drogon::Get, "JwtAuthFilter");
    METHOD_LIST_END

    void stats(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
               std::string shortCode);
};
