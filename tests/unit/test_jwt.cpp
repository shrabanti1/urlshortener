#include <gtest/gtest.h>

#include <cstdlib>
#include <thread>

#include "utils/Jwt.h"

namespace {

// Unit tests must not depend on a .env file, so the secret is set in-process.
class JwtTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ::setenv("JWT_SECRET",
                 "test-secret-that-is-long-enough-for-validation-0123456789", 1);
        ::setenv("JWT_EXPIRY_MINUTES", "60", 1);
    }
};

}  // namespace

TEST_F(JwtTest, RoundTripsClaims)
{
    const auto token = jwt_util::issueAccessToken(42, "alice@example.com");
    ASSERT_FALSE(token.empty());

    const auto claims = jwt_util::verifyAccessToken(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->userId, 42);
    EXPECT_EQ(claims->email, "alice@example.com");
}

TEST_F(JwtTest, ProducesThreeSegments)
{
    const auto token = jwt_util::issueAccessToken(1, "a@b.com");
    EXPECT_EQ(std::count(token.begin(), token.end(), '.'), 2);
}

TEST_F(JwtTest, RejectsGarbage)
{
    EXPECT_FALSE(jwt_util::verifyAccessToken("").has_value());
    EXPECT_FALSE(jwt_util::verifyAccessToken("not-a-jwt").has_value());
    EXPECT_FALSE(jwt_util::verifyAccessToken("a.b.c").has_value());
    EXPECT_FALSE(jwt_util::verifyAccessToken("....").has_value());
}

// Flipping any byte of the payload must invalidate the signature.
TEST_F(JwtTest, RejectsTamperedPayload)
{
    auto token = jwt_util::issueAccessToken(42, "alice@example.com");
    const auto firstDot = token.find('.');
    ASSERT_NE(firstDot, std::string::npos);

    // Mutate one character inside the payload segment.
    token[firstDot + 5] = (token[firstDot + 5] == 'A') ? 'B' : 'A';
    EXPECT_FALSE(jwt_util::verifyAccessToken(token).has_value());
}

TEST_F(JwtTest, RejectsTamperedSignature)
{
    auto token = jwt_util::issueAccessToken(42, "alice@example.com");
    token.back() = (token.back() == 'A') ? 'B' : 'A';
    EXPECT_FALSE(jwt_util::verifyAccessToken(token).has_value());
}

// A token signed with a different secret must not verify -- this is the whole
// security property of the scheme.
TEST_F(JwtTest, RejectsTokenSignedWithAnotherSecret)
{
    ::setenv("JWT_SECRET", "attacker-secret-also-long-enough-0123456789abcd", 1);
    const auto forged = jwt_util::issueAccessToken(99, "mallory@example.com");

    ::setenv("JWT_SECRET",
             "test-secret-that-is-long-enough-for-validation-0123456789", 1);
    EXPECT_FALSE(jwt_util::verifyAccessToken(forged).has_value());
}

// "alg: none" is the classic JWT bypass: an unsigned token must be refused.
TEST_F(JwtTest, RejectsAlgNoneToken)
{
    // {"alg":"none","typ":"JWT"} . {"sub":"1"} . <empty signature>
    const std::string algNone =
        "eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0.eyJzdWIiOiIxIn0.";
    EXPECT_FALSE(jwt_util::verifyAccessToken(algNone).has_value());
}

TEST_F(JwtTest, RejectsExpiredToken)
{
    ::setenv("JWT_EXPIRY_MINUTES", "0", 1);  // expires immediately
    const auto token = jwt_util::issueAccessToken(7, "bob@example.com");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_FALSE(jwt_util::verifyAccessToken(token).has_value());
}

// --- Startup secret validation -------------------------------------------
TEST(JwtSecretValidation, RejectsMissingSecret)
{
    ::unsetenv("JWT_SECRET");
    std::string problem;
    EXPECT_FALSE(jwt_util::validateSecretAtStartup(problem));
    EXPECT_FALSE(problem.empty());
}

TEST(JwtSecretValidation, RejectsShortSecret)
{
    ::setenv("JWT_SECRET", "tooshort", 1);
    std::string problem;
    EXPECT_FALSE(jwt_util::validateSecretAtStartup(problem));
}

TEST(JwtSecretValidation, RejectsPaddedPlaceholders)
{
    std::string problem;
    for (const char *bad : {"changeme0000000000000000000000000000",
                            "SECRET00000000000000000000000000000",
                            "password000000000000000000000000000"})
    {
        ::setenv("JWT_SECRET", bad, 1);
        EXPECT_FALSE(jwt_util::validateSecretAtStartup(problem)) << "accepted: " << bad;
    }
}

TEST(JwtSecretValidation, RejectsSingleRepeatedCharacter)
{
    ::setenv("JWT_SECRET", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1);
    std::string problem;
    EXPECT_FALSE(jwt_util::validateSecretAtStartup(problem));
}

TEST(JwtSecretValidation, AcceptsRealSecret)
{
    ::setenv("JWT_SECRET",
             "9f3a1c8e7b2d4f60a5e1c9d3b7f28a4e6c0d5b9f1a3e7c2d8b4f6a0e5c1d9b3f", 1);
    std::string problem;
    EXPECT_TRUE(jwt_util::validateSecretAtStartup(problem)) << problem;
}
