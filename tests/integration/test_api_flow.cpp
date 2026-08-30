#include <drogon/HttpClient.h>
#include <gtest/gtest.h>

#include <future>

#include "TestEnv.h"

namespace {

struct Reply
{
    int status = 0;
    Json::Value json;
    std::string location;
    std::string body;
};

// End-to-end: real HTTP over a socket into the running app.
Reply call(const std::string &method,
           const std::string &path,
           const Json::Value *payload = nullptr,
           const std::string &bearer = "")
{
    auto client = drogon::HttpClient::newHttpClient(testenv::baseUrl());

    drogon::HttpRequestPtr req;
    if (payload)
        req = drogon::HttpRequest::newHttpJsonRequest(*payload);
    else
        req = drogon::HttpRequest::newHttpRequest();

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
                out.status   = static_cast<int>(resp->getStatusCode());
                out.location = resp->getHeader("location");
                out.body     = std::string(resp->getBody());
                if (auto j = resp->getJsonObject()) out.json = *j;
            }
            promise.set_value(std::move(out));
        },
        10.0);
    return future.get();
}

std::string registerUser(const std::string &email, const std::string &pw)
{
    Json::Value body;
    body["email"] = email;
    body["password"] = pw;
    const auto r = call("POST", "/api/auth/register", &body);
    return r.json.get("accessToken", "").asString();
}

class ApiFlowTest : public ::testing::Test
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

TEST_F(ApiFlowTest, HealthReportsDatabaseConnected)
{
    const auto r = call("GET", "/health");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.json["status"].asString(), "ok");
    EXPECT_EQ(r.json["database"].asString(), "connected");
}

TEST_F(ApiFlowTest, RootReturnsServiceName)
{
    const auto r = call("GET", "/");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "URL Shortener API");
}

// The complete journey a real user takes.
TEST_F(ApiFlowTest, RegisterCreateShortenRedirect)
{
    const auto token = registerUser("alice@example.com", "correct-horse-battery");
    ASSERT_FALSE(token.empty());

    Json::Value body;
    body["url"] = "https://example.com/a/very/long/url";
    const auto created = call("POST", "/api/urls", &body, token);
    ASSERT_EQ(created.status, 201);

    const auto code = created.json["shortCode"].asString();
    EXPECT_EQ(code, "1000");  // Base62 of the first id
    EXPECT_EQ(created.json["shortUrl"].asString(),
              "http://127.0.0.1:18080/1000");

    const auto redirect = call("GET", "/" + code);
    EXPECT_EQ(redirect.status, 302);
    EXPECT_EQ(redirect.location, "https://example.com/a/very/long/url");
}

// Same request twice: second one is served from Redis. Both must be identical.
TEST_F(ApiFlowTest, RedirectIsIdenticalOnCacheHit)
{
    const auto token = registerUser("alice@example.com", "correct-horse-battery");
    Json::Value body;
    body["url"] = "https://example.com/cached";
    call("POST", "/api/urls", &body, token);

    const auto first  = call("GET", "/1000");
    const auto second = call("GET", "/1000");
    EXPECT_EQ(first.status, 302);
    EXPECT_EQ(second.status, 302);
    EXPECT_EQ(first.location, second.location);
}

TEST_F(ApiFlowTest, UnknownCodeReturns404)
{
    EXPECT_EQ(call("GET", "/zzzzz").status, 404);
}

TEST_F(ApiFlowTest, ProtectedRoutesRejectMissingOrBadTokens)
{
    Json::Value body;
    body["url"] = "https://example.com/x";
    EXPECT_EQ(call("POST", "/api/urls", &body).status, 401);
    EXPECT_EQ(call("POST", "/api/urls", &body, "not.a.jwt").status, 401);
    EXPECT_EQ(call("GET", "/api/urls").status, 401);
    EXPECT_EQ(call("DELETE", "/api/urls/1000").status, 401);
    EXPECT_EQ(call("GET", "/api/urls/1000/stats").status, 401);
}

TEST_F(ApiFlowTest, RejectsInvalidUrls)
{
    const auto token = registerUser("alice@example.com", "correct-horse-battery");
    for (const char *bad : {"", "example.com", "javascript:alert(1)", "https://"})
    {
        Json::Value body;
        body["url"] = bad;
        EXPECT_EQ(call("POST", "/api/urls", &body, token).status, 400)
            << "accepted: " << bad;
    }
}

TEST_F(ApiFlowTest, DuplicateRegistrationIsRejected)
{
    Json::Value body;
    body["email"] = "alice@example.com";
    body["password"] = "correct-horse-battery";
    EXPECT_EQ(call("POST", "/api/auth/register", &body).status, 201);
    EXPECT_EQ(call("POST", "/api/auth/register", &body).status, 409);
}

