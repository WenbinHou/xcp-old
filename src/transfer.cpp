#include "common.h"

namespace xcp
{
    //--------------------------------------------------------------------------
    // struct transfer_source
    //--------------------------------------------------------------------------
    bool transfer_source::invoke_portal(infra::socket_t sock) /*override*/
    {
        return true;
    }

    bool transfer_source::invoke_channel(infra::socket_t sock) /*override*/
    {
        return true;
    }

    transfer_source::transfer_source(const std::string& src_path)
    {
        // Get file_size
    }


    //--------------------------------------------------------------------------
    // struct transfer_destination
    //--------------------------------------------------------------------------
    bool transfer_destination::invoke_portal(infra::socket_t sock) /*override*/
    {
        return true;
    }

    bool transfer_destination::invoke_channel(infra::socket_t sock) /*override*/
    {
        return true;
    }

    transfer_destination::transfer_destination(const std::string& src_file_name, const std::string& dst_path)
    {
    }

    void transfer_destination::init_file_size(const uint64_t file_size)
    {
        this->file_size = file_size;
    }

}  // namespace xcp

