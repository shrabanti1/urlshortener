#pragma once
#include <drogon/drogon.h>

#include <functional>
#include <optional>
#include <string>

struct RefreshTokenRecord
{
    long long id = 0;
    long long userId = 0;
    bool revoked = false;
    bool expired = false;
};

class RefreshTokenRepository
{
  public:
    using ErrorCb = std::function<void(const std::string &)>;

    void store(long long userId,
               const std::string &tokenHash,
               int lifetimeDays,
               std::function<void(long long)> onSuccess,
               ErrorCb onError) const;

    // nullopt means the token was never issued.
    void findByHash(const std::string &tokenHash,
                    std::function<void(std::optional<RefreshTokenRecord>)> onSuccess,
                    ErrorCb onError) const;

    // Marks one token used and records which token replaced it, so reuse of an
    // already-rotated token is detectable.
    void revoke(long long id,
                long long replacedBy,
                std::function<void()> onSuccess,
                ErrorCb onError) const;

    // Used on logout-everywhere, and when a stolen token is detected.
    void revokeAllForUser(long long userId,
                          std::function<void(long long)> onSuccess,
                          ErrorCb onError) const;

    void deleteExpired(std::function<void(long long)> onSuccess,
                       ErrorCb onError) const;
};
