include(FetchContent)

# Pinned: an unpinned dependency makes builds non-reproducible.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# --- Unit tests: pure functions, no database, no network, no filesystem ----
add_executable(unit_tests
    unit/test_base62.cpp
    unit/test_url_validator.cpp
    unit/test_jwt.cpp
    unit/test_password.cpp
    unit/test_ip_hash.cpp
    unit/test_shortcode.cpp
    unit/test_refresh_token.cpp
)
target_link_libraries(unit_tests PRIVATE app_objects GTest::gtest_main)
gtest_discover_tests(unit_tests PROPERTIES LABELS "unit")

# --- Integration tests: require a real PostgreSQL and Redis ---------------
add_executable(integration_tests
    integration/main.cpp
    integration/test_repositories.cpp
    integration/test_api_flow.cpp
    integration/test_auth_refresh.cpp
    integration/test_click_batcher.cpp
    integration/TestEnv.cpp
)
target_link_libraries(integration_tests PRIVATE app_objects GTest::gtest)
gtest_discover_tests(integration_tests PROPERTIES LABELS "integration")
