#include "Net.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <drogon/drogon.h>

namespace net {

std::string resolveToIp(const std::string &host)
{
    if (host.empty()) return host;

    // Already a numeric IPv4 address: nothing to do.
    struct in_addr probe {};
    if (::inet_pton(AF_INET, host.c_str(), &probe) == 1) return host;

    struct addrinfo hints {};
    hints.ai_family = AF_INET;      // Drogon's InetAddress here expects IPv4
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || result == nullptr)
    {
        LOG_ERROR << "could not resolve host '" << host
                  << "': " << ::gai_strerror(rc);
        return host;
    }

    char buf[INET_ADDRSTRLEN] = {};
    auto *addr = reinterpret_cast<struct sockaddr_in *>(result->ai_addr);
    ::inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
    ::freeaddrinfo(result);

    const std::string ip(buf);
    if (ip != host) LOG_INFO << "resolved " << host << " -> " << ip;
    return ip.empty() ? host : ip;
}

}  // namespace net
