#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <optional>
#include <string>

// A plain data object: one row of the urls table, with no database types
// attached. Layers above this never touch drogon::orm::Result.
struct UrlRecord
{
    long long id = 0;
    std::string originalUrl;
    std::string shortCode;
};

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

    // std::nullopt means "no such code", which is not an error.
    void findByShortCode(const std::string &shortCode,
                         std::function<void(std::optional<UrlRecord>)> onSuccess,
                         ErrorCb onError) const;
};
