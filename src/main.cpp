#include <drogon/drogon.h>

#include <cstdio>

#include "utils/Config.h"
#include "utils/Jwt.h"
#include "utils/IpHash.h"
#include "utils/Net.h"
#include "cache/RedisPool.h"
#include "services/ClickBatcher.h"
#include "utils/ShortCode.h"
#include "utils/Password.h"

int main()
{
    // Line-buffer stdout.
    //
    // When stdout is a pipe -- which it always is under `docker compose up -d`,
    // systemd, or any log collector -- libc switches from line buffering to
    // FULL buffering. A few hundred bytes of start-up logs then sit in the 4 KB
    // buffer and never reach `docker logs` until enough further output fills
    // it. On a healthy, quiet service that can be hours.
    //
    // This must happen before anything writes to stdout.
    ::setvbuf(stdout, nullptr, _IOLBF, 0);

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

    std::string ipProblem;
    if (!iphash::validateSecretAtStartup(ipProblem))
    {
        LOG_FATAL << "Refusing to start: " << ipProblem
                  << ". Generate one with: openssl rand -hex 32";
        return 1;
    }

    std::string codeProblem;
    if (!shortcode::selfTest(codeProblem))
    {
        LOG_FATAL << "Refusing to start: " << codeProblem;
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
    RedisPool::instance().start();


    // Routes live in src/controllers/*. Drogon discovers them automatically,
    // which also makes them reachable from the test binary.

    LOG_INFO << "URL Shortener listening on http://" << appHost << ":" << appPort;

    // Timers need a running loop, so this is queued rather than called now.
    drogon::app().getLoop()->queueInLoop(
        []
        {
            if (config::get("ANALYTICS_BATCH", "true") == "true" &&
                config::get("ANALYTICS_MODE", "async") != "sync")
                ClickBatcher::instance().start();
            RedisPool::instance().startWatchdog();
        });

    // Flush buffered clicks instead of dropping them on shutdown. Drogon's
    // default handlers just call quit(), which would discard the buffer.
    auto gracefulShutdown = []
    {
        LOG_INFO << "shutting down: flushing buffered click events";
        ClickBatcher::instance().stop();
        RedisPool::instance().stop();
        drogon::app().quit();
    };
    drogon::app().setTermSignalHandler(gracefulShutdown);
    drogon::app().setIntSignalHandler(gracefulShutdown);

    drogon::app()
        .addListener(appHost, appPort)
        .setThreadNum(1)
        .run();

    return 0;
}
