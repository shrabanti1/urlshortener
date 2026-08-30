cmake_minimum_required(VERSION 3.16)
project(url_shortener CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# We built Drogon from source into ~/.local (the Homebrew bottle has no
# PostgreSQL backend). Tell CMake to look there, and at Homebrew's keg-only
# PostgreSQL, before it searches the system defaults.
list(APPEND CMAKE_PREFIX_PATH
    "$ENV{HOME}/.local"
    "/opt/homebrew/opt/postgresql@17"
    "/opt/homebrew"
)

find_package(Drogon CONFIG REQUIRED)

add_executable(url_shortener
    src/main.cpp
)

target_link_libraries(url_shortener PRIVATE Drogon::Drogon)
