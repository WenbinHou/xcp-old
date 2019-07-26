#include "common.h"


//==============================================================================
// Global variables
//==============================================================================

xcp::xcpd_program_options g_xcpd_program_options;

std::vector<std::shared_ptr<xcp::server_channel_state>> g_server_channels { };
std::shared_ptr<xcp::server_portal_state> g_server_portal = nullptr;



//==============================================================================
// Global functions
//==============================================================================

void global_remove_all_clients()
{
    assert(infra::sighandle::is_exit_required());
    LOG_DEBUG("Removing all clients...");
    

    LOG_DEBUG("All clients removed");
}

bool bind_server_channels()
{
    infra::sweeper sweep = [&]() {
        g_server_channels.clear();
    };

    for (infra::tcp_endpoint_repeatable& ep : g_xcpd_program_options.arg_channels) {
        std::shared_ptr<xcp::server_channel_state> state = std::make_shared<xcp::server_channel_state>(ep);
        if (!state->init()) {
            LOG_ERROR("Init server_channel_state failed for {}", ep.to_string());
            return false;
        }
        g_server_channels.emplace_back(std::move(state));
    }

    sweep.suppress_sweep();
    return true;
}

bool bind_server_portal()
{
    infra::sweeper sweep = [&]() {
        g_server_portal.reset();
    };

    assert(g_xcpd_program_options.arg_portal.has_value());
    g_server_portal = std::make_shared<xcp::server_portal_state>(g_xcpd_program_options.arg_portal.value());
    if (!g_server_portal->init()) {
        LOG_ERROR("Init server_portal_state failed for {}", g_xcpd_program_options.arg_portal->to_string());
        return false;
    }

    sweep.suppress_sweep();
    return true;
}


int main(int argc, char* argv[])
{
    //
    // Global initializer and finalizer
    //
    [[maybe_unused]]
    const infra::global_initialize_finalize_t __init_fin;

    //
    // Parse command line arguments
    //
    CLI::App app("xcp server", "xcpd");
    g_xcpd_program_options.add_options(app);
    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    if (!g_xcpd_program_options.post_process()) {
        LOG_ERROR("xcpd_program_options post_process() failed");
        return 1;
    }


    //
    // On-exit sweeper
    //
    infra::sweeper exit_sweep = [&]() {
        // Set exit required flag first!
        if (!infra::sighandle::is_exit_required()) {
            infra::sighandle::require_exit();
        }

        // Close server portal
        g_server_portal.reset();

        // Close server channels
        g_server_channels.clear();

        // Remove clients
        global_remove_all_clients();

        LOG_INFO("Bye!");
    };


    // Bind to server channels
    if (!bind_server_channels()) {
        LOG_ERROR("bind_server_channels() failed");
        return 1;
    }

    // Bind to server portal
    if (!bind_server_portal()) {
        LOG_ERROR("bind_server_portal() failed");
        return 1;
    }

    // Wait for exit...
    LOG_TRACE("Waiting for exit on main thread...");
    infra::sighandle::wait_for_exit_required();

    return 0;
}
