#include <gtest/gtest.h>

#include <limits>
#include <random>
#include <set>

#include "utils/Base62.h"

// --- Known values: these pin the encoding down. If someone reorders the
// --- alphabet, every previously issued short code would break; these tests
// --- turn that into a build failure instead of a production incident.
TEST(Base62, EncodesKnownValues)
{
    EXPECT_EQ(base62::encode(0), "0");
    EXPECT_EQ(base62::encode(9), "9");
    EXPECT_EQ(base62::encode(10), "A");   // digits then uppercase
    EXPECT_EQ(base62::encode(35), "Z");
    EXPECT_EQ(base62::encode(36), "a");   // then lowercase
    EXPECT_EQ(base62::encode(61), "z");
    EXPECT_EQ(base62::encode(62), "10");  // first two-digit value
    EXPECT_EQ(base62::encode(3843), "zz");
    EXPECT_EQ(base62::encode(3844), "100");
    EXPECT_EQ(base62::encode(238328), "1000");
}

TEST(Base62, DecodesKnownValues)
{
    EXPECT_EQ(base62::decode("0"), 0);
    EXPECT_EQ(base62::decode("A"), 10);
    EXPECT_EQ(base62::decode("z"), 61);
    EXPECT_EQ(base62::decode("10"), 62);
    EXPECT_EQ(base62::decode("1000"), 238328);
}

// Zero is the value the encoding loop cannot produce on its own, so it gets
// its own test rather than being buried in a range.
TEST(Base62, HandlesZeroExplicitly)
{
    EXPECT_EQ(base62::encode(0), "0");
    EXPECT_EQ(base62::decode(base62::encode(0)), 0);
}

// --- Property: decode(encode(n)) == n for every n. This is the invariant the
// --- whole short-code scheme depends on.
TEST(Base62, RoundTripsSequentialValues)
{
    for (long long i = 0; i < 100000; ++i)
        ASSERT_EQ(base62::decode(base62::encode(i)), i) << "failed at " << i;
}

TEST(Base62, RoundTripsRandomLargeValues)
{
    std::mt19937_64 rng(20260830);  // fixed seed: a failure must be reproducible
    std::uniform_int_distribution<long long> dist(
        0, std::numeric_limits<long long>::max());

    for (int i = 0; i < 50000; ++i)
    {
        const long long v = dist(rng);
        ASSERT_EQ(base62::decode(base62::encode(v)), v) << "failed at " << v;
    }
}

TEST(Base62, RoundTripsInt64Max)
{
    const long long mx = std::numeric_limits<long long>::max();
    EXPECT_EQ(base62::encode(mx), "AzL8n0Y58m7");
    EXPECT_EQ(base62::encode(mx).size(), 11u);  // sizes the VARCHAR column
    EXPECT_EQ(base62::decode(base62::encode(mx)), mx);
}

// --- Injectivity: distinct ids must never collide, since uniqueness of short
// --- codes rests entirely on this.
TEST(Base62, EncodingIsInjective)
{
    std::set<std::string> seen;
    for (long long i = 0; i < 50000; ++i)
        ASSERT_TRUE(seen.insert(base62::encode(i)).second) << "collision at " << i;
}

TEST(Base62, ProducesOnlyUrlSafeCharacters)
{
    const std::string allowed =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (long long i = 0; i < 20000; ++i)
        for (char c : base62::encode(i))
            ASSERT_NE(allowed.find(c), std::string::npos)
                << "unsafe character '" << c << "' from id " << i;
}

// --- Error handling -------------------------------------------------------
TEST(Base62, RejectsNegativeInput)
{
    EXPECT_THROW(base62::encode(-1), std::invalid_argument);
    EXPECT_THROW(base62::encode(std::numeric_limits<long long>::min()),
                 std::invalid_argument);
}

TEST(Base62, RejectsEmptyString)
{
    EXPECT_THROW(base62::decode(""), std::invalid_argument);
}

TEST(Base62, RejectsCharactersOutsideAlphabet)
{
    for (const std::string bad : {"ab-cd", "a/b", "a b", "a+b", "héllo", "a_b"})
        EXPECT_THROW(base62::decode(bad), std::invalid_argument) << "accepted: " << bad;
}

// A 12-character code exceeds int64. Signed overflow is undefined behaviour,
// so this must throw rather than wrap.
TEST(Base62, RejectsOverflow)
{
    EXPECT_THROW(base62::decode("zzzzzzzzzzzz"), std::invalid_argument);
    EXPECT_THROW(base62::decode("zzzzzzzzzzzzzzzzzzzz"), std::invalid_argument);
}
