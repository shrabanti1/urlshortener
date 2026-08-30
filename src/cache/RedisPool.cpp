#include "RedisPool.h"

#include "utils/Config.h"
#include "utils/Net.h"

RedisPool &RedisPool::instance()
{
    static RedisPool pool;
    return pool;
}

void RedisPool::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = config::get("CACHE_ENABLED", "true") == "true";
    if (!enabled_)
    {
        LOG_INFO << "Redis cache disabled (CACHE_ENABLED=false)";
        return;
    }

    host_ = config::get("REDIS_HOST", "127.0.0.1");
    port_ = static_cast<unsigned short>(config::getInt("REDIS_PORT", 6379));
    resolvedIp_ = net::resolveToIp(host_);

    // trantor::InetAddress parses a numeric address only, which is why the
    // hostname has to be resolved before we get here.
    client_ = drogon::nosql::RedisClient::newRedisClient(
        trantor::InetAddress(resolvedIp_, port_),
        static_cast<size_t>(config::getInt("REDIS_POOL_SIZE", 4)),
        config::get("REDIS_PASSWORD", ""));

    if (client_)
        client_->setTimeout(static_cast<double>(config::getInt("REDIS_TIMEOUT_SEC", 1)));

    LOG_INFO << "Redis cache enabled at " << host_ << " (" << resolvedIp_ << ":"
             << port_ << ")";
}

void RedisPool::startWatchdog()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || timerId_ != 0) return;

    const double interval = config::getInt("REDIS_RESOLVE_INTERVAL_SEC", 30);
    timerId_ = drogon::app().getLoop()->runEvery(
        interval, [this] { reconnectIfAddressChanged(); });
}

void RedisPool::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (timerId_ != 0)
    {
        drogon::app().getLoop()->invalidateTimer(timerId_);
        timerId_ = 0;
    }
    client_.reset();
    enabled_ = false;
}

drogon::nosql::RedisClientPtr RedisPool::get() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return client_;
}

std::string RedisPool::currentIp() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return resolvedIp_;
}

void RedisPool::notifyFailure()
{
    reconnectIfAddressChanged();
}

void RedisPool::reconnectIfAddressChanged()
{
    std::string host;
    unsigned short port;
    std::string knownIp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_) return;
        host = host_;
        port = port_;
        knownIp = resolvedIp_;
    }

    // DNS lookups can block, so this runs off the event loop threads.
    const std::string freshIp = net::resolveToIp(host);
    if (freshIp == knownIp || freshIp.empty()) return;

    LOG_WARN << "Redis address changed: " << knownIp << " -> " << freshIp
             << "; reconnecting";

    auto replacement = drogon::nosql::RedisClient::newRedisClient(
        trantor::InetAddress(freshIp, port),
        static_cast<size_t>(config::getInt("REDIS_POOL_SIZE", 4)),
        config::get("REDIS_PASSWORD", ""));
    if (!replacement) return;

    replacement->setTimeout(
        static_cast<double>(config::getInt("REDIS_TIMEOUT_SEC", 1)));

    std::lock_guard<std::mutex> lock(mutex_);
    resolvedIp_ = freshIp;
    client_ = replacement;  // old client is released once in-flight calls finish
}
