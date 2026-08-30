#include <gtest/gtest.h>

#include <cstdlib>
#include <set>

#include "utils/IpHash.h"

namespace {
class IpHashTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ::setenv("IP_HASH_SECRET", "unit-test-ip-secret-0123456789abcdef", 1);
    }
};
}  // namespace

// Counting unique visitors requires the same IP to hash to the same value.
TEST_F(IpHashTest, IsDeterministic)
{
    EXPECT_EQ(iphash::anonymize("192.168.1.1"), iphash::anonymize("192.168.1.1"));
}

TEST_F(IpHashTest, DifferentIpsGiveDifferentHashes)
{
    std::set<std::string> seen;
    for (int i = 1; i < 255; ++i)
        EXPECT_TRUE(seen.insert(iphash::anonymize("10.0.0." + std::to_string(i))).second);
}

TEST_F(IpHashTest, OutputDoesNotContainTheAddress)
{
    const auto h = iphash::anonymize("192.168.1.1");
    EXPECT_EQ(h.find("192"), std::string::npos);
    EXPECT_EQ(h.find('.'), std::string::npos);
}

TEST_F(IpHashTest, OutputIsFixedLengthHex)
{
    for (const char *ip : {"1.2.3.4", "255.255.255.255", "::1",
                           "2001:0db8:85a3:0000:0000:8a2e:0370:7334"})
    {
        const auto h = iphash::anonymize(ip);
        EXPECT_EQ(h.size(), 16u);
        EXPECT_EQ(h.find_first_not_of("0123456789abcdef"), std::string::npos);
    }
}

// The secret is what makes the hash irreversible: without it an attacker could
// hash all 2^32 IPv4 addresses. A different key must give a different result.
TEST_F(IpHashTest, SecretChangesTheHash)
{
    const auto withA = iphash::anonymize("192.168.1.1");
    ::setenv("IP_HASH_SECRET", "a-completely-different-secret-value-xyz", 1);
    EXPECT_NE(iphash::anonymize("192.168.1.1"), withA);
}

TEST_F(IpHashTest, EmptyInputGivesEmptyOutput)
{
    EXPECT_EQ(iphash::anonymize(""), "");
}

TEST(IpHashSecretValidation, RejectsMissingOrShortSecret)
{
    std::string problem;
    ::unsetenv("IP_HASH_SECRET");
    EXPECT_FALSE(iphash::validateSecretAtStartup(problem));

    ::setenv("IP_HASH_SECRET", "short", 1);
    EXPECT_FALSE(iphash::validateSecretAtStartup(problem));

    ::setenv("IP_HASH_SECRET", "a-perfectly-fine-secret-value", 1);
    EXPECT_TRUE(iphash::validateSecretAtStartup(problem)) << problem;
}
