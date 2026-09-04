#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <optional>
#include <vector>
#include <string>

#include "models/UrlRecord.h"

// All SQL for the urls table lives here and nowhere else.
class UrlRepository
{
  public:
    using ErrorCb = std::function<void(const std::string &)>;

    // Atomically reserves the next id from the sequence.
    void nextId(std::function<void(long long)> onSuccess, ErrorCb onError) const;

    // onSuccess(false) means the short code was already taken. The caller
    // decides what that means: a custom alias is a 409, whereas a generated
    // code just retries with the next id.
    void insert(long long id,
                const std::string &originalUrl,
                const std::string &shortCode,
                long long userId,
                bool isCustom,
                int expiresInDays,
                std::function<void(bool)> onSuccess,
                ErrorCb onError) const;

    // Daily click counts for the last N days, including days with zero
    // clicks -- a chart that silently skips empty days is misleading.
    void dailyClicks(long long urlId,
                     int days,
                     std::function<void(std::vector<std::pair<std::string, long long>>)> onSuccess,
                     ErrorCb onError) const;

    // Removes links that expired more than a grace period ago.
    void deleteExpired(int graceDays,
                       std::function<void(std::vector<std::string>)> onSuccess,
                       ErrorCb onError) const;

    // Newest first. Served by the (user_id, created_at DESC) index.
    void listByUser(long long userId,
                    long long limit,
                    long long offset,
                    std::function<void(std::vector<UrlRecord>)> onSuccess,
                    ErrorCb onError) const;

    // Deletes only if the row belongs to userId. onSuccess(false) means
    // "not found or not yours" -- the caller must not distinguish the two.
    void deleteOwned(const std::string &shortCode,
                     long long userId,
                     std::function<void(bool)> onSuccess,
                     ErrorCb onError) const;

    // Cache-aside: checks Redis first, falls back to PostgreSQL on a miss,
    // and populates the cache with what it finds.
    // std::nullopt means "no such code", which is not an error.
    void findByShortCode(const std::string &shortCode,
                         std::function<void(std::optional<UrlRecord>)> onSuccess,
                         ErrorCb onError) const;
};
