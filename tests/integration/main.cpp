#include <gtest/gtest.h>

#include "TestEnv.h"

// gtest_main is not used here: the Drogon event loop runs on a background
// thread and must be stopped before the process exits, otherwise the runtime
// calls std::terminate on a still-running thread.
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    testenv::startOnce();
    const int rc = RUN_ALL_TESTS();
    testenv::stop();
    return rc;
}
