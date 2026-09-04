#include <drogon/drogon.h>

#include <cstdio>

#include "utils/Config.h"
#include "utils/Jwt.h"
#include "utils/IpHash.h"
#include "utils/Net.h"
#include "cache/RedisPool.h"
#include "cache/UrlCache.h"
#include "repositories/UrlRepository.h"
#include "services/ClickBatcher.h"
#include "middleware/RateLimit.h"
#include "middleware/SecurityHeaders.h"
#include "utils/Migrations.h"
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
    // PaaS platforms (Render, Railway, Heroku) inject the port to listen on as
    // $PORT and route to it; it takes precedence over our own setting.
    const int appPort = config::getInt("PORT", config::getInt("APP_PORT", 8080));

    // STANDALONE=true means there is no nginx in front, so the app has to do
    // the jobs nginx normally does: serve the UI, add security headers and
    // rate limit. Behind Compose this stays false and nginx keeps doing them.
    const bool standalone = config::get("STANDALONE", "false") == "true";

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

    // A managed database has no init-script hook, so apply the schema here.
    // Every statement is idempotent, so this is safe on every boot.
    if (config::get("RUN_MIGRATIONS", "false") == "true")
    {
        // libpq conninfo: single-quote every value so passwords containing
        // spaces or symbols cannot break the string.
        auto quoted = [](const std::string &v)
        {
            std::string out = "'";
            for (char ch : v)
            {
                if (ch == '\'' || ch == '\\') out += '\\';
                out += ch;
            }
            return out + "'";
        };
        const std::string connInfo =
            "host=" + quoted(dbCfg.host) +
            " port=" + std::to_string(dbCfg.port) +
            " dbname=" + quoted(dbCfg.databaseName) +
            " user=" + quoted(dbCfg.username) +
            " password=" + quoted(dbCfg.password) +
            " connect_timeout=10";

        std::string migrationProblem;
        if (!migrations::run(config::get("MIGRATIONS_DIR", "db"), connInfo,
                             migrationProblem))
        {
            LOG_FATAL << "Refusing to start: " << migrationProblem;
            return 1;
        }
        LOG_INFO << "migrations complete";
    }

    if (standalone)
    {
        // Serve web/ as static files: the single-page UI and the API spec.
        drogon::app().setDocumentRoot(config::get("STATIC_ROOT", "web"));
        drogon::app().setStaticFilesCacheTime(0);
        // Drogon serves only an allow-list of extensions, and yaml/json are
        // not on it by default -- without this the OpenAPI spec 404s.
        drogon::app().setFileTypes(
            {"html", "css", "js", "yaml", "yml", "json", "txt",
             "svg", "png", "jpg", "jpeg", "gif", "ico", "webp", "woff2"});

        // Applied to every response, including static files and 404s.
        drogon::app().registerPreSendingAdvice(&security_headers::apply);

        // Runs before routing; a non-null return short-circuits with a 429.
        drogon::app().registerSyncAdvice(&rate_limit::check);

        LOG_INFO << "standalone mode: serving static files, headers and rate limits";
    }


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

            // Purge links that expired a while ago. Expired links already
            // return 410 on their own; this only stops the table growing
            // forever, so an hourly sweep is plenty.
            if (config::get("EXPIRY_CLEANUP_ENABLED", "true") == "true")
            {
                const double every =
                    config::getInt("EXPIRY_CLEANUP_INTERVAL_SEC", 3600);
                drogon::app().getLoop()->runEvery(
                    every,
                    []
                    {
                        static const UrlRepository repo;
                        static const UrlCache cache;
                        repo.deleteExpired(
                            config::getInt("EXPIRY_GRACE_DAYS", 7),
                            [](std::vector<std::string> codes)
                            {
                                if (codes.empty()) return;
                                static const UrlCache c;
                                for (const auto &code : codes) c.invalidate(code);
                                LOG_INFO << "expiry cleanup removed " << codes.size()
                                         << " links";
                            },
                            [](const std::string &err)
                            { LOG_WARN << "expiry cleanup failed: " << err; });
                    });
            }
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
        .setThreadNum(config::getInt("APP_THREADS", 1))
        .run();

    return 0;
}
