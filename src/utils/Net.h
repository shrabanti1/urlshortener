#pragma once
#include <string>

namespace net {

// Resolves a hostname to an IPv4 address string.
//
// Drogon's createRedisClient() passes its host argument straight into
// trantor::InetAddress, which parses it as a numeric address -- a hostname
// silently becomes 0.0.0.0 and every connection is refused. libpq is
// unaffected because it resolves names itself, which is why PostgreSQL worked
// in Docker while Redis did not.
//
// Returns the input unchanged if it is already an IP or cannot be resolved.
std::string resolveToIp(const std::string &host);

}  // namespace net
