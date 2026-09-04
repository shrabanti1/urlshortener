#pragma once
#include <string>

namespace alias_validator {

// Rules for a user-chosen short code.
// Returns an empty string when valid, otherwise the reason it is not.
std::string validate(const std::string &alias);

// True if the alias would shadow a real route. Exposed for tests, so the
// reserved list cannot silently drift away from the routing table.
bool isReserved(const std::string &alias);

}  // namespace alias_validator
