#include <drogon/drogon.h>

#include "utils/Config.h"
#include "utils/Jwt.h"
#include "utils/Password.h"

int main()
{
    config::loadDotEnv();

    // LOG_LEVEL=DEBUG surfaces the cache HIT/MISS lines.
    const std::string lvl = config::get("LOG_LEVEL", "INFO");
    if (lvl == "DEBUG")      drogon::app().setLogLevel(trantor::Logger::kDebug);
    else if (lvl == "WARN")  drogon::app().setLogLevel(trantor::Logger::kWarn);
    else if (lvl == "ERROR") drogon::app().setLogLevel(trantor::Logger::kError);
    else                     drogon::app().setLogLevel(trantor::Logger::kInfo);

    // Refuse to start rather than run insecurely. A misconfigured secret is
    // worse than a crash: it looks fine and silently accepts forged tokens.
    if (!password::init())
    {
        LOG_FATAL << "libsodium failed to initialise";
        return 1;
    }
    std::string jwtProblem;
    if (!jwt_util::validateSecretAtStartup(jwtProblem))
    {
        LOG_FATAL << "Refusing to start: " << jwtProblem
                  << ". Generate one with: openssl rand -hex 32";
        return 1;
    }

    const std::string appHost = config::get("APP_HOST", "127.0.0.1");
    const int appPort = config::getInt("APP_PORT", 8080);

    // Create the connection pool. Because the framework owns this client, it
    // stays alive for as long as the event loop does.
    drogon::orm::PostgresConfig dbCfg;
    dbCfg.host             = config::get("DB_HOST", "127.0.0.1");
    dbCfg.port             = static_cast<unsigned short>(config::getInt("DB_PORT", 5432));
    dbCfg.databaseName     = config::get("DB_NAME", "urlshortener");
    dbCfg.username         = config::get("DB_USER", "urlshortener");
    dbCfg.password         = config::get("DB_PASSWORD", "");
    dbCfg.connectionNumber = static_cast<size_t>(config::getInt("DB_POOL_SIZE", 4));
    dbCfg.name             = "default";
    dbCfg.isFast           = false;
    dbCfg.characterSet     = "";
    // Seconds before a query is abandoned. Without this, requests queue
    // forever when Postgres is unreachable instead of failing fast.
    dbCfg.timeout          = static_cast<double>(config::getInt("DB_TIMEOUT_SEC", 5));
    dbCfg.autoBatch        = false;
    drogon::app().addDbClient(dbCfg);

    // Redis is optional. If it cannot be reached the app still works; every
    // lookup simply falls through to PostgreSQL.
    if (config::get("CACHE_ENABLED", "true") == "true")
    {
        drogon::app().createRedisClient(
            config::get("REDIS_HOST", "127.0.0.1"),
            static_cast<unsigned short>(config::getInt("REDIS_PORT", 6379)),
            "default",
            config::get("REDIS_PASSWORD", ""),
            static_cast<size_t>(config::getInt("REDIS_POOL_SIZE", 4)),
            /*isFast=*/false,
            // Without a timeout, commands queue forever when Redis is
            // unreachable and the HTTP request hangs instead of falling
            // back to PostgreSQL.
            /*timeout=*/static_cast<double>(config::getInt("REDIS_TIMEOUT_SEC", 1)));
        LOG_INFO << "Redis cache enabled";
    }
    else
    {
        LOG_INFO << "Redis cache disabled (CACHE_ENABLED=false)";
    }

    drogon::app().registerHandler(
        "/",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            resp->setBody("URL Shortener API");
            callback(resp);
        },
        {drogon::Get});

    // Proves the DB pool actually works, and becomes the Docker health check.
    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback)
        {
            auto db = drogon::app().getDbClient();

            db->execSqlAsync(
                "SELECT 1",
                [callback](const drogon::orm::Result &)
                {
                    Json::Value json;
                    json["status"] = "ok";
                    json["database"] = "connected";
                    callback(drogon::HttpResponse::newHttpJsonResponse(json));
                },
                [callback](const drogon::orm::DrogonDbException &e)
                {
                    LOG_ERROR << "Health check DB failure: "
                              << e.base().what();
                    Json::Value json;
                    json["status"] = "error";
                    json["database"] = "unreachable";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
                    resp->setStatusCode(drogon::k503ServiceUnavailable);
                    callback(resp);
                });
        },
        {drogon::Get});

    LOG_INFO << "URL Shortener listening on http://" << appHost << ":" << appPort;

    drogon::app()
        .addListener(appHost, appPort)
        .setThreadNum(1)
        .run();

    return 0;
}
