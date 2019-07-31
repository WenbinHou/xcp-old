#if !defined(_XCP_INFRA_GATE_GUARD_H_INCLUDED_)
#define _XCP_INFRA_GATE_GUARD_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    class gate_guard
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(gate_guard)
        XCP_DISABLE_MOVE_CONSTRUCTOR(gate_guard)

        gate_guard() = default;

        void init(const uint64_t at_least_signal)
        {
            ASSERT(_curr_signal_count == 0);
            ASSERT(_at_least_signal == INVALID_SIGNAL_COUNT);
            ASSERT(at_least_signal != INVALID_SIGNAL_COUNT);
            ASSERT(at_least_signal > 0);
            _at_least_signal = at_least_signal;
        }

        void signal()
        {
            ASSERT(_at_least_signal != INVALID_SIGNAL_COUNT);

            uint64_t curr = ++_curr_signal_count;
            if (curr == _at_least_signal) {
                _mre.set();
            }
        }

        void force_signal_all()
        {
            ASSERT(_at_least_signal != INVALID_SIGNAL_COUNT);

            _curr_signal_count = _at_least_signal;
            _mre.set();
        }

        void wait()
        {
            ASSERT(_at_least_signal != INVALID_SIGNAL_COUNT);

            _mre.wait();
        }

        [[nodiscard]]
        bool can_pass() noexcept
        {
            ASSERT(_at_least_signal != INVALID_SIGNAL_COUNT);

            return (_curr_signal_count >= _at_least_signal);
        }

        [[nodiscard]]
        bool initialized() noexcept
        {
            return (_at_least_signal != INVALID_SIGNAL_COUNT);
        }

    private:
        static constexpr const uint64_t INVALID_SIGNAL_COUNT = (uint64_t)-1;
        
        manual_reset_event _mre { false };
        std::atomic_uint64_t _curr_signal_count { 0 };
        uint64_t _at_least_signal { INVALID_SIGNAL_COUNT };
    };

}  // namespace infra

#endif  // !defined(_XCP_INFRA_GATE_GUARD_H_INCLUDED_)
