#include <gtest/gtest.h>

#include "utils/Password.h"

namespace {
// Argon2id deliberately costs ~64 MiB and ~0.1s per hash, so these tests use
// few cases on purpose. Slowness here is the security property working.
class PasswordTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite() { ASSERT_TRUE(password::init()); }
};
}  // namespace

TEST_F(PasswordTest, VerifiesCorrectPassword)
{
    const auto h = password::hash("correct-horse-battery-staple");
    ASSERT_FALSE(h.empty());
    EXPECT_TRUE(password::verify("correct-horse-battery-staple", h));
}

TEST_F(PasswordTest, RejectsWrongPassword)
{
    const auto h = password::hash("correct-horse-battery-staple");
    EXPECT_FALSE(password::verify("wrong-password", h));
    EXPECT_FALSE(password::verify("", h));
    EXPECT_FALSE(password::verify("Correct-Horse-Battery-Staple", h));  // case matters
}

// The hash must never contain the password.
TEST_F(PasswordTest, HashDoesNotLeakPlaintext)
{
    const std::string pw = "supersecret-password-value";
    const auto h = password::hash(pw);
    EXPECT_EQ(h.find(pw), std::string::npos);
}

// A per-hash random salt means identical passwords produce different hashes,
// which is what defeats rainbow tables.
TEST_F(PasswordTest, SaltMakesIdenticalPasswordsHashDifferently)
{
    const auto a = password::hash("same-password-here");
    const auto b = password::hash("same-password-here");
    EXPECT_NE(a, b);
    EXPECT_TRUE(password::verify("same-password-here", a));
    EXPECT_TRUE(password::verify("same-password-here", b));
}

TEST_F(PasswordTest, UsesArgon2id)
{
    const auto h = password::hash("any-password-at-all");
    EXPECT_EQ(h.rfind("$argon2id$", 0), 0u) << "unexpected algorithm: " << h;
}

TEST_F(PasswordTest, HandlesMalformedStoredHash)
{
    EXPECT_FALSE(password::verify("anything", ""));
    EXPECT_FALSE(password::verify("anything", "not-a-hash"));
    EXPECT_FALSE(password::verify("anything", "$argon2id$broken"));
}

TEST_F(PasswordTest, HandlesUnicodeAndLongPasswords)
{
    const std::string unicode = "pässwörd-日本語-🔐";
    const auto h1 = password::hash(unicode);
    EXPECT_TRUE(password::verify(unicode, h1));

    const std::string long_pw(256, 'x');
    const auto h2 = password::hash(long_pw);
    EXPECT_TRUE(password::verify(long_pw, h2));
}
