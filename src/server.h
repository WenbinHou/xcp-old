#if !defined(_XCP_SERVER_H_INCLUDED_)
#define _XCP_SERVER_H_INCLUDED_

#if !defined(_XCP_COMMON_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include common.h"
#endif  // !defined(_XCP_COMMON_H_INCLUDED_)

namespace xcp
{
    struct server_portal_state;

    struct client_instance
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(client_instance)
        XCP_DISABLE_MOVE_CONSTRUCTOR(client_instance)

        ~client_instance() noexcept { dispose(); }

        client_instance(
            server_portal_state& server_portal,
            const infra::socket_t accepted_portal_socket,
            const infra::tcp_sockaddr& peer_endpoint,
            const infra::identity_t& client_identity) noexcept
            : server_portal(server_portal),
              accepted_portal_socket(accepted_portal_socket),
              peer_endpoint(peer_endpoint),
              client_identity(client_identity)
        { }

        void fn_portal();
        void fn_channel(infra::socket_t accepted_channel_socket, infra::tcp_sockaddr channel_peer_endpoint);
        void dispose() noexcept;

    public:
        server_portal_state& server_portal;

        infra::socket_t accepted_portal_socket;
        infra::tcp_sockaddr peer_endpoint;
        const infra::identity_t client_identity;

        std::vector<std::shared_ptr<std::thread>> all_threads;  // including portal thread and channel threads
        std::shared_mutex all_threads_mutex;

        struct {
            bool is_from_server_to_client { };
            std::string client_file_name;
            std::string server_path;
        } request;
    };


    struct server_channel_state
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(server_channel_state)
        XCP_DISABLE_MOVE_CONSTRUCTOR(server_channel_state)

        explicit server_channel_state(server_portal_state& portal, infra::tcp_endpoint_repeatable ep)
            : portal(portal),
              required_endpoint(std::move(ep))
        { }

        bool init();
        void dispose() noexcept;
        ~server_channel_state() noexcept { dispose(); }

    private:
        void fn_thread_accept();

    public:
        server_portal_state& portal;
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

        explicit server_portal_state(std::shared_ptr<xcpd_program_options> program_options)
            : program_options(std::move(program_options))
        { }

        bool init();
        void dispose() noexcept;
        ~server_portal_state() noexcept { dispose(); }

    private:
        void fn_thread_accept();
        void fn_task_accepted_portal(infra::socket_t accepted_sock, const infra::tcp_sockaddr& peer_addr);

    public:
        std::thread thread_accept { };
        std::shared_ptr<xcpd_program_options> program_options;
        infra::tcp_sockaddr bound_local_endpoint { };
        infra::socket_t sock = infra::INVALID_SOCKET_VALUE;

        std::vector<std::shared_ptr<xcp::server_channel_state>> channels;

        std::unordered_map<infra::identity_t, std::shared_ptr<client_instance>> clients { };
        std::shared_mutex clients_mutex { };
    };


}  // namespace xcp

#endif  // !defined(_XCP_SERVER_H_INCLUDED_)
