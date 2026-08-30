#include <gtest/gtest.h>

#include <cstdlib>
#include <set>

#include "utils/Base62.h"
#include "utils/ShortCode.h"

namespace {
class ShortCodePermuted : public ::testing::Test
{
  protected:
    void SetUp() override { ::setenv("SHORTCODE_PERMUTE", "true", 1); }
};
class ShortCodePlain : public ::testing::Test
{
  protected:
    void SetUp() override { ::setenv("SHORTCODE_PERMUTE", "false", 1); }
};
}  // namespace

// The multiplier must be coprime to 62^6 or it has no inverse and the whole
// scheme silently stops round-tripping.
TEST_F(ShortCodePermuted, SelfTestPasses)
{
    std::string problem;
    EXPECT_TRUE(shortcode::selfTest(problem)) << problem;
}

TEST_F(ShortCodePermuted, RoundTrips)
{
    for (long long id : {0LL, 1LL, 61LL, 62LL, 238328LL, 999999LL, 56800235583LL})
        EXPECT_EQ(shortcode::toId(shortcode::generate(id)), id) << "id " << id;
}

TEST_F(ShortCodePermuted, RoundTripsManyConsecutiveIds)
{
    for (long long id = 238328; id < 238328 + 50000; ++id)
        ASSERT_EQ(shortcode::toId(shortcode::generate(id)), id) << "id " << id;
}

// The whole point: distinct ids still give distinct codes.
TEST_F(ShortCodePermuted, IsInjective)
{
    std::set<std::string> seen;
    for (long long id = 238328; id < 238328 + 50000; ++id)
        ASSERT_TRUE(seen.insert(shortcode::generate(id)).second)
            << "collision at id " << id;
}

// Consecutive ids must NOT produce consecutive codes -- that was the leak.
TEST_F(ShortCodePermuted, ConsecutiveIdsAreNotAdjacent)
{
    const auto a = shortcode::generate(238328);
    const auto b = shortcode::generate(238329);
    const auto c = shortcode::generate(238330);

    EXPECT_NE(base62::decode(b) - base62::decode(a), 1);
    EXPECT_NE(base62::decode(c) - base62::decode(b), 1);

    // And they should not even share a long common prefix.
    EXPECT_NE(a.substr(0, 3), b.substr(0, 3));
}

TEST_F(ShortCodePermuted, CodesAreFixedWidth)
{
    for (long long id : {0LL, 1LL, 238328LL, 1000000LL, 5000000000LL})
        EXPECT_EQ(shortcode::generate(id).size(), 6u) << "id " << id;
}

TEST_F(ShortCodePermuted, UsesOnlyUrlSafeCharacters)
{
    const std::string allowed =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (long long id = 238328; id < 238328 + 5000; ++id)
        for (char c : shortcode::generate(id))
            ASSERT_NE(allowed.find(c), std::string::npos) << "bad char " << c;
}

TEST_F(ShortCodePermuted, RejectsNegativeIds)
{
    EXPECT_THROW(shortcode::generate(-1), std::invalid_argument);
}

// Backwards compatibility: turning the feature off restores plain Base62, so
// codes issued before the change still decode.
TEST_F(ShortCodePlain, FallsBackToPlainBase62)
{
    EXPECT_EQ(shortcode::generate(238328), "1000");
    EXPECT_EQ(shortcode::generate(0), "0");
    EXPECT_EQ(shortcode::toId("1000"), 238328);
}
