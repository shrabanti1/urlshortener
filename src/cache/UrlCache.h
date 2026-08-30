#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <optional>
#include <string>

#include "models/UrlRecord.h"

// Redis access for short-code lookups.
//
// Every method degrades gracefully: if Redis is missing, unreachable, or
// returns something unparseable, the callbacks report "not cached" rather
// than an error. Redis is an optimisation, never a source of truth, so a
// broken cache must never break a request.
class UrlCache
{
  public:
    // Calls onResult(std::nullopt) for a miss AND for any cache failure.
    void get(const std::string &shortCode,
             std::function<void(std::optional<UrlRecord>)> onResult) const;

    // Fire-and-forget. Failures are logged, never surfaced to the caller.
    void put(const UrlRecord &record) const;

    // Used when a URL is deleted or changed (Phase 4).
    void invalidate(const std::string &shortCode) const;

  private:
    static std::string key(const std::string &shortCode);
};
