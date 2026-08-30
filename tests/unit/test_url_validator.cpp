#include <gtest/gtest.h>

#include <string>

#include "utils/UrlValidator.h"

namespace {
// The contract: empty string means valid.
bool valid(const std::string &u) { return urlvalidator::validate(u).empty(); }
}  // namespace

TEST(UrlValidator, AcceptsOrdinaryUrls)
{
    EXPECT_TRUE(valid("https://example.com"));
    EXPECT_TRUE(valid("http://example.com"));
    EXPECT_TRUE(valid("https://example.com/a/very/long/path"));
    EXPECT_TRUE(valid("https://example.com/path?query=1&other=2"));
    EXPECT_TRUE(valid("https://example.com:8443/path#fragment"));
    EXPECT_TRUE(valid("http://localhost:3000/test"));
    EXPECT_TRUE(valid("https://192.168.1.1/admin"));
}

TEST(UrlValidator, RejectsEmpty)
{
    EXPECT_FALSE(valid(""));
    EXPECT_EQ(urlvalidator::validate(""), "url must not be empty");
}

TEST(UrlValidator, RejectsMissingScheme)
{
    EXPECT_FALSE(valid("example.com"));
    EXPECT_FALSE(valid("www.example.com"));
    EXPECT_FALSE(valid("//example.com"));
}

// Only http/https redirect safely. javascript: and data: are XSS vectors if a
// Location header ever reaches a browser; file: could probe the local disk.
TEST(UrlValidator, RejectsDangerousSchemes)
{
    EXPECT_FALSE(valid("javascript:alert(1)"));
    EXPECT_FALSE(valid("data:text/html,<script>alert(1)</script>"));
    EXPECT_FALSE(valid("file:///etc/passwd"));
    EXPECT_FALSE(valid("ftp://example.com"));
}

TEST(UrlValidator, RejectsSchemeWithoutHost)
{
    EXPECT_FALSE(valid("https://"));
    EXPECT_FALSE(valid("http://"));
}

TEST(UrlValidator, RejectsSpaces)
{
    EXPECT_FALSE(valid("https://exa mple.com"));
    EXPECT_FALSE(valid("https://example.com/a b"));
}

TEST(UrlValidator, EnforcesLengthLimit)
{
    EXPECT_TRUE(valid("https://example.com/" + std::string(2000, 'a')));
    EXPECT_FALSE(valid("https://example.com/" + std::string(3000, 'a')));
}

// Boundary: exactly 2048 is allowed, 2049 is not.
TEST(UrlValidator, LengthBoundaryIsExact)
{
    const std::string prefix = "https://e.com/";
    const std::string at    = prefix + std::string(2048 - prefix.size(), 'a');
    const std::string over  = at + "a";
    EXPECT_EQ(at.size(), 2048u);
    EXPECT_TRUE(valid(at));
    EXPECT_FALSE(valid(over));
}

// Validation is about data quality; parameter binding is what stops injection.
// These are stored as ordinary text, so they are accepted.
TEST(UrlValidator, AcceptsSqlLikeTextBecauseBindingHandlesSafety)
{
    EXPECT_TRUE(valid("https://example.com/?q=');DROP--"));
}
