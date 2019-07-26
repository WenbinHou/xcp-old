#if !defined(_XCP_INFRA_SEMAPHORE_H_INCLUDED_)
#define _XCP_INFRA_SEMAPHORE_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)

namespace infra
{
    class semaphore
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(semaphore)
        XCP_DISABLE_MOVE_CONSTRUCTOR(semaphore)

        explicit semaphore(const std::uint32_t init_value)
        {
#if PLATFORM_WINDOWS

            if (init_value > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max())) {
                LOG_ERROR("init_value {} is too large", init_value);
                throw std::invalid_argument("semaphore init_value is too large");
            }

            _sem_handle = CreateSemaphoreA(
                NULL,  // not inheritable
                static_cast<LONG>(init_value),
                std::numeric_limits<LONG>::max(),
                nullptr);
            if (_sem_handle == NULL) {
                const DWORD gle = GetLastError();
                LOG_ERROR("CreateSemaphoreA (init_value = {}) failed. GetLastError = {} ({})", init_value, gle, str_getlasterror(gle));
                THROW_SYSTEM_ERROR((int)gle, CreateSemaphoreA);
            }

#elif PLATFORM_LINUX || PLATFORM_CYGWIN
            if (sem_init(&_sem, /*pshared*/0, (unsigned int)init_value) != 0) {
                const int err = errno;
                if (err == EINVAL) {
                    // init_value exceeds SEM_VALUE_MAX
                    LOG_ERROR("init_value = {} exceeds SEM_VALUE_MAX", init_value);
                }
                else {
                    LOG_ERROR("sem_init (init_value = {}) failed. errno = {} ({})", init_value, err, strerror(err));
                }
                THROW_SYSTEM_ERROR(err, sem_init);
            }
#else
#   error "Unknown platform"
#endif
        }


        void wait()
        {
            internal_wait(/*is_nonblock*/false);
        }

        bool try_wait()
        {
            return internal_wait(/*is_nonblock*/true);
        }

        void post()
        {
#if PLATFORM_WINDOWS
            if (!ReleaseSemaphore(_sem_handle, 1, NULL)) {
                const DWORD gle = GetLastError();
                LOG_ERROR("ReleaseSemaphore() failed. GetLastError = {} ({})", gle, str_getlasterror(gle));
                THROW_SYSTEM_ERROR((int)gle, ReleaseSemaphore);
            }

#elif PLATFORM_LINUX || PLATFORM_CYGWIN
            if (sem_post(&_sem) != 0) {
                const int err = errno;
                LOG_ERROR("sem_post() failed. errno = {} ({})", err, strerror(err));
                THROW_SYSTEM_ERROR((int)err, sem_post);
            }
#else
#   error "Unknown platform"
#endif
        }

        ~semaphore() noexcept
        {
#if PLATFORM_WINDOWS
            if (_sem_handle) {
                (void)CloseHandle(_sem_handle);
                _sem_handle = nullptr;
            }

#elif PLATFORM_LINUX || PLATFORM_CYGWIN
            if (sem_destroy(&_sem) != 0) {
                LOG_ERROR("sem_destroy() failed. errno = {} ({})", errno, strerror(errno));
            }
#else
#   error "Unknown platform"
#endif
        }

    private:
        bool internal_wait(const bool is_nonblock)
        {
#if PLATFORM_WINDOWS
            const DWORD timeout = is_nonblock ? 0 : INFINITE;
            DWORD ret = WaitForSingleObject(_sem_handle, timeout);
            if (ret == WAIT_OBJECT_0) {
                return true;
            }
            else if (ret == WAIT_TIMEOUT && is_nonblock) {
                return false;
            }
            else {
                const DWORD gle = GetLastError();
                LOG_ERROR("WaitForSingleObject() returns {}. GetLastError = {} ({})", ret, gle, str_getlasterror(gle));
                THROW_SYSTEM_ERROR((int)gle, WaitForSingleObject);
            }

#elif PLATFORM_LINUX || PLATFORM_CYGWIN
            int rc;
            if (is_nonblock) {
                rc = sem_trywait(&_sem);
            }
            else {
                rc = sem_wait(&_sem);
            }

            if (rc == 0) {
                return true;
            }
            else {
                const int err = errno;
                if (err == EAGAIN) {
                    return false;
                }
                else {
                    if (is_nonblock) {
                        LOG_ERROR("sem_trywait() failed. errno = {} ({})", err, strerror(err));
                        THROW_SYSTEM_ERROR(err, sem_trywait);
                    }
                    else {
                        LOG_ERROR("sem_wait() failed. errno = {} ({})", err, strerror(err));
                        THROW_SYSTEM_ERROR(err, sem_wait);
                    }
                }
            }

#else
#   error "Unknown platform"
#endif
        }


    private:
#if PLATFORM_WINDOWS
        HANDLE _sem_handle = NULL;
#elif PLATFORM_LINUX || PLATFORM_CYGWIN
        sem_t _sem { };
#else
#   error "Unknown platform"
#endif

    };

}  // namespace infra


#endif  // !defined(_XCP_INFRA_SEMAPHORE_H_INCLUDED_)
