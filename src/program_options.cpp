#include "common.h"


//==============================================================================
// struct host_path
//==============================================================================

bool xcp::host_path::parse(const std::string& value)
{
    // Handle strings like:
    //  "relative"
    //  "relative/subpath"
    //  "/absolute/path"
    //  "C:"                (on Windows)
    //  "C:\"               (on Windows)
    //  "C:/"               (on Windows)
    //  "C:\absolute\path"  (on Windows)
    //  "C:/absolute\path"  (on Windows)
    //  "relative\subpath"
    //  "hostname:/"
    //  "hostname:/absolute"
    //  "127.0.0.1:C:"
    //  "1.2.3.4:C:\"
    //  "1.2.3.4:C:/"
    //  "[::1]:C:\absolute"
    //  "[1:2:3:4:5:6:7:8]:C:/absolute"
    //
    // NOTE:
    //  Remote path MUST be absolute
    //  Local path might be absolute or relative


    // See:
    //  https://stackoverflow.com/questions/53497/regular-expression-that-matches-valid-ipv6-addresses
    //  https://stackoverflow.com/questions/106179/regular-expression-to-match-dns-hostname-or-ip-address
    // NOTE: re_valid_hostname matches a superset of valid IPv4

    static std::regex* __re = nullptr;
    if (__re == nullptr) {
        const std::string re_valid_hostname = R"((?:(?:(?:[a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*(?:[A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])))";
        const std::string re_valid_ipv6 = R"((?:\[(?:(?:[0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,7}:|(?:[0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|(?:[0-9a-fA-F]{1,4}:){1,5}(?::[0-9a-fA-F]{1,4}){1,2}|(?:[0-9a-fA-F]{1,4}:){1,4}(?::[0-9a-fA-F]{1,4}){1,3}|(?:[0-9a-fA-F]{1,4}:){1,3}(?::[0-9a-fA-F]{1,4}){1,4}|(?:[0-9a-fA-F]{1,4}:){1,2}(?::[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:(?:(?::[0-9a-fA-F]{1,4}){1,6})|:(?:(?::[0-9a-fA-F]{1,4}){1,7}|:)|::(?:[Ff]{4}(?::0{1,4})?:)?(?:(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])\.){3,3}(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])|(?:[0-9a-fA-F]{1,4}:){1,4}:(?:(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9])\.){3,3}(?:25[0-5]|(?:2[0-4]|1?[0-9])?[0-9]))\]))";
        const std::string re_valid_host = "(?:" + re_valid_hostname + "|" + re_valid_ipv6 + ")";
        const std::string re_valid_host_path = "^(?:(?:([^@:]+)@)?(" + re_valid_host + "):)?(.+)$";
        __re = new std::regex(re_valid_host_path, std::regex::optimize);
    }

    if (value.empty()) return false;

    //
    // Special handling for Windows driver letters (local path)
    //
#if PLATFORM_WINDOWS || PLATFORM_CYGWIN
    if (value.size() >= 2 && value[1] == ':') {
        if ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')) {
            if (value.size() == 2 || value[2] == '/' || value[2] == '\\') {
                user.reset();
                host.reset();
                path = value;
                return true;
            }
        }
    }
#elif PLATFORM_LINUX
#else
#   error "Unknown platform"
#endif


    std::smatch match;
    if (!std::regex_match(value, match, *__re)) {
        return false;
    }

    {
        // The user part
        std::string tmp(match[1].str());
        if (tmp.empty())
            user.reset();
        else
            user= std::move(tmp);
    }

    {
        // The host part
        // NOTE: If host is IPv6 surrounded by '[' ']', DO NOT trim the beginning '[' and ending ']'
        std::string tmp(match[2].str());
        if (tmp.empty())
            host.reset();
        else
            host = std::move(tmp);
    }

    path = match[3].str();
    if (path.empty()) return false;

    return true;
}



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

    CLI::Option* opt_from = app.add_option(
        "from",
        this->arg_from_path,
        "Copy from this path");
    opt_from->type_name("<from>");
    opt_from->required();

    CLI::Option* opt_to = app.add_option(
        "to",
        this->arg_to_path,
        "Copy to this path");
    opt_to->type_name("<to>");
    opt_to->required();

    CLI::Option* opt_user = app.add_option(
        "-u,--user",
        this->arg_user,
        "Relative to this user's home directory on server side");
    opt_user->type_name("<user>");

    CLI::Option* opt_block = app.add_option(
        "-B,--block",
        this->arg_transfer_block_size,
        "Transfer block size");
    opt_block->type_name("<size>");
    opt_block->transform(CLI::AsSizeValue(false));
}

