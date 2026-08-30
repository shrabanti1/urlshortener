#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

#include "TestEnv.h"
#include "repositories/UserRepository.h"
#include "services/ClickBatcher.h"
#include "utils/Password.h"

namespace {

long long clickCount()
{
    auto db = drogon::app().getDbClient();
    const auto r = db->execSqlSync("SELECT count(*) AS n FROM click_events");
    return r[0]["n"].as<long long>();
}

long long makeUrl(long long userId, const char *code, long long id)
{
    auto db = drogon::app().getDbClient();
    db->execSqlSync(
        "INSERT INTO urls (id, original_url, short_code, user_id) "
        "VALUES ($1, $2, $3, NULLIF($4::bigint, 0))",
        id, std::string("https://example.com/") + code, code, userId);
    return id;
}

long long makeUser(const char *email)
{
    auto db = drogon::app().getDbClient();
    const auto r = db->execSqlSync(
        "INSERT INTO users (email, password_hash) VALUES ($1, $2) RETURNING id",
        email, password::hash("pw-for-test"));
    return r[0]["id"].as<long long>();
}

class ClickBatcherTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite() { testenv::startOnce(); }
    void SetUp() override
    {
        testenv::resetDatabase();
        ::setenv("ANALYTICS_BATCH_SIZE", "10", 1);
        ::setenv("ANALYTICS_FLUSH_MS", "300", 1);
    }
    void TearDown() override { ClickBatcher::instance().flushNow(); }
};

}  // namespace

// Events below the threshold stay buffered: that is the whole point.
TEST_F(ClickBatcherTest, BuffersUntilTheBatchIsFull)
{
    const auto user = makeUser("batch1@example.com");
    const auto urlId = makeUrl(user, "btch01", 500001);

    ClickEvent ev;
    ev.urlId = urlId;
    ev.referrer = "https://example.com";
    ev.userAgent = "test-agent";
    ev.ipHash = "abcdef0123456789";

    for (int i = 0; i < 5; ++i) ClickBatcher::instance().add(ev);

    EXPECT_EQ(ClickBatcher::instance().bufferedCount(), 5u);
    EXPECT_EQ(clickCount(), 0) << "events were written before the batch filled";
}

// Reaching the threshold triggers exactly one multi-row INSERT.
TEST_F(ClickBatcherTest, FlushesWhenTheBatchIsFull)
{
    const auto user = makeUser("batch2@example.com");
    const auto urlId = makeUrl(user, "btch02", 500002);

    ClickEvent ev;
    ev.urlId = urlId;
    ev.ipHash = "abcdef0123456789";

    for (int i = 0; i < 10; ++i) ClickBatcher::instance().add(ev);

    EXPECT_EQ(ClickBatcher::instance().bufferedCount(), 0u);
    for (int i = 0; i < 50 && clickCount() < 10; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(clickCount(), 10);
}

TEST_F(ClickBatcherTest, FlushNowWritesAPartialBatch)
{
    const auto user = makeUser("batch3@example.com");
    const auto urlId = makeUrl(user, "btch03", 500003);

    ClickEvent ev;
    ev.urlId = urlId;
    ev.ipHash = "abcdef0123456789";
    for (int i = 0; i < 3; ++i) ClickBatcher::instance().add(ev);

    ClickBatcher::instance().flushNow();
    for (int i = 0; i < 50 && clickCount() < 3; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(clickCount(), 3);
}

// Every field must survive the array round trip, not just url_id.
TEST_F(ClickBatcherTest, PreservesAllFields)
{
    const auto user = makeUser("batch4@example.com");
    const auto urlId = makeUrl(user, "btch04", 500004);

    ClickEvent ev;
    ev.urlId = urlId;
    ev.referrer = "https://news.ycombinator.com/item?id=1";
    ev.userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)";
    ev.ipHash = "0123456789abcdef";
    ClickBatcher::instance().add(ev);
    ClickBatcher::instance().flushNow();

    for (int i = 0; i < 50 && clickCount() < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto db = drogon::app().getDbClient();
    const auto r = db->execSqlSync(
        "SELECT referrer, user_agent, ip_hash FROM click_events WHERE url_id = $1",
        urlId);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0]["referrer"].as<std::string>(), ev.referrer);
    EXPECT_EQ(r[0]["user_agent"].as<std::string>(), ev.userAgent);
    EXPECT_EQ(r[0]["ip_hash"].as<std::string>(), ev.ipHash);
}

// Quotes and backslashes must not break the Postgres array literal.
TEST_F(ClickBatcherTest, EscapesQuotesAndBackslashes)
{
    const auto user = makeUser("batch5@example.com");
    const auto urlId = makeUrl(user, "btch05", 500005);

    ClickEvent ev;
    ev.urlId = urlId;
    ev.referrer = R"(https://x.com/?q="quoted"&p=back\slash)";
    ev.userAgent = R"(Agent "with" {braces} , comma)";
    ev.ipHash = "0123456789abcdef";
    ClickBatcher::instance().add(ev);
    ClickBatcher::instance().flushNow();

    for (int i = 0; i < 50 && clickCount() < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto db = drogon::app().getDbClient();
    const auto r = db->execSqlSync(
        "SELECT referrer, user_agent FROM click_events WHERE url_id = $1", urlId);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0]["referrer"].as<std::string>(), ev.referrer);
    EXPECT_EQ(r[0]["user_agent"].as<std::string>(), ev.userAgent);
}

TEST_F(ClickBatcherTest, HandlesEmptyStrings)
{
    const auto user = makeUser("batch6@example.com");
    const auto urlId = makeUrl(user, "btch06", 500006);

    ClickEvent ev;
    ev.urlId = urlId;   // no referrer, no user agent (direct hit)
    ClickBatcher::instance().add(ev);
    ClickBatcher::instance().flushNow();

    for (int i = 0; i < 50 && clickCount() < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(clickCount(), 1);
}

// add() is called from event-loop threads, so it must be race-free.
TEST_F(ClickBatcherTest, IsThreadSafe)
{
    const auto user = makeUser("batch7@example.com");
    const auto urlId = makeUrl(user, "btch07", 500007);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 50;

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
        workers.emplace_back(
            [urlId]
            {
                ClickEvent ev;
                ev.urlId = urlId;
                ev.ipHash = "0123456789abcdef";
                for (int i = 0; i < kPerThread; ++i) ClickBatcher::instance().add(ev);
            });
    for (auto &w : workers) w.join();

    ClickBatcher::instance().flushNow();
    const long long expected = kThreads * kPerThread;
    for (int i = 0; i < 100 && clickCount() < expected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(clickCount(), expected) << "events were lost under concurrency";
}
