#include "common.h"


//==============================================================================
// struct client_channel_state
//==============================================================================

void xcp::client_channel_state::dispose_impl() noexcept /*override*/
{
    if (sock) {
        sock->dispose();
        sock.reset();
    }

    if (thread_work.joinable()) {
        thread_work.join();
    }
    LOG_DEBUG("Close channel {} done", server_channel_sockaddr.to_string());
}

bool xcp::client_channel_state::init()
{
    thread_work = std::thread([this]() {
        try {
            this->fn_thread_work();
        }
        catch(const std::exception& ex) {
            PANIC_TERMINATE("fn_thread_work() exception: {}", ex.what());
        }
    });

    return true;
}


void xcp::client_channel_state::fn_thread_work()
{
    infra::sweeper error_cleanup = [&]() {
        // Just dispose 
        this->portal.async_dispose(false);
    };

    //
    // Create socket
    //
    {
        sock = std::make_shared<infra::os_socket_t>();
        if (!sock->init_tcp(server_channel_sockaddr.family())) {
            LOG_ERROR("os_socket_t init_tcp() failed for channel {}", server_channel_sockaddr.to_string());
            return;
        }
    }

    //
    // Connect to specified channel endpoint
    //
    {
        if (!sock->connect(server_channel_sockaddr, nullptr, nullptr)) {
            LOG_ERROR("os_socket_t connect() failed for channel {}", server_channel_sockaddr.to_string());
            return;
        }
        LOG_DEBUG("Connected to channel {}", server_channel_sockaddr.to_string());
    }

    //
    // Send greeting magic, role and identity
    //
    {
        infra::socket_io_vec vec[4];

        // GREETING_MAGIC_1
        const uint32_t magic1_be = htonl(portal_protocol::GREETING_MAGIC_1);
        vec[0].ptr = &magic1_be;
        vec[0].len = sizeof(magic1_be);

        // GREETING_MAGIC_2
        const uint32_t magic2_be = htonl(portal_protocol::GREETING_MAGIC_2);
        vec[1].ptr = &magic2_be;
        vec[1].len = sizeof(magic2_be);

        // ROLE
        const uint32_t role_be = htonl(portal_protocol::role::ROLE_CHANNEL);
        vec[2].ptr = &role_be;
        vec[2].len = sizeof(role_be);

        // identity
        vec[3].ptr = &portal.client_identity;
        vec[3].len = sizeof(portal.client_identity);

        if (!sock->sendv(vec)) {
            LOG_ERROR("Channel {}: os_socket_t sendv() greeting magic, role and identity to peer failed",
                      server_channel_sockaddr.to_string());
            return;
        }

        LOG_TRACE("Channel {}: sent identity to peer", server_channel_sockaddr.to_string());
    }

    //
    // Wait for client portal ready to transfer
    //
    {
        portal.gate_client_portal_ready.wait();
        if (portal.is_dispose_required()) {  // unlikely
            LOG_DEBUG("Channel {}: dispose() required", server_channel_sockaddr.to_string());
            return;
        }
    }

    //
    // Run transfer
    //
    {
        LOG_DEBUG("Channel {}: now transfer...", server_channel_sockaddr.to_string());

        ASSERT(portal.transfer != nullptr);
        const bool success = portal.transfer->invoke_channel(sock);
        if (!success) {
            LOG_ERROR("Channel {}: transfer failed", server_channel_sockaddr.to_string());
            portal.transfer_result_status = client_portal_state::TRANSFER_FAILED;
            return;
        }
    }

    error_cleanup.suppress_sweep();
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
            this->transfer = std::make_shared<transfer_destination>(program_options->arg_to_path.path);
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
        sock = std::make_shared<infra::os_socket_t>();

        infra::sweeper error_cleanup = [&]() {
            sock->dispose();
            sock.reset();
        };

        // Init socket
        if (!sock->init_tcp(addr.family())) {
            LOG_WARN("Client portal: socket init_tcp() (skipped)", addr.to_string());
            continue;
        }

        // Connect to specified endpoint
        if (!sock->connect(addr, nullptr, &connected_remote_endpoint)) {
            LOG_WARN("Can't connect() to {} (skipped)", addr.to_string());
            continue;
        }

        LOG_INFO("Connected to portal {} (peer: {})", program_options->server_portal.to_string(), connected_remote_endpoint.to_string());
        connected = true;

        error_cleanup.suppress_sweep();
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
            PANIC_TERMINATE("fn_thread_work() exception: {}", ex.what());
        }
    });

    return true;
}

