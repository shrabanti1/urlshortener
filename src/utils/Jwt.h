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

namespace refresh_token {

// A refresh token is opaque random bytes, not a JWT: it carries no claims and
// is only ever compared against a stored hash.
struct Issued
{
    std::string token;      // give to the client, never store
    std::string tokenHash;  // store this
};

Issued mint();

// SHA-256 hex. Fast on purpose: unlike a password this is 256 bits of
// randomness, so there is nothing to brute-force and no need for Argon2's cost.
std::string hash(const std::string &token);

int lifetimeDays();

}  // namespace refresh_token
