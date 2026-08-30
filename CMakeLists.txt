cmake_minimum_required(VERSION 3.16)
project(url_shortener CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_PREFIX_PATH
    "$ENV{HOME}/.local"
    "/opt/homebrew/opt/postgresql@17"
    "/opt/homebrew"
)

find_package(Drogon CONFIG REQUIRED)

# CONFIGURE_DEPENDS re-runs the glob when files are added, so you don't have
# to remember to edit this file every time you create a .cpp
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(url_shortener ${SOURCES})
target_include_directories(url_shortener PRIVATE src)
target_link_libraries(url_shortener PRIVATE Drogon::Drogon)
