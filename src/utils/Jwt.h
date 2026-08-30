#pragma once
#include <optional>
#include <string>

namespace jwt_util {

struct Claims
{
    long long userId = 0;
    std::string email;
};

// Signs an access token valid for JWT_EXPIRY_MINUTES.
std::string issueAccessToken(long long userId, const std::string &email);

// Verifies signature, algorithm, issuer and expiry. Returns nullopt if the
// token is invalid for ANY reason -- callers must not distinguish why.
std::optional<Claims> verifyAccessToken(const std::string &token);

// Fails fast at startup if JWT_SECRET is missing or too weak.
bool validateSecretAtStartup(std::string &problemOut);

}  // namespace jwt_util
