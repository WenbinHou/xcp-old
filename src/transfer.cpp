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
        ASSERT(this->_info.has_value());

        ASSERT(_init_transfer_block_size > 0);
        ASSERT(_init_transfer_block_size <= program_options_defaults::MAX_TRANSFER_BLOCK_SIZE);
        uint32_t curr_channel_block_size = (uint32_t)_init_transfer_block_size;

        for (size_t n_file = 0; n_file < _info->files_info.size(); ++n_file) {

            send_file_context& ctx = _send_ctx[n_file];

            while (true) {
                const uint64_t curr = ctx.curr_offset.fetch_add((uint64_t)curr_channel_block_size);
                if (curr >= this->_info->files_info[n_file].file_size) {
                    break;
                }

                uint32_t block_size = curr_channel_block_size;
                if (curr + curr_channel_block_size >= this->_info->files_info[n_file].file_size) {
                    block_size = (uint32_t)(this->_info->files_info[n_file].file_size - curr);
                }

                char header[16];
                *(uint32_t*)&header[0] = htonl((uint32_t)(curr >> 32));  // offset_high
                *(uint32_t*)&header[4] = htonl((uint32_t)(curr & 0xFFFFFFFF));  // offset_low
                *(uint32_t*)&header[8] = htonl(block_size);  // block size
                *(uint32_t*)&header[12] = htonl((uint32_t)n_file);  // n_file

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
                    if (!sock->send_file(ctx.file_handle, curr, block_size, &vec)) {
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

                    LOG_TRACE("Channel: sent {} took {} usec, new channel_block_size: {}",
                              curr_channel_block_size, elapsed_usec, tmp_block_size);
                    curr_channel_block_size = (uint32_t)tmp_block_size;
                }

                LOG_TRACE("Channel: sent file block: offset = {}, block_size = {}", curr, block_size);


                // Callback: report progress
                const uint64_t transferred_size = (_transferred_size += block_size);
                if (report_progress_callback != nullptr) {
                    report_progress_callback(transferred_size, this->_total_size);
                }
            }
        }

        //
        // Send a tail (indicating current channel is done)
        //
        {
            char final_tail[16];  // the same size as header, indicating the transfer is done
            *(uint32_t*)&final_tail[0] = htonl((uint32_t)0xFFFFFFFF);  // offset_high
            *(uint32_t*)&final_tail[4] = htonl((uint32_t)0xFFFFFFFF);  // offset_low
            *(uint32_t*)&final_tail[8] = htonl(0);  // block size
            *(uint32_t*)&final_tail[12] = htonl((uint32_t)0xFFFFFFFF);  // n_file

            if (!sock->send(final_tail, sizeof(final_tail))) {
                LOG_ERROR("Channel: send() final tail failed");
                return false;
            }
        }
        LOG_TRACE("Send done for current channel");

        return true;
    }

    void transfer_source::internal_open_regular_file(
        const stdfs::path& file_path,
        /*out*/ send_file_context& ctx,
        /*out*/ basic_file_info& fi)
    {
#if PLATFORM_WINDOWS
        ctx.file_handle = CreateFileW(
            file_path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (ctx.file_handle == INVALID_HANDLE_VALUE) {
            const DWORD gle = GetLastError();
            const std::string message = std::string("CreateFileW() ") + file_path.u8string() + " for read failed. GetLastError = " +
                                        std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
            LOG_ERROR("{}", message);
            throw transfer_error(EIO, message);
        }

        LARGE_INTEGER file_size { };
        if (!GetFileSizeEx(ctx.file_handle, &file_size)) {
            const DWORD gle = GetLastError();
            const std::string message = std::string("GetFileSizeEx() ") + file_path.u8string() + " failed. GetLastError = " +
                                        std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
            LOG_ERROR("{}", message);
            throw transfer_error(EIO, message);
        }

        fi.file_size = file_size.QuadPart;
        fi.posix_perm = 0;
        // For a regular file, its relative_path is the its filename
        fi.relative_path = stdfs::path(file_path).filename().u8string();

#elif PLATFORM_LINUX
        ctx.file_handle = open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (ctx.file_handle == -1) {
            LOG_ERROR("open() {} for read failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + file_path.c_str() + " for read failed");
        }

        struct stat64 st { };
        if (fstat64(ctx.file_handle, &st) != 0) {
            LOG_ERROR("fstat64() {} failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("fstat64() ") + file_path.c_str() + " failed");
        }

        fi.file_size = st.st_size;
        fi.posix_perm = (uint32_t)st.st_mode & 07777;
        // For a regular file, its relative_path is the its filename
        fi.relative_path = stdfs::path(file_path).filename();

#else
#   error "Unknown platform"
#endif

        _total_size += fi.file_size;
    }

    void transfer_source::prepare_transfer_regular_file(const stdfs::path& file_path)
    {
        ASSERT(!_info.has_value());
        this->_info = basic_transfer_info();
        this->_info->source_is_directory = false;

        send_file_context ctx;
        basic_file_info fi;
        internal_open_regular_file(file_path, ctx, fi);

        this->_info->files_info.emplace_back(std::move(fi));
        this->_send_ctx.emplace_back(std::move(ctx));
    }

    void transfer_source::prepare_transfer_directory(const stdfs::path& dir_path)
    {
        ASSERT(!_info.has_value());
        this->_info = basic_transfer_info();
        this->_info->source_is_directory = true;

        const stdfs::path dir_name = dir_path.filename();
        if (!dir_name.empty()) {
            basic_directory_info di;
            di.relative_path = dir_name.u8string();
            di.posix_perm = (uint32_t)stdfs::status(dir_path).permissions() & 07777;

            this->_info->dirs_info.emplace_back(std::move(di));
        }

        // Don't follow directory_symlink; Skip permission_denied
        for (auto& p : stdfs::recursive_directory_iterator(dir_path, stdfs::directory_options::skip_permission_denied)) {
            const std::string relative_path = (dir_name / stdfs::relative(p.path(), dir_path)).u8string();
            if (p.is_directory()) {
                basic_directory_info di;
                di.relative_path = relative_path;
                di.posix_perm = (uint32_t)p.status().permissions() & 07777;


                LOG_TRACE("Found directory: {}", relative_path);
                this->_info->dirs_info.emplace_back(std::move(di));
            }
            else if (p.is_regular_file()) {
                send_file_context ctx;
                basic_file_info fi;
                internal_open_regular_file(p.path(), ctx, fi);
                fi.relative_path = relative_path;  // overwrite relative path

                LOG_TRACE("Found file: {}", relative_path);
                this->_info->files_info.emplace_back(std::move(fi));
                this->_send_ctx.emplace_back(std::move(ctx));
            }
            else {
                const std::string error_message = "Can't transfer special file: " + p.path().u8string();
                LOG_ERROR("{}", error_message);
                throw transfer_error(ENOSYS, error_message);
            }
        }
    }

    void transfer_source::dispose_impl() noexcept /*override*/
    {
        if (this->_info) {
            for (send_file_context& ctx : _send_ctx) {
                ctx.close_handle();
            }
        }
    }

    transfer_source::transfer_source(const std::string& src_path, const uint64_t transfer_block_size, const bool recursive)
        : _is_transfer_block_size_fixed((transfer_block_size != 0)),
          _init_transfer_block_size((transfer_block_size != 0) ? (uint32_t)transfer_block_size : TRANSFER_BLOCK_SIZE_INIT)
    {
        ASSERT(_init_transfer_block_size > 0);
        ASSERT(transfer_block_size <= (uint64_t)program_options_defaults::MAX_TRANSFER_BLOCK_SIZE);

        infra::sweeper error_cleanup = [&]() {
            this->dispose();
        };

        const stdfs::file_status status = stdfs::status(src_path);  // symlink followed
        switch (status.type()) {
            case stdfs::file_type::regular: {
                prepare_transfer_regular_file(src_path);
                break;
            }
            case stdfs::file_type::directory: {
                if (!recursive) {
                    throw transfer_error(EINVAL, "Trying to transfer a directory without specifying \"-r\": " + src_path);
                }
                prepare_transfer_directory(src_path);
                break;
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
        // Wait for all channels finished
        this->_gate_all_channels_finished.wait();

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
            this->_gate_all_channels_finished.signal();
        };

        while (true) {

            // Receive header
            char header[16];
            uint64_t offset;
            uint32_t block_size;
            uint32_t n_file;
            {
                if (!sock->recv(header, sizeof(header))) {
                    LOG_ERROR("Channel: recv() file block header failed");
                    return false;
                }

                const uint32_t offset_high = ntohl(*(uint32_t*)&header[0]);
                const uint32_t offset_low = ntohl(*(uint32_t*)&header[4]);
                offset = ((std::uint64_t)offset_high) << 32 | ((std::uint64_t)offset_low);

                block_size = ntohl(*(uint32_t*)&header[8]);
                n_file = ntohl(*(uint32_t*)&header[12]);
            }

            if (block_size == 0) {  // indicating sending is done!
                ASSERT(offset == (uint64_t)-1);
                ASSERT(n_file == (uint32_t)-1);
                LOG_TRACE("Receive done for current channel");
                break;
            }

            // This instance might have been disposed here.
            ASSERT(n_file < _recv_ctx.size());

            recv_file_context& ctx = _recv_ctx[n_file];
            if (ctx.dst_file_mapped == nullptr) {
                ASSERT(this->is_dispose_required());
                return false;
            }

            // Receive body
            {
                if (!sock->recv((char*)ctx.dst_file_mapped + offset, block_size)) {
                    LOG_ERROR("Channel: recv() file block at offset={}, block_size={} failed", offset, block_size);
                    return false;
                }
                LOG_TRACE("Channel: received file block: offset = {}, block_size = {}", offset, block_size);

                const uint64_t file_size = _info->files_info[n_file].file_size;
                const uint64_t curr_received_size = (ctx.received_size += block_size);
                ASSERT(curr_received_size <= file_size);
                if (curr_received_size == file_size) {
                    LOG_DEBUG("Received file: {}", _info->files_info[n_file].relative_path);
                    _recv_ctx[n_file].unmap_file();
                    _recv_ctx[n_file].close_handle();
                }
            }

            // Callback: report progress
            const uint64_t transferred_size = (_transferred_size += block_size);
            if (report_progress_callback != nullptr) {
                report_progress_callback(transferred_size, _total_size);
            }
        }

        return true;
    }


    void transfer_destination::internal_open_regular_file(
        const stdfs::path& file_path,
        const basic_file_info& fi,
        /*out*/ recv_file_context& ctx)
    {
#if PLATFORM_WINDOWS
        ctx.file_handle = CreateFileW(
            file_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (ctx.file_handle == INVALID_HANDLE_VALUE) {
            const DWORD gle = GetLastError();
            const std::string message = std::string("CreateFileW() ") + file_path.u8string() + " for read failed. GetLastError = " +
                                        std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
            LOG_ERROR("{}", message);
            throw transfer_error(EIO, message);
        }

        // truncate file to file_size
        {
            // move file pointer to file_size
            LARGE_INTEGER eof;
            eof.QuadPart = (LONGLONG)fi.file_size;
            if (!SetFilePointerEx(ctx.file_handle, eof, NULL, FILE_BEGIN)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetFilePointerEx() ") + file_path.u8string() + " to " +
                                            std::to_string(fi.file_size) + " failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            if (!SetEndOfFile(ctx.file_handle)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetEndOfFile() ") + file_path.u8string() + " at " +
                                            std::to_string(fi.file_size) + " failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            // move file pointer back to 0
            eof.QuadPart = 0;
            if (!SetFilePointerEx(ctx.file_handle, eof, NULL, FILE_BEGIN)) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("SetFilePointerEx() ") + file_path.u8string() + " to 0 failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }
        }

        if (fi.file_size > 0) {
            if (fi.file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", fi.file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(fi.file_size));
            }

            HANDLE hMap = CreateFileMappingW(
                ctx.file_handle,
                NULL,
                PAGE_READWRITE,
                0,
                0,
                NULL);
            if (hMap == NULL) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("CreateFileMappingW() ") + file_path.u8string() + " (size: " +
                                            std::to_string(fi.file_size) + ") failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }

            infra::sweeper sweep_hmap = [&]() {
                CloseHandle(hMap);
                hMap = NULL;
            };

            ctx.dst_file_mapped = MapViewOfFile(
                hMap,
                FILE_MAP_READ | FILE_MAP_WRITE,
                0,
                0,
                0);
            if (ctx.dst_file_mapped == NULL) {
                const DWORD gle = GetLastError();
                const std::string message = std::string("MapViewOfFile() ") + file_path.u8string() + " (size: " +
                                            std::to_string(fi.file_size) + ") failed. GetLastError = " +
                                            std::to_string(gle) + " (" + infra::str_getlasterror(gle) + ")";
                LOG_ERROR("{}", message);
                throw transfer_error(EIO, message);
            }
            LOG_TRACE("MapViewOfFile() {} to {}", file_path.u8string(), ctx.dst_file_mapped);
        }

#elif PLATFORM_LINUX
        uint32_t perm;
        if (fi.posix_perm == 0) {
            perm = 0644;  // rw-r--r--
        }
        else {
            ASSERT((fi.posix_perm & 07777) == fi.posix_perm,
                   "posix_perm should not contains extra bit: {}", fi.posix_perm);
            perm = fi.posix_perm;
        }

        ctx.file_handle = open(file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, perm);
        if (ctx.file_handle == -1) {
            LOG_ERROR("open() {} for write failed. errno = {} ({})", file_path.c_str(), errno, strerror(errno));
            throw transfer_error(errno, std::string("open() ") + file_path.c_str() + " for write failed");
        }

        if (fchmod(ctx.file_handle, perm) != 0) {
            LOG_ERROR("fchmod() {} to {:o} failed. errno = {} ({})", file_path.c_str(), perm, errno, strerror(errno));
            throw transfer_error(errno, std::string("fchmod() ") + file_path.c_str() + " failed");
        }

        if (fi.file_size > 0) {
            if (fi.file_size > std::numeric_limits<size_t>::max()) {
                LOG_ERROR("File size is too large for this platform: {}", fi.file_size);
                throw transfer_error(ENOSYS, std::string("File size is too large for this platform: ") + std::to_string(fi.file_size));
            }

            if (ftruncate64(ctx.file_handle, fi.file_size) != 0) {
                LOG_ERROR("ftruncate64() {} to {} failed. errno = {} ({})", file_path.c_str(), fi.file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("ftruncate64() ") + file_path.c_str() + " to " + std::to_string(fi.file_size) + " failed");
            }

#   if !defined(MAP_HUGE_2MB)
#       define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#   endif
#   if !defined(MAP_HUGE_1GB)
#       define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#   endif
            ctx.dst_file_mapped = mmap(
                nullptr,
                (size_t)fi.file_size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,  // TODO: support MAP_HUGETLB | MAP_HUGE_2MB ?
                ctx.file_handle,
                0);
            if (ctx.dst_file_mapped == MAP_FAILED) {
                ctx.dst_file_mapped = nullptr;
                LOG_ERROR("mmap({}, size={}) for write failed. errno = {} ({})", file_path.c_str(), fi.file_size, errno, strerror(errno));
                throw transfer_error(errno, std::string("mmap() ") + file_path.c_str() + " for write failed");
            }
            LOG_TRACE("mmap() {} to {}", file_path.c_str(), ctx.dst_file_mapped);

            if (madvise(ctx.dst_file_mapped, fi.file_size, MADV_SEQUENTIAL) != 0) {
                LOG_WARN("madvise({}, size={}, MADV_SEQUENTIAL) failed. errno = {} ({})", ctx.dst_file_mapped, fi.file_size, errno, strerror(errno));
                // Don't throw a transfer_error
            }
        }
#else
#   error "Unknown platform"
#endif

        ctx.mapped_length = (size_t)fi.file_size;
        _total_size += fi.file_size;
    }

    void transfer_destination::prepare_transfer_regular_file()
    {
        ASSERT(_info.has_value());
        ASSERT(_info->files_info.size() == 1);

        recv_file_context ctx;
        internal_open_regular_file(_requested_dst_path, _info->files_info[0], ctx);

        this->_recv_ctx.emplace_back(std::move(ctx));
    }

    void transfer_destination::prepare_transfer_directory()
    {
        ASSERT(_info.has_value());

        // Create directories in advance
        for (basic_directory_info& di : _info->dirs_info) {
            stdfs::path dir = (_requested_dst_path / di.relative_path);
            stdfs::create_directories(dir);
            stdfs::permissions(dir, (stdfs::perms)di.posix_perm, stdfs::perm_options::replace);
            LOG_TRACE("Create directory: {} (perm: {:04o})", dir.u8string(), di.posix_perm);
        }

        // Open files for write in advance
        for (basic_file_info& fi : _info->files_info) {
            stdfs::path file_path = (_requested_dst_path / fi.relative_path);
            LOG_TRACE("Receive file: {} (perm: {:04o})", file_path.u8string(), fi.posix_perm);

            recv_file_context ctx;
            internal_open_regular_file(file_path, fi, ctx);
            _recv_ctx.emplace_back(std::move(ctx));
        }
    }

    void transfer_destination::dispose_impl() noexcept /*override*/
    {
        if (this->_info) {
            for (recv_file_context& ctx : _recv_ctx) {
                ctx.close_handle();
                ctx.unmap_file();
            }
        }

        if (_gate_all_channels_finished.initialized()) {
            _gate_all_channels_finished.force_signal_all();
        }
    }

    transfer_destination::transfer_destination(std::string dst_path)
        : _requested_dst_path(std::move(dst_path))
    { }

    void transfer_destination::init_transfer_info(const basic_transfer_info& transfer_info, const size_t total_channel_repeats_count)
    {
        // Ugly but we can't do this in ctor
        ASSERT(total_channel_repeats_count > 0);
        this->_gate_all_channels_finished.init(total_channel_repeats_count);


        ASSERT(!this->_info.has_value());
        this->_info = transfer_info;

        stdfs::path dst_real_path = stdfs::weakly_canonical(_requested_dst_path);
        const stdfs::file_status status = stdfs::status(dst_real_path);
        switch (status.type()) {
            case stdfs::file_type::regular: {
                if (this->_info->source_is_directory) {
                    throw transfer_error(ENOSYS, "Transfer source directory to a file: " + _requested_dst_path.u8string());
                }
                ASSERT(this->_info->files_info.size() == 1);

                prepare_transfer_regular_file();
                break;
            }
            case stdfs::file_type::directory: {
                if (this->_info->source_is_directory) {
                    prepare_transfer_directory();
                }
                else {
                    ASSERT(this->_info->files_info.size() == 1);
                    prepare_transfer_regular_file();
                }
                break;
            }
            case stdfs::file_type::not_found: {
                if (this->_info->source_is_directory) {
                    stdfs::create_directory(_requested_dst_path);
                    prepare_transfer_directory();
                }
                else {
                    ASSERT(this->_info->files_info.size() == 1);
                    prepare_transfer_regular_file();
                }
                break;
            }
            case stdfs::file_type::block: {
                throw transfer_error(ENOSYS, "Transfer to a block device is not supported: " + _requested_dst_path.u8string());
            }
            case stdfs::file_type::character: {
                throw transfer_error(ENOSYS, "Transfer to a character device is not supported: " + _requested_dst_path.u8string());
            }
            case stdfs::file_type::fifo: {
                throw transfer_error(ENOSYS, "Transfer to a FIFO file is not supported: " + _requested_dst_path.u8string());
            }
            case stdfs::file_type::socket: {
                throw transfer_error(ENOSYS, "Transfer to a socket file is not supported: " + _requested_dst_path.u8string());
            }
            case stdfs::file_type::none:
            case stdfs::file_type::symlink:
            case stdfs::file_type::unknown:
            default: {
                throw transfer_error(ENOSYS, "Unknown file type: " + _requested_dst_path.u8string());
            }
        }
    }

}  // namespace xcp

