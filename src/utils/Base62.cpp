#include "Base62.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace base62 {

const char *kAlphabet =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

namespace {

constexpr long long kBase = 62;

// Reverse lookup: character -> its value. Built once at startup so decode()
// is O(1) per character instead of scanning the alphabet each time.
// -1 marks a character that is not part of the alphabet.
const std::array<int8_t, 256> kValues = []
{
    std::array<int8_t, 256> table{};
    table.fill(-1);
    for (int8_t i = 0; i < kBase; ++i)
        table[static_cast<unsigned char>(kAlphabet[i])] = i;
    return table;
}();

}  // namespace

std::string encode(long long number)
{
    if (number < 0)
        throw std::invalid_argument("base62::encode requires a non-negative number");

    // Zero has no digits under the loop below, so it is handled explicitly.
    if (number == 0)
        return std::string(1, kAlphabet[0]);

    std::string out;
    out.reserve(11);  // 11 base-62 digits covers the whole int64 range

    // Repeated division: each remainder is one digit, produced least
    // significant first.
    while (number > 0)
    {
        out.push_back(kAlphabet[number % kBase]);
        number /= kBase;
    }

    std::reverse(out.begin(), out.end());
    return out;
}

long long decode(const std::string &code)
{
    if (code.empty())
        throw std::invalid_argument("base62::decode requires a non-empty string");

    long long result = 0;
    for (const char c : code)
    {
        const int8_t digit = kValues[static_cast<unsigned char>(c)];
        if (digit < 0)
            throw std::invalid_argument(
                std::string("base62::decode found an invalid character: '") + c + "'");

        // Horner's method: result = result * 62 + digit, with overflow checks
        // so a long code returns an error instead of silently wrapping.
        if (result > (std::numeric_limits<long long>::max() - digit) / kBase)
            throw std::invalid_argument("base62::decode overflowed int64");

        result = result * kBase + digit;
    }
    return result;
}

}  // namespace base62
