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
        while (true) {
            const uint64_t curr = _curr_offset.fetch_add((uint64_t)BLOCK_SIZE);
            if (curr >= this->file_size) {
                break;
            }

            uint32_t block_size = BLOCK_SIZE;
            if (curr + BLOCK_SIZE > this->file_size) {
                block_size = (uint32_t)(this->file_size - curr);
            }

            char header[12];
            *(uint32_t*)&header[0] = htonl((uint32_t)(curr >> 32));  // offset_high
            *(uint32_t*)&header[4] = htonl((uint32_t)(curr & 0xFFFFFFFF));  // offset_low
            *(uint32_t*)&header[8] = htonl(block_size);  // block size

#if PLATFORM_WINDOWS
#           error "TODO"

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
#if PLATFORM_WINDOWS
#           error "TODO"

#elif PLATFORM_LINUX
        {
            char header[12];
            *(uint32_t*)&header[0] = htonl((uint32_t)0xFFFFFFFF);  // offset_high
            *(uint32_t*)&header[4] = htonl((uint32_t)0xFFFFFFFF);  // offset_low
            *(uint32_t*)&header[8] = htonl(0);  // block size

            const ssize_t cnt = send(sock, header, sizeof(header), 0);
            if (cnt != (ssize_t)sizeof(header)) {
                LOG_ERROR("send() expects {}, but returns {}. {}",
                          sizeof(header), cnt, infra::socket_error_description());
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
        _file_handle = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);  // TODO
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for read failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + file_path.c_str() + " for read failed");
        }
        this->file_size = stdfs::file_size(file_path);

#elif PLATFORM_LINUX
        _file_handle = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for read failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + file_path.c_str() + " for read failed");
        }
        this->file_size = stdfs::file_size(file_path);

#else
#   error "Unknown platform"
#endif
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
        const stdfs::path src_real_path = stdfs::weakly_canonical(src_path);
        const stdfs::file_status status = stdfs::status(src_real_path);
        switch (status.type()) {
            case std::filesystem::file_type::regular: {
                prepare_transfer_regular_file(src_real_path);
                break;
            }
            case std::filesystem::file_type::directory: {
                throw transfer_error(ENOSYS, "Transfer a directory is not supported: " + src_path);
            }
            case std::filesystem::file_type::not_found: {
                throw transfer_error(ENOENT, "File not found: " + src_path);
            }
            case std::filesystem::file_type::block: {
                throw transfer_error(ENOSYS, "Transfer a block device is not supported: " + src_path);
            }
            case std::filesystem::file_type::character: {
                throw transfer_error(ENOSYS, "Transfer a character device is not supported: " + src_path);
            }
            case std::filesystem::file_type::fifo: {
                throw transfer_error(ENOSYS, "Transfer a FIFO file is not supported: " + src_path);
            }
            case std::filesystem::file_type::socket: {
                throw transfer_error(ENOSYS, "Transfer a socket file is not supported: " + src_path);
            }
            case std::filesystem::file_type::none:
            case std::filesystem::file_type::symlink:
            case std::filesystem::file_type::unknown:
            default: {
                throw transfer_error(ENOSYS, "Unknown file type: " + src_path);
            }
        }
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

#if PLATFORM_WINDOWS
#           error "TODO"

#elif PLATFORM_LINUX
            // Receive header
            uint64_t offset;
            uint32_t block_size;
            {
                const ssize_t cnt = recv(sock, header, sizeof(header), MSG_WAITALL);
                if (cnt != (ssize_t) sizeof(header)) {
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
                assert(offset == -1);
                LOG_TRACE("Receive done for current channel");
                break;
            }

            assert(_dst_file_mapped != nullptr);

            // Receive body
            {
                const ssize_t cnt = recv(sock, (char*)_dst_file_mapped + offset, block_size, MSG_WAITALL);
                if (cnt != (ssize_t) block_size) {
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
            // TODO: msync?
            munmap(_dst_file_mapped, this->file_size);
            _dst_file_mapped = nullptr;
        }
#else
#   error "Unknown platform"
#endif
    }

    transfer_destination::transfer_destination(const std::string& src_file_name, const std::string& dst_path)
    {
        stdfs::path dst_real_path = stdfs::weakly_canonical(dst_path);
        const stdfs::file_status status = stdfs::status(dst_real_path);
        switch (status.type()) {
            case std::filesystem::file_type::regular: {
                _dst_file_path = dst_path;  // TODO: or dst_real_path?
                break;
            }
            case std::filesystem::file_type::directory: {
                if (src_file_name.empty()) {
                    throw transfer_error(ENOSYS, "Transfer to a directory without given a file name: " + dst_path);
                }
                _dst_file_path = dst_real_path / src_file_name;
                break;
            }
            case std::filesystem::file_type::not_found: {
                _dst_file_path = dst_path;  // create a new file
                break;
            }
            case std::filesystem::file_type::block: {
                throw transfer_error(ENOSYS, "Transfer to a block device is not supported: " + dst_path);
            }
            case std::filesystem::file_type::character: {
                throw transfer_error(ENOSYS, "Transfer to a character device is not supported: " + dst_path);
            }
            case std::filesystem::file_type::fifo: {
                throw transfer_error(ENOSYS, "Transfer to a FIFO file is not supported: " + dst_path);
            }
            case std::filesystem::file_type::socket: {
                throw transfer_error(ENOSYS, "Transfer to a socket file is not supported: " + dst_path);
            }
            case std::filesystem::file_type::none:
            case std::filesystem::file_type::symlink:
            case std::filesystem::file_type::unknown:
            default: {
                throw transfer_error(ENOSYS, "Unknown file type: " + dst_path);
            }
        }

        LOG_TRACE("Save to destination file: {}", _dst_file_path.string());
    }

    void transfer_destination::init_file_size(const uint64_t the_file_size, const size_t total_channel_repeats_count)
    {
        this->_total_channel_repeats_count = total_channel_repeats_count;
        this->file_size = the_file_size;

#if PLATFORM_WINDOWS
#       error "TODO"

#elif PLATFORM_LINUX
        _file_handle = open(_dst_file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0777);  // TODO: file permission?
        if (_file_handle == -1) {
            LOG_ERROR("open() {} for write failed. errno = {} ({})", _dst_file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + _dst_file_path.c_str() + " for write failed");
        }

        if (file_size > 0) {
            if (file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(file_size));
            }

            if (ftruncate64(_file_handle, file_size) != 0) {
                LOG_ERROR("ftruncate64() {} to {} failed. errno = {} ({})", _dst_file_path.c_str(), file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("ftruncate64() ") + _dst_file_path.c_str() + " to " + std::to_string(file_size) + " failed");
            }

#   if !defined(MAP_HUGE_2MB)
#       define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#   endif
#   if !defined(MAP_HUGE_1GB)
#       define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#   endif
            _dst_file_mapped = mmap(
                nullptr,
                (size_t)file_size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,  // TODO: support MAP_HUGETLB | MAP_HUGE_2MB ?
                _file_handle,
                0);
            if (_dst_file_mapped == MAP_FAILED) {
                _dst_file_mapped = nullptr;
                LOG_ERROR("mmap({}, size={}) for write failed. errno = {} ({})", _dst_file_path.c_str(), file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("mmap() ") + _dst_file_path.c_str() + " for write failed");
            }
            LOG_TRACE("mmap() {} to {}", _dst_file_path.c_str(), _dst_file_mapped);
        }
#else
#   error "Unknown platform"
#endif
    }

}  // namespace xcp

