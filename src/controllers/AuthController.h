#pragma once
#include <drogon/HttpController.h>

class AuthController : public drogon::HttpController<AuthController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::_register, "/api/auth/register", drogon::Post);
    ADD_METHOD_TO(AuthController::login,     "/api/auth/login",    drogon::Post);
    ADD_METHOD_TO(AuthController::refresh,   "/api/auth/refresh",  drogon::Post);
    ADD_METHOD_TO(AuthController::logout,    "/api/auth/logout",   drogon::Post);
    METHOD_LIST_END

    // "register" is a C++ keyword, hence the underscore.
    void _register(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    void login(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Exchanges a refresh token for a new access token, rotating the refresh
    // token in the process.
    void refresh(const drogon::HttpRequestPtr &req,
                 std::function<void(const drogon::HttpResponsePtr &)> &&callback);

    // Revokes the presented refresh token (or all of the user's tokens).
    void logout(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
