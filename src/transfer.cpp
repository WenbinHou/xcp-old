#include "common.h"

namespace xcp
{
    //--------------------------------------------------------------------------
    // struct transfer_source
    //--------------------------------------------------------------------------
    bool transfer_source::invoke_portal(infra::socket_t sock) /*override*/
    {
        // Wait for message_transfer_destination_finished
        {
            message_transfer_destination_finished msg;
            if (!message_recv(sock, msg)) {
                LOG_ERROR("Transfer error: receive message_transfer_destination_finished failed");
                return false;
            }

            if (msg.error_code != 0) {
                LOG_ERROR("Transfer error: {}. errno = {} ({})", msg.error_message, msg.error_code, strerror(msg.error_code));
                return false;
            }
        }

        LOG_TRACE("Transfer source portal done!");
        return true;
    }

    bool transfer_source::invoke_channel(infra::socket_t sock) /*override*/
    {
#if PLATFORM_WINDOWS
        HANDLE hEvent = CreateEventW(NULL, /*bManualReset*/TRUE, FALSE, NULL);
        if (hEvent == NULL) {
            const DWORD gle = GetLastError();
            LOG_ERROR("CreateEventW() failed. GetLastError = {} ({})", gle, infra::str_getlasterror(gle));
            return false;
        }

        const infra::sweeper sweep_event = [&]() {
            CloseHandle(hEvent);
            hEvent = NULL;
        };

#elif PLATFORM_LINUX
        // Nothing to do
#else
#   error "Unknown platform"
#endif

        ASSERT(this->_file_size != TRANSFER_INVALID_FILE_SIZE);

        while (true) {
            const uint64_t curr = _curr_offset.fetch_add((uint64_t)BLOCK_SIZE);
            if (curr >= this->_file_size) {
                break;
            }

            uint32_t block_size = BLOCK_SIZE;
            if (curr + BLOCK_SIZE >= this->_file_size) {
                block_size = (uint32_t)(this->_file_size - curr);
            }

            char header[12];
            *(uint32_t*)&header[0] = htonl((uint32_t)(curr >> 32));  // offset_high
            *(uint32_t*)&header[4] = htonl((uint32_t)(curr & 0xFFFFFFFF));  // offset_low
            *(uint32_t*)&header[8] = htonl(block_size);  // block size

#if PLATFORM_WINDOWS
            // Send header and body
            {
                TRANSMIT_FILE_BUFFERS headtail { };
                headtail.Head = &header;
                headtail.HeadLength = sizeof(header);
                headtail.Tail = nullptr;
                headtail.TailLength = 0;

                OVERLAPPED overlapped { };
                overlapped.Offset = (DWORD)(curr & 0xFFFFFFFF);
                overlapped.OffsetHigh = (DWORD)(curr >> 32);
                overlapped.hEvent = hEvent;

                const BOOL success = TransmitFile(
                    sock,
                    this->_file_handle,
                    (DWORD)block_size,
                    0,
                    &overlapped,
                    &headtail,
                    TF_USE_KERNEL_APC);
                if (success) {
                    // nothing to do
                }
                else {
                    const int wsagle = WSAGetLastError();
                    static_assert(WSA_IO_PENDING == ERROR_IO_PENDING);
                    if (wsagle == WSA_IO_PENDING) {
                        DWORD written = 0;
                        if (!GetOverlappedResult((HANDLE)sock, &overlapped, &written, TRUE)) {
                            LOG_ERROR("GetOverlappedResult() failed. GetLastError = {} ({})", GetLastError(), infra::str_getlasterror(GetLastError()));
                            return false;
                        }

                        const uint32_t expected_written = block_size + sizeof(header);
                        if (written != expected_written) {
                            LOG_ERROR("TransmitFile() overlapped result error: expected written {} bytes, actually written {} bytes",
                                      expected_written, written);
                            return false;
                        }
                    }
                    else {
                        LOG_ERROR("TransmitFile() failed. WSAGetLastError = {} ({})", wsagle, infra::str_getlasterror(wsagle));
                        return false;
                    }
                }

                // Reset the event
                if (!ResetEvent(hEvent)) {
                    LOG_ERROR("ResetEvent() failed. GetLastError = {} ({})", GetLastError(), infra::str_getlasterror(GetLastError()));
                    return false;
                }
            }

#elif PLATFORM_LINUX
            // Send header
            {
                const ssize_t cnt = send(sock, header, sizeof(header), MSG_MORE);
                if (cnt != (ssize_t)sizeof(header)) {
                    LOG_ERROR("send() header expects {}, but returns {}. {}",
                              sizeof(header), cnt, infra::socket_error_description());
                    return false;
                }
            }

            // Send body
            {
                off64_t offset = curr;
                const ssize_t cnt = sendfile64(sock, this->_file_handle, &offset, block_size);
                if (cnt != (ssize_t)block_size) {
                    LOG_ERROR("sendfile(offset={}, len={}) expects {}, but returns {}. {}",
                              offset, block_size, block_size, cnt, infra::socket_error_description());
                    return false;
                }
            }
#else
#   error "Unknown platform"
#endif

            LOG_TRACE("Sent file block: offset = {}, block_size = {}", curr, block_size);
        }


        //
        // Send a tail (indicating current channel is done)
        //
        char final_tail[12];  // the same size as header, indicating the transfer is done
        *(uint32_t*)&final_tail[0] = htonl((uint32_t)0xFFFFFFFF);  // offset_high
        *(uint32_t*)&final_tail[4] = htonl((uint32_t)0xFFFFFFFF);  // offset_low
        *(uint32_t*)&final_tail[8] = htonl(0);  // block size

#if PLATFORM_WINDOWS || PLATFORM_LINUX
        {
            const int cnt = (int)send(sock, final_tail, sizeof(final_tail), 0);
            if (cnt != (int)sizeof(final_tail)) {
                LOG_ERROR("send() expects {}, but returns {}. {}",
                          sizeof(final_tail), cnt, infra::socket_error_description());
                return false;
            }
        }

#else
#   error "Unknown platform"
#endif
        LOG_TRACE("Send done for current channel");

        return true;
    }

