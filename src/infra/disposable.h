#if !defined(_XCP_INFRA_DISPOSABLE_H_INCLUDED_)
#define _XCP_INFRA_DISPOSABLE_H_INCLUDED_

#if !defined(_XCP_INFRA_INFRA_H_INCLUDED_)
#error "Please don't directly #include this file. Instead, #include infra.h"
#endif  // !defined(_XCP_INFRA_INFRA_H_INCLUDED_)


namespace infra
{
    class disposable
    {
    protected:
        virtual void dispose_impl() noexcept = 0;
        disposable() noexcept = default;

    public:
        XCP_DISABLE_COPY_CONSTRUCTOR(disposable)
        XCP_DISABLE_MOVE_CONSTRUCTOR(disposable)

        virtual ~disposable() noexcept
        {
            ASSERT(_status == STATUS_DISPOSED);
        }

        void dispose() noexcept
        {
            int status = STATUS_NORMAL;
            if (_status.compare_exchange_strong(status, STATUS_DISPOSING)) {
                dispose_impl();
                _status = STATUS_DISPOSED;
                return;
            }

            if (status == STATUS_DISPOSED) {
                return;
            }

            wait_for_dispose_done();
        }

        void async_dispose(const bool wait_for_done)
        {
            int status = STATUS_NORMAL;
            if (_status.compare_exchange_strong(status, STATUS_DISPOSING)) {

                std::thread thr([this]() noexcept -> void {
                    dispose_impl();
                    _status = STATUS_DISPOSED;
                });

                if (wait_for_done) {
                    thr.join();
                }
                else {
                    thr.detach();
                }
                return;
            }

            if (status == STATUS_DISPOSED) {
                return;
            }

            ASSERT(status == STATUS_DISPOSING);
            if (wait_for_done) {
                wait_for_dispose_done();
            }
        }

        bool is_dispose_required() const noexcept
        {
            return (_status != STATUS_NORMAL);
        }

    private:
        void wait_for_dispose_done() const
        {
            while (_status == STATUS_DISPOSING) {
                //std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ASSERT(_status == STATUS_DISPOSED);
        }

    private:
        static constexpr const int STATUS_NORMAL = 0;
        static constexpr const int STATUS_DISPOSING = 1;
        static constexpr const int STATUS_DISPOSED = 2;

        std::atomic_int _status { STATUS_NORMAL };
    };

}  // namespace infra


#endif  // !defined(_XCP_INFRA_DISPOSABLE_H_INCLUDED_)
