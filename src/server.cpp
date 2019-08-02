#include "common.h"

/** 

Server & client protocol version negotiation on portal:

    Step_0:
        C: Send: magic(8B) role(4B) identity(16B) min_ver(2B) max_ver(2B)
        S: Send: ver(2B)


After that...
Protocol V1:

    Step_0 (cont.):
        S: send message_server_information (including channels)

    Step_1:
        C: receive message_server_information
        C: (async) connect server channels
        C: send message_client_transfer_request

    Step_2:
        S: receive message_client_transfer_request
        S: If successful, wait for all channels connected
        S: send back message_server_transfer_response
        S: call into transfer invoke_portal and invoke_channel

    Step_3:
        C: receive message_server_transfer_response
        C: call into transfer invoke_portal and invoke_channel
*/



//==============================================================================
// struct client_instance
//==============================================================================

void xcp::client_instance::fn_portal()
{
    const infra::sweeper exit_cleanup = [this]() {
        this->async_dispose(false);
    };

    // NOTE: client identity has been received


    //
    // Receive client supported protocol version
    //
    {
        char buffer[4];
        if (!accepted_portal_socket->recv(buffer, sizeof(buffer))) {
            LOG_ERROR("Client #{} portal: receive client supported protocol version failed", this->id);
            return;
        }

        const uint16_t min_ver = ntohs(*(uint16_t*)&buffer[0]);
        const uint16_t max_ver = ntohs(*(uint16_t*)&buffer[2]);

        if (min_ver > max_ver) {
            LOG_ERROR("Client #{} portal: invalid protocol version range: min={}, max={} (min is larger than max)",
                      this->id, min_ver, max_ver);
            return;
        }

        // Currently we only supports version V1
        static constexpr const uint16_t SERVER_MIN_PROTOCOL_VERSION = portal_protocol::version::V1;
        static constexpr const uint16_t SERVER_MAX_PROTOCOL_VERSION = portal_protocol::version::V1;
        static_assert(SERVER_MIN_PROTOCOL_VERSION <= SERVER_MAX_PROTOCOL_VERSION);

        if (min_ver > SERVER_MAX_PROTOCOL_VERSION) {
            this->protocol_version = portal_protocol::version::INVALID;
            LOG_ERROR("Client #{} portal: client protocol version is too high: [{}, {}], but server only supports max={}",
                      this->id, min_ver, max_ver, SERVER_MAX_PROTOCOL_VERSION);
        }
        else if (max_ver < SERVER_MIN_PROTOCOL_VERSION) {
            this->protocol_version = portal_protocol::version::INVALID;
            LOG_ERROR("Client #{} portal: client protocol version is too low: [{}, {}], but server only supports min={}",
                      this->id, min_ver, max_ver, SERVER_MIN_PROTOCOL_VERSION);
        }
        else {
            // Use the highest protocol version
            this->protocol_version = std::min<uint16_t>(SERVER_MAX_PROTOCOL_VERSION, max_ver);
            LOG_DEBUG("Client #{} portal: client protocol version range: [{}, {}], server protocol version range: [{}, {}], finally use {}",
                      this->id, min_ver, max_ver, SERVER_MIN_PROTOCOL_VERSION, SERVER_MAX_PROTOCOL_VERSION, this->protocol_version);
        }
    }


    //
    // Send chosen protocol version, and go on if negotiation succeeds
    //
    {
        uint16_t use_protocol_version_be = htons(this->protocol_version);
        if (!accepted_portal_socket->send(&use_protocol_version_be, sizeof(uint16_t))) {
            LOG_ERROR("Client #{} portal: send protocol version ({}) failed", this->id, this->protocol_version);
            return;
        }

        if (this->protocol_version == portal_protocol::version::INVALID) {
            LOG_ERROR("Client #{} portal: protocol version negotiation failed", this->id);
            return;
        }
    }


    //
    // Send server information (like channels)
    //
    {
        message_server_information msg;

        // Tell client: server channels
        if (server_portal.program_options->arg_portal->repeats.has_value()) {
            // Reuse portal as channel
            ASSERT(server_portal.program_options->arg_portal->repeats.value() > 0);
            msg.server_channels.emplace_back(
                server_portal.bound_local_endpoint,
                server_portal.program_options->arg_portal->repeats.value());
        }
        else {
            // No reuse portal as channel
            ASSERT(!server_portal.channels.empty());
        }

        for (const std::shared_ptr<server_channel_state>& chan : server_portal.channels) {
            ASSERT(chan->required_endpoint.repeats.value() > 0);
            msg.server_channels.emplace_back(chan->bound_local_endpoint, chan->required_endpoint.repeats.value());
        }

        if (!message_send(accepted_portal_socket, msg)) {
            LOG_ERROR("Client #{} portal: send message_server_information failed", this->id);
            return;
        }

        LOG_TRACE("Client #{} portal: sent message_server_information", this->id);
    }


    //
    // Receive message_client_transfer_request
    //
    struct {
        bool is_from_server_to_client { };
        std::string server_path;
        uint64_t transfer_block_size;
        std::optional<basic_transfer_info> transfer_info;
        bool is_recursive;
        infra::user_name_t user;
    } transfer_request;
    {
        message_client_transfer_request msg;
        if (!message_recv(accepted_portal_socket, msg)) {
            LOG_ERROR("Client #{} portal: receive message_client_transfer_request failed", this->id);
            return;
        }

        LOG_TRACE("Client #{} portal: request: is_from_server_to_client={}, server_path={}, user={}",
                  this->id, msg.is_from_server_to_client, msg.server_path, msg.user.to_string());

        transfer_request.is_from_server_to_client = msg.is_from_server_to_client;
        transfer_request.server_path = std::move(msg.server_path);
        transfer_request.transfer_block_size = msg.transfer_block_size;
        transfer_request.is_recursive = msg.is_recursive;
        transfer_request.user = std::move(msg.user);
        transfer_request.transfer_info = std::move(msg.transfer_info);
    }


    //
    // Initialize transfer
    //   - If successful, wait for all channels connected
    // Send back message_server_transfer_response to client
    //
    {
        message_server_transfer_response msg;
        msg.error_code = 0;

        // Check against relative path on server side
        if (stdfs::path(transfer_request.server_path).is_relative()) {
            // Get home directory for requested user
            stdfs::path home_path = infra::get_user_home_path(transfer_request.user);
            if (home_path.empty()) {
                msg.error_code = EINVAL;
                msg.error_message = "Server side path is relative, but home path for requested user " + transfer_request.user.to_string() +
                                    " is not found";
                LOG_ERROR("Client #{} portal: {}", this->id, msg.error_message);
            }
            else if (stdfs::status(home_path).type() != stdfs::file_type::directory) {
                msg.error_code = EINVAL;
                msg.error_message = "Server side path is relative, but home path for requested user " + transfer_request.user.to_string() +
                                    " is not found or not a directory: " + home_path.u8string();
                LOG_ERROR("Client #{} portal: {}", this->id, msg.error_message);
            }
            else {  // found home path
                std::string tmp = transfer_request.server_path;
                if (tmp.size() >= 2 && tmp[0] == '~' && (tmp[1] == '/' || tmp[1] == '\\')) {
                    tmp = tmp.substr(2);
                }
                else if (tmp.size() == 1 && tmp[0] == '~') {
                    tmp = "";
                }
                std::string new_server_path = (home_path / tmp).u8string();
                LOG_DEBUG("Client #{} portal: server side path {} resolves to {}",
                          this->id, transfer_request.server_path, new_server_path);
                transfer_request.server_path = new_server_path;
            }
        }

        if (msg.error_code == 0) {
            try {
                if (transfer_request.is_from_server_to_client) {  // from server to client
                    ASSERT(!transfer_request.transfer_info.has_value());
                    this->transfer = std::make_shared<transfer_source>(
                        transfer_request.server_path,
                        transfer_request.transfer_block_size,
                        transfer_request.is_recursive);

                    msg.transfer_info = this->transfer->get_transfer_info();
                }
                else {  // from client to server
                    ASSERT(transfer_request.transfer_info.has_value());
                    this->transfer = std::make_shared<transfer_destination>(transfer_request.server_path);
                    std::dynamic_pointer_cast<transfer_destination>(this->transfer)->init_transfer_info(
                        transfer_request.transfer_info.value(),
                        server_portal.program_options->total_channel_repeats_count);
                }
            }
            catch(const transfer_error& ex) {
                msg.error_code = ex.error_code;
                msg.error_message = ex.error_message;
                LOG_ERROR("Client #{} portal: {}", this->id, msg.error_message);
            }
        }

        // If successful, wait for all channels connected
        if (msg.error_code == 0) {
            gate_all_channel_repeats_connected.wait();
            if (is_dispose_required()) {  // unlikely
                LOG_TRACE("Client #{} portal: dispose() required", this->id);
                return;
            }
        }

        if (!message_send(accepted_portal_socket, msg)) {
            LOG_ERROR("Client #{} portal: send message_server_transfer_response failed", this->id);
            return;
        }

        if (msg.error_code == 0) {
            LOG_TRACE("Client #{} portal: sent message_server_transfer_response", this->id);
        }
        else {
            LOG_ERROR("Client #{} portal: sent message_server_transfer_response, error_code={}, error_message={}",
                      this->id, msg.error_code, msg.error_message);
            return;
        }
    }


    //
    // Signals portal is ready to transfer
    //
    gate_portal_ready_to_transfer.signal();


    //
    // Run transfer
    //
    {
        const bool success = this->transfer->invoke_portal(accepted_portal_socket);
        if (!success) {
            LOG_ERROR("Client #{} portal: transfer failed", this->id);
        }
    }

    // exit_cleanup dtor do cleanups...
}

