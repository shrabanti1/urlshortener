#pragma once
#include <string>

namespace shortcode {

// Turns a database id into a short code.
//
// Plain Base62 of a sequential id leaks information: consecutive codes are
// adjacent, so anyone can enumerate every URL in the system and estimate how
// many are created per day.
//
// Before encoding, the id is multiplied by a fixed constant modulo 62^6. That
// is a BIJECTION (the multiplier is coprime to the modulus), so it scatters
// consecutive ids across the code space while still guaranteeing that distinct
// ids produce distinct codes. No collision checks, no retry loop.
//
// Set SHORTCODE_PERMUTE=false to fall back to plain Base62. Existing rows keep
// working either way, because short_code is stored in the database rather than
// recomputed.
std::string generate(long long id);

// Inverse of generate(). Only meaningful for codes produced with the same
// SHORTCODE_PERMUTE setting; the database remains the authority.
long long toId(const std::string &code);

// Verifies that the multiplier really is invertible and that encode/decode
// round-trip. Called at start-up so a bad constant fails loudly.
bool selfTest(std::string &problemOut);

}  // namespace shortcode
