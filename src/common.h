#if !defined(_XCP_COMMON_H_INCLUDED_)
#define _XCP_COMMON_H_INCLUDED_

#include "infra/infra.h"

#include "program_options.h"
#include "message.h"
#include "server.h"
#include "client.h"


// TODO: move to thread pool?
inline std::thread* launch_thread(const std::function<void(std::thread*)>& fn)
{
    auto pair = new std::pair<std::mutex, std::thread*>;
    std::lock_guard<std::mutex> lock(pair->first);

    std::thread* const thr = new std::thread(
        [fn, pair]() mutable  // they MUST be passed by value!
        {
            { std::lock_guard<std::mutex> lock(pair->first); }

            assert(pair->second != nullptr);
            std::thread* const thr = pair->second;
            delete pair;

            // Use thr now
            fn(thr);
        });
    pair->second = thr;
    return thr;
}


#endif  // !defined(_XCP_COMMON_H_INCLUDED_)
