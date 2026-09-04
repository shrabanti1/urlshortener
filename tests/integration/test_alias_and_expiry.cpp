#include <drogon/HttpClient.h>
#include <gtest/gtest.h>

#include <future>
#include <thread>

#include "TestEnv.h"

namespace {

struct Reply
{
    int status = 0;
    Json::Value json;
    std::string location;
    std::string body;
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

std::string tokenFor(const std::string &email)
{
    Json::Value body;
    body["email"] = email;
    body["password"] = "correct-horse-battery";
    return call("POST", "/api/auth/register", &body).json["accessToken"].asString();
}

Reply createUrl(const std::string &token,
                const std::string &url,
                const std::string &alias = "",
                int expiresInDays = 0)
{
    Json::Value body;
    body["url"] = url;
    if (!alias.empty()) body["alias"] = alias;
    if (expiresInDays > 0) body["expiresInDays"] = expiresInDays;
    return call("POST", "/api/urls", &body, token);
}

class AliasExpiryTest : public ::testing::Test
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

// ---- aliases ------------------------------------------------------------

TEST_F(AliasExpiryTest, CreatesAndResolvesACustomAlias)
{
    const auto t = tokenFor("alice@example.com");
    const auto created = createUrl(t, "https://example.com/blog", "my-blog");
    ASSERT_EQ(created.status, 201);
    EXPECT_EQ(created.json["shortCode"].asString(), "my-blog");

    const auto redirect = call("GET", "/my-blog");
    EXPECT_EQ(redirect.status, 302);
    EXPECT_EQ(redirect.location, "https://example.com/blog");
}

TEST_F(AliasExpiryTest, AliasCannotBeClaimedTwice)
{
    const auto alice = tokenFor("alice@example.com");
    const auto bob   = tokenFor("bob@example.com");

    EXPECT_EQ(createUrl(alice, "https://example.com/a", "taken").status, 201);
    // Even for a different user: the namespace is global.
    EXPECT_EQ(createUrl(bob, "https://example.com/b", "taken").status, 409);
}

TEST_F(AliasExpiryTest, RejectsInvalidAliases)
{
    const auto t = tokenFor("alice@example.com");
    for (const char *bad : {"ab", "my link", "my/link", "-lead", "trail-",
                            "seventeencharactx"})
        EXPECT_EQ(createUrl(t, "https://example.com/x", bad).status, 400)
            << "accepted: " << bad;
}

// An alias that shadowed a route would break the API itself.
TEST_F(AliasExpiryTest, RejectsReservedAliases)
{
    const auto t = tokenFor("alice@example.com");
    for (const char *r : {"api", "docs", "health", "admin", "login"})
        EXPECT_EQ(createUrl(t, "https://example.com/x", r).status, 400)
            << "accepted reserved alias: " << r;
}

TEST_F(AliasExpiryTest, RealRoutesStillWorkAlongsideAliases)
{
    const auto t = tokenFor("alice@example.com");
    createUrl(t, "https://example.com/x", "my-link");

    EXPECT_EQ(call("GET", "/health").status, 200);
    EXPECT_EQ(call("GET", "/api/urls", nullptr, t).status, 200);
}

// A generated code could land on an alias someone already took; the request
// must still succeed by taking the next id.
TEST_F(AliasExpiryTest, GeneratedCodeRetriesPastAClaimedAlias)
{
    const auto t = tokenFor("alice@example.com");

    // "Kmx000" is what the first generated id produces (permutation off in
    // tests means plain Base62, so the first generated code is "1000").
    ASSERT_EQ(createUrl(t, "https://example.com/squatted", "1000").status, 201);

    // The next generated code would have been "1000"; it must skip and succeed.
    const auto generated = createUrl(t, "https://example.com/generated");
    ASSERT_EQ(generated.status, 201);
    EXPECT_NE(generated.json["shortCode"].asString(), "1000");

    EXPECT_EQ(call("GET", "/" + generated.json["shortCode"].asString()).status, 302);
}

TEST_F(AliasExpiryTest, ListMarksCustomAliases)
{
    const auto t = tokenFor("alice@example.com");
    createUrl(t, "https://example.com/a", "custom-one");
    createUrl(t, "https://example.com/b");

    const auto list = call("GET", "/api/urls", nullptr, t);
    ASSERT_EQ(list.status, 200);
    ASSERT_EQ(list.json["count"].asInt(), 2);

    bool sawCustom = false, sawGenerated = false;
    for (const auto &u : list.json["urls"])
    {
        if (u["shortCode"].asString() == "custom-one")
        {
            EXPECT_TRUE(u["isCustom"].asBool());
            sawCustom = true;
        }
        else
        {
            EXPECT_FALSE(u["isCustom"].asBool());
            sawGenerated = true;
        }
    }
    EXPECT_TRUE(sawCustom && sawGenerated);
}

// ---- expiry -------------------------------------------------------------

TEST_F(AliasExpiryTest, LinkWithFutureExpiryStillWorks)
{
    const auto t = tokenFor("alice@example.com");
    const auto created = createUrl(t, "https://example.com/soon", "", 30);
    ASSERT_EQ(created.status, 201);
    EXPECT_EQ(created.json["expiresInDays"].asInt(), 30);

    EXPECT_EQ(call("GET", "/" + created.json["shortCode"].asString()).status, 302);
}

TEST_F(AliasExpiryTest, ExpiredLinkReturns410NotFound)
{
    const auto t = tokenFor("alice@example.com");
    const auto created = createUrl(t, "https://example.com/old", "gone-link", 1);
    ASSERT_EQ(created.status, 201);

    // Move the expiry into the past directly; waiting a day is not an option.
    auto db = drogon::app().getDbClient();
    db->execSqlSync("UPDATE urls SET expires_at = NOW() - INTERVAL '1 hour' "
                    "WHERE short_code = 'gone-link'");
    testenv::flushCache();   // the pre-expiry entry may still be cached

    const auto r = call("GET", "/gone-link");
    EXPECT_EQ(r.status, 410) << "expired link should be Gone, not 404";
    EXPECT_NE(r.body.find("expired"), std::string::npos);
}

// 410 and 404 must stay distinguishable: one existed, one never did.
TEST_F(AliasExpiryTest, MissingLinkIsStill404)
{
    EXPECT_EQ(call("GET", "/never-existed").status, 404);
}

TEST_F(AliasExpiryTest, ExpiredLinkRecordsNoClicks)
{
    const auto t = tokenFor("alice@example.com");
    createUrl(t, "https://example.com/old", "no-clicks", 1);
    auto db = drogon::app().getDbClient();
    db->execSqlSync("UPDATE urls SET expires_at = NOW() - INTERVAL '1 hour' "
                    "WHERE short_code = 'no-clicks'");
    testenv::flushCache();

    call("GET", "/no-clicks");
    call("GET", "/no-clicks");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto stats = call("GET", "/api/urls/no-clicks/stats", nullptr, t);
    ASSERT_EQ(stats.status, 200);
    EXPECT_EQ(stats.json["totalClicks"].asInt64(), 0);
    EXPECT_TRUE(stats.json["expired"].asBool());
}

TEST_F(AliasExpiryTest, RejectsSillyExpiryValues)
{
    const auto t = tokenFor("alice@example.com");
    for (int bad : {-1, 4000})
    {
        Json::Value body;
        body["url"] = "https://example.com/x";
        body["expiresInDays"] = bad;
        EXPECT_EQ(call("POST", "/api/urls", &body, t).status, 400)
            << "accepted expiresInDays=" << bad;
    }
}

// ---- daily click series -------------------------------------------------

TEST_F(AliasExpiryTest, StatsIncludeDailySeriesWithZeroFilledDays)
{
    const auto t = tokenFor("alice@example.com");
    const auto code = createUrl(t, "https://example.com/chart", "charted")
                          .json["shortCode"].asString();

    for (int i = 0; i < 3; ++i) call("GET", "/" + code);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    const auto stats = call("GET", "/api/urls/" + code + "/stats?days=7", nullptr, t);
    ASSERT_EQ(stats.status, 200);

    const auto series = stats.json["dailyClicks"];
    ASSERT_TRUE(series.isArray());
    // Seven days requested means seven points, even though only today has any
    // clicks. A chart that skipped empty days would misrepresent the traffic.
    EXPECT_EQ(series.size(), 7u);

    long long total = 0;
    for (const auto &p : series)
    {
        EXPECT_TRUE(p.isMember("date"));
        EXPECT_TRUE(p.isMember("clicks"));
        total += p["clicks"].asInt64();
    }
    EXPECT_EQ(total, 3);
    EXPECT_EQ(series[series.size() - 1]["clicks"].asInt64(), 3) << "today should hold them";
}

TEST_F(AliasExpiryTest, DaysParameterIsClamped)
{
    const auto t = tokenFor("alice@example.com");
    const auto code = createUrl(t, "https://example.com/x", "clamped")
                          .json["shortCode"].asString();

    EXPECT_EQ(call("GET", "/api/urls/" + code + "/stats?days=500", nullptr, t)
                  .json["dailyClicks"].size(), 90u);
    EXPECT_EQ(call("GET", "/api/urls/" + code + "/stats?days=0", nullptr, t)
                  .json["dailyClicks"].size(), 1u);
}
