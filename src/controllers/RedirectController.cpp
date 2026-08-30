#include "RedirectController.h"

#include "repositories/UrlRepository.h"

namespace {

drogon::HttpResponsePtr notFound()
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k404NotFound);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->setBody("Short URL not found");
    return resp;
}

}  // namespace

void RedirectController::redirect(
    const drogon::HttpRequestPtr &,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback,
    std::string shortCode)
{
    static const UrlRepository repo;

    repo.findByShortCode(
        shortCode,
        [callback](std::optional<UrlRecord> found)
        {
            if (!found)
            {
                callback(notFound());
                return;
            }
            // 302 (temporary), not 301: a permanent redirect is cached by the
            // browser forever, so later requests never reach us again.
            callback(drogon::HttpResponse::newRedirectionResponse(
                found->originalUrl, drogon::k302Found));
        },
        [callback](const std::string &err)
        {
            LOG_ERROR << "lookup failed: " << err;
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k500InternalServerError);
            resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            resp->setBody("Internal error");
            callback(resp);
        });
}
