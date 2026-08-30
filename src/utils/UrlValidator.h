#pragma once
#include <string>

namespace urlvalidator {

// Phase 1 rules: non-empty, within length limits, and http/https scheme.
// Returns an empty string when valid, otherwise the reason it is not.
std::string validate(const std::string &url);

}  // namespace urlvalidator
