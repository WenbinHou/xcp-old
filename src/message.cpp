#include "common.h"

namespace xcp
{
    template<typename T>
    bool message_send(std::shared_ptr<infra::os_socket_t> sock, const T& msg)
    {
        const message_type type = T::type;

        // TODO: optimize for better performance
        std::string binary;
        try {
            std::ostringstream oss(std::ios::binary);
            cereal::PortableBinaryOutputArchive ar(oss);
            ar(msg);
            binary = oss.str();

            if (binary.size() > (uint32_t)INT32_MAX) {
                LOG_ERROR("Message ({} bytes) is too large to send", binary.size());
                return false;
            }
        }
        catch(const std::exception& ex) {
            PANIC_TERMINATE("Serialization exception: {}", ex.what());
        }

        const uint32_t binary_size = (uint32_t)binary.size();

        char buffer[8];
        *(std::uint32_t*)&buffer[0] = htonl((std::uint32_t)type);
        *(std::uint32_t*)&buffer[4] = htonl(binary_size);

        {
            infra::socket_io_vec vec[2];

            // header
            vec[0].ptr = &buffer;
            vec[0].len = sizeof(buffer);

            // message
            vec[1].ptr = binary.data();
            vec[1].len = binary_size;

            if (!sock->sendv(vec)) {
                LOG_ERROR("message_send: sendv() failed");
                return false;
            }
        }

        return true;
    }


    template<typename T>
    bool message_recv(std::shared_ptr<infra::os_socket_t> sock, /*out*/ T& msg)
    {
        const message_type expected_type = T::type;

        char buffer[8];
        if (!sock->recv(buffer, sizeof(buffer))) {
            LOG_ERROR("message_recv: recv() message header failed");
            return false;
        }

        const message_type type = (message_type)ntohl(*(std::uint32_t*)&buffer[0]);
        const uint32_t binary_size = ntohl(*(std::uint32_t*)&buffer[4]);

        if (type != expected_type) {
            LOG_ERROR("Expects message type {}, but got message type {}",
                      (std::uint32_t)expected_type, (std::uint32_t)type);
            return false;
        }

        // TODO: optimize for better performance
        std::string binary;
        binary.resize(binary_size);
        if (!sock->recv(binary.data(), binary_size)) {
            LOG_ERROR("message_recv: recv() message body failed");
            return false;
        }

        try {
            std::istringstream iss(binary,std::ios::binary);
            cereal::PortableBinaryInputArchive ar(iss);
            ar(msg);
        }
        catch(const std::exception& ex) {
            PANIC_TERMINATE("Deserialization exception: {}", ex.what());
        }

        return true;
    }


#define _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(_Message_) \
    template bool message_send<_Message_>(std::shared_ptr<infra::os_socket_t> sock, const _Message_& msg); \
    template bool message_recv<_Message_>(std::shared_ptr<infra::os_socket_t> sock, /*out*/ _Message_& msg);

    _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(message_client_hello_request)
    _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(message_server_hello_response)
    _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(message_server_ready_to_transfer)
    _DECLARE_MESSAGE_SEND_RECV_TEMPLATE(message_transfer_destination_finished)

#undef _DECLARE_MESSAGE_SEND_RECV_TEMPLATE

}  // namespace xcp