void xcp::client_instance::fn_channel(std::shared_ptr<infra::os_socket_t> accepted_channel_socket)
{
    infra::sweeper error_cleanup = [&]() {
        this->async_dispose(false);
    };

    // Signal one channel connection is established
    gate_all_channel_repeats_connected.signal();

    // Wait for portal to be ready to transfer
    gate_portal_ready_to_transfer.wait();
    if (is_dispose_required()) {  // unlikely
        LOG_DEBUG("Client #{} channel: dispose() required", this->id);
        return;
    }
    LOG_TRACE("Client #{} channel: channel initialization done", this->id);

    // Run transfer
    {
        ASSERT(this->transfer != nullptr);
        const bool success = this->transfer->invoke_channel(std::move(accepted_channel_socket));
        if (!success) {
            LOG_ERROR("Client #{} channel: transfer failed", this->id);
            return;
        }
    }

    error_cleanup.suppress_sweep();
}

void xcp::client_instance::dispose_impl() noexcept /*override*/
{
    // Wait for all portal thread to exit...
    if (accepted_portal_socket) {
        accepted_portal_socket->dispose();
        accepted_portal_socket.reset();
    }

    // gate_all_channel_repeats_connected: allow portal thread to go on
    if (gate_all_channel_repeats_connected.initialized()) {
        gate_all_channel_repeats_connected.force_signal_all();
    }

    // gate_portal_ready_to_transfer: allow channel threads to go on
    if (gate_portal_ready_to_transfer.initialized()) {
        gate_portal_ready_to_transfer.force_signal_all();
    }

    // Close transfer file handles (if any)
    if (this->transfer) {
        this->transfer->dispose();
        this->transfer.reset();
    }

    // Wait for portal thread done
    if (portal_thread) {
        ASSERT(portal_thread->joinable());
        portal_thread->join();
        portal_thread.reset();
    }

    // Wait for all channel threads to exit...
    {
        std::unique_lock<std::shared_mutex> lock(_rundown.acquire_rundown());
        ASSERT(_rundown.required_rundown());

        for (std::pair<std::shared_ptr<infra::os_socket_t>, std::shared_ptr<std::thread>>& pair : channel_threads) {
            if (pair.first) {
                pair.first->dispose();
                pair.first.reset();
            }

            if (pair.second) {
                ASSERT(pair.second->joinable());
                pair.second->join();
                pair.second.reset();
            }
        }
        channel_threads.clear();
    }


    // Remove itself from server_portal_state.clients
    //
    // If we found server portal is disposing, do not need to do anything
    // As server portal dispose() will clear all living clients
    {
        if (!server_portal.is_dispose_required()) {
            const infra::identity_t identity = this->client_identity;
            const uint64_t client_id = this->id;
            server_portal_state& portal = this->server_portal;

            // TODO: use thread pool?
            std::thread thr([identity, client_id, &portal]() {
                if (!portal.is_dispose_required()) {
                    std::unique_lock<std::shared_mutex> lock(portal.clients_mutex);
                    portal.clients.erase(identity);
                    LOG_DEBUG("Remove client #{}", client_id);
                }
            });
            thr.detach();
        }
    }
}



