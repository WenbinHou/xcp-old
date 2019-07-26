#if !defined(_XCP_SERVER_H_INCLUDED_)
#define _XCP_SERVER_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)

namespace xcp
{
    struct server_channel_state
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(server_channel_state)
        XCP_DISABLE_MOVE_CONSTRUCTOR(server_channel_state)

        explicit server_channel_state(infra::tcp_endpoint_repeatable ep)
            : required_endpoint(std::move(ep))
        { }

        bool init();
        ~server_channel_state() noexcept;

    private:
        void fn_thread_accept();

    public:
        std::thread thread_accept { };
        const infra::tcp_endpoint_repeatable required_endpoint;
        infra::tcp_sockaddr bound_local_endpoint { };
        infra::socket_t sock = infra::INVALID_SOCKET_VALUE;
    };


    struct server_portal_state
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(server_portal_state)
        XCP_DISABLE_MOVE_CONSTRUCTOR(server_portal_state)

        explicit server_portal_state(infra::tcp_endpoint ep)
            : required_endpoint(std::move(ep))
        { }

        bool init();
        ~server_portal_state() noexcept;

    private:
        void fn_thread_accept();
        static void fn_task_accepted_portal(infra::socket_t accepted_sock, infra::tcp_sockaddr peer_addr);

    public:
        std::thread thread_accept { };
        const infra::tcp_endpoint required_endpoint;
        infra::tcp_sockaddr bound_local_endpoint { };
        infra::socket_t sock = infra::INVALID_SOCKET_VALUE;
    };


}  // namespace xcp

#endif  // !defined(_XCP_SERVER_H_INCLUDED_)
