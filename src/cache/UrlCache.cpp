#include "UrlCache.h"

#include <atomic>
#include <chrono>

#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "RedisPool.h"
#include "utils/Config.h"

namespace {

// ---------------------------------------------------------------------------
// Circuit breaker.
//
// A timeout alone is not enough: with Redis down, EVERY request still pays the
// full timeout before falling back. After a few consecutive failures we stop
// calling Redis entirely for a short cool-off, so requests go straight to
// PostgreSQL at full speed. One probe after the cool-off closes the circuit
// again when Redis recovers.
// ---------------------------------------------------------------------------
std::atomic<int> gConsecutiveFailures{0};
std::atomic<long long> gSkipUntilMs{0};

long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
}

int failureThreshold() { return config::getInt("REDIS_BREAKER_FAILURES", 3); }
int cooloffMs()        { return config::getInt("REDIS_BREAKER_COOLOFF_SEC", 10) * 1000; }

// True while the breaker is open, i.e. we are deliberately skipping Redis.
bool circuitOpen()
{
    return nowMs() < gSkipUntilMs.load(std::memory_order_relaxed);
}

void recordFailure()
{
    if (gConsecutiveFailures.fetch_add(1, std::memory_order_relaxed) + 1 >=
        failureThreshold())
    {
        gSkipUntilMs.store(nowMs() + cooloffMs(), std::memory_order_relaxed);
        LOG_WARN << "redis circuit OPEN, bypassing cache for "
                 << cooloffMs() / 1000 << "s";
    }
}

void recordSuccess()
{
    if (gConsecutiveFailures.exchange(0, std::memory_order_relaxed) != 0)
        LOG_INFO << "redis circuit CLOSED, cache back in use";
    gSkipUntilMs.store(0, std::memory_order_relaxed);
}


// Namespacing keys keeps this data distinguishable from anything else that
// might share the Redis instance later (sessions, rate limits, ...).
constexpr const char *kKeyPrefix = "url:v2:";

int ttlSeconds()
{
    return config::getInt("REDIS_TTL_SECONDS", 3600);
}

// Postgres renders TIMESTAMPTZ as "YYYY-MM-DD HH:MM:SS.ffffff+TZ".
// Returns 0 if it cannot be parsed, which makes the caller fall back to the
// normal TTL rather than caching something forever.
int secondsUntil(const std::string &timestamptz)
{
    std::tm tm{};
    if (::strptime(timestamptz.c_str(), "%Y-%m-%d %H:%M:%S", &tm) == nullptr)
        return 0;

    // The string carries its own offset; parse it so the comparison is not
    // silently done in local time.
    int offsetHours = 0, offsetMinutes = 0;
    const auto plus = timestamptz.find_last_of("+-");
    if (plus != std::string::npos && plus > 10)
    {
        const std::string off = timestamptz.substr(plus);
        offsetHours = std::atoi(off.substr(1, 2).c_str());
        if (off.size() >= 5) offsetMinutes = std::atoi(off.substr(4, 2).c_str());
        if (off[0] == '-') { offsetHours = -offsetHours; offsetMinutes = -offsetMinutes; }
    }

    tm.tm_isdst = 0;
    const std::time_t asUtc = ::timegm(&tm);
    const std::time_t expiry =
        asUtc - (offsetHours * 3600 + offsetMinutes * 60);
    const std::time_t now = std::time(nullptr);
    const long long remaining = static_cast<long long>(expiry - now);
    if (remaining <= 0) return 0;
    return static_cast<int>(std::min<long long>(remaining, 2147483647LL));
}

}  // namespace

std::string UrlCache::key(const std::string &shortCode)
{
    return std::string(kKeyPrefix) + shortCode;
}

