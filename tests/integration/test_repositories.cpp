#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "TestEnv.h"
#include "cache/UrlCache.h"
#include "repositories/UrlRepository.h"
#include "repositories/UserRepository.h"
#include "utils/Password.h"

namespace {

// The repositories are callback-based. A promise/future turns each async call
// back into something a test can assert on synchronously.
//
// CRITICAL: every path must complete the promise, and every wait must have a
// timeout. A callback that silently never fires would otherwise turn a test
// failure into a hang, which is far harder to diagnose than a red test.
constexpr auto kWaitTimeout = std::chrono::seconds(10);

template <typename T>
T await(std::function<void(std::function<void(T)>, std::function<void(const std::string &)>)> call)
{
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();
    auto done = std::make_shared<std::atomic<bool>>(false);

    call([promise, done](T value)
         {
             if (!done->exchange(true)) promise->set_value(std::move(value));
         },
         [promise, done](const std::string &err)
         {
             if (!done->exchange(true))
                 promise->set_exception(
                     std::make_exception_ptr(std::runtime_error(err)));
         });

    if (future.wait_for(kWaitTimeout) != std::future_status::ready)
        throw std::runtime_error("timed out waiting for repository callback");
    return future.get();
}

// Same guarantees for calls whose success carries no value.
inline void awaitVoid(
    std::function<void(std::function<void()>, std::function<void(const std::string &)>)> call)
{
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    auto done = std::make_shared<std::atomic<bool>>(false);

    call([promise, done]
         {
             if (!done->exchange(true)) promise->set_value();
         },
         [promise, done](const std::string &err)
         {
             if (!done->exchange(true))
                 promise->set_exception(
                     std::make_exception_ptr(std::runtime_error(err)));
         });

    if (future.wait_for(kWaitTimeout) != std::future_status::ready)
        throw std::runtime_error("timed out waiting for repository callback");
    future.get();
}

// Returns the error message, or "" when the call succeeded.
inline std::string awaitExpectingFailure(
    std::function<void(std::function<void()>, std::function<void(const std::string &)>)> call)
{
    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();
    auto done = std::make_shared<std::atomic<bool>>(false);

    call([promise, done]
         {
             if (!done->exchange(true)) promise->set_value("");
         },
         [promise, done](const std::string &err)
         {
             if (!done->exchange(true)) promise->set_value(err);
         });

    if (future.wait_for(kWaitTimeout) != std::future_status::ready)
        return "timed out";
    return future.get();
}

class RepositoryTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite() { testenv::startOnce(); }
    void SetUp() override
    {
        testenv::resetDatabase();
        testenv::flushCache();
    }
    UrlRepository urls;
    UserRepository users;
};

}  // namespace

TEST_F(RepositoryTest, NextIdReturnsIncreasingValues)
{
    const auto a = await<long long>([&](auto ok, auto err) { urls.nextId(ok, err); });
    const auto b = await<long long>([&](auto ok, auto err) { urls.nextId(ok, err); });
    EXPECT_GT(b, a);
    EXPECT_EQ(a, 238328);  // sequence starts at 62^3
}

TEST_F(RepositoryTest, InsertThenFindRoundTrips)
{
    const bool stored = await<bool>(
        [&](auto ok, auto err)
        { urls.insert(238328, "https://example.com/one", "1000", 0, false, 0, ok, err); });
    ASSERT_TRUE(stored);

    const auto found = await<std::optional<UrlRecord>>(
        [&](auto ok, auto err) { urls.findByShortCode("1000", ok, err); });

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, 238328);
    EXPECT_EQ(found->originalUrl, "https://example.com/one");
    EXPECT_EQ(found->shortCode, "1000");
}

// "Not found" is a normal answer, not an error.
TEST_F(RepositoryTest, FindMissingCodeReturnsNulloptNotError)
{
    const auto found = await<std::optional<UrlRecord>>(
        [&](auto ok, auto err) { urls.findByShortCode("nosuch", ok, err); });
    EXPECT_FALSE(found.has_value());
}

// This is the bug that shipped in Phase 5: a cache hit dropped userId, so the
// owner was denied access to their own stats. A unit test could not catch it,
// because it only appears when Redis is warm.
TEST_F(RepositoryTest, CacheHitReturnsSameRecordAsCacheMiss)
{
    // Needs a real user row: user_id has a foreign key to users(id).
    const auto owner = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("owner@example.com", password::hash("pw-for-test"), ok, err); });
    ASSERT_TRUE(owner.has_value());

    ASSERT_TRUE(await<bool>(
        [&](auto ok, auto err)
        { urls.insert(238328, "https://example.com/owned", "1000", owner->id,
                      false, 0, ok, err); }));

    // First read: cache miss, straight from PostgreSQL.
    const auto fromDb = await<std::optional<UrlRecord>>(
        [&](auto ok, auto err) { urls.findByShortCode("1000", ok, err); });
    ASSERT_TRUE(fromDb.has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // let SETEX land

    // Second read: served from Redis.
    const auto fromCache = await<std::optional<UrlRecord>>(
        [&](auto ok, auto err) { urls.findByShortCode("1000", ok, err); });
    ASSERT_TRUE(fromCache.has_value());

    EXPECT_EQ(fromCache->id, fromDb->id);
    EXPECT_EQ(fromCache->originalUrl, fromDb->originalUrl);
    EXPECT_EQ(fromCache->shortCode, fromDb->shortCode);
    EXPECT_EQ(fromCache->userId, fromDb->userId) << "cache dropped userId";
    EXPECT_EQ(fromCache->userId, owner->id);
}

