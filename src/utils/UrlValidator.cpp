#include "UrlValidator.h"

#include <algorithm>

namespace {
constexpr size_t kMaxUrlLength = 2048;
}

namespace urlvalidator {

std::string validate(const std::string &url)
{
    if (url.empty())
        return "url must not be empty";

    if (url.size() > kMaxUrlLength)
        return "url must be at most 2048 characters";

    const bool http  = url.rfind("http://", 0) == 0;
    const bool https = url.rfind("https://", 0) == 0;
    if (!http && !https)
        return "url must start with http:// or https://";

    // Reject "https://" with nothing after the scheme.
    const auto schemeEnd = url.find("://") + 3;
    if (url.size() <= schemeEnd)
        return "url must contain a host";

    // A raw space is never valid in a URL and usually signals a typo.
    if (url.find(' ') != std::string::npos)
        return "url must not contain spaces";

    return "";
}

}  // namespace urlvalidator
