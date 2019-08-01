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
    // Send greeting magic, role and identity
    //
    {
        // Offset 0-3: GREETING_MAGIC_1
        // Offset 4-7: GREETING_MAGIC_1
        // Offset 8-11: ROLE
        // Offset 12-27: identity
        char header[28];

        // GREETING_MAGIC_1
        const uint32_t magic1_be = htonl(portal_protocol::GREETING_MAGIC_1);
        *(uint32_t*)((uintptr_t)header + 0) = magic1_be;

        // GREETING_MAGIC_2
        const uint32_t magic2_be = htonl(portal_protocol::GREETING_MAGIC_2);
        *(uint32_t*)((uintptr_t)header + 4) = magic2_be;

        // ROLE
        const uint32_t role_be = htonl(portal_protocol::role::ROLE_CHANNEL);
        *(uint32_t*)((uintptr_t)header + 8) = role_be;

        // identity
        static_assert(sizeof(infra::identity_t) == 16);
        *(infra::identity_t*)((uintptr_t)header + 12) = portal.client_identity;


        if (!sock->connect_and_send(server_channel_sockaddr, header, sizeof(header), nullptr, nullptr)) {
            LOG_ERROR("os_socket_t connect_and_send() failed for channel {}", server_channel_sockaddr.to_string());
            return;
        }
        LOG_DEBUG("Connected and sent identity to channel {}", server_channel_sockaddr.to_string());
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


    //
    // Prepare first packet to send with TCP_FASTOPEN
    //
    // Offset 0-3: GREETING_MAGIC_1
    // Offset 4-7: GREETING_MAGIC_1
    // Offset 8-11: ROLE
    // Offset 12-27: identity
    // Offset 28-29: min supported protocol version
    // Offset 30-31: max supported protocol version
    char header[32];
    {
        // GREETING_MAGIC_1
        const uint32_t magic1_be = htonl(portal_protocol::GREETING_MAGIC_1);
        *(uint32_t*)((uintptr_t)header + 0) = magic1_be;

        // GREETING_MAGIC_2
        const uint32_t magic2_be = htonl(portal_protocol::GREETING_MAGIC_2);
        *(uint32_t*)((uintptr_t)header + 4) = magic2_be;

        // ROLE
        const uint32_t role_be = htonl(portal_protocol::role::ROLE_PORTAL);
        *(uint32_t*)((uintptr_t)header + 8) = role_be;

        // identity
        static_assert(sizeof(infra::identity_t) == 16);
        *(infra::identity_t*)((uintptr_t)header + 12) = client_identity;

        // min_ver, max_ver
        static constexpr const uint16_t CLIENT_MIN_PROTOCOL_VERSION = portal_protocol::version::V1;
        static constexpr const uint16_t CLIENT_MAX_PROTOCOL_VERSION = portal_protocol::version::V1;
        static_assert(CLIENT_MIN_PROTOCOL_VERSION <= CLIENT_MAX_PROTOCOL_VERSION);

        const uint16_t min_ver_be = htons(CLIENT_MIN_PROTOCOL_VERSION);
        *(uint16_t*)((uintptr_t)header + 28) = min_ver_be;

        const uint16_t max_ver_be = htons(CLIENT_MAX_PROTOCOL_VERSION);
        *(uint16_t*)((uintptr_t)header + 30) = max_ver_be;
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

        // Connect to specified endpoint, and send header
        if (!sock->connect_and_send(addr, header, sizeof(header), nullptr, &connected_remote_endpoint)) {
            LOG_WARN("Client portal: connect_and_send() to {} failed (skipped)", addr.to_string());
            continue;
        }

        LOG_DEBUG("Client portal: Connected to portal {} (peer: {}). Now negotiate protocol version",
                  program_options->server_portal.to_string(), connected_remote_endpoint.to_string());
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

    // Close transfer file handles (if any)
    if (this->transfer) {
        this->transfer->dispose();
        this->transfer.reset();
    }

    // Let connected channels not block
    if (gate_client_portal_ready.initialized()) {
        gate_client_portal_ready.force_signal_all();
    }

    // Wait for portal thread done
    if (thread_work.joinable()) {
        thread_work.join();
    }

    // Dispose all channels
    {
        std::unique_lock<std::shared_mutex> lock(rundown.acquire_rundown());
        ASSERT(rundown.required_rundown());

        for (std::shared_ptr<client_channel_state>& chan : channels) {
            chan->dispose();
        }
        channels.clear();
    }
}

void xcp::client_portal_state::fn_thread_work()
{
    const infra::sweeper sweep = [&]() {
        this->async_dispose(false);

        // Require exits
        infra::sighandle::require_exit();
    };

    //
    // We have connected and sent header...
    //
    LOG_TRACE("Server portal connected: {}", connected_remote_endpoint.to_string());


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
                    std::unique_lock<std::shared_mutex> lock(rundown.acquire_unique());
                    if (!lock.owns_lock()) {  // unlikely
                        ASSERT(rundown.required_rundown());

                        chan->dispose();
                        chan.reset();

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
    // Setup report_progress_callback
    //
    _report_progress_context.busy = false;
    _report_progress_context.first_msec = 0;  // to be filled later
    _report_progress_context.first_transferred = 0;  // to be filled later
    _report_progress_context.last_msec = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())).count();;

    this->transfer->report_progress_callback = [this](const uint64_t transferred, const uint64_t total) mutable {
        bool expected_busy = false;
        if (!_report_progress_context.busy.compare_exchange_strong(expected_busy, true)) return;  // skip this one

        do {
            const uint64_t now_msec = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())).count();
            if (_report_progress_context.first_msec == 0) {
                if ((int64_t)(now_msec - _report_progress_context.last_msec) < 3000) {  // skip first 3 seconds
                    break;
                }
                _report_progress_context.first_msec = now_msec;
                _report_progress_context.first_transferred = transferred;
            }
            else {
                if ((int64_t)(now_msec - _report_progress_context.last_msec) < 1000) {  // too frequent!
                    break;
                }
                LOG_INFO("... {:.3f} MB / {:.3f} MB ({:.3f} MB/s)",
                         (double)transferred / (double)1048576,
                         (double)total / (double)1048576,
                         (double)((transferred - _report_progress_context.first_transferred) * 1000) / (double)((now_msec - _report_progress_context.first_msec) * 1048576));
            }
            _report_progress_context.last_msec = now_msec;
        } while(false);

        _report_progress_context.busy = false;
    };

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
