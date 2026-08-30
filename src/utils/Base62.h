#pragma once
#include <cstdint>
#include <string>

namespace base62 {

// The alphabet's ORDER defines the encoding. Changing it invalidates every
// short code ever issued, so treat this string as permanent.
extern const char *kAlphabet;  // "0-9A-Za-z", 62 characters

// Converts a non-negative integer to its base-62 representation.
// encode(0) == "0". Throws std::invalid_argument on negative input.
std::string encode(long long number);

// Inverse of encode(). Throws std::invalid_argument on an empty string,
// a character outside the alphabet, or a value too large for int64.
long long decode(const std::string &code);

}  // namespace base62
