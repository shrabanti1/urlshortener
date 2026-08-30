#pragma once
#include <string>

// One row of the urls table, with no database or cache types attached.
// Shared by the repository and the cache so both speak the same language.
struct UrlRecord
{
    long long id = 0;
    std::string originalUrl;
    std::string shortCode;
    // 0 means "no owner": links created before Phase 4 stay resolvable.
    long long userId = 0;
    std::string createdAt;
};
