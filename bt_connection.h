//
// Created by Michael Nosthoff on 19.07.20.
//

#ifndef SMA_BLUETOOTH_BTSOCKET_H
#define SMA_BLUETOOTH_BTSOCKET_H

#include <string>

class BTConnection
{
public:
    explicit BTConnection(std::string address, std::size_t num_retries = 10);
    ~BTConnection();
    BTConnection(const BTConnection&) = delete;
    BTConnection operator=(const BTConnection&) = delete;

    [[nodiscard]] int get_socket() const { return m_socket; }

private:
    bool Connect(std::size_t num_retries);
    int m_socket{-1};
    std::string m_address;
};

#endif  //SMA_BLUETOOTH_BTSOCKET_H