bool xcp::xcp_program_options::post_process()
{
    if (!base_program_options::post_process()) {
        return false;
    }

    //----------------------------------------------------------------
    // arg_from_path, arg_to_path
    //----------------------------------------------------------------
    if (arg_from_path.is_remote()) {
        ASSERT(arg_from_path.host.has_value());
        LOG_DEBUG("Copy from remote host {} path {}", arg_from_path.host.value(), arg_from_path.path);
    }
    else {
        LOG_DEBUG("Copy from local path {}", arg_from_path.path);
    }

    if (arg_to_path.is_remote()) {
        ASSERT(arg_to_path.host.has_value());
        LOG_DEBUG("Copy to remote host {} path {}", arg_to_path.host.value(), arg_to_path.path);
    }
    else {
        LOG_DEBUG("Copy to local path {}", arg_to_path.path);
    }


    if (arg_from_path.is_remote() && arg_to_path.is_remote()) {
        LOG_ERROR("Can't copy between remote hosts");
        return false;
    }
    else if (arg_from_path.is_remote()) {
        is_from_server_to_client = true;
        server_portal.host = arg_from_path.host.value();
    }
    else if (arg_to_path.is_remote()) {
        is_from_server_to_client = false;
        server_portal.host = arg_to_path.host.value();
    }
    else {
        LOG_ERROR("Copy from local to local is to be supported");  // TODO
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
    server_portal.port = arg_port.value();


    LOG_DEBUG("Server portal: {}", server_portal.to_string());
    if (!server_portal.resolve()) {
        LOG_ERROR("Can't resolve specified server portal: {}", server_portal.to_string());
        return false;
    }
    else {
        for (const infra::tcp_sockaddr& addr : server_portal.resolved_sockaddrs) {
            LOG_DEBUG("  Candidate server portal endpoint: {}", addr.to_string());
        }
    }


    //----------------------------------------------------------------
    // arg_user
    //----------------------------------------------------------------
    bool got_user = false;
    if (arg_user.has_value()) {
        server_user.user_name = arg_user.value();
        got_user = true;
    }
    else {
        if (arg_from_path.is_remote()) {
            ASSERT(is_from_server_to_client);
            if (arg_from_path.user.has_value()) {
                server_user.user_name = arg_from_path.user.value();
                got_user = true;
            }
        }
        else if (arg_to_path.is_remote()) {
            ASSERT(!is_from_server_to_client);
            if (arg_to_path.user.has_value()) {
                server_user.user_name = arg_to_path.user.value();
                got_user = true;
            }
        }
    }
    if (!got_user) {
        if (infra::get_user_name(server_user)) {
            got_user = true;
        }
    }
    if (got_user) {
        LOG_TRACE("Use this user for server side: {}", server_user.to_string());
    }
    else {
        server_user = { };
        LOG_WARN("Can't get user. Use no user for server side");
    }


    //----------------------------------------------------------------
    // arg_transfer_block_size
    //----------------------------------------------------------------
    if (arg_transfer_block_size.has_value()) {
        if (arg_transfer_block_size.value() > program_options_defaults::MAX_TRANSFER_BLOCK_SIZE) {
            LOG_WARN("Transfer block size is too large: {}. Set to MAX_TRANSFER_BLOCK_SIZE: {}",
                     arg_transfer_block_size.value(), program_options_defaults::MAX_TRANSFER_BLOCK_SIZE);
            arg_transfer_block_size = program_options_defaults::MAX_TRANSFER_BLOCK_SIZE;
        }
    }
    else {
        arg_transfer_block_size = 0;
    }

    if (arg_transfer_block_size.value() == 0) {
        LOG_TRACE("Transfer block size: automatic tuning");
    }
    else if (arg_transfer_block_size.value() < 1024 * 64) {
        LOG_WARN("Transfer block size {} is too small. You may encounter bad performance!", arg_transfer_block_size.value());
    }
    else {
        LOG_TRACE("Transfer block size: {}", arg_transfer_block_size.value());
    }

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
    total_channel_repeats_count = 0;
    if (arg_channels.empty()) {
        // Reuse portal as channel
        // Default arg_portal->repeats if not specified by user
        if (!arg_portal->repeats.has_value()) {
            arg_portal->repeats = program_options_defaults::SERVER_CHANNEL_REPEATS;
        }
        total_channel_repeats_count += arg_portal->repeats.value();

        LOG_INFO("Server channels are not specified, reuse portal as channel: {}", arg_portal->to_string());
    }

    for (infra::tcp_endpoint& ep : arg_channels) {
        if (!ep.port.has_value()) {
            ep.port = static_cast<uint16_t>(0);
        }
        if (!ep.repeats.has_value()) {
            ep.repeats = static_cast<size_t>(1);
        }
        ASSERT(ep.repeats.value() > 0);
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

        total_channel_repeats_count += ep.repeats.value();
    }
    LOG_INFO("Total server channels (including repeats): {}", total_channel_repeats_count);

    return true;
}
