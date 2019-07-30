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

        void set_port(uint16_t port_in_host_endian) noexcept;
        [[nodiscard]]
        uint16_t port() const noexcept;

        [[nodiscard]]
        uint16_t family() const noexcept { return addr.ss_family; }

        [[nodiscard]]
        const sockaddr* address() const noexcept { return (const sockaddr*)&addr; }

        [[nodiscard]]
        sockaddr* address() noexcept { return (sockaddr*)&addr; }

        [[nodiscard]]
        socklen_t socklen() const noexcept;

        [[nodiscard]]
        socklen_t max_socklen() const noexcept { return (socklen_t)sizeof(sockaddr_storage); }

        [[nodiscard]]
        std::string to_string() const;

        [[nodiscard]]
        bool is_addr_any() const noexcept;

        XCP_DEFAULT_SERIALIZATION(cereal::binary_data(&addr, sizeof(sockaddr_storage)))
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

        [[nodiscard]]
        std::string to_string() const;
    };

    typedef basic_tcp_endpoint</*_WithRepeats*/false> tcp_endpoint;
    typedef basic_tcp_endpoint</*_WithRepeats*/true> tcp_endpoint_repeatable;



    //
    // OS-specific "socket" type
    //
    struct socket_io_vec
    {
        const void* ptr { };
        size_t len { };

        socket_io_vec() noexcept = default;
        socket_io_vec(const void* const ptr, const size_t len) noexcept
            : ptr(ptr), len(len)
        { }
    };

    struct os_socket_t : disposable
    {
    private:
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        typedef SOCKET socket_t;
        typedef HANDLE file_handle_t;
        static constexpr const SOCKET INVALID_SOCKET_VALUE = INVALID_SOCKET;

#elif PLATFORM_LINUX
        typedef int socket_t;
        typedef int file_handle_t;
        static constexpr const int INVALID_SOCKET_VALUE = -1;

#else
#   error "Unknown platform"
#endif

    public:
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        static std::string error_description(const DWORD wsagle) {
            return std::string("WSAGetLastError = ") + std::to_string(wsagle) + " (" + str_getlasterror(wsagle) + ")";
        }
        static std::string error_description() {
            return error_description(WSAGetLastError());
        }

#elif PLATFORM_LINUX
        static std::string error_description(const int err) {
            return std::string("errno = ") + std::to_string(err) + " (" + strerror(err) + ")";
        }
        static std::string error_description() {
            return error_description(errno);
        }

#else
#   error "Unknown platform"
#endif

    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(os_socket_t)
        XCP_DISABLE_MOVE_CONSTRUCTOR(os_socket_t)
        os_socket_t() noexcept = default;
        explicit os_socket_t(const socket_t sock) noexcept : _sock(sock) { }

        void dispose_impl() noexcept override final { this->non_thread_safe_close(); }
        virtual ~os_socket_t() override final { this->dispose(); /*no need to async_dispose(true)*/ }

        bool init_tcp(uint16_t family);
        bool bind(const tcp_sockaddr& addr, /*out,opt*/tcp_sockaddr* bound_addr);
        bool listen(int backlog);
        std::shared_ptr<os_socket_t> accept(/*out,opt*/ tcp_sockaddr* remote_addr);
        bool connect(const tcp_sockaddr& addr, /*out,opt*/tcp_sockaddr* local_addr, /*out,opt*/tcp_sockaddr* remote_addr);
        bool send_file(file_handle_t file_handle, uint64_t offset, uint32_t size, /*in,opt*/const socket_io_vec* header);
        bool recv(void* ptr, uint32_t size);

        bool send(const void* ptr, const uint32_t size) {
            const socket_io_vec vec[1] { { ptr, size } };
            return sendv(vec);
        }

        template<typename T>
        bool send(const T* obj) {
            ASSERT(obj != nullptr);
            const socket_io_vec vec[1] { { (void*)obj, sizeof(*obj) } };
            return sendv(vec);
        }

#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
        template<uint32_t _N>
        bool sendv(const socket_io_vec (&vec)[_N]) {
            WSABUF buffers[_N];
            uint64_t expected_written = 0;
            for (uint32_t i = 0; i < _N; ++i) {
                buffers[i].buf = (char*)vec[i].ptr;
                ASSERT(vec[i].len <= ULONG_MAX);
                buffers[i].len = (ULONG)vec[i].len;
                expected_written += vec[i].len;
            }
            return internal_sendv((void*)buffers, _N, expected_written);
        }
#elif PLATFORM_LINUX
        template<uint32_t _N>
        bool sendv(const socket_io_vec (&vec)[_N]) {
            iovec buffers[_N];
            uint64_t expected_written = 0;
            for (uint32_t i = 0; i < _N; ++i) {
                buffers[i].iov_base = (void*)vec[i].ptr;
                buffers[i].iov_len = vec[i].len;
                expected_written += vec[i].len;
            }
            return internal_sendv((void*)buffers, _N, expected_written);
        }
#else
#   error "Unknown platform"
#endif

    private:
        bool internal_sendv(const void* vec, uint32_t vec_count, uint64_t expected_written);
        void non_thread_safe_close() noexcept;

    private:
        socket_t _sock = INVALID_SOCKET_VALUE;
    };



    struct identity_t
    {
    public:
        friend struct std::hash<identity_t>;

        void init()
        {
            std::random_device rd;
            _timestamp = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            _random1 = static_cast<uint32_t>(rd());
            _random2 = static_cast<uint32_t>(rd());
        }

        bool operator ==(const identity_t& other) const noexcept
        {
            return (_timestamp == other._timestamp) && (_random1 == other._random1) && (_random2 == other._random2);
        }

        bool operator !=(const identity_t& other) const noexcept
        {
            return !(*this == other);
        }

    private:
        uint64_t _timestamp = 0;
        uint32_t _random1 = 0;
        uint32_t _random2 = 0;
    };

    static_assert(sizeof(identity_t) == 16);

}  // namespace infra



//
// std::hash<> specialization for infra::identity_t
//
namespace std
{
    template<>
    struct hash<infra::identity_t>
    {
        size_t operator()(const infra::identity_t& x) const noexcept
        {
            return static_cast<size_t>(x._timestamp) ^ static_cast<size_t>(x._random1) ^ static_cast<size_t>(x._random2);
        }
    };
}  // namespace std


#endif  // !defined(_XCP_INFRA_NETWORK_H_INCLUDED_)
