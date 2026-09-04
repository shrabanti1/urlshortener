#include <gtest/gtest.h>

#include "utils/AliasValidator.h"

namespace {
bool valid(const std::string &a) { return alias_validator::validate(a).empty(); }
}

TEST(AliasValidator, AcceptsReasonableAliases)
{
    EXPECT_TRUE(valid("my-link"));
    EXPECT_TRUE(valid("my_link"));
    EXPECT_TRUE(valid("blog2026"));
    EXPECT_TRUE(valid("AbC"));
    EXPECT_TRUE(valid("a-b_c-1"));
    EXPECT_TRUE(valid("sixteencharacter"));   // exactly 16
}

TEST(AliasValidator, EnforcesLength)
{
    EXPECT_FALSE(valid(""));
    EXPECT_FALSE(valid("ab"));                      // 2, too short
    EXPECT_TRUE(valid("abc"));                      // 3, the minimum
    EXPECT_FALSE(valid("seventeencharact"  "x"));   // 17, too long
}

TEST(AliasValidator, RejectsIllegalCharacters)
{
    for (const std::string bad : {"my link", "my/link", "my.link", "my@link",
                                  "my#link", "my?link", "café", "my+link"})
        EXPECT_FALSE(valid(bad)) << "accepted: " << bad;
}

TEST(AliasValidator, RejectsLeadingOrTrailingPunctuation)
{
    EXPECT_FALSE(valid("-abc"));
    EXPECT_FALSE(valid("abc-"));
    EXPECT_FALSE(valid("_abc"));
    EXPECT_FALSE(valid("abc_"));
}

// An alias that shadows a real route would make that route unreachable, or
// silently never work. These must stay reserved as long as the routes exist.
TEST(AliasValidator, ReservesRealRoutes)
{
    for (const char *r : {"api", "docs", "health", "metrics", "admin",
                          "login", "logout", "register", "app", "static"})
        EXPECT_TRUE(alias_validator::isReserved(r)) << r << " is not reserved";
}

TEST(AliasValidator, ReservedCheckIsCaseInsensitive)
{
    EXPECT_TRUE(alias_validator::isReserved("API"));
    EXPECT_TRUE(alias_validator::isReserved("Docs"));
    EXPECT_FALSE(valid("HEALTH"));
}

TEST(AliasValidator, ReservedAliasesReportWhy)
{
    EXPECT_EQ(alias_validator::validate("api"), "that alias is reserved");
}
