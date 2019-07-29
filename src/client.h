#if !defined(_XCP_CLIENT_H_INCLUDED_)
#define _XCP_CLIENT_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)


namespace xcp
{
    struct client_portal_state;


    struct client_channel_state : infra::disposable
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(client_channel_state)
        XCP_DISABLE_MOVE_CONSTRUCTOR(client_channel_state)

        explicit client_channel_state(client_portal_state& portal, infra::tcp_sockaddr addr)
            : portal(portal),
              server_channel_sockaddr(std::move(addr))
        { }

        bool init();

        void dispose_impl() noexcept override final;
        ~client_channel_state() noexcept override final { this->async_dispose(true); }

    private:
        void fn_thread_work();

    public:
        client_portal_state& portal;
        std::thread thread_work { };
        const infra::tcp_sockaddr server_channel_sockaddr { };
        infra::socket_t sock = infra::INVALID_SOCKET_VALUE;
    };


    struct client_portal_state : infra::disposable
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(client_portal_state)
        XCP_DISABLE_MOVE_CONSTRUCTOR(client_portal_state)

        explicit client_portal_state(std::shared_ptr<xcp_program_options> program_options)
            : program_options(std::move(program_options))
        {
            client_identity.init();
        }

        bool init();

        void dispose_impl() noexcept override final;
        ~client_portal_state() noexcept override final { this->async_dispose(true); }

    private:
        void fn_thread_work();

    public:
        std::shared_ptr<xcp_program_options> program_options;
        infra::identity_t client_identity;

        std::vector<std::shared_ptr<client_channel_state>> channels;
        std::shared_mutex channels_mutex { };

        std::thread thread_work { };
        infra::tcp_sockaddr connected_remote_endpoint { };
        infra::socket_t sock = infra::INVALID_SOCKET_VALUE;

        std::shared_ptr<transfer_base> transfer { nullptr };

        std::atomic_int transfer_result_status { TRANSFER_UNKNOWN };

        static constexpr const int TRANSFER_UNKNOWN = 0;  // early stopped before setting a status value
        static constexpr const int TRANSFER_FAILED = 1;
        static constexpr const int TRANSFER_SUCCEEDED = 2;
    };

}  // namespace xcp

#endif  // !defined(_XCP_CLIENT_H_INCLUDED_)
