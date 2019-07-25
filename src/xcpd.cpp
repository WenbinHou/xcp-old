#include "infra/infra.h"

int main()
{
    [[maybe_unused]]
    const infra::global_initialize_finalize_t __init_fin;

    LOG_INFO("Hello from xcpd");
    return 0;
}
