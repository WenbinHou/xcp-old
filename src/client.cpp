#include "common.h"

using namespace xcp;
using namespace infra;


//==============================================================================
// Global variables
//==============================================================================
extern infra::identity_t g_client_identity;


//==============================================================================
// struct client_portal_state
//==============================================================================

bool client_portal_state::init()
{
    bool connected = false;
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

        // Connect to specified endpoint
        if (connect(sock, addr.address(), addr.socklen()) != 0) {
            LOG_WARN("Can't connect() to {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        // Get remote endpoint
        connected_remote_endpoint = addr;  // actually only family is important
        socklen_t len = connected_remote_endpoint.socklen();
        if (getpeername(sock, connected_remote_endpoint.address(), &len) != 0) {
            LOG_ERROR("getpeername() failed. Can't get connected endpoint for {}. {} (skipped)", addr.to_string(), socket_error_description());
            continue;
        }

        LOG_INFO("Connected to portal {} (peer: {})", required_endpoint.to_string(), connected_remote_endpoint.to_string());
        connected = true;
        sweep.suppress_sweep();
        break;
    }

    if (!connected) {
        LOG_ERROR("Can't create and connect to server portal endpoint {}", required_endpoint.to_string());
        return false;
    }

    thread_work = std::thread([&]() {
        try {
            this->fn_thread_work();
        }
        catch(const std::exception& ex) {
            LOG_ERROR("fn_thread_work() exception: {}", ex.what());
            std::abort();
        }
    });

    return true;
}

client_portal_state::~client_portal_state() noexcept
{
    if (sock != INVALID_SOCKET_VALUE) {
        LOG_INFO("Close server portal {} (peer: {})", required_endpoint.to_string(), connected_remote_endpoint.to_string());
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    }

    if (thread_work.joinable()) {
        thread_work.join();
    }
}

void client_portal_state::fn_thread_work()
{
    sweeper sweep = [&]() {
        if (sock != INVALID_SOCKET_VALUE) {
            close_socket(sock);
            sock = INVALID_SOCKET_VALUE;
        }
    };

    LOG_TRACE("Server portal connected: {}", connected_remote_endpoint.to_string());

    //
    // Send my identity
    //
    {
        g_client_identity.init();
        const int cnt = (int)send(sock, (char*)&g_client_identity, sizeof(identity_t), 0);
        if (cnt != (int)sizeof(identity_t)) {
            LOG_TRACE("Client portal: send() identity to peer {} expects {}, but returns {}. {}",
                      connected_remote_endpoint.to_string(), sizeof(identity_t), cnt, socket_error_description());
            return;
        }

        LOG_TRACE("Client portal: send identity to peer {}", connected_remote_endpoint.to_string());
    }

    //
    // Receive server channels
    //
}