void xcp::client_portal_state::dispose_impl() noexcept
{
    if (sock) {
        LOG_INFO("Close server portal {} (peer: {})", program_options->server_portal.to_string(), connected_remote_endpoint.to_string());
        sock->dispose();
        sock.reset();
    }

    // Let connected channels not block
    gate_client_portal_ready.force_signal_all();

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
    const infra::sweeper sweep = [&]() {
        this->async_dispose(false);

        // Require exits
        infra::sighandle::require_exit();
    };

    LOG_TRACE("Server portal connected: {}", connected_remote_endpoint.to_string());

    //
    // Send greeting magic, role, identity, and protocol versions
    //
    {
        infra::socket_io_vec vec[6];

        // GREETING_MAGIC_1
        const uint32_t magic1_be = htonl(portal_protocol::GREETING_MAGIC_1);
        vec[0].ptr = &magic1_be;
        vec[0].len = sizeof(magic1_be);

        // GREETING_MAGIC_2
        const uint32_t magic2_be = htonl(portal_protocol::GREETING_MAGIC_2);
        vec[1].ptr = &magic2_be;
        vec[1].len = sizeof(magic2_be);

        // ROLE
        const uint32_t role_be = htonl(portal_protocol::role::ROLE_PORTAL);
        vec[2].ptr = &role_be;
        vec[2].len = sizeof(role_be);

        // identity
        vec[3].ptr = &client_identity;
        vec[3].len = sizeof(client_identity);

        // protocol version
        // Currently we only supports version V1
        static constexpr const uint16_t CLIENT_MIN_PROTOCOL_VERSION = portal_protocol::version::V1;
        static constexpr const uint16_t CLIENT_MAX_PROTOCOL_VERSION = portal_protocol::version::V1;
        static_assert(CLIENT_MIN_PROTOCOL_VERSION <= CLIENT_MAX_PROTOCOL_VERSION);

        const uint16_t min_ver_be = htons(CLIENT_MIN_PROTOCOL_VERSION);
        vec[4].ptr = &min_ver_be;
        vec[4].len = sizeof(uint16_t);

        const uint16_t max_ver_be = htons(CLIENT_MAX_PROTOCOL_VERSION);
        vec[5].ptr = &max_ver_be;
        vec[5].len = sizeof(uint16_t);

        if (!sock->sendv(vec)) {
            LOG_ERROR("Client portal: sendv() greeting magic, role, identity and protocol version to peer {} failed",
                      connected_remote_endpoint.to_string());
            return;
        }

        LOG_TRACE("Client portal: negotiate protocol version with peer {}", connected_remote_endpoint.to_string());
    }

    //
    // Receive negotiated protocol version (decided by server)
    //
    {
        uint16_t ver_be = portal_protocol::version::INVALID;
        if (!sock->recv(&ver_be, sizeof(uint16_t))) {
            LOG_ERROR("Client portal: receive negotiated protocol version from peer {} failed",
                      connected_remote_endpoint.to_string());
            return;
        }

        protocol_version = ntohs(ver_be);
        if (protocol_version == portal_protocol::version::INVALID) {
            LOG_ERROR("Client portal: negotiate protocol version with peer {} failed. "
                      "The server might be either too old or too new",
                      connected_remote_endpoint.to_string());
            return;
        }

        LOG_DEBUG("Client portal: negotiated protocol version: {}", protocol_version);
    }

    // Currently we only supports version V1
    ASSERT(protocol_version == portal_protocol::version::V1);


    //
    // Receive server information
    //
    std::vector<std::tuple<infra::tcp_sockaddr, size_t>> server_channels;
    {
        message_server_information msg;
        if (!message_recv(sock, msg)) {
            LOG_ERROR("Client portal: receive message_server_transfer_response failed");
            return;
        }

        server_channels = std::move(msg.server_channels);
    }


    //
    // Create channels (and connect)
    //
    size_t total_channel_repeats_count = 0;
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
            total_channel_repeats_count += repeats;

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
        LOG_TRACE("Client portal: server total_channel_repeats_count: {}", total_channel_repeats_count);
    }


    //
    // Send client transfer request
    //
    {
        message_client_transfer_request msg;
        msg.is_from_server_to_client = program_options->is_from_server_to_client;
        ASSERT(program_options->arg_transfer_block_size.has_value());
        msg.transfer_block_size = program_options->arg_transfer_block_size.value();
        if (msg.is_from_server_to_client) {
            msg.server_path = program_options->arg_from_path.path;
        }
        else {  // from client to server
            msg.server_path = program_options->arg_to_path.path;
            msg.file_info = this->transfer->get_file_info();
        }

        if (!message_send(sock, msg)) {
            LOG_ERROR("Client portal: send message_client_transfer_request failed");
            return;
        }

        LOG_TRACE("Client portal: sent message_client_transfer_request to peer {}", connected_remote_endpoint.to_string());
    }


    //
    // Receive server transfer response
    //
    {
        message_server_transfer_response msg;
        if (!message_recv(sock, msg)) {
            LOG_ERROR("Client portal: receive message_server_transfer_response failed");
            return;
        }

        if (msg.error_code != 0) {
            LOG_ERROR("Server responds an error: {}. errno = {} ({})", msg.error_message, msg.error_code, strerror(msg.error_code));
            return;
        }

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


    //
    // Signals that client portal is ready
    //
    gate_client_portal_ready.signal();


    //
    // Run transfer
    //
    {
        LOG_DEBUG("Client portal: now transfer...");

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
            }
        }
    }

    // sweep dtor do cleanups...
}
