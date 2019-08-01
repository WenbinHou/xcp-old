#include "infra.h"
#include "network.h"



//==============================================================================
// struct tcp_sockaddr
//==============================================================================

void infra::tcp_sockaddr::set_port(const uint16_t port_in_host_endian) noexcept
{
    if (family() == AF_INET) {
        addr_ipv4().sin_port = htons(port_in_host_endian);
    }
    else if (family() == AF_INET6) {
        addr_ipv6().sin6_port = htons(port_in_host_endian);
    }
    else {
        PANIC_TERMINATE("BUG: Unknown family: {}", address()->sa_family);
    }
}

uint16_t infra::tcp_sockaddr::port() const noexcept
{
    if (family() == AF_INET) {
        return ntohs(addr_ipv4().sin_port);
    }
    else if (family() == AF_INET6) {
        return ntohs(addr_ipv6().sin6_port);
    }
    else {
        PANIC_TERMINATE("BUG: Unknown family: {}", address()->sa_family);
    }
}

socklen_t infra::tcp_sockaddr::socklen() const noexcept
{
    if (family() == AF_INET) {
        return sizeof(addr_ipv4());
    }
    else if (family() == AF_INET6) {
        return sizeof(addr_ipv6());
    }
    else {
        PANIC_TERMINATE("BUG: Unknown family: {}", address()->sa_family);
    }
}

std::string infra::tcp_sockaddr::to_string() const
{
    char buffer[64];
    std::string result;

    if (family() == AF_INET) {
        if (inet_ntop(AF_INET, (void*)&addr_ipv4().sin_addr, buffer, sizeof(buffer)) == nullptr) {
            LOG_ERROR("inet_ntop(AF_INET) unexpecedly failed");
            return "FAILED_TO_STRING";
        }
        result = buffer;
        result += ":";
        result += std::to_string(ntohs(addr_ipv4().sin_port));
    }
    else if (family() == AF_INET6) {
        if (inet_ntop(AF_INET6, (void*)&addr_ipv6().sin6_addr, buffer, sizeof(buffer)) == nullptr) {
            LOG_ERROR("inet_ntop(AF_INET6) unexpecedly failed");
            return "FAILED_TO_STRING";
        }
        result = "[";
        result += buffer;
        result += "]:";
        result += std::to_string(ntohs(addr_ipv6().sin6_port));
    }
    else {
        PANIC_TERMINATE("BUG: Unknown family: {}", address()->sa_family);
    }

    return result;
}

bool infra::tcp_sockaddr::is_addr_any() const noexcept
{
    if (family() == AF_INET) {
        return addr_ipv4().sin_addr.s_addr == INADDR_ANY;
    }
    else if (family() == AF_INET6) {
        return memcmp(&addr_ipv6().sin6_addr, &in6addr_any, sizeof(struct in6_addr)) == 0;
    }
    else {
        PANIC_TERMINATE("BUG: Unknown family: {}", address()->sa_family);
    }
}



//==============================================================================
// struct tcp_endpoint
//==============================================================================

