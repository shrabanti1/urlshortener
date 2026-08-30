#pragma once
#include <drogon/HttpController.h>

class AuthController : public drogon::HttpController<AuthController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::_register, "/api/auth/register", drogon::Post);
    ADD_METHOD_TO(AuthController::login,     "/api/auth/login",    drogon::Post);
    METHOD_LIST_END

    // "register" is a C++ keyword, hence the underscore.
    void _register(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void login(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
