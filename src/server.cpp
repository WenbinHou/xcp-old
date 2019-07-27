#include "common.h"

// TODO: refactor and remove using namespace
using namespace xcp;
using namespace infra;


//==============================================================================
// struct client_instance
//==============================================================================

void client_instance::fn_portal()
{
    // NOTE: lock all_threads fo a while...
    // This allows current thread to be saved into all_threads, before it actually do things
    {
        [[maybe_unused]]
        std::shared_lock<std::shared_mutex> lock(all_threads_mutex);
    }

    // NOTE: client identity has been received

    // Receive client request
    {
        message_client_hello_request msg;
        if (!message_recv(accepted_portal_socket, msg)) {
            LOG_ERROR("Peer {}: receive message_client_hello_request failed", peer_endpoint.to_string());
            return;
        }

        LOG_TRACE("Peer {}: request: is_from_server_to_client={}, client_file_name={}, server_path={}",
                  peer_endpoint.to_string(), msg.is_from_server_to_client, msg.client_file_name, msg.server_path);

        request.is_from_server_to_client = msg.is_from_server_to_client;
        request.client_file_name = std::move(msg.client_file_name);
        request.server_path = std::move(msg.server_path);
    }


    // Send back server portal information to client, and response to client request
    {
        message_server_hello_response msg;
        msg.error_code = 0;

        do {
            const stdfs::path server_path(request.server_path);
            if (server_path.is_relative()) {
                LOG_ERROR("Server portal (peer {}): server side path {} should not be relative path", peer_endpoint.to_string(), server_path.string());
                msg.error_code = EINVAL;
                msg.error_message = "Server side path is relative: " + request.server_path;
                break;
            }

            // TODO: try to open the file for write?
            msg.file_size = 0x123456;  // TODO

        } while(false);

        if (msg.error_code == 0) {  // no error
            // Set server channels
            assert(!server_portal.channels.empty());
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
}

void client_instance::fn_channel(infra::socket_t accepted_channel_socket, infra::tcp_sockaddr channel_peer_endpoint)
{
    infra::sweeper exit_cleanup = [&]() {
        infra::close_socket(accepted_channel_socket);
        accepted_channel_socket = infra::INVALID_SOCKET_VALUE;
    };

    LOG_TRACE("Channel (peer {}): channel initialization done", channel_peer_endpoint.to_string());
}

void client_instance::dispose() noexcept
{
    if (accepted_portal_socket) {
        close_socket(accepted_portal_socket);
        accepted_portal_socket = infra::INVALID_SOCKET_VALUE;
    }

    // Wait for all threads to exit...
    {
        std::unique_lock<std::shared_mutex> lock(all_threads_mutex);
        for (std::shared_ptr<std::thread>& thr : all_threads) {
            assert(thr->joinable());
            thr->join();
        }
        all_threads.clear();
    }
}



//==============================================================================
// struct server_channel_state
//==============================================================================

bool server_channel_state::init()
{
    assert(!required_endpoint.resolved_sockaddrs.empty());

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

void server_channel_state::dispose() noexcept
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
        tcp_sockaddr peer_addr;
        peer_addr.addr.ss_family = bound_local_endpoint.family();
        socklen_t len = peer_addr.socklen();

        socket_t accepted_sock = accept(sock, peer_addr.address(), &len);
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
        LOG_TRACE("Server channel accepted a new socket from {}", peer_addr.to_string());


        // Create a separated thread to handle the channel
        launch_thread([this, accepted_sock, peer_addr](std::thread* const thr) mutable {

            infra::sweeper sweep = [&]() {
                infra::close_socket(accepted_sock);
                accepted_sock = infra::INVALID_SOCKET_VALUE;
            };

            // Receive identity
            identity_t identity;
            {
                const int cnt = (int)recv(accepted_sock, (char*)&identity, sizeof(identity), MSG_WAITALL);
                if (cnt != (int)sizeof(identity)) {
                    LOG_ERROR("Server channel: receive identity from peer {} expects {}, but returns {}. {}",
                              peer_addr.to_string(), sizeof(identity), cnt, socket_error_description());
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

            // Add current thread to client all_threads
            {
                std::unique_lock<std::shared_mutex> lock(client->all_threads_mutex);
                client->all_threads.emplace_back(std::shared_ptr<std::thread>(thr));
            }

            client->fn_channel(accepted_sock, peer_addr);
        });
    }
}



//==============================================================================
// struct server_portal_state
//==============================================================================

bool server_portal_state::init()
{
    //
    // Initialize channels
    //
    {
        for (infra::tcp_endpoint_repeatable& ep : program_options->arg_channels) {
            std::shared_ptr<xcp::server_channel_state> chan = std::make_shared<xcp::server_channel_state>(*this, ep);
            if (!chan->init()) {
                LOG_ERROR("Init server_channel_state failed for {}", ep.to_string());
                return false;
            }
            channels.emplace_back(std::move(chan));
        }
    }


    //
    // Initialize portal
    //
    bool bound = false;
    for (const tcp_sockaddr& addr : program_options->arg_portal->resolved_sockaddrs) {
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

        LOG_INFO("Server portal {} binds to {}", program_options->arg_portal->to_string(), bound_local_endpoint.to_string());
        bound = true;
        sweep.suppress_sweep();
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
            LOG_ERROR("fn_thread_listener() exception: {}", ex.what());
            std::abort();
        }
    });

    return true;
}

void server_portal_state::dispose() noexcept
{
    // Stop server portal socket
    if (sock != INVALID_SOCKET_VALUE) {
        LOG_INFO("Close server portal {} bound to {}", program_options->arg_portal->to_string(), bound_local_endpoint.to_string());
        close_socket(sock);
        sock = INVALID_SOCKET_VALUE;
    }

    if (thread_accept.joinable()) {
        thread_accept.join();
    }

    // Stop all channels
    {
        for (std::shared_ptr<server_channel_state>& chan : channels) {
            chan->dispose();
        }
        channels.clear();
    }

    // Remove all clients
    LOG_DEBUG("Removing all clients...");
    {
        std::unique_lock<std::shared_mutex> lock(clients_mutex);
        for (const auto& it : clients) {
            it.second->dispose();
        }
        clients.clear();
    }
    LOG_DEBUG("All clients removed");
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

        std::thread thr([this, accepted_sock, peer_addr]() {
            try {
                fn_task_accepted_portal(accepted_sock, peer_addr);
            }
            catch(const std::exception& ex) {
                LOG_ERROR("fn_task_accepted_portal() exception: {}", ex.what());
                // Don't std::abort()!
            }
        });
        thr.detach();
    }
}