TEST_F(ApiFlowTest, LoginReturnsUsableToken)
{
    registerUser("alice@example.com", "correct-horse-battery");

    Json::Value creds;
    creds["email"] = "alice@example.com";
    creds["password"] = "correct-horse-battery";
    const auto login = call("POST", "/api/auth/login", &creds);
    ASSERT_EQ(login.status, 200);

    const auto token = login.json["accessToken"].asString();
    EXPECT_EQ(call("GET", "/api/urls", nullptr, token).status, 200);
}

TEST_F(ApiFlowTest, LoginFailuresAreIndistinguishable)
{
    registerUser("alice@example.com", "correct-horse-battery");

    Json::Value wrongPw;
    wrongPw["email"] = "alice@example.com";
    wrongPw["password"] = "definitely-not-it";

    Json::Value noUser;
    noUser["email"] = "nobody@example.com";
    noUser["password"] = "correct-horse-battery";

    const auto a = call("POST", "/api/auth/login", &wrongPw);
    const auto b = call("POST", "/api/auth/login", &noUser);
    EXPECT_EQ(a.status, 401);
    EXPECT_EQ(b.status, 401);
    EXPECT_EQ(a.json["error"].asString(), b.json["error"].asString())
        << "responses reveal which emails exist";
}

// Authorization, end to end.
TEST_F(ApiFlowTest, UsersCannotSeeOrDeleteEachOthersUrls)
{
    const auto alice = registerUser("alice@example.com", "correct-horse-battery");
    const auto bob   = registerUser("bob@example.com", "another-good-password");

    Json::Value body;
    body["url"] = "https://example.com/alice-private";
    const auto created = call("POST", "/api/urls", &body, alice);
    ASSERT_EQ(created.status, 201);
    const auto code = created.json["shortCode"].asString();

    // Bob's list is empty; Alice's has one.
    EXPECT_EQ(call("GET", "/api/urls", nullptr, bob).json["count"].asInt(), 0);
    EXPECT_EQ(call("GET", "/api/urls", nullptr, alice).json["count"].asInt(), 1);

    // Bob cannot read stats or delete, and gets 404 (not 403) either way.
    EXPECT_EQ(call("GET", "/api/urls/" + code + "/stats", nullptr, bob).status, 404);
    EXPECT_EQ(call("DELETE", "/api/urls/" + code, nullptr, bob).status, 404);

    // The row survived Bob's attempt.
    EXPECT_EQ(call("GET", "/" + code).status, 302);

    // Alice can.
    EXPECT_EQ(call("DELETE", "/api/urls/" + code, nullptr, alice).status, 204);
    EXPECT_EQ(call("GET", "/" + code).status, 404);
}

// Deleting must drop the Redis entry, or the link would keep resolving.
TEST_F(ApiFlowTest, DeleteInvalidatesTheCache)
{
    const auto token = registerUser("alice@example.com", "correct-horse-battery");
    Json::Value body;
    body["url"] = "https://example.com/to-delete";
    const auto code = call("POST", "/api/urls", &body, token).json["shortCode"].asString();

    EXPECT_EQ(call("GET", "/" + code).status, 302);   // warms the cache
    EXPECT_EQ(call("DELETE", "/api/urls/" + code, nullptr, token).status, 204);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(call("GET", "/" + code).status, 404) << "cache served a deleted url";
}

TEST_F(ApiFlowTest, StatsCountClicks)
{
    const auto token = registerUser("alice@example.com", "correct-horse-battery");
    Json::Value body;
    body["url"] = "https://example.com/tracked";
    const auto code = call("POST", "/api/urls", &body, token).json["shortCode"].asString();

    for (int i = 0; i < 5; ++i) call("GET", "/" + code);

    const auto stats = call("GET", "/api/urls/" + code + "/stats", nullptr, token);
    ASSERT_EQ(stats.status, 200);
    EXPECT_EQ(stats.json["shortCode"].asString(), code);
    EXPECT_EQ(stats.json["totalClicks"].asInt64(), 5);
    EXPECT_EQ(stats.json["todayClicks"].asInt64(), 5);
}

TEST_F(ApiFlowTest, ShortCodesAreSequentialBase62)
{
    const auto token = registerUser("alice@example.com", "correct-horse-battery");
    std::vector<std::string> codes;
    for (int i = 0; i < 3; ++i)
    {
        Json::Value body;
        body["url"] = "https://example.com/" + std::to_string(i);
        codes.push_back(call("POST", "/api/urls", &body, token).json["shortCode"].asString());
    }
    EXPECT_EQ(codes[0], "1000");
    EXPECT_EQ(codes[1], "1001");
    EXPECT_EQ(codes[2], "1002");
}
