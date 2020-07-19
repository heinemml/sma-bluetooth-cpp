//
// Created by Michael Nosthoff on 19.07.20.
//

#include "bt_connection.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <fmt/format.h>
#include <sys/socket.h>
#include <zconf.h>

#include <stdexcept>

BTConnection::BTConnection(std::string address, size_t num_retries) : m_address(std::move(address))
{
    if (!Connect(num_retries))
        throw std::runtime_error(fmt::format("error connecting to {}", address));
}

BTConnection::~BTConnection()
{
    if (m_socket > -1)
        ::close(m_socket);
}

bool BTConnection::Connect(size_t num_retries)
{
    m_socket = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (m_socket < 0)
        throw std::runtime_error(fmt::format("error creating socket: {}", strerror(errno)));

    sockaddr_rc addr{};

    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = 1;
    str2ba(m_address.c_str(), &addr.rc_bdaddr);

    while (num_retries > 0) {
        if (::connect(m_socket, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return true;

        fmt::print(stderr, "Error connecting to {}: {}", m_address, strerror(errno));
        --num_retries;
    }

    return false;
}
