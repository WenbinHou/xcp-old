#if !defined(_XCP_INFRA_SWEEPER_H_INCLUDED_)
#define _XCP_INFRA_SWEEPER_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    class sweeper
    {
    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(sweeper)
        XCP_DISABLE_MOVE_CONSTRUCTOR(sweeper)

        template<typename TFunc>
        sweeper(TFunc fn)  // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
            : _suppressed(false),
              _sweep_fn(std::move(fn))
        { }

        ~sweeper()
        {
            if (!_suppressed) {
                if (_sweep_fn) {
                    _sweep_fn();
                }
            }
        }

        void suppress_sweep() noexcept
        {
            _suppressed = true;
        }

    private:
        bool _suppressed;
        const std::function<void() /*noexcept*/> _sweep_fn;
    };

}  // namespace infra


#endif  // !defined(_XCP_INFRA_SWEEPER_H_INCLUDED_)
