#include "Config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string &s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}  // namespace

namespace config {

void loadDotEnv(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;  // absent .env is fine: use the real environment

    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key.empty()) continue;

        // 0 = do not overwrite: a real env var takes precedence over .env
        ::setenv(key.c_str(), value.c_str(), 0);
    }
}

std::string get(const std::string &key, const std::string &fallback)
{
    const char *v = ::getenv(key.c_str());
    return (v && *v) ? std::string(v) : fallback;
}

int getInt(const std::string &key, int fallback)
{
    const std::string v = get(key);
    if (v.empty()) return fallback;
    try { return std::stoi(v); } catch (...) { return fallback; }
}

}  // namespace config
