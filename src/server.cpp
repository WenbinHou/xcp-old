#include "common.h"

//==============================================================================
// struct server_channel_state
//==============================================================================
using namespace xcp;
using namespace infra;

bool server_channel_state::init()
{
    assert(required_endpoint.resolved_sockaddrs.size() > 0);
    bool bound = false;
    for (const tcp_sockaddr& addr : required_endpoint.resolved_sockaddrs) {
        sock = socket(addr.family(), SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET_VALUE) {
            LOG_WARN("Can't create socket for channel {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        // Enable SO_REUSEADDR, SO_REUSEPORT
        constexpr int value_1 = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&value_1, sizeof(value_1)) != 0) {
            LOG_WARN("setsockopt(SO_REUSEADDR) failed. {} (skipped)", socket_error_description());
            continue;
        }

#if defined(SO_REUSEPORT)
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&value_1, sizeof(value_1)) != 0) {
            LOG_WARN("setsockopt(SO_REUSEPORT) failed. {} (skipped)", socket_error_description());
            continue;
        }
#endif

        sweeper sweep = [&]() {
            close_socket(sock);
            sock = INVALID_SOCKET_VALUE;
        };

        // Bind to specified endpoint
        if (bind(sock, addr.address(), addr.socklen()) != 0) {
            LOG_WARN("Can't bind to {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        // Get local endpoint
        bound_local_endpoint = addr;
        socklen_t len = bound_local_endpoint.socklen();
        if (getsockname(sock, bound_local_endpoint.address(), &len) != 0) {
            LOG_ERROR("getsockname() failed. Can't get bound endpoint for {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        // Start listen()
        if (listen(sock, /*backlog*/256) != 0) {
            LOG_ERROR("listen() failed. Can't listen on {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        LOG_INFO("Server channel {} binds to {}", required_endpoint.to_string(), bound_local_endpoint.to_string());
        bound = true;
        sweep.suppress_sweep();
        break;
    }

    if (!bound) {
        LOG_ERROR("Can't create and bind server channel endpoint {}", required_endpoint.to_string());
        return false;
    }

    thread_accept = std::thread([&]() {
        try {
            this->fn_thread_accept();
        }
        catch(const std::exception& ex) {
            LOG_ERROR("fn_thread_listener() exception: {}", ex.what());
            std::abort();
        }
    });

    return true;
}

server_channel_state::~server_channel_state() noexcept
{
    if (sock != INVALID_SOCKET_VALUE) {
        LOG_INFO("Close server channel {} bound to {}", required_endpoint.to_string(), bound_local_endpoint.to_string());
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    }

    if (thread_accept.joinable()) {
        thread_accept.join();
    }
}

void server_channel_state::fn_thread_accept()
{
    LOG_TRACE("Server channel accepting: {}", bound_local_endpoint.to_string());

    while (true) {
        tcp_sockaddr addr;
        addr.addr.ss_family = bound_local_endpoint.family();
        socklen_t len = addr.socklen();

        const socket_t accepted_sock = accept(sock, addr.address(), &len);
        if (accepted_sock == INVALID_SOCKET_VALUE) {
            if (sighandle::is_exit_required()) {
                LOG_DEBUG("Exit requried. Stop accept() on server channel {}", bound_local_endpoint.to_string());
                break;
            }
            else {
                LOG_ERROR("accept() on {} failed. {}", bound_local_endpoint.to_string(), socket_error_description());
                continue;
            }
        }

        // TODO
    }
}



//==============================================================================
// struct server_portal_state
//==============================================================================

bool server_portal_state::init()
{
    bool bound = false;
    for (const tcp_sockaddr& addr : required_endpoint.resolved_sockaddrs) {
        sock = socket(addr.family(), SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET_VALUE) {
            LOG_WARN("Can't create socket for portal {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        sweeper sweep = [&]() {
            close_socket(sock);
            sock = INVALID_SOCKET_VALUE;
        };

        // Enable SO_REUSEADDR, SO_REUSEPORT
        constexpr int value_1 = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&value_1, sizeof(value_1)) != 0) {
            LOG_WARN("setsockopt(SO_REUSEADDR) failed. {} (skipped)", socket_error_description());
            continue;
        }

#if defined(SO_REUSEPORT)
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&value_1, sizeof(value_1)) != 0) {
            LOG_WARN("setsockopt(SO_REUSEPORT) failed. {} (skipped)", socket_error_description());
            continue;
        }
#endif

        // Bind to specified endpoint
        if (bind(sock, addr.address(), addr.socklen()) != 0) {
            LOG_WARN("Can't bind to {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        // Get local endpoint
        bound_local_endpoint = addr;  // actually only family is important
        socklen_t len = bound_local_endpoint.socklen();
        if (getsockname(sock, bound_local_endpoint.address(), &len) != 0) {
            LOG_ERROR("getsockname() failed. Can't get bound endpoint for {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }
        // Start listen()
        if (listen(sock, /*backlog*/256) != 0) {
            LOG_ERROR("listen() failed. Can't listen on {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        LOG_INFO("Server portal {} binds to {}", required_endpoint.to_string(), bound_local_endpoint.to_string());
        bound = true;
        sweep.suppress_sweep();
        break;
    }

    if (!bound) {
        LOG_ERROR("Can't create and bind server portal endpoint {}", required_endpoint.to_string());
        return false;
    }

    thread_accept = std::thread([&]() {
        try {
            this->fn_thread_accept();
        }
        catch(const std::exception& ex) {
            LOG_ERROR("fn_thread_listener() exception: {}", ex.what());
            std::abort();
        }
    });

    return true;
}

server_portal_state::~server_portal_state() noexcept
{
    if (sock != INVALID_SOCKET_VALUE) {
        LOG_INFO("Close server portal {} bound to {}", required_endpoint.to_string(), bound_local_endpoint.to_string());
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    }

    if (thread_accept.joinable()) {
        thread_accept.join();
    }
}

void server_portal_state::fn_thread_accept()
{
    LOG_TRACE("Server portal accepting: {}", bound_local_endpoint.to_string());

    while (true) {
        tcp_sockaddr peer_addr { };
        peer_addr.addr.ss_family = bound_local_endpoint.family();
        socklen_t len = peer_addr.socklen();

        const socket_t accepted_sock = accept(sock, peer_addr.address(), &len);
        if (accepted_sock == INVALID_SOCKET_VALUE) {
            if (sighandle::is_exit_required()) {
                LOG_DEBUG("Exit requried. Stop accept() on server portal {}", bound_local_endpoint.to_string());
                break;
            }
            else {
                LOG_ERROR("accept() on {} failed. {}", bound_local_endpoint.to_string(), socket_error_description());
                continue;
            }
        }
        LOG_DEBUG("Accepted from portal: {}", peer_addr.to_string());

        std::thread thr([accepted_sock, peer_addr]() {
            fn_task_accepted_portal(accepted_sock, peer_addr);
        });
        thr.detach();
    }
}

/*static*/ void server_portal_state::fn_task_accepted_portal(socket_t accepted_sock, const tcp_sockaddr peer_addr)
{
    sweeper sweep = [&]() {
        if (accepted_sock != INVALID_SOCKET_VALUE) {
            close_socket(accepted_sock);
            accepted_sock = INVALID_SOCKET_VALUE;
        }
    };

    //const auto erase_from_pending_client_portals = [&]() -> bool {
    //    std::lock_guard<std::mutex> lock(g_pending_client_portals_mutex);
    //    if (g_pending_client_portals.erase(accepted_sock) != 1) {
    //        assert(sighandle::is_exit_required());
    //        sweep.suppress_sweep();
    //        return false;
    //    }
    //    return true;
    //};

    // Receive client identity from portal
    identity_t identity;
    {
        const int cnt = (int)recv(accepted_sock, (char*)&identity, sizeof(identity), MSG_WAITALL);
        if (cnt != (int)sizeof(identity)) {
            LOG_TRACE("Server portal: receive identity from peer {} expects {}, but returns {}. {}",
                      peer_addr.to_string(), sizeof(identity), cnt, socket_error_description());
            return;
        }

        LOG_TRACE("Server portal: received identity from peer {}", peer_addr.to_string());
    }

    //// Create client instance and remove from pending client portals
    //{
    //    std::lock_guard<std::mutex> lock(g_clients_mutex);

    //    if (!erase_from_pending_client_portals()) {  // must be guarded by g_clients_mutex
    //        return;
    //    }

    //    std::shared_ptr<client_instance> client = std::make_shared<client_instance>(accepted_sock, peer_addr);
    //    g_clients.insert(std::make_pair(identity, client));
    //}

    // TODO: Send back server portal information...
}

