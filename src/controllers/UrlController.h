#pragma once
#include <drogon/HttpController.h>

// Inheriting from HttpController<T> makes Drogon discover and register this
// class automatically at startup. We never construct it in main.cpp.
class UrlController : public drogon::HttpController<UrlController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(UrlController::createUrl, "/api/urls", drogon::Post);
    METHOD_LIST_END

    void createUrl(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
