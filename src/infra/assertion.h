#if !defined(_XCP_INFRA_ASSERTION_H_INCLUDED_)
#define _XCP_INFRA_ASSERTION_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


#define ASSERT(_What_, ...) \
    do { \
        if (!(_What_)) { \
            JUST(LOG_ERROR("ASSERT(" #_What_ ") failed. " __VA_ARGS__)); \
            std::abort(); \
        } \
    } while(false)


#define PANIC_TERMINATE(...) \
    do { \
        JUST(LOG_ERROR("PANIC! " __VA_ARGS__)); \
        std::abort(); \
    } while(false)


#endif  // !defined(_XCP_INFRA_ASSERTION_H_INCLUDED_)
