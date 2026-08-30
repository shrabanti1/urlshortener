#include <drogon/HttpClient.h>
#include <gtest/gtest.h>

#include <future>

#include "TestEnv.h"

namespace {

struct Reply
{
    int status = 0;
    Json::Value json;
};

Reply call(const std::string &method,
           const std::string &path,
           const Json::Value *payload = nullptr,
           const std::string &bearer = "")
{
    auto client = drogon::HttpClient::newHttpClient(testenv::baseUrl());
    drogon::HttpRequestPtr req = payload
                                     ? drogon::HttpRequest::newHttpJsonRequest(*payload)
                                     : drogon::HttpRequest::newHttpRequest();
    req->setPath(path);
    if (method == "POST")        req->setMethod(drogon::Post);
    else if (method == "GET")    req->setMethod(drogon::Get);
    else if (method == "DELETE") req->setMethod(drogon::Delete);
    if (!bearer.empty()) req->addHeader("Authorization", "Bearer " + bearer);

    std::promise<Reply> promise;
    auto future = promise.get_future();
    client->sendRequest(
        req,
        [&promise](drogon::ReqResult r, const drogon::HttpResponsePtr &resp)
        {
            Reply out;
            if (r == drogon::ReqResult::Ok && resp)
            {
                out.status = static_cast<int>(resp->getStatusCode());
                if (auto j = resp->getJsonObject()) out.json = *j;
            }
            promise.set_value(std::move(out));
        },
        10.0);
    return future.get();
}

Reply registerUser(const std::string &email, const std::string &pw)
{
    Json::Value body;
    body["email"] = email;
    body["password"] = pw;
    return call("POST", "/api/auth/register", &body);
}

Reply doRefresh(const std::string &token)
{
    Json::Value body;
    body["refreshToken"] = token;
    return call("POST", "/api/auth/refresh", &body);
}

class RefreshFlowTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite() { testenv::startOnce(); }
    void SetUp() override
    {
        testenv::resetDatabase();
        testenv::flushCache();
    }
};

}  // namespace

TEST_F(RefreshFlowTest, RegisterReturnsBothTokens)
{
    const auto r = registerUser("alice@example.com", "correct-horse-battery");
    ASSERT_EQ(r.status, 201);
    EXPECT_FALSE(r.json["accessToken"].asString().empty());
    EXPECT_FALSE(r.json["refreshToken"].asString().empty());
    EXPECT_EQ(r.json["refreshToken"].asString().size(), 64u);
    EXPECT_GT(r.json["refreshExpiresIn"].asInt64(), 0);
}

TEST_F(RefreshFlowTest, LoginReturnsBothTokens)
{
    registerUser("alice@example.com", "correct-horse-battery");
    Json::Value creds;
    creds["email"] = "alice@example.com";
    creds["password"] = "correct-horse-battery";
    const auto r = call("POST", "/api/auth/login", &creds);
    ASSERT_EQ(r.status, 200);
    EXPECT_FALSE(r.json["refreshToken"].asString().empty());
}

TEST_F(RefreshFlowTest, RefreshReturnsWorkingAccessToken)
{
    const auto reg = registerUser("alice@example.com", "correct-horse-battery");
    const auto refreshed = doRefresh(reg.json["refreshToken"].asString());
    ASSERT_EQ(refreshed.status, 200);

    const auto newAccess = refreshed.json["accessToken"].asString();
    ASSERT_FALSE(newAccess.empty());
    EXPECT_EQ(call("GET", "/api/urls", nullptr, newAccess).status, 200);
}

// Rotation: every refresh issues a NEW refresh token.
TEST_F(RefreshFlowTest, RefreshRotatesTheToken)
{
    const auto reg = registerUser("alice@example.com", "correct-horse-battery");
    const auto first = reg.json["refreshToken"].asString();

    const auto refreshed = doRefresh(first);
    ASSERT_EQ(refreshed.status, 200);
    const auto second = refreshed.json["refreshToken"].asString();

    EXPECT_FALSE(second.empty());
    EXPECT_NE(first, second) << "refresh token was not rotated";
    EXPECT_EQ(doRefresh(second).status, 200) << "the new token should work";
}