void UrlCache::get(const std::string &shortCode,
                   std::function<void(std::optional<UrlRecord>)> onResult) const
{
    auto redis = RedisPool::instance().get();

    if (!redis || circuitOpen())
    {
        onResult(std::nullopt);  // disabled or breaker open -> treat as a miss
        return;
    }

    const std::string k = key(shortCode);

    redis->execCommandAsync(
        [onResult, shortCode](const drogon::nosql::RedisResult &r)
        {
            recordSuccess();

            if (r.isNil())
            {
                onResult(std::nullopt);  // genuine cache miss
                return;
            }

            Json::Value json;
            Json::Reader reader;
            if (!reader.parse(r.asString(), json))
            {
                LOG_WARN << "cache entry for " << shortCode << " is not valid JSON";
                onResult(std::nullopt);
                return;
            }

            UrlRecord rec;
            rec.id          = json.get("id", 0).asInt64();
            rec.originalUrl = json.get("originalUrl", "").asString();
            rec.userId      = json.get("userId", 0).asInt64();
            rec.expiresAt   = json.get("expiresAt", "").asString();
            rec.isCustom    = json.get("isCustom", false).asBool();
            rec.shortCode   = shortCode;
            // A cached entry is only written for a live link, and its TTL is
            // capped at the link's own lifetime, so anything still in the
            // cache has not expired yet.
            rec.expired     = false;

            if (rec.originalUrl.empty())
            {
                onResult(std::nullopt);
                return;
            }
            onResult(rec);
        },
        [onResult, shortCode](const drogon::nosql::RedisException &e)
        {
            // Redis is down. Log it and fall through to PostgreSQL.
            recordFailure();
            // A changed container IP looks exactly like a dead Redis, so give
            // the pool a chance to notice and reconnect.
            RedisPool::instance().notifyFailure();
            LOG_WARN << "redis GET failed for " << shortCode << ": " << e.what();
            onResult(std::nullopt);
        },
        "GET %s", k.c_str());
}

void UrlCache::put(const UrlRecord &record) const
{
    auto redis = RedisPool::instance().get();
    if (!redis || circuitOpen()) return;

    Json::Value json;
    json["id"]          = static_cast<Json::Int64>(record.id);
    json["originalUrl"] = record.originalUrl;
    // Consumers do ownership checks on this, so it MUST be cached; a partial
    // record made cache hits behave differently from cache misses.
    json["userId"]      = static_cast<Json::Int64>(record.userId);
    json["expiresAt"]   = record.expiresAt;
    json["isCustom"]    = record.isCustom;

    Json::FastWriter writer;
    std::string payload = writer.write(json);
    if (!payload.empty() && payload.back() == '\n') payload.pop_back();

    const std::string k = key(record.shortCode);

    // Never cache a link for longer than it has left to live, or Redis would
    // keep redirecting after the link expired.
    int ttl = ttlSeconds();
    if (!record.expiresAt.empty())
    {
        const int remaining = secondsUntil(record.expiresAt);
        if (remaining <= 0) return;            // already expired: do not cache
        ttl = std::min(ttl, remaining);
    }

    // SETEX = SET with an expiry, atomically. Using SET then EXPIRE would
    // risk leaving a key with no TTL if the second command failed.
    redis->execCommandAsync(
        [](const drogon::nosql::RedisResult &) {},
        [code = record.shortCode](const drogon::nosql::RedisException &e)
        {
            LOG_WARN << "redis SETEX failed for " << code << ": " << e.what();
        },
        "SETEX %s %d %s", k.c_str(), ttl, payload.c_str());
}

void UrlCache::invalidate(const std::string &shortCode) const
{
    auto redis = RedisPool::instance().get();
    if (!redis) return;

    const std::string k = key(shortCode);
    redis->execCommandAsync(
        [](const drogon::nosql::RedisResult &) {},
        [shortCode](const drogon::nosql::RedisException &e)
        {
            LOG_WARN << "redis DEL failed for " << shortCode << ": " << e.what();
        },
        "DEL %s", k.c_str());
}
