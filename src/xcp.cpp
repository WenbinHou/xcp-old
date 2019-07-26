#include "common.h"


//==============================================================================
// Global variables
//==============================================================================

xcp::xcp_program_options g_xcp_program_options;

std::vector<std::shared_ptr<xcp::client_channel_state>> g_client_channels;
std::shared_ptr<xcp::client_portal_state> g_client_portal = nullptr;

infra::identity_t g_client_identity;



//==============================================================================
// Global functions
//==============================================================================

bool connect_server_portal()
{
    infra::sweeper sweep = [&]() {
        g_client_portal.reset();
    };

    //
    // Connect to server portal
    //
    g_client_portal = std::make_shared<xcp::client_portal_state>(g_xcp_program_options.server_portal);
    if (!g_client_portal->init()) {
        LOG_ERROR("Init server_portal_state failed for {}", g_xcp_program_options.server_portal.to_string());
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
    CLI::App app("xcp client", "xcp");
    g_xcp_program_options.add_options(app);
    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    if (!g_xcp_program_options.post_process()) {
        LOG_ERROR("xcp_program_options post_process() failed");
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

        // Close client portal
        g_client_portal.reset();

        // Close client channels
        g_client_channels.clear();
        
        LOG_INFO("Bye!");
    };

    // Connect to server portal
    if (!connect_server_portal()) {
        LOG_ERROR("connect_server_portal() failed");
        return 1;
    }

    // Wait for exit...
    LOG_TRACE("Waiting for exit on main thread...");
    infra::sighandle::wait_for_exit_required();

    return 0;
}
