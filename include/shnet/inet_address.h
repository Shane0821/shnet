#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cassert>
#include <cstring>
#include <string>

class InetAddress {
   public:
    /// Constructs an endpoint with given port number.
    /// Mostly used in TcpServer listening.
    explicit InetAddress(uint16_t port = 0, bool loopbackOnly = false) {
        memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        in_addr_t ip = loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY;
        addr_.sin_addr.s_addr = htonl(ip);
        addr_.sin_port = htons(port);
    }

    /// Constructs an endpoint with given struct @c sockaddr_in
    /// Mostly used when accepting new connections
    explicit InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}

    sa_family_t family() const { return addr_.sin_family; }

    std::string toIpPort() const {
        static char buf[IP_LEN_MAX] = "";
        ::inet_ntop(AF_INET, &addr_.sin_addr, buf, IP_LEN_MAX);
        size_t end = ::strlen(buf);
        uint16_t port = ntohs(addr_.sin_port);
        assert(end < IP_LEN_MAX);
        snprintf(buf + end, IP_LEN_MAX - end, ":%u", port);
        return buf;
    }

    std::string toIp() const {
        static char buf[IP_LEN_MAX] = "";
        ::inet_ntop(AF_INET, &addr_.sin_addr, buf, IP_LEN_MAX);
        return buf;
    }

    uint16_t port() const { return ntohs(portNetEndian()); }

    uint32_t ipv4NetEndian() const {
        assert(family() == AF_INET);
        return addr_.sin_addr.s_addr;
    }
    uint16_t portNetEndian() const { return addr_.sin_port; }

    static constexpr size_t IP_LEN_MAX = 64;

   private:
    struct sockaddr_in addr_;
};