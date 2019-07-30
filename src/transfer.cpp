#include "common.h"

namespace xcp
{
    //--------------------------------------------------------------------------
    // struct transfer_source
    //--------------------------------------------------------------------------
    bool transfer_source::invoke_portal(std::shared_ptr<infra::os_socket_t> sock) /*override*/
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

        LOG_TRACE("Transfer source portal done successfully!");
        return true;
    }

    bool transfer_source::invoke_channel(std::shared_ptr<infra::os_socket_t> sock) /*override*/
    {
        ASSERT(this->_file_info.has_value());

        ASSERT(_init_transfer_block_size > 0);
        ASSERT(_init_transfer_block_size <= program_options_defaults::MAX_TRANSFER_BLOCK_SIZE);
        uint32_t curr_channel_block_size = (uint32_t)_init_transfer_block_size;

        while (true) {
            const uint64_t curr = _curr_offset.fetch_add((uint64_t)curr_channel_block_size);
            if (curr >= this->_file_info->file_size) {
                break;
            }

            uint32_t block_size = curr_channel_block_size;
            if (curr + curr_channel_block_size >= this->_file_info->file_size) {
                block_size = (uint32_t)(this->_file_info->file_size - curr);
            }

            char header[12];
            *(uint32_t*)&header[0] = htonl((uint32_t)(curr >> 32));  // offset_high
            *(uint32_t*)&header[4] = htonl((uint32_t)(curr & 0xFFFFFFFF));  // offset_low
            *(uint32_t*)&header[8] = htonl(block_size);  // block size

            // Get start time
            std::chrono::steady_clock::time_point start_time { };
            if (!_is_transfer_block_size_fixed) {
                start_time = std::chrono::steady_clock::now();
            }

            // Send the file
            {
                infra::socket_io_vec vec;
                vec.ptr = header;
                vec.len = sizeof(header);
                if (!sock->send_file(_file_handle, curr, block_size, &vec)) {
                    LOG_ERROR("Channel: send_file(offset={}, block_size={}) failed", curr, block_size);
                    return false;
                }
            }

            // Get end time
            std::chrono::steady_clock::time_point end_time { };
            if (!_is_transfer_block_size_fixed) {
                end_time = std::chrono::steady_clock::now();
            }

            // Adjust curr_channel_block_size according to current channel bandwidth
            // We would like to to a sendfile or TransmitFile at every 1 sec
            if (!_is_transfer_block_size_fixed) {
                uint64_t tmp_block_size = curr_channel_block_size;  // use uint64_t to prevent overflow

                const int64_t elapsed_usec = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
                if (elapsed_usec <= 0) {
                    LOG_WARN("Channel: end_time <= start_time: What happened?");
                    tmp_block_size *= 2; // Just double block size
                }
                else if (elapsed_usec <= 1000 * 1000) {  // elapsed time <= 1 sec
                    tmp_block_size = (uint64_t)((double)(tmp_block_size * 1000 * 1000) / (double)elapsed_usec);
                }
                else {  // elapsed time > 1 sec
                    // Use a decay rate: decay = 0.8
                    static constexpr const double DECAY = 0.8;
                    double tmp = (double)(tmp_block_size * 1000 * 1000) / (double)elapsed_usec;
                    tmp_block_size = (uint64_t)(((double)tmp_block_size) * DECAY + tmp * (1 - DECAY));
                }

                // Up-align block size to 64K
                constexpr const uint32_t ALIGN_SHIFT = 16;  // 64K = 1 << 16
                tmp_block_size = ((tmp_block_size + (1U << ALIGN_SHIFT) - 1) >> ALIGN_SHIFT) << ALIGN_SHIFT;

                // Make sure don't excceed MAX_TRANSFER_BLOCK_SIZE
                if (tmp_block_size > program_options_defaults::MAX_TRANSFER_BLOCK_SIZE) {
                    tmp_block_size = program_options_defaults::MAX_TRANSFER_BLOCK_SIZE;
                }
                else if (tmp_block_size == 0) {
                    tmp_block_size = (1U << ALIGN_SHIFT);
                }
                curr_channel_block_size = (uint32_t)tmp_block_size;

                LOG_TRACE("Channel: elapsed: {} usec, new channel_block_size: {}", elapsed_usec, curr_channel_block_size);
            }

            LOG_TRACE("Channel: sent file block: offset = {}, block_size = {}", curr, block_size);
        }


        //
        // Send a tail (indicating current channel is done)
        //
        {
            char final_tail[12];  // the same size as header, indicating the transfer is done
            *(uint32_t*)&final_tail[0] = htonl((uint32_t)0xFFFFFFFF);  // offset_high
            *(uint32_t*)&final_tail[4] = htonl((uint32_t)0xFFFFFFFF);  // offset_low
            *(uint32_t*)&final_tail[8] = htonl(0);  // block size

            if (!sock->send(final_tail, sizeof(final_tail))) {
                LOG_ERROR("Channel: send() final tail failed");
                return false;
            }
        }
        LOG_TRACE("Send done for current channel");

        return true;
    }

    void transfer_source::prepare_transfer_regular_file(const stdfs::path& file_path)
    {
        ASSERT(!_file_info.has_value());

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

        LARGE_INTEGER file_size { };
        if (!GetFileSizeEx(_file_handle, &file_size)) {
            const DWORD gle = GetLastError();
            const std::string message = std::string("GetFileSizeEx() ") + file_path.u8string() + " failed. GetLastError = " +
                                        std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
            LOG_ERROR("{}", message);
            throw transfer_error(EIO, message);
        }

        this->_file_info = basic_file_info();
        this->_file_info->file_size = file_size.QuadPart;
        this->_file_info->posix_perm = 0;

#elif PLATFORM_LINUX
        _file_handle = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for read failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + file_path.c_str() + " for read failed");
        }

        struct stat64 st { };
        if (fstat64(_file_handle, &st) != 0) {
            LOG_ERROR("fstat64() {} failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("fstat64() ") + file_path.c_str() + " failed");
        }

        this->_file_info = basic_file_info();
        this->_file_info->file_size = st.st_size;
        this->_file_info->posix_perm = (uint32_t)st.st_mode & 07777;

#else
#   error "Unknown platform"
#endif

        ASSERT(_file_info.has_value());
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

    transfer_source::transfer_source(const std::string& src_path, const uint64_t transfer_block_size)
        : _is_transfer_block_size_fixed((transfer_block_size != 0)),
          _init_transfer_block_size((transfer_block_size != 0) ? (uint32_t)transfer_block_size : TRANSFER_BLOCK_SIZE_INIT)
    {
        ASSERT(_init_transfer_block_size > 0);
        ASSERT(transfer_block_size <= (uint64_t)program_options_defaults::MAX_TRANSFER_BLOCK_SIZE);

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
    bool transfer_destination::invoke_portal(std::shared_ptr<infra::os_socket_t> sock) /*override*/
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

    bool transfer_destination::invoke_channel(std::shared_ptr<infra::os_socket_t> sock) /*override*/
    {
        const infra::sweeper sweep = [&]() {
            const size_t finished = ++this->_finished_channel_repeats_count;
            if (finished == this->_total_channel_repeats_count) {
                this->_sem_all_channels_finished.post();
            }
        };

        while (true) {

            // Receive header
            char header[12];
            uint64_t offset;
            uint32_t block_size;
            {
                if (!sock->recv(header, sizeof(header))) {
                    LOG_ERROR("Channel: recv() file block header failed");
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
                if (!sock->recv((char*)_dst_file_mapped + offset, block_size)) {
                    LOG_ERROR("Channel: recv() file block at offset={}, block_size={} failed", offset, block_size);
                    return false;
                }
            }

            LOG_TRACE("Channel: received file block: offset = {}, block_size = {}", offset, block_size);
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
            ASSERT(this->_file_info.has_value());
            FlushViewOfFile(_dst_file_mapped, this->_file_info->file_size);
            UnmapViewOfFile(_dst_file_mapped);
            _dst_file_mapped = nullptr;
        }

#elif PLATFORM_LINUX
        if (_file_handle != -1) {
            close(_file_handle);
            _file_handle = -1;
        }

        if (_dst_file_mapped != nullptr) {
            ASSERT(this->_file_info.has_value());
            msync(_dst_file_mapped, this->_file_info->file_size, MS_ASYNC);
            munmap(_dst_file_mapped, this->_file_info->file_size);
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

    void transfer_destination::init_file(const basic_file_info& file_info, const size_t total_channel_repeats_count)
    {
        ASSERT(!this->_file_info.has_value());
        this->_file_info = file_info;
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

        // truncate file to file_size
        {
            // move file pointer to file_size
            LARGE_INTEGER eof;
            eof.QuadPart = (LONGLONG)this->_file_info->file_size;
            if (!SetFilePointerEx(_file_handle, eof, NULL, FILE_BEGIN)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetFilePointerEx() ") + _dst_file_path.u8string() + " to " +
                                            std::to_string(_file_info->file_size) + " failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            if (!SetEndOfFile(_file_handle)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetEndOfFile() ") + _dst_file_path.u8string() + " at " +
                                            std::to_string(_file_info->file_size) + " failed. GetLastError = " +
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

        if (_file_info->file_size > 0) {
            if (_file_info->file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", _file_info->file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(_file_info->file_size));
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
                                            std::to_string(_file_info->file_size) + ") failed. GetLastError = " +
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
                                            std::to_string(_file_info->file_size) + ") failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }
            LOG_TRACE("MapViewOfFile() {} to {}", _dst_file_path.u8string(), _dst_file_mapped);
        }

#elif PLATFORM_LINUX
        uint32_t perm;
        if (_file_info->posix_perm == 0) {
            perm = 0644;  // rw-r--r--
        }
        else {
            ASSERT((_file_info->posix_perm & 07777) == _file_info->posix_perm,
                   "posix_perm should not contains extra bit: {}", _file_info->posix_perm);
            perm = _file_info->posix_perm;
        }

        _file_handle = open(_dst_file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, perm);
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for write failed. errno = {} ({})", _dst_file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + _dst_file_path.c_str() + " for write failed");
        }

        if (fchmod(_file_handle, perm) != 0) {
            LOG_ERROR("fchmod() {} to {:o} failed. errno = {} ({})", _dst_file_path.c_str(), perm, errno, strerror(errno));
            throw transfer_error(errno, std::string("fchmod() ") + _dst_file_path.c_str() + " failed");
        }

        if (_file_info->file_size > 0) {
            if (_file_info->file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", _file_info->file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(_file_info->file_size));
            }

            if (ftruncate64(_file_handle, _file_info->file_size) != 0) {
                LOG_ERROR("ftruncate64() {} to {} failed. errno = {} ({})", _dst_file_path.c_str(), _file_info->file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("ftruncate64() ") + _dst_file_path.c_str() + " to " + std::to_string(_file_info->file_size) + " failed");
            }

#   if !defined(MAP_HUGE_2MB)
#       define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#   endif
#   if !defined(MAP_HUGE_1GB)
#       define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#   endif
            _dst_file_mapped = mmap(
                nullptr,
                (size_t)_file_info->file_size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,  // TODO: support MAP_HUGETLB | MAP_HUGE_2MB ?
                _file_handle,
                0);
            if (_dst_file_mapped == MAP_FAILED) {
                _dst_file_mapped = nullptr;
                LOG_ERROR("mmap({}, size={}) for write failed. errno = {} ({})", _dst_file_path.c_str(), _file_info->file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("mmap() ") + _dst_file_path.c_str() + " for write failed");
            }
            LOG_TRACE("mmap() {} to {}", _dst_file_path.c_str(), _dst_file_mapped);
        }
#else
#   error "Unknown platform"
#endif
    }

}  // namespace xcp

