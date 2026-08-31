#include "RateLimit.h"

#include <drogon/RateLimiter.h>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "utils/Config.h"
#include "utils/Http.h"

namespace {

enum class Bucket { Auth, Api, Redirect };

struct Entry
{
    drogon::RateLimiterPtr limiter;
    std::chrono::steady_clock::time_point lastSeen;
};

std::mutex gMutex;
std::unordered_map<std::string, Entry> gBuckets;
std::chrono::steady_clock::time_point gLastSweep = std::chrono::steady_clock::now();

bool enabled() { return config::get("RATE_LIMIT_ENABLED", "false") == "true"; }

size_t capacityFor(Bucket b)
{
    switch (b)
    {
        case Bucket::Auth:     return static_cast<size_t>(config::getInt("RATE_LIMIT_AUTH_PER_MIN", 20));
        case Bucket::Api:      return static_cast<size_t>(config::getInt("RATE_LIMIT_API_PER_MIN", 300));
        default:               return static_cast<size_t>(config::getInt("RATE_LIMIT_REDIRECT_PER_MIN", 1200));
    }
}

Bucket bucketFor(const std::string &path)
{
    if (path.rfind("/api/auth/", 0) == 0) return Bucket::Auth;
    if (path.rfind("/api/", 0) == 0)      return Bucket::Api;
    return Bucket::Redirect;
}

const char *bucketName(Bucket b)
{
    switch (b)
    {
        case Bucket::Auth: return "auth";
        case Bucket::Api:  return "api";
        default:           return "redirect";
    }
}

// The socket peer is the platform's proxy, so the real client comes from
// X-Forwarded-For. Only trusted when TRUST_PROXY_HEADERS says a proxy we
// control is in front, otherwise anyone could forge it and evade the limit.
std::string clientKey(const drogon::HttpRequestPtr &req)
{
    if (config::get("TRUST_PROXY_HEADERS", "false") == "true")
    {
        const std::string xff = req->getHeader("x-forwarded-for");
        if (!xff.empty())
        {
            const auto comma = xff.find(',');
            std::string first = comma == std::string::npos ? xff : xff.substr(0, comma);
            // trim
            const auto b = first.find_first_not_of(" \t");
            const auto e = first.find_last_not_of(" \t");
            if (b != std::string::npos) return first.substr(b, e - b + 1);
        }
    }
    return req->getPeerAddr().toIp();
}

// Without this the map grows once per unique IP, forever.
void sweepIfDue(std::chrono::steady_clock::time_point now)
{
    if (now - gLastSweep < std::chrono::minutes(5)) return;
    gLastSweep = now;
    for (auto it = gBuckets.begin(); it != gBuckets.end();)
        it = (now - it->second.lastSeen > std::chrono::minutes(10)) ? gBuckets.erase(it)
                                                                   : std::next(it);
}

}  // namespace

namespace rate_limit {

drogon::HttpResponsePtr check(const drogon::HttpRequestPtr &req)
{
    if (!enabled()) return nullptr;

    const Bucket bucket = bucketFor(req->path());
    const std::string key = std::string(bucketName(bucket)) + "|" + clientKey(req);

    bool allowed = true;
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(gMutex);
        sweepIfDue(now);

        auto it = gBuckets.find(key);
        if (it == gBuckets.end())
        {
            auto limiter = drogon::RateLimiter::newRateLimiter(
                drogon::RateLimiterType::kTokenBucket, capacityFor(bucket),
                std::chrono::seconds(60));
            it = gBuckets.emplace(key, Entry{limiter, now}).first;
        }
        it->second.lastSeen = now;
        allowed = it->second.limiter->isAllowed();
    }

    if (allowed) return nullptr;

    auto resp = http_util::jsonError(drogon::k429TooManyRequests,
                                     "too many requests, please slow down");
    resp->addHeader("Retry-After", "60");
    return resp;
}

}  // namespace rate_limit
