#pragma once
#include <drogon/drogon.h>

#include <string>

// Boots the real Drogon app against a dedicated TEST database, on a background
// thread, once for the whole integration suite.
//
// A separate database is essential: tests truncate tables between cases, and
// pointing that at a development database would destroy real data.
namespace testenv {

void startOnce();
void stop();

// Removes all rows so each test starts from a known state.
void resetDatabase();
void flushCache();

int port();
std::string baseUrl();

}  // namespace testenv
