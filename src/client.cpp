#include "common.h"


//==============================================================================
// struct client_channel_state
//==============================================================================

void xcp::client_channel_state::dispose_impl() noexcept /*override*/
{
    if (sock != infra::INVALID_SOCKET_VALUE) {
        LOG_DEBUG("Close channel: {}", server_channel_sockaddr.to_string());
        infra::close_socket(sock);
        sock = infra::INVALID_SOCKET_VALUE;
    }

    if (thread_work.joinable()) {
        thread_work.join();
    }
}

bool xcp::client_channel_state::init()
{
    sock = socket(server_channel_sockaddr.family(), SOCK_STREAM, IPPROTO_TCP);
    if (sock == infra::INVALID_SOCKET_VALUE) {
        LOG_ERROR("Can't create socket for channel {}. {}", server_channel_sockaddr.to_string(), infra::socket_error_description());
        return false;
    }

    infra::sweeper sweep = [&]() {
        infra::close_socket(sock);
        sock = infra::INVALID_SOCKET_VALUE;
    };

    // Connect to specified endpoint
    if (connect(sock, server_channel_sockaddr.address(), server_channel_sockaddr.socklen()) != 0) {
        LOG_ERROR("Can't connect() to {}. {} (skipped)", server_channel_sockaddr.to_string(), infra::socket_error_description());
        return false;
    }
    LOG_DEBUG("Connected to channel {}", server_channel_sockaddr.to_string());

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


void xcp::client_channel_state::fn_thread_work()
{
    infra::sweeper exit_cleanup = [&]() {
        // Just dispose 
        this->portal.async_dispose(false);
    };

    //
    // Send identity
    //
    {
        const int cnt = (int)send(sock, (char*)&portal.client_identity, sizeof(infra::identity_t), 0);
        if (cnt != (int)sizeof(infra::identity_t)) {
            LOG_ERROR("Channel {}: send() identity to peer expects {}, but returns {}. {}",
                      server_channel_sockaddr.to_string(), sizeof(infra::identity_t), cnt, infra::socket_error_description());
            return;
        }

        LOG_TRACE("Channel {}: sent identity to peer", server_channel_sockaddr.to_string());
    }

    // Run transfer
    {
        ASSERT(portal.transfer != nullptr);
        const bool success = portal.transfer->invoke_channel(sock);
        if (!success) {
            LOG_ERROR("Channel {}: transfer failed", server_channel_sockaddr.to_string());
            portal.transfer_result_status = client_portal_state::TRANSFER_FAILED;
            return;
        }
    }

    exit_cleanup.suppress_sweep();
}



//==============================================================================
// struct client_portal_state
//==============================================================================

bool xcp::client_portal_state::init()
{
    //
    // Prepare file
    //
    try {
        if (program_options->is_from_server_to_client) {  // from server to client
            this->transfer = std::make_shared<transfer_destination>(
                stdfs::path(program_options->arg_from_path.path).filename().u8string(),
                program_options->arg_to_path.path);
        }
        else {  // from client to server
            ASSERT(program_options->arg_transfer_block_size.has_value());
            this->transfer = std::make_shared<transfer_source>(program_options->arg_from_path.path, program_options->arg_transfer_block_size.value());
        }
    }
    catch(const transfer_error& ex) {
        LOG_ERROR("Can't transfer file: {}", ex.error_message);
        return false;
    }


    bool connected = false;
    for (const infra::tcp_sockaddr& addr : program_options->server_portal.resolved_sockaddrs) {
        sock = socket(addr.family(), SOCK_STREAM, IPPROTO_TCP);
        if (sock == infra::INVALID_SOCKET_VALUE) {
            LOG_WARN("Can't create socket for portal {}. {} (skipped)", addr.to_string(), infra::socket_error_description());
            continue;
        }

        infra::sweeper sweep = [&]() {
            infra::close_socket(sock);
            sock = infra::INVALID_SOCKET_VALUE;
        };

        // Connect to specified endpoint
        if (connect(sock, addr.address(), addr.socklen()) != 0) {
            LOG_WARN("Can't connect() to {}. {} (skipped)", addr.to_string(), infra::socket_error_description());
            continue;
        }

        // Get remote endpoint
        connected_remote_endpoint = addr;  // actually only family is important
        socklen_t len = connected_remote_endpoint.socklen();
        if (getpeername(sock, connected_remote_endpoint.address(), &len) != 0) {
            LOG_ERROR("getpeername() failed. Can't get connected endpoint for {}. {} (skipped)", addr.to_string(), infra::socket_error_description());
            continue;
        }

        LOG_INFO("Connected to portal {} (peer: {})", program_options->server_portal.to_string(), connected_remote_endpoint.to_string());
        connected = true;
        sweep.suppress_sweep();
        break;
    }

    if (!connected) {
        LOG_ERROR("Can't create and connect to server portal endpoint {}", program_options->server_portal.to_string());
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

void xcp::client_portal_state::dispose_impl() noexcept
{
    if (sock != infra::INVALID_SOCKET_VALUE) {
        LOG_INFO("Close server portal {} (peer: {})", program_options->server_portal.to_string(), connected_remote_endpoint.to_string());
        infra::close_socket(sock);
        sock = infra::INVALID_SOCKET_VALUE;
    }

    if (thread_work.joinable()) {
        thread_work.join();
    }

    // Dispose all channels
    for (std::shared_ptr<client_channel_state>& chan : channels) {
        chan->dispose();
    }
    channels.clear();

    // Close transfer file handles (if any)
    if (this->transfer) {
        this->transfer->dispose();
        this->transfer.reset();
    }
}

void xcp::client_portal_state::fn_thread_work()
{
    infra::sweeper sweep = [&]() {
        this->async_dispose(false);

        // Require exits
        infra::sighandle::require_exit();
    };

    LOG_TRACE("Server portal connected: {}", connected_remote_endpoint.to_string());


    //
    // Send my identity
    //
    {
        const int cnt = (int)send(sock, (char*)&client_identity, sizeof(infra::identity_t), 0);
        if (cnt != (int)sizeof(infra::identity_t)) {
            LOG_ERROR("Client portal: send() identity to peer {} expects {}, but returns {}. {}",
                      connected_remote_endpoint.to_string(), sizeof(infra::identity_t), cnt, infra::socket_error_description());
            return;
        }

        LOG_TRACE("Client portal: send identity to peer {}", connected_remote_endpoint.to_string());
    }

    //
    // Send client request
    //
    {
        message_client_hello_request msg;
        msg.is_from_server_to_client = program_options->is_from_server_to_client;
        ASSERT(program_options->arg_transfer_block_size.has_value());
        msg.transfer_block_size = program_options->arg_transfer_block_size.value();
        if (msg.is_from_server_to_client) {
            msg.server_path = program_options->arg_from_path.path;
            msg.client_file_name = stdfs::path(program_options->arg_to_path.path).filename().string();
        }
        else {  // from client to server
            msg.server_path = program_options->arg_to_path.path;
            msg.client_file_name = stdfs::path(program_options->arg_from_path.path).filename().string();
            msg.file_info = this->transfer->get_file_info();
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
            LOG_ERROR("Server responds an error: {}. errno = {} ({})", msg.error_message, msg.error_code, strerror(msg.error_code));
            return;
        }

        server_channels = std::move(msg.server_channels);

        // Get total_channel_repeats_count
        size_t total_channel_repeats_count = 0;
        for (const auto& tuple : server_channels) {
            const size_t repeats = std::get<1>(tuple);
            total_channel_repeats_count += repeats;
        }
        LOG_TRACE("Client portal: server total_channel_repeats_count: {}", total_channel_repeats_count);

        // Init file size if from server to client
        if (program_options->is_from_server_to_client) {  // from server to client
            ASSERT(msg.file_info.has_value());
            try {
                std::dynamic_pointer_cast<transfer_destination>(this->transfer)->init_file(msg.file_info.value(), total_channel_repeats_count);
            }
            catch(const transfer_error& ex) {
                LOG_ERROR("Transfer error: {}. errno = {} ({})", ex.error_message, ex.error_code, strerror(ex.error_code));
                return;
            }
        }
        else {  // from client to server
            ASSERT(!msg.file_info.has_value());
        }
    }


    // Create channels
    {
        for (const auto& tuple : server_channels) {
            const infra::tcp_sockaddr& addr = std::get<0>(tuple);
            const size_t repeats = std::get<1>(tuple);

            infra::tcp_sockaddr chan_addr { };
            if (addr.is_addr_any()) {
                chan_addr = this->connected_remote_endpoint;  // don't use IP from 'addr', as it is "any"
                chan_addr.set_port(addr.port());
            }
            else {
                chan_addr = addr;  // use IP and port from 'addr'
            }
            LOG_DEBUG("Client portal: server channel {} (repeats: {})", chan_addr.to_string(), repeats);

            size_t cnt = 0;
            for (size_t i = 0; i < repeats; ++i) {
                if (++cnt >= 16) {  // don't sleep on first 16 connections
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));  // sleep 5 msec
                }

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


    //
    // Wait for server ready_to_transfer
    //
    {
        message_server_ready_to_transfer msg;
        if (!message_recv(sock, msg)) {
            LOG_ERROR("Client portal: receive message_server_ready_to_transfer failed");
            return;
        }

        if (msg.error_code != 0) {
            LOG_ERROR("Server responds error: {}. errno = {} ({})", msg.error_message, msg.error_code, strerror(msg.error_code));
            return;
        }
        LOG_DEBUG("Client portal: server is ready to transfer");
    }

    // Run transfer
    {
        ASSERT(this->transfer != nullptr);
        const bool success = this->transfer->invoke_portal(sock);
        if (!success) {
            LOG_DEBUG("Client portal: transfer failed");
            transfer_result_status = TRANSFER_FAILED;
        }
        else {
            int expected = client_portal_state::TRANSFER_UNKNOWN;
            if (transfer_result_status.compare_exchange_strong(expected, client_portal_state::TRANSFER_SUCCEEDED)) {
                LOG_DEBUG("Client transfer done successfully");
            }
            else {
                LOG_DEBUG("Client portal: transfer failed");
            };
        }
    }

    // sweep dtor do cleanups...
}
