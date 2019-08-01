#include "common.h"


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
    std::shared_ptr<xcp::xcp_program_options> options = std::make_shared<xcp::xcp_program_options>();
    CLI::App app("xcp client", "xcp");
    options->add_options(app);
    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    if (!options->post_process()) {
        LOG_ERROR("xcp_program_options post_process() failed");
        return 1;
    }

    // Initialize signal handler
    infra::sighandle::setup_signal_handler();

    // temp test
    infra::user_name_t user;
    infra::get_user_name(user);
    infra::get_user_home_path(user);

    std::shared_ptr<xcp::client_portal_state> client_portal = std::make_shared<xcp::client_portal_state>(options);
    bool success = false;

    {
        //
        // On-exit sweeper
        //
        infra::sweeper exit_sweep = [&]() {
            // Set exit required flag first!
            if (!infra::sighandle::is_exit_required()) {
                infra::sighandle::require_exit();
            }

            // Close client portal
            client_portal->dispose();
            success = (client_portal->transfer_result_status == xcp::client_portal_state::TRANSFER_SUCCEEDED);
            client_portal.reset();

            LOG_INFO("Bye!");
        };

        if (!client_portal->init()) {
            LOG_ERROR("Init server_portal_state failed for {}", options->server_portal.to_string());
            return 1;
        }


        // Wait for exit...
        LOG_TRACE("Waiting for exit on main thread...");
        infra::sighandle::wait_for_exit_required();
    }

    return (success ? 0 : 1);
}
