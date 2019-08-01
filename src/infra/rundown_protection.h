#if !defined(_XCP_INFRA_RUNDOWN_PROTECTION_H_INCLUDED_)
#define _XCP_INFRA_RUNDOWN_PROTECTION_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    class rundown_protection
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(rundown_protection)
        XCP_DISABLE_MOVE_CONSTRUCTOR(rundown_protection)
        rundown_protection() = default;
        ~rundown_protection() noexcept = default;

        std::shared_lock<std::shared_mutex> acquire_shared()
        {
            std::lock_guard<std::mutex> serial(_serial);

            if (_required_rundown) {
                return std::shared_lock<std::shared_mutex>();
            }
            return std::shared_lock<std::shared_mutex>(_mutex);
        }

        std::unique_lock<std::shared_mutex> acquire_unique()
        {
            std::lock_guard<std::mutex> serial(_serial);

            if (_required_rundown) {
                return std::unique_lock<std::shared_mutex>();
            }
            return std::unique_lock<std::shared_mutex>(_mutex);
        }

        std::unique_lock<std::shared_mutex> acquire_rundown()
        {
            std::lock_guard<std::mutex> serial(_serial);

            _required_rundown = true;
            return std::unique_lock<std::shared_mutex>(_mutex);
        }

        //
        // NOTE:
        //  required_rundown() returns true doesn't mean rundown has completed
        //  (i.e., unique_lock acquired by acquire_rundown() may have NOT been unlocked)
        //
        bool required_rundown() const noexcept
        {
            return _required_rundown;
        }

    private:
        std::mutex _serial { };
        std::shared_mutex _mutex { };
        std::atomic_bool _required_rundown { false };
    };

}  // namespace infra

#endif  // !defined(_XCP_INFRA_RUNDOWN_PROTECTION_H_INCLUDED_)
