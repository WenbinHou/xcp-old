#include "common.h"

// TODO: refactor and remove using namespace
using namespace xcp;
using namespace infra;


//==============================================================================
// Global variables
//==============================================================================
extern xcp::xcp_program_options g_xcp_program_options;

extern std::shared_ptr<xcp::client_portal_state> g_client_portal;



//==============================================================================
// struct client_channel_state
//==============================================================================

void client_channel_state::dispose_impl() noexcept
{
    if (sock != INVALID_SOCKET_VALUE) {
        LOG_INFO("Close channel: {}", server_channel_sockaddr.to_string());
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    }

    if (thread_work.joinable()) {
        thread_work.join();
    }
}

bool client_channel_state::init()
{
    sock = socket(server_channel_sockaddr.family(), SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET_VALUE) {
        LOG_ERROR("Can't create socket for channel {}. {}", server_channel_sockaddr.to_string(), socket_error_description());
        return false;
    }

    sweeper sweep = [&]() {
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    };

    // Connect to specified endpoint
    if (connect(sock, server_channel_sockaddr.address(), server_channel_sockaddr.socklen()) != 0) {
        LOG_ERROR("Can't connect() to {}. {} (skipped)", server_channel_sockaddr.to_string(), socket_error_description());
        return false;
    }
    LOG_INFO("Connected to channel {}", server_channel_sockaddr.to_string());

    thread_work = std::thread([this]() {
        try {
            this->fn_thread_work();
        }
        catch(const std::exception& ex) {
            LOG_ERROR("fn_thread_work() exception: {}", ex.what());
            std::abort();
        }
    });

    sweep.suppress_sweep();
    return true;
}


void client_channel_state::fn_thread_work()
{
    sweeper sweep = [&]() {
        // Just dispose 
        this->portal.async_dispose(false);
    };

    //
    // Send identity
    //
    {
        const int cnt = (int)send(sock, (char*)&portal.client_identity, sizeof(identity_t), 0);
        if (cnt != (int)sizeof(identity_t)) {
            LOG_ERROR("Channel {}: send() identity to peer expects {}, but returns {}. {}",
                      server_channel_sockaddr.to_string(), sizeof(identity_t), cnt, socket_error_description());
            return;
        }

        LOG_TRACE("Channel {}: sent identity to peer", server_channel_sockaddr.to_string());
    }

    // TODO
}



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

void client_portal_state::dispose_impl() noexcept
{
    if (sock != INVALID_SOCKET_VALUE) {
        LOG_INFO("Close server portal {} (peer: {})", required_endpoint.to_string(), connected_remote_endpoint.to_string());
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    }

    if (thread_work.joinable()) {
        thread_work.join();
    }

    // Dispose all channels
    for (std::shared_ptr<client_channel_state>& chan : channels) {
        chan->dispose();
    }
    channels.clear();
}

void client_portal_state::fn_thread_work()
{
    sweeper sweep = [&]() {
        this->async_dispose(false);

        // Require exits
        sighandle::require_exit();
    };

    LOG_TRACE("Server portal connected: {}", connected_remote_endpoint.to_string());

    //
    // Send my identity
    //
    {
        const int cnt = (int)send(sock, (char*)&client_identity, sizeof(identity_t), 0);
        if (cnt != (int)sizeof(identity_t)) {
            LOG_ERROR("Client portal: send() identity to peer {} expects {}, but returns {}. {}",
                      connected_remote_endpoint.to_string(), sizeof(identity_t), cnt, socket_error_description());
            return;
        }

        LOG_TRACE("Client portal: send identity to peer {}", connected_remote_endpoint.to_string());
    }

    //
    // Send client request
    //
    {
        message_client_hello_request msg;
        msg.is_from_server_to_client = g_xcp_program_options.is_from_server_to_client;
        if (msg.is_from_server_to_client) {
            msg.server_path = g_xcp_program_options.arg_from_path.path;
            msg.client_file_name = stdfs::path(g_xcp_program_options.arg_to_path.path).filename().string();
        }
        else {  // from client to server
            msg.server_path = g_xcp_program_options.arg_to_path.path;
            msg.client_file_name = stdfs::path(g_xcp_program_options.arg_from_path.path).filename().string();
        }

        if (!message_send(sock, msg)) {
            LOG_ERROR("Client portal: Send message_client_hello_request failed");
            return;
        }

        LOG_TRACE("Client portal: send message_client_hello_request to peer {}", connected_remote_endpoint.to_string());
    }

    //
    // Receive server response (including channels)
    //
    std::vector<std::tuple<infra::tcp_sockaddr, size_t>> server_channels;
    {
        message_server_hello_response msg;
        if (!message_recv(sock, msg)) {
            LOG_ERROR("Client portal: receive message_server_hello_response failed");
            return;
        }

        if (msg.error_code != 0) {
            LOG_ERROR("Server responds error: {}. errno = {} ({})", msg.error_message, msg.error_code, strerror(msg.error_code));
            return;
        }

        server_channels = std::move(msg.server_channels);
    }

    // TODO: Prepare for local file!

    // Create channels
    {
        for (const auto& tuple : server_channels) {
            const infra::tcp_sockaddr& addr = std::get<0>(tuple);
            const size_t repeats = std::get<1>(tuple);

            infra::tcp_sockaddr chan_addr = this->connected_remote_endpoint;
            chan_addr.set_port(addr.port());  // don't use IP from 'addr'!
            LOG_DEBUG("Client portal: server channel {} (repeats: {})", chan_addr.to_string(), repeats);

            bool is_first = true;
            for (size_t i = 0; i < repeats; ++i) {
                if (!is_first) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));  // sleep 5 msec
                }
                is_first = false;

                std::shared_ptr<client_channel_state> chan = std::make_shared<client_channel_state>(*this, chan_addr);
                if (!chan->init()) {
                    LOG_ERROR("Client portal: client_channel_state({}) init() failed", chan_addr.to_string());
                    return;
                }

                {
                    std::unique_lock<std::shared_mutex> lock(channels_mutex);
                    if (this->is_dispose_required()) {  // unlikely
                        lock.unlock();
                        chan->dispose();
                        chan.reset();

                        this->async_dispose(false);
                        return;
                    }
                    else {
                        channels.emplace_back(chan);
                    }
                }
            }
        }
    }

    // TODO: Wait for file transportation done
}
