#include <gtest/gtest.h>

#include <set>

#include "utils/Jwt.h"

// Tokens must be unpredictable: they are bearer credentials with no signature.
TEST(RefreshToken, MintsUniqueTokens)
{
    std::set<std::string> seen;
    for (int i = 0; i < 2000; ++i)
    {
        const auto issued = refresh_token::mint();
        EXPECT_EQ(issued.token.size(), 64u);  // 32 bytes as hex
        EXPECT_TRUE(seen.insert(issued.token).second) << "duplicate token";
    }
}

TEST(RefreshToken, TokenIsHexOnly)
{
    const auto issued = refresh_token::mint();
    EXPECT_EQ(issued.token.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST(RefreshToken, HashIsDeterministicAndDiffersFromToken)
{
    const auto issued = refresh_token::mint();
    EXPECT_EQ(refresh_token::hash(issued.token), issued.tokenHash);
    EXPECT_NE(issued.tokenHash, issued.token);
    EXPECT_EQ(issued.tokenHash.size(), 64u);  // sha256 hex
}

// The stored hash must not reveal the token, exactly like a password hash.
TEST(RefreshToken, DifferentTokensHashDifferently)
{
    std::set<std::string> hashes;
    for (int i = 0; i < 1000; ++i)
        EXPECT_TRUE(hashes.insert(refresh_token::mint().tokenHash).second);
}

TEST(RefreshToken, KnownSha256Vector)
{
    // sha256("abc")
    EXPECT_EQ(refresh_token::hash("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