TEST_F(RepositoryTest, UniqueConstraintRejectsDuplicateShortCode)
{
    ASSERT_TRUE(await<bool>(
        [&](auto ok, auto err)
        { urls.insert(238328, "https://example.com/a", "1000", 0, false, 0, ok, err); }));

    // ON CONFLICT DO NOTHING turns a taken code into a normal "false" answer
    // rather than an exception, so callers can decide what it means.
    const bool second = await<bool>(
        [&](auto ok, auto err)
        { urls.insert(238329, "https://example.com/b", "1000", 0, false, 0, ok, err); });
    EXPECT_FALSE(second) << "duplicate short_code was accepted";
}

TEST_F(RepositoryTest, DeleteOnlyAffectsTheOwnersRow)
{
    const auto alice = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("alice@example.com", password::hash("pw-for-test"), ok, err); });
    const auto bob = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("bob@example.com", password::hash("pw-for-test"), ok, err); });
    ASSERT_TRUE(alice.has_value());
    ASSERT_TRUE(bob.has_value());

    ASSERT_TRUE(await<bool>(
        [&](auto ok, auto err)
        { urls.insert(238328, "https://example.com/alice", "1000", alice->id,
                      false, 0, ok, err); }));

    const bool byStranger = await<bool>(
        [&](auto ok, auto err) { urls.deleteOwned("1000", bob->id, ok, err); });
    EXPECT_FALSE(byStranger) << "another user deleted the row";

    const bool byOwner = await<bool>(
        [&](auto ok, auto err) { urls.deleteOwned("1000", alice->id, ok, err); });
    EXPECT_TRUE(byOwner);

    const bool again = await<bool>(
        [&](auto ok, auto err) { urls.deleteOwned("1000", alice->id, ok, err); });
    EXPECT_FALSE(again) << "deleting twice should report nothing removed";
}

TEST_F(RepositoryTest, ListByUserReturnsOnlyThatUsersRowsNewestFirst)
{
    const auto u1 = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("one@example.com", password::hash("pw-for-test"), ok, err); });
    const auto u2 = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("two@example.com", password::hash("pw-for-test"), ok, err); });
    ASSERT_TRUE(u1.has_value());
    ASSERT_TRUE(u2.has_value());

    auto add = [&](long long id, const char *code, long long owner)
    {
        ASSERT_TRUE(await<bool>(
            [&](auto ok, auto err)
            { urls.insert(id, std::string("https://example.com/") + code, code,
                          owner, false, 0, ok, err); }));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // distinct created_at
    };
    add(238328, "1000", u1->id);
    add(238329, "1001", u1->id);
    add(238330, "1002", u2->id);

    const auto mine = await<std::vector<UrlRecord>>(
        [&](auto ok, auto err) { urls.listByUser(u1->id, 50, 0, ok, err); });
    ASSERT_EQ(mine.size(), 2u);
    EXPECT_EQ(mine[0].shortCode, "1001") << "expected newest first";
    EXPECT_EQ(mine[1].shortCode, "1000");

    const auto theirs = await<std::vector<UrlRecord>>(
        [&](auto ok, auto err) { urls.listByUser(u2->id, 50, 0, ok, err); });
    ASSERT_EQ(theirs.size(), 1u);
    EXPECT_EQ(theirs[0].shortCode, "1002");
}

TEST_F(RepositoryTest, ListByUserPaginates)
{
    const auto u = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("pager@example.com", password::hash("pw-for-test"), ok, err); });
    ASSERT_TRUE(u.has_value());

    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(await<bool>(
            [&](auto ok, auto err)
            { urls.insert(238328 + i, "https://example.com/x",
                          std::to_string(1000 + i), u->id, false, 0, ok, err); }));

    const auto page1 = await<std::vector<UrlRecord>>(
        [&](auto ok, auto err) { urls.listByUser(u->id, 2, 0, ok, err); });
    const auto page2 = await<std::vector<UrlRecord>>(
        [&](auto ok, auto err) { urls.listByUser(u->id, 2, 2, ok, err); });
    EXPECT_EQ(page1.size(), 2u);
    EXPECT_EQ(page2.size(), 2u);
    EXPECT_NE(page1[0].shortCode, page2[0].shortCode);
}

// --- Users ---------------------------------------------------------------
TEST_F(RepositoryTest, CreateUserThenFindByEmail)
{
    const auto created = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("alice@example.com", password::hash("pw-for-test"), ok, err); });
    ASSERT_TRUE(created.has_value());
    EXPECT_GT(created->id, 0);

    const auto found = await<std::optional<User>>(
        [&](auto ok, auto err) { users.findByEmail("alice@example.com", ok, err); });
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, created->id);
}

// Registering ALICE@ when alice@ exists must not create a second account.
TEST_F(RepositoryTest, EmailUniquenessIsCaseInsensitive)
{
    const auto first = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("alice@example.com", password::hash("pw-one-here"), ok, err); });
    ASSERT_TRUE(first.has_value());

    const auto second = await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("ALICE@example.com", password::hash("pw-two-here"), ok, err); });
    EXPECT_FALSE(second.has_value()) << "duplicate account created";
}

TEST_F(RepositoryTest, FindByEmailIsCaseInsensitive)
{
    await<std::optional<User>>(
        [&](auto ok, auto err)
        { users.create("Alice@Example.com", password::hash("pw-for-test"), ok, err); });

    const auto found = await<std::optional<User>>(
        [&](auto ok, auto err) { users.findByEmail("alice@example.COM", ok, err); });
    EXPECT_TRUE(found.has_value());
}
