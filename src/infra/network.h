#if !defined(_XCP_INFRA_NETWORK_H_INCLUDED_)
#define _XCP_INFRA_NETWORK_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    inline void global_initialize_network()
    {
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        const WORD wVersionRequested = MAKEWORD(2, 2);
        WSADATA wsaData { };
        const int ret = WSAStartup(wVersionRequested, &wsaData);
        if (ret != 0) {
            LOG_ERROR("WSAStartup() failed with {} ({})", ret, str_getlasterror(ret));
            throw std::system_error(std::error_code(ret, std::system_category()), "WSAStartup() failed");
        }
        //LOG_TRACE("WSAStartup() done");  // never displayed as default logging level is INFO
#elif PLATFORM_LINUX
#else
#   error "Unknown platform"
#endif
    }

    inline void global_finalize_network()
    {
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        if (WSACleanup() != 0) {
            const int wsagle = WSAGetLastError();
            LOG_ERROR("WSACleanup() failed. WSAGetLastError = {} ({})", wsagle, str_getlasterror(wsagle));
            throw std::system_error(std::error_code((int)wsagle, std::system_category()), "WSACleanup() failed");
        }
        LOG_TRACE("WSACleanup() done");
#elif PLATFORM_LINUX
#else
#   error "Unknown platform"
#endif
    }


    //
    // OS-specific "socket" type
    //
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
    typedef SOCKET socket_t;
    static constexpr const SOCKET INVALID_SOCKET_VALUE = INVALID_SOCKET;

    inline int close_socket(SOCKET sock) noexcept {
        shutdown(sock, SD_BOTH);
        return closesocket(sock);
    }

    inline std::string socket_error_description() {
        const int wsagle = WSAGetLastError();
        return std::string("WSAGetLastError = ") + std::to_string(wsagle) + " (" + str_getlasterror(wsagle) + ")";
    }

#elif PLATFORM_LINUX
    typedef int socket_t;
    static constexpr const int INVALID_SOCKET_VALUE = -1;

    inline int close_socket(int sock) noexcept {
        shutdown(sock, SHUT_RDWR);
        return close(sock);
    }

    inline std::string socket_error_description() {
        return std::string("errno = ") + std::to_string(errno) + " (" + strerror(errno) + ")";
    }

#else
#   error "Unknown platform"
#endif



    union tcp_sockaddr
    {
    public:
        struct sockaddr_storage addr;
        struct sockaddr_in addr_ipv4;
        struct sockaddr_in6 addr_ipv6;

    public:
        XCP_DEFAULT_COPY_CONSTRUCTOR(tcp_sockaddr)
        XCP_DEFAULT_MOVE_CONSTRUCTOR(tcp_sockaddr)
        tcp_sockaddr() = default;

        uint16_t family() const noexcept { return addr.ss_family; }

        const sockaddr* address() const noexcept { return (const sockaddr*)&addr; }
        sockaddr* address() noexcept { return (sockaddr*)&addr; }

        socklen_t socklen() const noexcept;

        std::string to_string() const;
    };


    template<bool _WithRepeats>
    struct basic_tcp_endpoint
    {
    public:
        std::string host { };
        std::optional<uint16_t> port { };
        std::optional<size_t> repeats { };
        std::vector<tcp_sockaddr> resolved_sockaddrs;

    public:
        XCP_DEFAULT_COPY_CONSTRUCTOR(basic_tcp_endpoint)

        XCP_DEFAULT_MOVE_ASSIGN(basic_tcp_endpoint)

        basic_tcp_endpoint(basic_tcp_endpoint&& other) noexcept
        {
            XCP_MOVE_FROM_OTHER(host);
            XCP_MOVE_FROM_OTHER(port);
            XCP_MOVE_FROM_OTHER(repeats);
            XCP_MOVE_FROM_OTHER(resolved_sockaddrs);
        }

        basic_tcp_endpoint() = default;

        bool parse(const std::string& value);
        bool resolve();
        std::string to_string() const;
    };

    typedef basic_tcp_endpoint</*_WithRepeats*/false> tcp_endpoint;
    typedef basic_tcp_endpoint</*_WithRepeats*/true> tcp_endpoint_repeatable;

}  // namespace infra


#endif  // !defined(_XCP_INFRA_NETWORK_H_INCLUDED_)
