#pragma once
#include <drogon/drogon.h>
#include <string>

namespace http_util {

// Every error in this API has the same shape, so clients can rely on it.
inline drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode code,
                                         const std::string &message)
{
    Json::Value body;
    body["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    return resp;
}

}  // namespace http_util
