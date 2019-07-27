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
    std::shared_ptr<xcp::xcpd_program_options> options = std::make_shared<xcp::xcpd_program_options>();
    CLI::App app("xcp server", "xcpd");
    options->add_options(app);
    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    if (!options->post_process()) {
        LOG_ERROR("xcpd_program_options post_process() failed");
        return 1;
    }

    {
        std::shared_ptr<xcp::server_portal_state> server_portal = std::make_shared<xcp::server_portal_state>(options);

        //
        // On-exit sweeper
        //
        infra::sweeper exit_sweep = [&]() {
            // Set exit required flag first!
            if (!infra::sighandle::is_exit_required()) {
                infra::sighandle::require_exit();
            }

            // Close server portal
            server_portal->dispose();
            server_portal.reset();

            LOG_INFO("Bye!");
        };

        if (!server_portal->init()) {
            LOG_ERROR("server_portal init() failed");
            return 1;
        }


        // Wait for exit...
        LOG_TRACE("Waiting for exit on main thread...");
        infra::sighandle::wait_for_exit_required();
    }

    return 0;
}
