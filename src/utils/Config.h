#pragma once
#include <string>

namespace config {

// Reads a KEY=VALUE file and injects any keys that are not already present
// in the real environment. Real environment variables always win, which is
// what lets Docker/production override .env in Phase 7.
void loadDotEnv(const std::string &path = ".env");

std::string get(const std::string &key, const std::string &fallback = "");
int getInt(const std::string &key, int fallback);

}  // namespace config
