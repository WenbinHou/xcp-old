#include "common.h"


//==============================================================================
// struct client_instance
//==============================================================================

void xcp::client_instance::fn_portal()
{
    const infra::sweeper exit_cleanup = [this]() {
        this->async_dispose(false);
    };


    // NOTE: client identity has been received

    // Receive client request
    struct {
        bool is_from_server_to_client { };
        std::string client_file_name;
        std::string server_path;
        uint64_t transfer_block_size;
        std::optional<basic_file_info> file_info;
    } transfer_request;
    {
        message_client_hello_request msg;
        if (!message_recv(accepted_portal_socket, msg)) {
            LOG_ERROR("Peer {}: receive message_client_hello_request failed", peer_endpoint.to_string());
            return;
        }

        LOG_TRACE("Peer {}: request: is_from_server_to_client={}, client_file_name={}, server_path={}",
                  peer_endpoint.to_string(), msg.is_from_server_to_client, msg.client_file_name, msg.server_path);

        transfer_request.is_from_server_to_client = msg.is_from_server_to_client;
        transfer_request.client_file_name = std::move(msg.client_file_name);
        transfer_request.server_path = std::move(msg.server_path);
        transfer_request.transfer_block_size = std::move(msg.transfer_block_size);
        transfer_request.file_info = std::move(msg.file_info);
    }


    // Send back server portal information to client, and response to client request
    {
        message_server_hello_response msg;
        msg.error_code = 0;

        // Check against relative path on server side
        if (stdfs::path(transfer_request.server_path).is_relative()) {
            LOG_ERROR("Server portal (peer {}): server side path {} should not be relative path",
                      peer_endpoint.to_string(), transfer_request.server_path);
            msg.error_code = EINVAL;
            msg.error_message = "Server side path is relative: " + transfer_request.server_path;
        }

        if (msg.error_code == 0) {
            try {
                if (transfer_request.is_from_server_to_client) {  // from server to client
                    ASSERT(!transfer_request.file_info.has_value());
                    this->transfer = std::make_shared<transfer_source>(transfer_request.server_path, transfer_request.transfer_block_size);
                    msg.file_info = this->transfer->get_file_info();
                }
                else {  // from client to server
                    ASSERT(transfer_request.file_info.has_value());
                    this->transfer = std::make_shared<transfer_destination>(
                        transfer_request.client_file_name,
                        transfer_request.server_path);
                    std::dynamic_pointer_cast<transfer_destination>(this->transfer)->init_file(
                        transfer_request.file_info.value(),
                        server_portal.program_options->total_channel_repeats_count);
                }
            }
            catch(const transfer_error& ex) {
                msg.error_code = ex.error_code;
                msg.error_message = ex.error_message;
                LOG_ERROR("Server portal (peer {}): {}", peer_endpoint.to_string(), msg.error_message);
            }
        }

        if (msg.error_code == 0) {  // no error
            // Tell client: server channels
            ASSERT(!server_portal.channels.empty());
            for (const std::shared_ptr<xcp::server_channel_state>& chan : server_portal.channels) {
                msg.server_channels.emplace_back(chan->bound_local_endpoint, chan->required_endpoint.repeats.value());
            }
        }

        if (!message_send(accepted_portal_socket, msg)) {
            LOG_ERROR("Server portal (peer {}): send message_server_hello_response failed", peer_endpoint.to_string());
            return;
        }

        LOG_TRACE("Server portal (peer {}): response: error_code={}, server_channels.size()={}",
                  peer_endpoint.to_string(), msg.error_code, msg.server_channels.size());
    }

    // Wait for all channel repeats are connected
    {
        sem_all_channel_repeats_connected.wait();
        if (is_dispose_required()) {  // unlikely
            LOG_TRACE("Server portal (peer {}): dispose() required", peer_endpoint.to_string());
            return;
        }

        message_server_ready_to_transfer msg;
        msg.error_code = 0;
        if (!message_send(accepted_portal_socket, msg)) {
            LOG_ERROR("Server portal (peer {}): send message_server_ready_to_transfer failed", peer_endpoint.to_string());
            return;
        }
        LOG_TRACE("Server portal (peer {}): now ready to transfer", peer_endpoint.to_string());
    }

    // Run transfer
    {
        const bool success = this->transfer->invoke_portal(accepted_portal_socket);
        if (!success) {
            LOG_ERROR("Server portal (peer {}): transfer failed", peer_endpoint.to_string());
        }
    }
}