    void transfer_source::prepare_transfer_regular_file(const stdfs::path& file_path)
    {
#if PLATFORM_WINDOWS
        _file_handle = CreateFileW(
            file_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (_file_handle == INVALID_HANDLE_VALUE) {
            const DWORD gle = GetLastError();
            const std::string message = std::string("CreateFileW() ") + file_path.u8string() + " for read failed. GetLastError = " +
                                        std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
            LOG_ERROR("{}", message);
            throw transfer_error(EIO, message);
        }
        this->_file_size = stdfs::file_size(file_path);  // TODO: use Windows API and check error

#elif PLATFORM_LINUX
        _file_handle = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for read failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + file_path.c_str() + " for read failed");
        }
        this->_file_size = stdfs::file_size(file_path);  // TODO: use Linux API and check error

#else
#   error "Unknown platform"
#endif

        ASSERT(this->_file_size != TRANSFER_INVALID_FILE_SIZE);
    }

    void transfer_source::dispose_impl() noexcept /*override*/
    {
#if PLATFORM_WINDOWS
        if (_file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(_file_handle);
            _file_handle = INVALID_HANDLE_VALUE;
        }

#elif PLATFORM_LINUX
        if (_file_handle != -1) {
            close(_file_handle);
            _file_handle = -1;
        }
#else
#   error "Unknown platform"
#endif
    }

    transfer_source::transfer_source(const std::string& src_path)
    {
        infra::sweeper error_cleanup = [&]() {
            this->dispose();
        };

        const stdfs::path src_real_path = stdfs::weakly_canonical(src_path);
        const stdfs::file_status status = stdfs::status(src_real_path);
        switch (status.type()) {
            case stdfs::file_type::regular: {
                prepare_transfer_regular_file(src_real_path);
                break;
            }
            case stdfs::file_type::directory: {
                throw transfer_error(ENOSYS, "Transfer a directory is not supported: " + src_path);
            }
            case stdfs::file_type::not_found: {
                throw transfer_error(ENOENT, "File not found: " + src_path);
            }
            case stdfs::file_type::block: {
                throw transfer_error(ENOSYS, "Transfer a block device is not supported: " + src_path);
            }
            case stdfs::file_type::character: {
                throw transfer_error(ENOSYS, "Transfer a character device is not supported: " + src_path);
            }
            case stdfs::file_type::fifo: {
                throw transfer_error(ENOSYS, "Transfer a FIFO file is not supported: " + src_path);
            }
            case stdfs::file_type::socket: {
                throw transfer_error(ENOSYS, "Transfer a socket file is not supported: " + src_path);
            }
            case stdfs::file_type::none:
            case stdfs::file_type::symlink:
            case stdfs::file_type::unknown:
            default: {
                throw transfer_error(ENOSYS, "Unknown file type: " + src_path);
            }
        }

        error_cleanup.suppress_sweep();
    }


    //--------------------------------------------------------------------------
    // struct transfer_destination
    //--------------------------------------------------------------------------
    bool transfer_destination::invoke_portal(infra::socket_t sock) /*override*/
    {
        // Wait for _sem_all_channels_finished
        this->_sem_all_channels_finished.wait();

        // Send message_transfer_destination_finished
        LOG_TRACE("Transfer portal: sending message_transfer_destination_finished...");
        {
            message_transfer_destination_finished msg;
            msg.error_code = 0;

            if (!message_send(sock, msg)) {
                LOG_ERROR("Transfer error: send message_transfer_destination_finished failed");
                return false;
            }
        }

        LOG_TRACE("Transfer destination portal done!");
        return true;
    }

    bool transfer_destination::invoke_channel(infra::socket_t sock) /*override*/
    {
        const infra::sweeper sweep = [&]() {
            const size_t finished = ++this->_finished_channel_repeats_count;
            if (finished == this->_total_channel_repeats_count) {
                this->_sem_all_channels_finished.post();
            }
        };

        while (true) {
            char header[12];

#if PLATFORM_WINDOWS || PLATFORM_LINUX
            // Receive header
            uint64_t offset;
            uint32_t block_size;
            {
                const int cnt = (int)recv(sock, header, sizeof(header), MSG_WAITALL);
                if (cnt != (int)sizeof(header)) {
                    LOG_ERROR("recv() header expects {}, but returns {}. {}",
                              sizeof(header), cnt, infra::socket_error_description());
                    return false;
                }

                const uint32_t offset_high = ntohl(*(uint32_t*)&header[0]);
                const uint32_t offset_low = ntohl(*(uint32_t*)&header[4]);
                offset = ((std::uint64_t)offset_high) << 32 | ((std::uint64_t)offset_low);

                block_size = ntohl(*(uint32_t*)&header[8]);
            }

            if (block_size == 0) {  // indicating sending is done!
                ASSERT(offset == (uint64_t)-1);
                LOG_TRACE("Receive done for current channel");
                break;
            }

            ASSERT(_dst_file_mapped != nullptr);

            // Receive body
            {
                const int cnt = (int)recv(sock, (char*)_dst_file_mapped + offset, block_size, MSG_WAITALL);
                if (cnt != (int)block_size) {
                    LOG_ERROR("recv(offset={}, len={}) expects {}, but returns {}. {}",
                              offset, block_size, block_size, cnt, infra::socket_error_description());
                    return false;
                }
            }

#else
#   error "Unknown platform"
#endif

            LOG_TRACE("Received file block: offset = {}, block_size = {}", offset, block_size);
        }

        return true;
    }

    void transfer_destination::dispose_impl() noexcept /*override*/
    {
#if PLATFORM_WINDOWS
        if (_file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(_file_handle);
            _file_handle = INVALID_HANDLE_VALUE;
        }

        if (_dst_file_mapped != nullptr) {
            ASSERT(this->_file_size != TRANSFER_INVALID_FILE_SIZE);
            // TODO: FlushViewOfFile?
            UnmapViewOfFile(_dst_file_mapped);
            _dst_file_mapped = nullptr;
        }

#elif PLATFORM_LINUX
        if (_file_handle != -1) {
            close(_file_handle);
            _file_handle = -1;
        }

        if (_dst_file_mapped != nullptr) {
            ASSERT(this->_file_size != TRANSFER_INVALID_FILE_SIZE);
            // TODO: msync?
            munmap(_dst_file_mapped, this->_file_size);
            _dst_file_mapped = nullptr;
        }
#else
#   error "Unknown platform"
#endif
    }

    transfer_destination::transfer_destination(const std::string& src_file_name, const std::string& dst_path)
    {
        infra::sweeper error_cleanup = [&]() {
            this->dispose();
        };

        stdfs::path dst_real_path = stdfs::weakly_canonical(dst_path);
        const stdfs::file_status status = stdfs::status(dst_real_path);
        switch (status.type()) {
            case stdfs::file_type::regular: {
                _dst_file_path = dst_path;  // TODO: or dst_real_path?
                break;
            }
            case stdfs::file_type::directory: {
                if (src_file_name.empty()) {
                    throw transfer_error(ENOSYS, "Transfer to a directory without given a file name: " + dst_path);
                }
                _dst_file_path = dst_real_path / src_file_name;
                break;
            }
            case stdfs::file_type::not_found: {
                _dst_file_path = dst_path;  // create a new file
                break;
            }
            case stdfs::file_type::block: {
                throw transfer_error(ENOSYS, "Transfer to a block device is not supported: " + dst_path);
            }
            case stdfs::file_type::character: {
                throw transfer_error(ENOSYS, "Transfer to a character device is not supported: " + dst_path);
            }
            case stdfs::file_type::fifo: {
                throw transfer_error(ENOSYS, "Transfer to a FIFO file is not supported: " + dst_path);
            }
            case stdfs::file_type::socket: {
                throw transfer_error(ENOSYS, "Transfer to a socket file is not supported: " + dst_path);
            }
            case stdfs::file_type::none:
            case stdfs::file_type::symlink:
            case stdfs::file_type::unknown:
            default: {
                throw transfer_error(ENOSYS, "Unknown file type: " + dst_path);
            }
        }

        LOG_TRACE("Save to destination file: {}", _dst_file_path.string());

        error_cleanup.suppress_sweep();
    }

    void transfer_destination::init_file(const uint64_t file_size, const size_t total_channel_repeats_count)
    {
        ASSERT(file_size != TRANSFER_INVALID_FILE_SIZE);
        this->_file_size = file_size;
        this->_total_channel_repeats_count = total_channel_repeats_count;

#if PLATFORM_WINDOWS
        _file_handle = CreateFileW(
            _dst_file_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (_file_handle == INVALID_HANDLE_VALUE) {
            const DWORD gle = GetLastError();
            const std::string message = std::string("CreateFileW() ") + _dst_file_path.u8string() + " for read failed. GetLastError = " +
                                        std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
            LOG_ERROR("{}", message);
            throw transfer_error(EIO, message);
        }

        // truncate file to _file_size
        {
            // move file pointer to _file_size
            LARGE_INTEGER eof;
            eof.QuadPart = (LONGLONG)this->_file_size;
            if (!SetFilePointerEx(_file_handle, eof, NULL, FILE_BEGIN)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetFilePointerEx() ") + _dst_file_path.u8string() + " to " +
                                            std::to_string(_file_size) + " failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            if (!SetEndOfFile(_file_handle)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetEndOfFile() ") + _dst_file_path.u8string() + " at " +
                                            std::to_string(_file_size) + " failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            // move file pointer back to 0
            eof.QuadPart = 0;
            if (!SetFilePointerEx(_file_handle, eof, NULL, FILE_BEGIN)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetFilePointerEx() ") + _dst_file_path.u8string() + " to 0 failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }
        }

        if (_file_size > 0) {
            if (_file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", _file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(_file_size));
            }

            HANDLE hMap = CreateFileMappingW(
                _file_handle,
                NULL,
                PAGE_READWRITE,
                0,
                0,
                NULL);
            if (hMap == NULL) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("CreateFileMappingW() ") + _dst_file_path.u8string() + " (size: " +
                                            std::to_string(_file_size) + ") failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            infra::sweeper sweep_hmap = [&]() {
                CloseHandle(hMap);
                hMap = NULL;
            };

            _dst_file_mapped = MapViewOfFile(
                hMap,
                FILE_MAP_READ | FILE_MAP_WRITE,
                0,
                0,
                0);
            if (_dst_file_mapped == NULL) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("MapViewOfFile() ") + _dst_file_path.u8string() + " (size: " +
                                            std::to_string(_file_size) + ") failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }
            LOG_TRACE("MapViewOfFile() {} to {}", _dst_file_path.u8string(), _dst_file_mapped);
        }

#elif PLATFORM_LINUX
        _file_handle = open(_dst_file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0777);  // TODO: file permission?
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for write failed. errno = {} ({})", _dst_file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + _dst_file_path.c_str() + " for write failed");
        }

        if (_file_size > 0) {
            if (_file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", _file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(_file_size));
            }

            if (ftruncate64(_file_handle, _file_size) != 0) {
                LOG_ERROR("ftruncate64() {} to {} failed. errno = {} ({})", _dst_file_path.c_str(), _file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("ftruncate64() ") + _dst_file_path.c_str() + " to " + std::to_string(_file_size) + " failed");
            }

#   if !defined(MAP_HUGE_2MB)
#       define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#   endif
#   if !defined(MAP_HUGE_1GB)
#       define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#   endif
            _dst_file_mapped = mmap(
                nullptr,
                (size_t)_file_size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,  // TODO: support MAP_HUGETLB | MAP_HUGE_2MB ?
                _file_handle,
                0);
            if (_dst_file_mapped == MAP_FAILED) {
                _dst_file_mapped = nullptr;
                LOG_ERROR("mmap({}, size={}) for write failed. errno = {} ({})", _dst_file_path.c_str(), _file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("mmap() ") + _dst_file_path.c_str() + " for write failed");
            }
            LOG_TRACE("mmap() {} to {}", _dst_file_path.c_str(), _dst_file_mapped);
        }
#else
#   error "Unknown platform"
#endif
    }

}  // namespace xcp

