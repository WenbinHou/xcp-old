#include "infra.h"


//==============================================================================
// union tcp_sockaddr
//==============================================================================

void infra::tcp_sockaddr::set_port(const uint16_t port_in_host_endian) noexcept
{
    if (family() == AF_INET) {
        addr_ipv4.sin_port = htons(port_in_host_endian);
    }
    else if (family() == AF_INET6) {
        addr_ipv6.sin6_port = htons(port_in_host_endian);
    }
    else {
        LOG_ERROR("BUG: Unknown addr.ss_family: {}", addr.ss_family);
        std::abort();
    }
}

uint16_t infra::tcp_sockaddr::port() const noexcept
{
    if (family() == AF_INET) {
        return ntohs(addr_ipv4.sin_port);
    }
    else if (family() == AF_INET6) {
        return ntohs(addr_ipv6.sin6_port);
    }
    else {
        LOG_ERROR("BUG: Unknown addr.ss_family: {}", addr.ss_family);
        std::abort();
    }
}

socklen_t infra::tcp_sockaddr::socklen() const noexcept
{
    if (family() == AF_INET) {
        return sizeof(addr_ipv4);
    }
    else if (family() == AF_INET6) {
        return sizeof(addr_ipv6);
    }
    else {
        LOG_ERROR("BUG: Unknown addr.ss_family: {}", addr.ss_family);
        std::abort();
    }
}

std::string infra::tcp_sockaddr::to_string() const
{
    char buffer[64];
    std::string result;

    if (family() == AF_INET) {
        if (inet_ntop(AF_INET, (void*)&addr_ipv4.sin_addr, buffer, sizeof(buffer)) == nullptr) {
            LOG_ERROR("inet_ntop(AF_INET) unexpecedly failed");
            return "FAILED_TO_STRING";
        }
        result = buffer;
        result += ":";
        result += std::to_string(ntohs(addr_ipv4.sin_port));
    }
    else if (family() == AF_INET6) {
        if (inet_ntop(AF_INET6, (void*)&addr_ipv6.sin6_addr, buffer, sizeof(buffer)) == nullptr) {
            LOG_ERROR("inet_ntop(AF_INET6) unexpecedly failed");
            return "FAILED_TO_STRING";
        }
        result = "[";
        result += buffer;
        result += "]:";
        result += std::to_string(ntohs(addr_ipv6.sin6_port));
    }
    else {
        LOG_ERROR("BUG: Unknown addr.ss_family: {}", addr.ss_family);
        std::abort();
    }

    return result;
}



//==============================================================================
// struct basic_tcp_endpoint
//==============================================================================