void server_portal_state::fn_task_accepted_portal(socket_t accepted_sock, const tcp_sockaddr& peer_addr)
{
    sweeper sweep = [&]() {
        if (accepted_sock != INVALID_SOCKET_VALUE) {
            close_socket(accepted_sock);
            accepted_sock = INVALID_SOCKET_VALUE;
        }
    };


    // Receive client identity from portal
    identity_t identity;
    {
        const int cnt = (int)recv(accepted_sock, (char*)&identity, sizeof(identity), MSG_WAITALL);
        if (cnt != (int)sizeof(identity)) {
            LOG_ERROR("Server portal: receive identity from peer {} expects {}, but returns {}. {}",
                      peer_addr.to_string(), sizeof(identity), cnt, socket_error_description());
            return;
        }

        LOG_TRACE("Server portal: received identity from peer {}", peer_addr.to_string());
    }

    // Create client instance and add to clients
    std::shared_ptr<client_instance> client;
    {
        std::unique_lock<std::shared_mutex> lock(clients_mutex);

        if (sighandle::is_exit_required()) {  // must be guarded by clients_mutex (double check)
            LOG_INFO("Exit required. Abandon initializing peer {}", peer_addr.to_string());
            return;
        }

        client = std::make_shared<client_instance>(*this, accepted_sock, peer_addr, identity);
        clients.insert(std::make_pair(identity, client));
    }

    // Run client's fn_portal
    client->fn_portal();
}

