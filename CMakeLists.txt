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
find_package(OpenSSL REQUIRED)          # jwt-cpp signs with OpenSSL

# libsodium has no CMake config, so locate it by hand.
find_path(SODIUM_INCLUDE_DIR sodium.h HINTS /opt/homebrew/include)
find_library(SODIUM_LIBRARY NAMES sodium HINTS /opt/homebrew/lib)
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium not found. Run: brew install libsodium")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(url_shortener ${SOURCES})

target_include_directories(url_shortener PRIVATE
    src
    third_party/jwt-cpp/include     # header-only, vendored
    ${SODIUM_INCLUDE_DIR}
)

target_link_libraries(url_shortener PRIVATE
    Drogon::Drogon
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_LIBRARY}
)
