#include <drogon/drogon.h>

int main()
{
    // Route: GET /  ->  returns the plain-text string "URL Shortener API"
    drogon::app().registerHandler(
        "/",
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            // TODO (your task — 4 lines):
            //   1. Make an empty response with HttpResponse::newHttpResponse()
            //   2. Set status 200 with  ->setStatusCode(drogon::k200OK)
            //   3. Set body "URL Shortener API" with ->setBody(...)
            //   4. Hand it back by calling callback(resp)
            //
            // Bonus: make it plain text instead of HTML with
            //   ->setContentTypeCode(drogon::CT_TEXT_PLAIN)
        },
        {drogon::Get});

    LOG_INFO << "URL Shortener listening on http://127.0.0.1:8080";

    drogon::app()
        .addListener("127.0.0.1", 8080)
        .setThreadNum(1)   // 1 event-loop thread is plenty for now
        .run();

    return 0;
}