//==============================================================================
// struct server_channel_state
//==============================================================================

bool xcp::server_channel_state::init()
{
    ASSERT(!required_endpoint.resolved_sockaddrs.empty());

    bool bound = false;
    for (const infra::tcp_sockaddr& addr : required_endpoint.resolved_sockaddrs) {
        sock = std::make_shared<infra::os_socket_t>();

        infra::sweeper error_cleanup = [&]() {
            sock->dispose();
            sock.reset();
        };

        // Init socket
        if (!sock->init_tcp(addr.family())) {
            LOG_WARN("Server channel: socket init_tcp() (skipped)", addr.to_string());
            continue;
        }

        // Bind to specified endpoint
        if (!sock->bind(addr, &bound_local_endpoint)) {
            LOG_WARN("Server channel: can't bind to {} (skipped)", addr.to_string());
            continue;
        }

        // Start listen()
        if (!sock->listen(256)) {
            LOG_ERROR("Server channel: listen() on {} failed (skipped)", addr.to_string());
            continue;
        }

        LOG_INFO("Server channel {} binds to {}", required_endpoint.to_string(), bound_local_endpoint.to_string());
        bound = true;
        error_cleanup.suppress_sweep();
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
            PANIC_TERMINATE("fn_thread_listener() exception: {}", ex.what());
        }
    });

    return true;
}