bool infra::tcp_endpoint::parse(const std::string& value)
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
    // NOTE: re_valid_hostname also includes '*', which is specially treated in resolve()

    static std::regex* __re = nullptr;
    if (__re == nullptr) {
        const std::string re_valid_hostname = R"((?:(?:(?:[a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*(?:[A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])|\*))";
        const std::string re_valid_ipv6 = R"((?:\[(?:(?:[0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,7}:|(?:[0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,5}(?::[0-9a-fA-F]{1,4}){1,2}|(?:[0-9a-fA-F]{1,4}:){1,4}(?::[0-9a-fA-F]{1,4}){1,3}|(?:[0-9a-fA-F]{1,4}:){1,3}(?::[0-9a-fA-F]{1,4}){1,4}|(?:[0-9a-fA-F]{1,4}:){1,2}(?::[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:(?:(?::[0-9a-fA-F]{1,4}){1,6})|:(?:(?::[0-9a-fA-F]{1,4}){1,7}|:)|::(?:[Ff]{4}(?::0{1,4})?:)?(?:(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])\.){3,3}(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])|(?:[0-9a-fA-F]{1,4}:){1,4}:(?:(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])\.){3,3}(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9]))\]))";
        const std::string re_valid_host = "(?:" + re_valid_hostname + "|" + re_valid_ipv6 + ")";
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

            // NOTE: host might be '*', resolve() should take care of this
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

bool infra::tcp_endpoint::resolve()
{
    std::vector<tcp_sockaddr> sockaddrs;

    // Special treat '*' as host
    if (this->host == "*") {
        // IPv6: [::]
        {
            tcp_sockaddr tmp { };
            tmp.addr_ipv6().sin6_family = AF_INET6;
            tmp.addr_ipv6().sin6_addr = in6addr_any;
            tmp.addr_ipv6().sin6_port = htons(this->port.value());
            sockaddrs.emplace_back(tmp);
        }
        // IPv4: 0.0.0.0
        {
            tcp_sockaddr tmp { };
            tmp.addr_ipv4().sin_family = AF_INET;
            tmp.addr_ipv4().sin_addr.s_addr = INADDR_ANY;
            tmp.addr_ipv4().sin_port = htons(this->port.value());
            sockaddrs.emplace_back(tmp);
        }
        this->resolved_sockaddrs = std::move(sockaddrs);
        return true;
    }

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
            tmp.addr_ipv4() = *reinterpret_cast<sockaddr_in*>(p->ai_addr);
            tmp.addr_ipv4().sin_port = htons(this->port.value());
        }
        else if (p->ai_family == AF_INET6) {
            tmp.addr_ipv6() = *reinterpret_cast<sockaddr_in6*>(p->ai_addr);
            tmp.addr_ipv6().sin6_port = htons(this->port.value());
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

std::string infra::tcp_endpoint::to_string() const
{
    std::string result = host;
    if (port.has_value()) {
        result += ":";
        result += std::to_string(port.value());
    }

    if (repeats.has_value()) {
        result += "@";
        result += std::to_string(repeats.value());
    }

    return result;
}



//==============================================================================
// struct os_socket_t
//==============================================================================

void infra::os_socket_t::non_thread_safe_close() noexcept
{
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
    if (_sock != INVALID_SOCKET_VALUE) {
        (void)shutdown(_sock, SD_BOTH);
        (void)closesocket(_sock);
        _sock = INVALID_SOCKET_VALUE;
    }
#elif PLATFORM_LINUX
    if (_sock != INVALID_SOCKET_VALUE) {
        (void)shutdown(_sock, SHUT_RDWR);
        (void)close(_sock);
        _sock = INVALID_SOCKET_VALUE;
    }
#else
#   error "Unknown platform"
#endif
}

bool infra::os_socket_t::init_tcp(const uint16_t family)
{
#if defined(SOL_TCP)
    static_assert(SOL_TCP == IPPROTO_TCP, "SOL_TCP should match IPPROTO_TCP");
#endif
    const int value_1 = 1;

    ASSERT(_sock == INVALID_SOCKET_VALUE);

    //
    // Create socket
    //
    _sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (_sock == INVALID_SOCKET_VALUE) {
        LOG_ERROR("socket() failed. {}", error_description());
        return false;
    }

    sweeper error_cleanup = [&]() {
        this->non_thread_safe_close();
    };


    //
    // Enable SO_REUSEADDR, SO_REUSEPORT
    //
    if (setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&value_1, sizeof(value_1)) != 0) {
        LOG_ERROR("setsockopt(SO_REUSEADDR) failed. {}", error_description());
        return false;
    }

#if defined(SO_REUSEPORT)
    if (setsockopt(_sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&value_1, sizeof(value_1)) != 0) {
        LOG_ERROR("setsockopt(SO_REUSEPORT) failed. {}", error_description());
        return false;
    }
#endif

    //
    // Enable TCP_NODELAY
    //
    if (setsockopt(_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&value_1, sizeof(value_1)) != 0) {
        LOG_ERROR("setsockopt(TCP_NODELAY) failed. {}", error_description());
        return false;
    }

    //
    // Enable KeepAlive
    //
    if (setsockopt(_sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&value_1, sizeof(value_1)) != 0) {
        LOG_ERROR("setsockopt(SO_KEEPALIVE) failed. {}", error_description());
        return false;
    }
    // TODO: maybe also set TCP_KEEPCNT, TCP_KEEPIDLE, TCP_KEEPINTVL?


    error_cleanup.suppress_sweep();
    return true;
}

bool infra::os_socket_t::bind(const infra::tcp_sockaddr& addr, /*out*/ infra::tcp_sockaddr* bound_addr)
{
    ASSERT(_sock != INVALID_SOCKET_VALUE);

    // Enable dual stack on IPv6
    enable_dual_stack_if_inet6(addr.family());

    // Bind to specified endpoint
    if (::bind(_sock, addr.address(), addr.socklen()) != 0) {
        LOG_ERROR("bind() to {} failed. {}", addr.to_string(), error_description());
        return false;
    }

    // Get local endpoint
    if (bound_addr) {
        *bound_addr = addr;
        socklen_t len = bound_addr->socklen();
        if (getsockname(_sock, bound_addr->address(), &len) != 0) {
            LOG_ERROR("bind() to {} succeeded but getsockname() failed. {}", addr.to_string(), error_description());
            return false;
        }
    }

    return true;
}

bool infra::os_socket_t::listen(const int backlog)
{
    ASSERT(_sock != INVALID_SOCKET_VALUE);

    //
    // Enable TCP_FASTOPEN for server if supported
    //
#if defined(TCP_FASTOPEN)
    const int32_t value_1 = 1;
    if (setsockopt(_sock, IPPROTO_TCP, TCP_FASTOPEN, (const char*)&value_1, sizeof(value_1)) != 0) {
        LOG_WARN("setsockopt(TCP_FASTOPEN) failed. {}", error_description());
        // TODO: Give more precise information based on running OS
        // Don't return false
    }
#else
    LOG_DEBUG("Compiled without TCP_FASTOPEN support");
    _tcp_fastopen_supported = false;
#endif

    if (::listen(_sock, backlog) != 0) {
        LOG_ERROR("listen(backlog={}) failed. {}", backlog, error_description());
        return false;
    }

    return true;
}

std::shared_ptr<infra::os_socket_t> infra::os_socket_t::accept(/*out,opt*/ tcp_sockaddr* const remote_addr)
{
    // _sock might be INVALID_SOCKET_VALUE is already disposed
    //ASSERT(_sock != INVALID_SOCKET_VALUE);

    sockaddr* addr = nullptr;
    socklen_t dummy_len;
    socklen_t* plen = nullptr;
    if (remote_addr) {
        addr = remote_addr->address();
        dummy_len = remote_addr->max_socklen();
        plen = &dummy_len;
    }

    const socket_t new_sock = ::accept(_sock, addr, plen);
    if (new_sock == INVALID_SOCKET_VALUE) {
        LOG_ERROR("accept() failed. {}", error_description());
        return nullptr;
    }

    return std::make_shared<os_socket_t>(new_sock);
}

bool infra::os_socket_t::connect(const tcp_sockaddr& addr, tcp_sockaddr* local_addr, tcp_sockaddr* remote_addr)
{
    ASSERT(_sock != INVALID_SOCKET_VALUE);

    // Enable dual stack on IPv6
    enable_dual_stack_if_inet6(addr.family());

    // Connect to specified endpoint
    if (::connect(_sock, addr.address(), addr.socklen()) != 0) {
        LOG_ERROR("connect() to {} failed. {}", addr.to_string(), error_description());
        return false;
    }

    // Get local endpoint
    if (local_addr) {
        socklen_t len = local_addr->max_socklen();
        if (getsockname(_sock, local_addr->address(), &len) != 0) {
            LOG_ERROR("connect() to {} succeeded but getsockname() failed. {}", addr.to_string(), error_description());
            return false;
        }
    }

    // Get remote endpoint
    if (remote_addr) {
        socklen_t len = remote_addr->max_socklen();
        if (getpeername(_sock, remote_addr->address(), &len) != 0) {
            LOG_ERROR("connect() to {} succeeded but getpeername() failed. {}", addr.to_string(), error_description());
            return false;
        }
    }

    return true;
}

bool infra::os_socket_t::send_file(
    const file_handle_t file_handle,
    const uint64_t offset,
    const uint32_t size,
    const socket_io_vec* const header)
{
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
    TRANSMIT_FILE_BUFFERS headtail;
    headtail.Head = header ? (void*)header->ptr : nullptr;
    headtail.HeadLength = header ? (DWORD)header->len : 0;
    headtail.Tail = nullptr;
    headtail.TailLength = 0;

    OVERLAPPED overlapped { };
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = (DWORD)(offset >> 32);
    overlapped.hEvent = NULL;  // TODO: do we need a event anyway?

    const BOOL success = TransmitFile(
        _sock,
        file_handle,
        size,
        0,
        &overlapped,
        &headtail,
        TF_USE_KERNEL_APC);
    if (success) {
        // nothing to do
    }
    else {
        const int wsagle = WSAGetLastError();
        static_assert(WSA_IO_PENDING == ERROR_IO_PENDING);
        if (wsagle == WSA_IO_PENDING) {
            DWORD written = 0;
            if (!GetOverlappedResult((HANDLE)_sock, &overlapped, &written, TRUE)) {
                LOG_ERROR("GetOverlappedResult() failed. GetLastError = {} ({})", GetLastError(), infra::str_getlasterror(GetLastError()));
                return false;
            }

            const uint32_t expected_written = size + (uint32_t)header->len;
            if (written != expected_written) {
                LOG_ERROR("TransmitFile() overlapped result error: expected written {} bytes, actually written {} bytes",
                          expected_written, written);
                return false;
            }
        }
        else {
            LOG_ERROR("TransmitFile() failed. {}", wsagle, error_description());
            return false;
        }
    }


#elif PLATFORM_LINUX
    // Send header
    if (header) {
        const ssize_t cnt = ::send(_sock, header->ptr, header->len, MSG_MORE);
        if (cnt < 0 || (size_t)cnt != header->len) {
            LOG_ERROR("send() header expects {}, but returns {}. {}",
                      header->len, cnt, error_description());
            return false;
        }
    }

    // Send body
    {
        off64_t mutable_off = offset;
        const ssize_t cnt = sendfile64(_sock, file_handle, &mutable_off, size);
        if (cnt < 0 || cnt != (ssize_t)size) {
            LOG_ERROR("sendfile(offset={}, len={}) expects {}, but returns {}. {}",
                      offset, size, size, cnt, error_description());
            return false;
        }
    }

#else
#   error "Unknown platform"
#endif

    return true;
}

bool infra::os_socket_t::recv(void* const ptr, const uint32_t size)
{
    const int ret = (int)::recv(_sock, (char*)ptr, size, MSG_WAITALL);
    if (ret < 0 || ret != (int)size) {
        LOG_ERROR("recv() expects to return {}, but returns {}. {}",
                  size, ret, error_description());
        return false;
    }

    return true;
}

void infra::os_socket_t::enable_dual_stack_if_inet6(const uint16_t family)
{
    // If not IPv6, do nothing
    if (family != AF_INET6) {
        return;
    }

    //
    // Enable dual stack for IPv6 & IPv4
    //
    const int value_0 = 0;
    if (setsockopt(_sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&value_0, sizeof(value_0)) != 0) {
        LOG_WARN("setsockopt(IPV6_V6ONLY) to 0 failed. {}", error_description());
    }
}

bool infra::os_socket_t::internal_sendv(const void* const vec, const uint32_t vec_count, const uint64_t expected_written)
{
    // _sock might be INVALID_SOCKET_VALUE is already disposed
    //ASSERT(_sock != INVALID_SOCKET_VALUE);

#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
    OVERLAPPED overlapped { };

    const int ret = WSASend(
        _sock,
        (LPWSABUF)vec,
        vec_count,
        NULL,
        0,
        &overlapped,
        NULL);
    if (ret == 0) {
        // Immediately done, nothing to do
    }
    else {
        const int wsagle = WSAGetLastError();
        if (wsagle == WSA_IO_PENDING) {
            DWORD sent = 0;
            if (!GetOverlappedResult((HANDLE)_sock, &overlapped, &sent, TRUE)) {
                LOG_ERROR("GetOverlappedResult() failed. GetLastError = {} ({})", GetLastError(), str_getlasterror(GetLastError()));
                return false;
            }

            if (sent != expected_written) {
                LOG_ERROR("WSASend() overlapped result error: expected to sent {} bytes, actually sent {} bytes",
                          expected_written, sent);
                return false;
            }
        }
        else {
            LOG_ERROR("WSASend({} parts, totally {} bytes) failed with {}. {}",
                      vec_count, expected_written, ret, error_description(wsagle));
            return false;
        }
    }


#elif PLATFORM_LINUX
    const ssize_t cnt = writev(_sock, (const iovec*)vec, vec_count);
    if ((uint64_t)cnt != expected_written) {
        LOG_ERROR("writev({} parts) expects to return {}, but returns {}. {}",
                  vec_count, expected_written, cnt, error_description());
        return false;
    }

#else
#   error "Unknown platform"
#endif

    return true;
}


bool infra::os_socket_t::connect_and_send(
    const infra::tcp_sockaddr& addr,
    const void* const ptr,
    const uint32_t size,
    infra::tcp_sockaddr* const local_addr,
    infra::tcp_sockaddr* const remote_addr)
{
    if (size > 0) ASSERT(ptr != nullptr);

#if defined(TCP_FASTOPEN)
#   if PLATFORM_WINDOWS || PLATFORM_CYGWIN
    {
        // TODO: Use ConnectEx() for TCP_FASTOPEN
        // But we need to confirm Windows is recent enough for TFO
        // Also see issue: https://github.com/WenbinHou/xcp/issues/15

        return this->connect(addr, local_addr, remote_addr) && this->send(ptr, size);
    }
#   elif PLATFORM_LINUX
    {
        const ssize_t ret = sendto(_sock, ptr, size, MSG_FASTOPEN, addr.address(), addr.socklen());
        if (ret < 0 || (uint64_t)ret != (uint64_t)size) {
            LOG_ERROR("TFO sendto() expects {}, but returns {}. {}", size, ret, error_description());
            return false;
        }

        // Get local endpoint
        if (local_addr) {
            socklen_t len = local_addr->max_socklen();
            if (getsockname(_sock, local_addr->address(), &len) != 0) {
                LOG_ERROR("TFO sendto() to {} succeeded but getsockname() failed. {}", addr.to_string(), error_description());
                return false;
            }
        }

        // Get remote endpoint
        if (remote_addr) {
            socklen_t len = remote_addr->max_socklen();
            if (getpeername(_sock, remote_addr->address(), &len) != 0) {
                LOG_ERROR("TFO sendto() to {} succeeded but getpeername() failed. {}", addr.to_string(), error_description());
                return false;
            }
        }

        return true;
    }
#   else
#       error "Unknown platform"
#   endif

#else  // !defined(TCP_FASTOPEN)

    // Not compiled with TFO support
    return this->connect(addr, local_addr, remote_addr) && this->send(ptr, size);

#endif
}
