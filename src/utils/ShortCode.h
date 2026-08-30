#pragma once
#include <string>

namespace shortcode {

// TEMPORARY (Phase 1): the decimal form of the database id.
// Phase 2 replaces the body of this function with Base62 encoding.
// The seam stays identical: database id -> short code.
std::string generate(long long id);

}  // namespace shortcode
