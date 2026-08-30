#pragma once
#include <drogon/HttpController.h>

// These used to live in main.cpp as registerHandler() calls. Routes defined in
// main are invisible to the test binary (which has its own main), so they were
// untestable. As a controller they auto-register wherever the code is linked.
class HealthController : public drogon::HttpController<HealthController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HealthController::root,   "/",       drogon::Get);
    ADD_METHOD_TO(HealthController::health, "/health", drogon::Get);
    METHOD_LIST_END

    void root(const drogon::HttpRequestPtr &req,
              std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void health(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
