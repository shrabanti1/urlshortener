#include "ShortCode.h"

#include <cstdint>
#include <stdexcept>

#include "Base62.h"
#include "Config.h"

namespace {

// 62^6 = 56,800,235,584 -- about 56 billion codes, all exactly 6 characters.
constexpr unsigned long long kModulus = 56800235584ULL;

// Must be coprime to kModulus. 62 = 2 x 31, so kModulus = 2^6 * 31^6, which
// means the multiplier has to be odd AND not divisible by 31.
// (31415926535 looks fine but is divisible by 31, so it has no inverse.)
constexpr unsigned long long kMultiplier = 27182818283ULL;

constexpr size_t kCodeLength = 6;

// 128-bit intermediate: id * multiplier overflows 64 bits well before the
// modulus is applied.
unsigned long long mulMod(unsigned long long a, unsigned long long b)
{
    return static_cast<unsigned long long>(
        static_cast<unsigned __int128>(a) * b % kModulus);
}

// Extended Euclid. Returns 0 if a has no inverse modulo kModulus.
unsigned long long modInverse(unsigned long long a)
{
    long long g = static_cast<long long>(kModulus);
    long long x = 0, x1 = 1;
    long long a1 = static_cast<long long>(a % kModulus);

    while (a1 != 0)
    {
        const long long q = g / a1;
        long long t = g - q * a1;
        g = a1;
        a1 = t;
        t = x - q * x1;
        x = x1;
        x1 = t;
    }
    if (g != 1) return 0;  // not coprime
    const long long m = static_cast<long long>(kModulus);
    return static_cast<unsigned long long>(((x % m) + m) % m);
}

const unsigned long long kInverse = modInverse(kMultiplier);

bool permuteEnabled()
{
    return config::get("SHORTCODE_PERMUTE", "true") == "true";
}

std::string padTo(std::string s, size_t width)
{
    if (s.size() >= width) return s;
    return std::string(width - s.size(), '0') + s;
}

}  // namespace

namespace shortcode {

std::string generate(long long id)
{
    if (id < 0) throw std::invalid_argument("shortcode::generate requires id >= 0");

    if (!permuteEnabled()) return base62::encode(id);

    // Ids beyond the modulus cannot be permuted bijectively in this space.
    // At 56 billion urls that is a good problem to have; encode them plainly.
    if (static_cast<unsigned long long>(id) >= kModulus) return base62::encode(id);

    const unsigned long long scrambled = mulMod(static_cast<unsigned long long>(id),
                                                kMultiplier);
    // Pad so every code is the same width; otherwise small permuted values
    // would produce 1- or 2-character codes and leak that they are small.
    return padTo(base62::encode(static_cast<long long>(scrambled)), kCodeLength);
}

long long toId(const std::string &code)
{
    const long long decoded = base62::decode(code);
    if (!permuteEnabled()) return decoded;

    if (static_cast<unsigned long long>(decoded) >= kModulus) return decoded;

    return static_cast<long long>(
        mulMod(static_cast<unsigned long long>(decoded), kInverse));
}

bool selfTest(std::string &problemOut)
{
    if (kInverse == 0)
    {
        problemOut = "shortcode multiplier is not invertible modulo 62^6";
        return false;
    }
    if (mulMod(kMultiplier, kInverse) != 1)
    {
        problemOut = "shortcode multiplier and inverse do not agree";
        return false;
    }
    // Spot-check the full pipeline on values spanning the range.
    for (long long id : {0LL, 1LL, 238328LL, 1000000LL, 56800235583LL})
    {
        if (toId(generate(id)) != id)
        {
            problemOut = "shortcode round-trip failed for id " + std::to_string(id);
            return false;
        }
    }
    return true;
}

}  // namespace shortcode
