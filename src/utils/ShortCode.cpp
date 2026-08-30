#include "ShortCode.h"

#include "Base62.h"

namespace shortcode {

std::string generate(long long id)
{
    return base62::encode(id);
}

}  // namespace shortcode