// The old token must stop working the moment it is rotated.
TEST_F(RefreshFlowTest, RotatedTokenIsRejected)
{
    const auto reg = registerUser("alice@example.com", "correct-horse-battery");
    const auto first = reg.json["refreshToken"].asString();
    ASSERT_EQ(doRefresh(first).status, 200);
    EXPECT_EQ(doRefresh(first).status, 401) << "a rotated token was accepted twice";
}

// Replaying a rotated token means it probably leaked. Every session for that
// user is revoked, so an attacker cannot keep using a stolen chain.
TEST_F(RefreshFlowTest, ReplayRevokesEverySession)
{
    const auto reg = registerUser("alice@example.com", "correct-horse-battery");
    const auto first = reg.json["refreshToken"].asString();

    const auto second = doRefresh(first).json["refreshToken"].asString();
    ASSERT_FALSE(second.empty());

    // Attacker replays the already-used token.
    EXPECT_EQ(doRefresh(first).status, 401);

    // The legitimate current token is now dead too, forcing a fresh login.
    EXPECT_EQ(doRefresh(second).status, 401)
        << "reuse detection did not revoke the token family";
}

TEST_F(RefreshFlowTest, RejectsUnknownAndMalformedTokens)
{
    EXPECT_EQ(doRefresh(std::string(64, 'a')).status, 401);
    EXPECT_EQ(doRefresh("not-a-token").status, 401);

    Json::Value empty(Json::objectValue);
    EXPECT_EQ(call("POST", "/api/auth/refresh", &empty).status, 400);
}

TEST_F(RefreshFlowTest, LogoutRevokesTheToken)
{
    const auto reg = registerUser("alice@example.com", "correct-horse-battery");
    const auto token = reg.json["refreshToken"].asString();

    Json::Value body;
    body["refreshToken"] = token;
    EXPECT_EQ(call("POST", "/api/auth/logout", &body).status, 204);
    EXPECT_EQ(doRefresh(token).status, 401);

    // Logging out twice is not an error.
    EXPECT_EQ(call("POST", "/api/auth/logout", &body).status, 204);
}

TEST_F(RefreshFlowTest, LogoutAllDevicesRevokesEverySession)
{
    const auto a = registerUser("alice@example.com", "correct-horse-battery");

    Json::Value creds;
    creds["email"] = "alice@example.com";
    creds["password"] = "correct-horse-battery";
    const auto b = call("POST", "/api/auth/login", &creds);   // second device

    const auto tokenA = a.json["refreshToken"].asString();
    const auto tokenB = b.json["refreshToken"].asString();
    ASSERT_NE(tokenA, tokenB);

    Json::Value body;
    body["refreshToken"] = tokenA;
    body["allDevices"] = true;
    EXPECT_EQ(call("POST", "/api/auth/logout", &body).status, 204);

    EXPECT_EQ(doRefresh(tokenA).status, 401);
    EXPECT_EQ(doRefresh(tokenB).status, 401) << "other device was not logged out";
}

// Two users' tokens must never be interchangeable.
TEST_F(RefreshFlowTest, TokensAreScopedToTheirUser)
{
    const auto alice = registerUser("alice@example.com", "correct-horse-battery");
    const auto bob   = registerUser("bob@example.com", "another-good-password");

    const auto refreshed = doRefresh(bob.json["refreshToken"].asString());
    ASSERT_EQ(refreshed.status, 200);

    // Bob's refreshed access token must only see Bob's urls.
    Json::Value url;
    url["url"] = "https://example.com/alice";
    call("POST", "/api/urls", &url, alice.json["accessToken"].asString());

    const auto list = call("GET", "/api/urls", nullptr,
                           refreshed.json["accessToken"].asString());
    EXPECT_EQ(list.json["count"].asInt(), 0);
}
