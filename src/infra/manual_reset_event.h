#if !defined(_XCP_INFRA_MANUAL_RESET_EVENT_H_INCLUDED_)
#define _XCP_INFRA_MANUAL_RESET_EVENT_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    class manual_reset_event
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(manual_reset_event)
        XCP_DISABLE_MOVE_CONSTRUCTOR(manual_reset_event)

        explicit manual_reset_event(const bool is_set)
            : _is_set(is_set)
        { }

        void reset()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _is_set = false;
        }

        void set()
        {
            if (_is_set) return;

            std::unique_lock<std::mutex> lock(_mutex);
            _is_set = true;
            _cond.notify_all();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            while (!_is_set) {
                _cond.wait(lock);
            }
        }

        [[nodiscard]]
        bool is_set() const noexcept
        {
            return _is_set;
        }

    private:
        std::mutex _mutex { };
        std::condition_variable _cond { };
        std::atomic_bool _is_set;
    };

}  // namespace infra

#endif  // !defined(_XCP_INFRA_MANUAL_RESET_EVENT_H_INCLUDED_)
