#include "common.h"


//==============================================================================
// Global variables
//==============================================================================

xcp::xcp_program_options g_xcp_program_options;



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
    // adsf
    //


    LOG_INFO("Hello from xcp");
    return 0;
}
