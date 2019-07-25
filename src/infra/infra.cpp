#define __INFRA_CPP__
#include "infra.h"

namespace infra
{
    struct global_initialize_finalize_t
    {
        global_initialize_finalize_t()
        {
            // Initialize logging
            // This must be called BEFORE initializing everything else
            global_initialize_logging();

            // Initialize network
            global_initialize_network();
        }

        ~global_initialize_finalize_t() noexcept
        {
            // Finalize network
            global_finalize_network();

            // Finalize logging
            // This must be called AFTER finalizing everything else
            global_finalize_logging();
        }
    };

    [[maybe_unused]]
    const global_initialize_finalize_t g_global_initialize_finalize;  // NOLINT(cert-err58-cpp)

}  // namespace infra