void xcp::client_instance::fn_channel(
    std::shared_ptr<infra::os_socket_t> accepted_channel_socket,
    infra::tcp_sockaddr channel_peer_endpoint)
{
    infra::sweeper error_cleanup = [&]() {
        this->async_dispose(false);
    };

    LOG_TRACE("Channel (peer {}): channel initialization done", channel_peer_endpoint.to_string());

    // Signal if all channel repeats are connected
    if (++connected_channel_repeats_count == total_channel_repeats_count) {
        LOG_TRACE("Client: all {} channel repeats are connected", total_channel_repeats_count);
        sem_all_channel_repeats_connected.post();
    }

    // Run transfer
    {
        ASSERT(this->transfer != nullptr);
        const bool success = this->transfer->invoke_channel(accepted_channel_socket);
        if (!success) {
            LOG_ERROR("Server channel (peer {}): transfer failed", channel_peer_endpoint.to_string());
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

    // Post sem_all_channel_repeats_connected to allow portal_thread to go on (if necessary)
    if (connected_channel_repeats_count < total_channel_repeats_count) {
        sem_all_channel_repeats_connected.post();
    }

    if (portal_thread) {
        ASSERT(portal_thread->joinable());
        portal_thread->join();
        portal_thread.reset();
    }

    // Wait for all channel threads to exit...
    {
        std::unique_lock<std::shared_mutex> lock(channel_threads_mutex);
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

    // Close transfer file handles (if any)
    if (this->transfer) {
        this->transfer->dispose();
        this->transfer.reset();
    }

    // Remove itself from server_portal_state.clients
    //
    // If we found server portal is disposing, do not need to do anything
    // As server portal dispose() will clear all living clients
    {
        if (!server_portal.is_dispose_required()) {
            const infra::identity_t identity = this->client_identity;
            server_portal_state& portal = this->server_portal;

            // TODO: use thread pool?
            std::thread thr([identity, &portal]() {
                if (!portal.is_dispose_required()) {
                    std::unique_lock<std::shared_mutex> lock(portal.clients_mutex);
                    portal.clients.erase(identity);
                    LOG_DEBUG("Remove client instance");
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
        launch_thread([this, accepted_sock, peer_addr](std::thread* const thr) mutable {

            infra::sweeper error_cleanup = [&]() {
                accepted_sock->dispose();
                accepted_sock.reset();

                // Cleanup current thread (make it detached)
                thr->detach();
                delete thr;
            };

            // Receive identity
            infra::identity_t identity;
            {
                if (!accepted_sock->recv(&identity, sizeof(infra::identity_t))) {
                    LOG_ERROR("Server channel: receive identity from peer {} failed", peer_addr.to_string());
                    return;
                }

                LOG_TRACE("Server channel: received identity from peer {}", peer_addr.to_string());
            }

            // Lookup identity and find the client
            std::shared_ptr<client_instance> client;
            {
                std::unique_lock<std::shared_mutex> lock(portal.clients_mutex);
                const auto it = portal.clients.find(identity);
                if (it == portal.clients.end()) {
                    LOG_ERROR("Server channel: Unknown identity from peer {}", peer_addr.to_string());
                    return;
                }
                client = it->second;
            }
            //LOG_TRACE("Client found: {}", client->peer_endpoint.to_string());

            // Add current thread to client channel_threads
            {
                std::unique_lock<std::shared_mutex> lock(client->channel_threads_mutex);
                if (client->is_dispose_required()) {  // unlikely
                    lock.unlock();

                    LOG_ERROR("Server channel: client instance has required dispose()");
                    return;
                }
                else {
                    client->channel_threads.emplace_back(accepted_sock, std::shared_ptr<std::thread>(thr));
                }
            }

            error_cleanup.suppress_sweep();
            client->fn_channel(accepted_sock, peer_addr);
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
        for (infra::tcp_endpoint_repeatable& ep : program_options->arg_channels) {
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

        launch_thread([this, accepted_sock, peer_addr](std::thread* thr) {
            try {
                fn_task_accepted_portal(thr, accepted_sock, peer_addr);
            }
            catch(const std::exception& ex) {
                LOG_ERROR("fn_task_accepted_portal() exception: {}", ex.what());
                // Don't PANIC_TERMINATE
            }
        });
    }
}

void xcp::server_portal_state::fn_task_accepted_portal(
    std::thread* const thr, 
    std::shared_ptr<infra::os_socket_t> accepted_sock,
    const infra::tcp_sockaddr& peer_addr)
{
    infra::sweeper error_cleanup = [&]() {
        if (accepted_sock) {
            accepted_sock->dispose();
            accepted_sock.reset();
        }

        // Cleanup current thread (make it detached)
        thr->detach();
        delete thr;
    };


    // Receive client identity from portal
    infra::identity_t identity;
    {
        if (!accepted_sock->recv(&identity, sizeof(identity))) {
            LOG_ERROR("Server portal: receive identity from peer {} failed", peer_addr.to_string());
            return;
        }

        LOG_TRACE("Server portal: received identity from peer {}", peer_addr.to_string());
    }

    // Create client instance and add to clients
    std::shared_ptr<client_instance> client;
    {
        std::unique_lock<std::shared_mutex> lock(clients_mutex);

        if (this->is_dispose_required()) {  // unlikely, must be guarded by clients_mutex (double check)
            LOG_INFO("Dispose required. Abandon initializing client (peer {})", peer_addr.to_string());
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
    }

    // Run client's fn_portal
    error_cleanup.suppress_sweep();
    client->fn_portal();
}
