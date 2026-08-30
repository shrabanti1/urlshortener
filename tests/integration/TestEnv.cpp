#include "TestEnv.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#include "utils/Password.h"

namespace {

constexpr int kPort = 18080;
std::thread gAppThread;
std::atomic<bool> gStarted{false};

void setTestEnvironment()
{
    ::setenv("DB_HOST", "127.0.0.1", 1);
    ::setenv("DB_PORT", "5432", 1);
    ::setenv("DB_NAME", "urlshortener_test", 1);
    ::setenv("DB_USER", "urlshortener", 1);
    ::setenv("DB_PASSWORD", "devpassword", 1);
    ::setenv("REDIS_HOST", "127.0.0.1", 1);
    ::setenv("REDIS_PORT", "6379", 1);
    ::setenv("JWT_SECRET",
             "integration-test-secret-long-enough-0123456789abcdef", 1);
    ::setenv("IP_HASH_SECRET", "integration-test-ip-secret-0123456789", 1);
    ::setenv("BASE_URL", "http://127.0.0.1:18080", 1);
    ::setenv("ANALYTICS_MODE", "sync", 1);  // deterministic: no race in assertions
}

}  // namespace

namespace testenv {

int port() { return kPort; }
std::string baseUrl() { return "http://127.0.0.1:" + std::to_string(kPort); }

void startOnce()
{
    if (gStarted.exchange(true)) return;

    setTestEnvironment();
    password::init();

    gAppThread = std::thread(
        []
        {
            drogon::orm::PostgresConfig db;
            db.host             = "127.0.0.1";
            db.port             = 5432;
            db.databaseName     = "urlshortener_test";
            db.username         = "urlshortener";
            db.password         = "devpassword";
            db.connectionNumber = 2;
            db.name             = "default";
            db.isFast           = false;
            db.timeout          = 5.0;
            db.autoBatch        = false;
            drogon::app().addDbClient(db);

            drogon::app().createRedisClient("127.0.0.1", 6379, "default", "", 2,
                                            false, 1.0);

            drogon::app().setLogLevel(trantor::Logger::kWarn);
            drogon::app().addListener("127.0.0.1", kPort).setThreadNum(2).run();
        });

    // Wait for the event loop to accept connections.
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void stop()
{
    if (!gStarted) return;
    drogon::app().getLoop()->queueInLoop([] { drogon::app().quit(); });
    if (gAppThread.joinable()) gAppThread.join();
}

void resetDatabase()
{
    auto db = drogon::app().getDbClient();
    db->execSqlSync("TRUNCATE click_events, urls, users RESTART IDENTITY CASCADE");
    db->execSqlSync("ALTER SEQUENCE urls_id_seq RESTART WITH 238328");
}

void flushCache()
{
    try
    {
        auto redis = drogon::app().getRedisClient();
        if (redis) redis->execCommandSync<int>(
            [](const drogon::nosql::RedisResult &) { return 0; }, "FLUSHDB");
    }
    catch (...)
    {
        // A missing cache must not fail the suite.
    }
}

}  // namespace testenv
