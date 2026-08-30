#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <optional>
#include <string>

#include "models/UrlRecord.h"

// All SQL for the urls table lives here and nowhere else.
class UrlRepository
{
  public:
    using ErrorCb = std::function<void(const std::string &)>;

    // Atomically reserves the next id from the sequence.
    void nextId(std::function<void(long long)> onSuccess, ErrorCb onError) const;

    void insert(long long id,
                const std::string &originalUrl,
                const std::string &shortCode,
                std::function<void()> onSuccess,
                ErrorCb onError) const;

    // Cache-aside: checks Redis first, falls back to PostgreSQL on a miss,
    // and populates the cache with what it finds.
    // std::nullopt means "no such code", which is not an error.
    void findByShortCode(const std::string &shortCode,
                         std::function<void(std::optional<UrlRecord>)> onSuccess,
                         ErrorCb onError) const;
};
