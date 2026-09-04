#include "AliasValidator.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace {

constexpr size_t kMinLength = 3;   // shorter than a generated code would be odd
constexpr size_t kMaxLength = 16;  // matches VARCHAR(16)

// Words that must never become a short code, because something else answers
// on that path -- either nginx (docs), the app's own routes (api, health), or
// a path browsers and crawlers request by convention.
//
// If a route is ever added, it belongs here too; test_alias_validator.cpp
// asserts the important ones stay reserved.
constexpr std::array<const char *, 26> kReserved{
    "api", "docs", "health", "admin", "static", "assets", "public",
    "metrics", "status", "login", "logout", "register", "signup", "signin",
    "app", "www", "favicon", "robots", "sitemap", "well-known",
    "settings", "account", "dashboard", "about", "help", "support"};

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

namespace alias_validator {

bool isReserved(const std::string &alias)
{
    const std::string lower = toLower(alias);
    return std::any_of(kReserved.begin(), kReserved.end(),
                       [&lower](const char *r) { return lower == r; });
}

std::string validate(const std::string &alias)
{
    if (alias.size() < kMinLength)
        return "alias must be at least 3 characters";

    if (alias.size() > kMaxLength)
        return "alias must be at most 16 characters";

    for (const char c : alias)
    {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) ||
                        c == '-' || c == '_';
        if (!ok)
            return "alias may only contain letters, numbers, hyphens and underscores";
    }

    // Leading/trailing punctuation reads badly and invites lookalike codes.
    if (alias.front() == '-' || alias.front() == '_' ||
        alias.back() == '-' || alias.back() == '_')
        return "alias must start and end with a letter or number";

    if (isReserved(alias))
        return "that alias is reserved";

    return "";
}

}  // namespace alias_validator