void xcp::server_channel_state::dispose_impl() noexcept /*override*/
{
    if (sock) {
        LOG_INFO("Close server channel {} bound to {}", required_endpoint.to_string(), bound_local_endpoint.to_string());
        sock->dispose();
        sock.reset();
    }

    if (thread_accept.joinable()) {
        thread_accept.join();
    }
}

void xcp::server_channel_state::fn_thread_accept()
{
    LOG_TRACE("Server channel accepting: {}", bound_local_endpoint.to_string());

    while (true) {
        infra::tcp_sockaddr peer_addr { };
        std::shared_ptr<infra::os_socket_t> accepted_sock = sock->accept(&peer_addr);
        if (!accepted_sock) {
            if (infra::sighandle::is_exit_required()) {
                LOG_DEBUG("Exit requried. Stop accept() on server channel {}", bound_local_endpoint.to_string());
                break;
            }
            else {
                LOG_ERROR("Channel: accept() on {} failed", bound_local_endpoint.to_string());
                continue;
            }
        }
        LOG_TRACE("Server channel accepted a new socket from {}", peer_addr.to_string());


        // Create a separated thread to handle the channel
        launch_thread([this, accepted_sock, peer_addr](std::thread* const thr) {
            try {
                this->portal.fn_thread_unified_accepted_socket(
                    thr,
                    accepted_sock,
                    peer_addr,
                    false,
                    true);
            }
            catch(const std::exception& ex) {
                LOG_ERROR("fn_thread_unified_accepted_socket() exception: {}", ex.what());
                // Don't PANIC_TERMINATE
            }
        });
    }
}



//==============================================================================
// struct server_portal_state
//==============================================================================

