#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#define _XCP_INFRA_INFRA_H_INCLUDED_

#include "predef.h"
#include "macro.h"

#include "logging.h"
#include "assertion.h"
#include "sweeper.h"
#include "disposable.h"

#include "semaphore.h"
#include "manual_reset_event.h"
#include "rundown_protection.h"
#include "gate_guard.h"
#include "sighandle.h"

#include "network.h"
#include "user.h"



//
// Struct for globally initialization and finalization
//
namespace infra
{
    struct global_initialize_finalize_t
    {
        XCP_DISABLE_COPY_CONSTRUCTOR(global_initialize_finalize_t)
        XCP_DISABLE_MOVE_CONSTRUCTOR(global_initialize_finalize_t)

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

}  // namespace infra


#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