template<bool _WithRepeats>
bool infra::basic_tcp_endpoint<_WithRepeats>::parse(const std::string& value)
{
    // Valid format:
    //   <host>
    //   <host>:<port>
    //   <host>:*
    //   <host>@<repeats>           (if allow_repeats is true)
    //   <host>:<port>@<repeats>    (if allow_repeats is true)
    //   <host>:*@<repeats>         (if allow_repeats is true)

    // See:
    //  https://stackoverflow.com/questions/53497/regular-expression-that-matches-valid-ipv6-addresses
    //  https://stackoverflow.com/questions/106179/regular-expression-to-match-dns-hostname-or-ip-address
    // NOTE: re_valid_hostname matches a superset of valid IPv4

    static std::regex* __re = nullptr;
    if (__re == nullptr) {
        const std::string re_valid_hostname = R"((?:(?:[a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*(?:[A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9]))";
        const std::string re_valid_ipv6 = R"(\[(?:(?:[0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,7}:|(?:[0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,5}(?::[0-9a-fA-F]{1,4}){1,2}|(?:[0-9a-fA-F]{1,4}:){1,4}(?::[0-9a-fA-F]{1,4}){1,3}|(?:[0-9a-fA-F]{1,4}:){1,3}(?::[0-9a-fA-F]{1,4}){1,4}|(?:[0-9a-fA-F]{1,4}:){1,2}(?::[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:(?:(?::[0-9a-fA-F]{1,4}){1,6})|:(?:(?::[0-9a-fA-F]{1,4}){1,7}|:)|::(?:[Ff]{4}(?::0{1,4})?:)?(?:(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])\.){3,3}(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])|(?:[0-9a-fA-F]{1,4}:){1,4}:(?:(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])\.){3,3}(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9]))\])";
        const std::string re_valid_host = "(?:(?:" + re_valid_hostname + ")|(?:" + re_valid_ipv6 + "))";
        const std::string re_valid_endpoint = "^(" + re_valid_host + ")(?::([0-9]+|\\*))?(?:@([1-9][0-9]*))?$";
        __re = new std::regex(re_valid_endpoint, std::regex::optimize);
    }

    std::smatch match;
    if (!std::regex_match(value, match, *__re)) {
        return false;
    }

    try {
        {
            host = match[1].str();

            // NOTE: If host is IPv6 surrounded by '[' ']', DO NOT trim the beginning '[' and ending ']'
            // resolve() should take care of this
        }

        {
            std::string port_str = match[2].str();
            if (port_str.empty()) {
                port = { };
            }
            else if (port_str == "*") {
                port = static_cast<uint16_t>(0);
            }
            else {
                const int port_int = std::stoi(port_str);
                if (port_int < 0 || port_int >= 65536) {
                    //LOG_ERROR("Invalid port {} from {}", port_str, value);
                    return false;
                }
                else {
                    port = (uint16_t)port_int;
                }
            }
        }

        {
            const std::string repeats_str = match[3].str();
            if (repeats_str.empty()) {
                repeats = { };
            }
            else {
                if constexpr(!_WithRepeats) {
                    //LOG_ERROR("Didn't expect @{} from {}", repeats_str, value);
                    return false;
                }
                const int repeats_int = std::stoi(repeats_str);
                if (repeats_int <= 0) {
                    //LOG_ERROR("Invalid @{} number from {}", repeats_str, value);
                    return false;
                }
                else {
                    repeats = (std::size_t)repeats_int;
                }
            }
        }
    }
    catch (const std::invalid_argument& /*ex*/) {
        return false;
    }
    catch (const std::out_of_range& /*ex*/) {
        return false;
    }

    return true;

}

template <bool _WithRepeats>
bool infra::basic_tcp_endpoint<_WithRepeats>::resolve()
{
    std::vector<tcp_sockaddr> sockaddrs;

    addrinfo hint { };
    hint.ai_family = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_ADDRCONFIG;

    addrinfo* results = nullptr;

    const char* str_host = host.c_str();
    std::string trimmed_host;
    if (*this->host.begin() == '[' && *this->host.rbegin() == ']') {
        // host is IPv6, trim the beginning '[' and ending ']'
        trimmed_host = this->host.substr(1, this->host.length() - 2);
        str_host = trimmed_host.c_str();
    }

    const int ret = getaddrinfo(
        str_host,
        nullptr,
        &hint,
        &results);
    if (ret != 0) {
        LOG_ERROR("getaddrinfo() '{}' failed with {} ({})", host, ret, gai_strerror(ret));
        return false;
    }

    ASSERT(this->port.has_value());

    for (addrinfo* p = results; p != nullptr; p = p->ai_next) {
        tcp_sockaddr tmp { };
        if (p->ai_family == AF_INET) {
            tmp.addr_ipv4 = *reinterpret_cast<sockaddr_in*>(p->ai_addr);
            tmp.addr_ipv4.sin_port = htons(this->port.value());
        }
        else if (p->ai_family == AF_INET6) {
            tmp.addr_ipv6 = *reinterpret_cast<sockaddr_in6*>(p->ai_addr);
            tmp.addr_ipv6.sin6_port = htons(this->port.value());
        }
        else {
            LOG_ERROR("Unknown ai_family: {} (ignored)", p->ai_family);
            continue;
        }
        sockaddrs.emplace_back(std::move(tmp));
    }

    freeaddrinfo(results);

    this->resolved_sockaddrs = std::move(sockaddrs);
    return true;
}

template <bool _WithRepeats>
std::string infra::basic_tcp_endpoint<_WithRepeats>::to_string() const
{
    std::string result = host;
    if (port.has_value()) {
        result += ":";
        result += std::to_string(port.value());
    }

    if constexpr(_WithRepeats) {
        if (repeats.has_value()) {
            result += "@";
            result += std::to_string(repeats.value());
        }
    }

    return result;
}


//
// Explicit specialization of basic_tcp_endpoint
//
template struct infra::basic_tcp_endpoint<true>;
template struct infra::basic_tcp_endpoint<false>;
