#include "common.h"


//==============================================================================
// struct base_program_options
//==============================================================================

void xcp::base_program_options::add_options(CLI::App& app)
{
    app.add_flag(
        "-V,--version",
        [&](const size_t /*count*/) {
            printf("Version %d.%d.%d\nGit branch %s commit %s\n",
                XCP_VERSION_MAJOR, XCP_VERSION_MINOR, XCP_VERSION_PATCH,
                TEXTIFY(XCP_GIT_BRANCH), TEXTIFY(XCP_GIT_COMMIT_HASH));
            exit(0);
        },
        "Print version and exit");

    app.add_flag(
        "-q,--quiet",
        [&](const size_t count) { this->arg_verbosity -= static_cast<int>(count); },
        "Be more quiet");

    app.add_flag(
        "-v,--verbose",
        [&](const size_t count) { this->arg_verbosity += static_cast<int>(count); },
        "Be more verbose");
}

bool xcp::base_program_options::post_process()
{
    // Set verbosity
    infra::set_logging_verbosity(this->arg_verbosity);

    return true;
}



//==============================================================================
// struct xcp_program_options
//==============================================================================

void xcp::xcp_program_options::add_options(CLI::App& app)
{
    base_program_options::add_options(app);

    CLI::Option* opt_port = app.add_option(
        "-P,--port",
        this->arg_port,
        "Server portal port to connect to");
    opt_port->type_name("<port>");
}

bool xcp::xcp_program_options::post_process()
{
    if (!base_program_options::post_process()) {
        return false;
    }

    //----------------------------------------------------------------
    // arg_port
    //----------------------------------------------------------------
    if (arg_port.has_value()) {
        if (arg_port.value() == 0) {
            LOG_ERROR("Server portal port 0 is invalid");
            return false;
        }
    }
    else {  // !arg_portal->port.has_value()
        arg_port = program_options_defaults::SERVER_PORTAL_PORT;
    }

    LOG_INFO("Server portal port: {}", arg_port.value());
    /*
    if (!arg_portal->resolve()) {
        LOG_ERROR("Can't resolve specified server portal: {}", arg_portal->to_string());
        return false;
    }
    else {
        for (const infra::tcp_sockaddr& addr : arg_portal->resolved_sockaddrs) {
            LOG_DEBUG("  Candidate server portal endpoint: {}", addr.to_string());
        }
    }
    */


    //----------------------------------------------------------------
    // arg_local_endpoint
    //----------------------------------------------------------------
    /*
    assert(arg_local_endpoint != nullptr);

    if (is_reverse_forwarding) {  // local connect (reverse forwarding)
        if (arg_local_endpoint->port.has_value()) {
            if (arg_local_endpoint->port.value() == 0) {
                LOG_ERROR("Local endpoint port 0 is invalid: {} (reverse forwarding)", arg_local_endpoint->to_string());
                return false;
            }
        }
        else {
            LOG_ERROR("Local endpoint port requried: {} (reverse forwarding)", arg_local_endpoint->to_string());
            return false;
        }
        LOG_INFO("Local endpoint: {} (reverse forwarding)", arg_local_endpoint->to_string());
    }
    else {  // local listen
        if (arg_local_endpoint->port.has_value()) {
            LOG_TRACE("Local endpoint binds to port {}", arg_local_endpoint->port.value());
        }
        else {
            arg_local_endpoint->port = static_cast<uint16_t>(0);
            LOG_TRACE("Local endpoint binds to random port");
        }
        LOG_INFO("Local endpoint: {}", arg_local_endpoint->to_string());
    }

    if (!arg_local_endpoint->resolve()) {
        LOG_ERROR("Can't resolve specified local endpoint: {}", arg_local_endpoint->to_string());
        return false;
    }
    else {
        for (const infra::tcp_sockaddr& addr : arg_local_endpoint->resolved_sockaddrs) {
            LOG_DEBUG("  Candidate local endpoint: {}", addr.to_string());
        }
    }


    //----------------------------------------------------------------
    // arg_remote_endpoint
    //----------------------------------------------------------------
    assert(arg_remote_endpoint != nullptr);

    if (is_reverse_forwarding) {  // remote listen (reverse forwarding)
        if (arg_remote_endpoint->port.has_value()) {
            LOG_TRACE("Remote endpoint binds to port {} (reverse forwarding)", arg_remote_endpoint->port.value());
        }
        else {
            arg_remote_endpoint->port = static_cast<uint16_t>(0);
            LOG_TRACE("Remote endpoint binds to random port (reverse forwarding)");
        }
        LOG_INFO("Remote endpoint: {} (reverse forwarding)", arg_remote_endpoint->to_string());
    }
    else {  // remote connect
        if (arg_remote_endpoint->port.has_value()) {
            if (arg_remote_endpoint->port.value() == 0) {
                LOG_ERROR("Remote endpoint port 0 is invalid: {}", arg_remote_endpoint->to_string());
                return false;
            }
        }
        else {
            LOG_ERROR("Remote endpoint port requried: {}", arg_remote_endpoint->to_string());
            return false;
        }
        LOG_INFO("Remote endpoint: {}", arg_remote_endpoint->to_string());
    }
    */

    return true;
}