bool xcp::server_portal_state::init()
{
    //
    // Initialize channels
    //
    {
        for (infra::tcp_endpoint& ep : program_options->arg_channels) {
            std::shared_ptr<xcp::server_channel_state> chan = std::make_shared<xcp::server_channel_state>(*this, ep);
            if (!chan->init()) {
                LOG_ERROR("Init server_channel_state failed for {}", ep.to_string());

                chan->dispose();
                chan.reset();
                return false;
            }
            channels.emplace_back(std::move(chan));
        }
    }


    //
    // Initialize portal
    //
    bool bound = false;
    for (const infra::tcp_sockaddr& addr : program_options->arg_portal->resolved_sockaddrs) {
        sock = std::make_shared<infra::os_socket_t>();

        infra::sweeper error_cleanup = [&]() {
            sock->dispose();
            sock.reset();
        };

        // Init socket
        if (!sock->init_tcp(addr.family())) {
            LOG_WARN("Portal: socket init_tcp() failed (skipped)");
            continue;
        }

        // Bind to specified endpoint
        if (!sock->bind(addr, &bound_local_endpoint)) {
            LOG_WARN("Portal: can't bind to {} (skipped)", addr.to_string());
            continue;
        }

        // Start listen()
        if (!sock->listen(256)) {
            LOG_ERROR("Portal: listen() on {} failed (skipped)", addr.to_string());
            continue;
        }

        LOG_INFO("Server portal {} binds to {}", program_options->arg_portal->to_string(), bound_local_endpoint.to_string());
        bound = true;
        error_cleanup.suppress_sweep();
        break;
    }

    if (!bound) {
        LOG_ERROR("Can't create and bind server portal endpoint {}", program_options->arg_portal->to_string());
        return false;
    }


    //
    // Start a thread to accept on portal
    //
    thread_accept = std::thread([&]() {
        try {
            this->fn_thread_accept();
        }
        catch(const std::exception& ex) {
            PANIC_TERMINATE("fn_thread_listener() exception: {}", ex.what());
        }
    });

    return true;
}

void xcp::server_portal_state::dispose_impl() noexcept /*override*/
{
    // Stop server portal socket
    if (sock) {
        LOG_INFO("Close server portal {} bound to {}", program_options->arg_portal->to_string(), bound_local_endpoint.to_string());
        sock->dispose();
        sock.reset();
    }

    if (thread_accept.joinable()) {
        thread_accept.join();
    }

    // Stop all channels
    {
        for (std::shared_ptr<server_channel_state>& chan : channels) {
            chan->dispose();
            chan.reset();
        }
        channels.clear();
    }

    // Remove all clients
    LOG_DEBUG("Removing all clients...");
    {
        std::unique_lock<std::shared_mutex> lock(clients_mutex);
        for (auto& it : clients) {
            it.second->dispose();
            it.second.reset();
        }
        clients.clear();
    }
    LOG_DEBUG("All clients removed");
}

void xcp::server_portal_state::fn_thread_accept()
{
    LOG_TRACE("Server portal accepting: {}", bound_local_endpoint.to_string());

    while (true) {
        infra::tcp_sockaddr peer_addr { };
        std::shared_ptr<infra::os_socket_t> accepted_sock = sock->accept(&peer_addr);
        if (!accepted_sock) {
            if (infra::sighandle::is_exit_required()) {
                LOG_DEBUG("Exit requried. Stop accept() on server portal {}", bound_local_endpoint.to_string());
                break;
            }
            else {
                LOG_ERROR("Server portal: accept() on {} failed", bound_local_endpoint.to_string());
                continue;
            }
        }
        LOG_DEBUG("Accepted from portal: {}", peer_addr.to_string());

        launch_thread([this, accepted_sock, peer_addr](std::thread* const thr) {
            try {
                fn_thread_unified_accepted_socket(
                    thr,
                    accepted_sock,
                    peer_addr,
                    true,
                    (this->program_options->arg_portal->repeats.has_value()));
            }
            catch(const std::exception& ex) {
                LOG_ERROR("fn_thread_unified_accepted_socket() exception: {}", ex.what());
                // Don't PANIC_TERMINATE
            }
        });
    }
}


