#pragma once
#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>

#include <mutex>
#include <string>

// Owns the Redis client so the hostname can be re-resolved.
//
// drogon::app().createRedisClient() resolves REDIS_HOST once, at start-up, and
// stores the address forever. In Docker or Kubernetes a restarted Redis often
// comes back on a DIFFERENT IP, and the app would then talk to an address that
// no longer exists until someone restarted it.
//
// This class re-resolves periodically and swaps in a new client when the
// address changes, so recovery is automatic.
class RedisPool
{
  public:
    static RedisPool &instance();

    // Resolves and connects. Safe to call before the event loop is running.
    void start();

    // Begins periodic re-resolution. Requires a running event loop.
    void startWatchdog();

    void stop();

    // May return nullptr when Redis is disabled or unreachable.
    drogon::nosql::RedisClientPtr get() const;

    // Called when a command fails, so a bad address is re-checked promptly
    // instead of waiting for the next timer tick.
    void notifyFailure();

    std::string currentIp() const;

  private:
    RedisPool() = default;
    void reconnectIfAddressChanged();

    mutable std::mutex mutex_;
    drogon::nosql::RedisClientPtr client_;
    std::string host_;
    std::string resolvedIp_;
    unsigned short port_ = 6379;
    bool enabled_ = false;
    trantor::TimerId timerId_ = 0;
};
