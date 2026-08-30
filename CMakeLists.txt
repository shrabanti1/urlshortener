cmake_minimum_required(VERSION 3.16)
project(url_shortener CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_PREFIX_PATH
    "$ENV{HOME}/.local"
    "/opt/homebrew/opt/postgresql@17"
    "/opt/homebrew/opt/openssl@3"
    "/opt/homebrew"
)

find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(SODIUM_INCLUDE_DIR sodium.h HINTS /opt/homebrew/include)
find_library(SODIUM_LIBRARY NAMES sodium HINTS /opt/homebrew/lib)
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium not found. Run: brew install libsodium")
endif()

# ---------------------------------------------------------------------------
# All application code lives in an OBJECT library so both the server binary
# and the test binaries can use it.
#
# OBJECT (not STATIC) matters: Drogon registers controllers and filters through
# static initialisers. A linker discards unreferenced objects from a static
# archive, which would silently drop every controller. OBJECT libraries link
# every object file unconditionally.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(app_objects OBJECT ${LIB_SOURCES})
target_include_directories(app_objects PUBLIC
    src
    third_party/jwt-cpp/include
    ${SODIUM_INCLUDE_DIR}
)
target_link_libraries(app_objects PUBLIC
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_LIBRARY}
)

add_executable(url_shortener src/main.cpp $<TARGET_OBJECTS:app_objects>)
target_link_libraries(url_shortener PRIVATE app_objects)

option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