void xcp::server_portal_state::fn_thread_unified_accepted_socket(
    std::thread* const thr,
    std::shared_ptr<infra::os_socket_t> accepted_sock,
    const infra::tcp_sockaddr& peer_addr,
    const bool allow_role_portal,
    const bool allow_role_channel)
{
    //
    // NOTE:
    //  This is the entry for **all** newly accepted sockets
    //  No matter the listening socket is portal or channel
    //

    infra::sweeper error_cleanup = [&]() {
        if (accepted_sock) {
            accepted_sock->dispose();
            accepted_sock.reset();
        }

        // Cleanup current thread (make it detached)
        thr->detach();
        delete thr;
    };


    //
    // Receive GREETING_MAGIC_1, GREETING_MAGIC_2, ROLE from client
    //
    uint32_t role;
    {
        char buffer[12];
        if (!accepted_sock->recv(buffer, sizeof(buffer))) {
            LOG_ERROR("Server: receive greeting magics and role from peer {} failed", peer_addr.to_string());
            return;
        }

        const uint32_t magic1 = ntohl(*(uint32_t*)&buffer[0]);
        const uint32_t magic2 = ntohl(*(uint32_t*)&buffer[4]);
        role = ntohl(*(uint32_t*)&buffer[8]);

        if (magic1 != portal_protocol::GREETING_MAGIC_1 || magic2 != portal_protocol::GREETING_MAGIC_2) {
            LOG_ERROR("Server: greeting magics from peer {} mismatched: "
                      "expects (0x{:08x}, 0x{:08x}), but got (0x{:08x}, 0x{:08x})",
                      peer_addr.to_string(), portal_protocol::GREETING_MAGIC_1, portal_protocol::GREETING_MAGIC_2, magic1, magic2);
            return;
        }

        if (role == portal_protocol::role::ROLE_PORTAL) {
            if (!allow_role_portal) {
                LOG_ERROR("Server: this is a channel endpoint but not a portal! (peer {})", peer_addr.to_string());
                return;
            }
        }
        else if (role == portal_protocol::role::ROLE_CHANNEL) {
            if (!allow_role_channel) {
                LOG_ERROR("Server: client requests to use portal as channel, "
                          "but server is not started in this way (peer: {})", peer_addr.to_string());
                return;
            }
        }
        else {
            LOG_ERROR("Server: got unknown role from peer {}: 0x{:08x}", peer_addr.to_string(), role);
            return;
        }
    }


    //
    // Receive client identity from this socket
    //
    infra::identity_t identity;
    {
        if (!accepted_sock->recv(&identity, sizeof(identity))) {
            LOG_ERROR("Server: receive identity from peer {} failed", peer_addr.to_string());
            return;
        }

        LOG_TRACE("Server: received identity from peer {}", peer_addr.to_string());
    }


    //
    // Call functions, according to role
    //
    if (role == portal_protocol::role::ROLE_PORTAL) {
        ASSERT(allow_role_portal);

        {
            // Create client instance and add to clients
            std::shared_ptr<client_instance> client;
            {
                std::unique_lock<std::shared_mutex> lock(clients_mutex);

                if (this->is_dispose_required()) {  // unlikely, must be guarded by clients_mutex (double check)
                    LOG_INFO("Server portal: dispose required. Abandon initializing client (peer {})", peer_addr.to_string());
                    return;
                }

                client = std::make_shared<client_instance>(
                    *this,
                    accepted_sock,
                    std::shared_ptr<std::thread>(thr),
                    peer_addr,
                    identity,
                    program_options->total_channel_repeats_count);
                clients.insert(std::make_pair(identity, client));
                LOG_TRACE("Server portal: create new client #{}", client->id);
            }
            // Run client's fn_portal
            error_cleanup.suppress_sweep();
            client->fn_portal();
        }
        return;
    }

    if (role == portal_protocol::role::ROLE_CHANNEL) {
        ASSERT(allow_role_channel);

        {
            // Lookup identity and find the client
            std::shared_ptr<client_instance> client;
            {
                std::unique_lock<std::shared_mutex> lock(clients_mutex);
                const auto it = clients.find(identity);
                if (it == clients.end()) {
                    LOG_ERROR("Server channel: unknown identity from peer {}", peer_addr.to_string());
                    return;
                }
                client = it->second;
                ASSERT(client != nullptr);
            }
            //LOG_TRACE("Client found: {}", client->peer_endpoint.to_string());

            // Add current thread to client channel_threads
            {
                std::unique_lock<std::shared_mutex> lock(client->_rundown.acquire_unique());
                if (!lock.owns_lock()) {  // unlikely
                    ASSERT(client->_rundown.required_rundown());
                    LOG_ERROR("Server channel: client instance has required dispose()");
                    return;
                }
                else {
                    client->channel_threads.emplace_back(accepted_sock, std::shared_ptr<std::thread>(thr));
                    LOG_TRACE("Server channel: establish one channel between client #{} (peer: {})", client->id, peer_addr.to_string());
                }
            }

            error_cleanup.suppress_sweep();
            client->fn_channel(accepted_sock);
        }
        return;
    }

    PANIC_TERMINATE("Unknown role should have been rejected: 0x{:08x}", role);
}