//==============================================================================
// struct xcpd_program_options
//==============================================================================

void xcp::xcpd_program_options::add_options(CLI::App& app)
{
    base_program_options::add_options(app);

    CLI::Option* opt_portal = app.add_option(
        "-p,--portal",
        this->arg_portal,
        "Server portal endpoint to bind and listen");
    opt_portal->type_name("<endpoint>");

    CLI::Option* opt_channel = app.add_option(
        "-C,--channel",
        this->arg_channels,
        "Server channels to bind and listen for transportation");
    opt_channel->type_name("<endpoint>");
}

bool xcp::xcpd_program_options::post_process()
{
    if (!base_program_options::post_process()) {
        return false;
    }

    //----------------------------------------------------------------
    // arg_portal
    //----------------------------------------------------------------
    if (!arg_portal.has_value()) {
        arg_portal = infra::tcp_endpoint();
        arg_portal->host = program_options_defaults::SERVER_PORTAL_HOST;
        LOG_INFO("Server portal is not specified, use default host {}", program_options_defaults::SERVER_PORTAL_HOST);
    }

    if (arg_portal->port.has_value()) {
        if (arg_portal->port.value() == 0) {
            LOG_WARN("Server portal binds to a random port");
        }
        else {
            LOG_TRACE("Server portal binds to port {}", arg_portal->port.value());
        }
    }
    else {  // !arg_portal->port.has_value()
        arg_portal->port = program_options_defaults::SERVER_PORTAL_PORT;
        LOG_TRACE("Server portal binds to default port {}", arg_portal->port.value());
    }

    LOG_INFO("Server portal endpoint: {}", arg_portal->to_string());
    if (!arg_portal->resolve()) {
        LOG_ERROR("Can't resolve specified portal: {}", arg_portal->to_string());
        return false;
    }
    else {
        for (const infra::tcp_sockaddr& addr : arg_portal->resolved_sockaddrs) {
            LOG_DEBUG("  Candidate server portal endpoint: {}", addr.to_string());
        }
    }


    //----------------------------------------------------------------
    // arg_channels
    //----------------------------------------------------------------
    if (arg_channels.empty()) {
        infra::tcp_endpoint_repeatable tmp;
        tmp.host = program_options_defaults::SERVER_CHANNEL_HOST;
        tmp.port = program_options_defaults::SERVER_CHANNEL_PORT;
        tmp.repeats = program_options_defaults::SERVER_CHANNEL_REPEATS;

        LOG_INFO("Server channels are not specified, use default channel: {}", tmp.to_string());
        arg_channels.emplace_back(std::move(tmp));
    }

    size_t total_channel_repeats = 0;
    for (infra::tcp_endpoint_repeatable& ep : arg_channels) {
        if (!ep.port.has_value()) {
            ep.port = static_cast<uint16_t>(0);
        }
        if (!ep.repeats.has_value()) {
            ep.repeats = static_cast<size_t>(1);
        }
        LOG_INFO("Server channel: {}", ep.to_string());

        if (!ep.resolve()) {
            LOG_ERROR("Can't resolve specified channel: {}", ep.to_string());
            return false;
        }
        else {
            for (const infra::tcp_sockaddr& addr : ep.resolved_sockaddrs) {
                LOG_DEBUG("  Candidate server channel endpoint: {}", addr.to_string());
            }
        }

        total_channel_repeats += ep.repeats.value();
    }
    LOG_INFO("Total server channels (including repeats): {}", total_channel_repeats);

    return true;
}
