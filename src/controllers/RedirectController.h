#pragma once
#include <drogon/HttpController.h>

class RedirectController : public drogon::HttpController<RedirectController>
{
  public:
    METHOD_LIST_BEGIN
    // A regex route, so only plausible short codes match. This deliberately
    // cannot match "/api/..." because the pattern forbids a slash.
    // Hyphens and underscores are allowed because custom aliases may use them;
    // generated Base62 codes never do.
    ADD_METHOD_VIA_REGEX(RedirectController::redirect,
                         "^/([0-9A-Za-z_-]{1,16})$", drogon::Get);
    METHOD_LIST_END

    void redirect(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                  std::string shortCode);
};
