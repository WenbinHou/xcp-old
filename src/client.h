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

        void dispose_impl() noexcept override;
        bool init();

        virtual ~client_channel_state() noexcept = default;

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
        void dispose_impl() noexcept override;

        virtual ~client_portal_state() noexcept = default;

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
    };

}  // namespace xcp

#endif  // !defined(_XCP_CLIENT_H_INCLUDED_)
