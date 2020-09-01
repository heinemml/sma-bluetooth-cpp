#ifndef SMA_BLUETOOTH_STREAM_HANDLING_H
#define SMA_BLUETOOTH_STREAM_HANDLING_H

#include <string>

inline bool IsNullValue(const unsigned char *stream, const std::size_t length)
{
    for (std::size_t i = 0; i < length; ++i) {
        if (stream[i] != 0xff)  // at least one value needs to be non 0xff, else it's a null value
            return false;
    }

    return true;
}

template <typename T>
inline T ConvertStreamTo(const unsigned char *stream, const std::size_t length)
{
    if (IsNullValue(stream, length))
        return T{};

    T value{0};
    for (std::size_t i = 0; i < length; ++i) {
        value = value + static_cast<T>(stream[i] * pow(256, i));
    }

    return value;
}

template <>
inline std::string ConvertStreamTo<std::string>(const unsigned char *stream, const std::size_t length)
{
    if (IsNullValue(stream, length))
        return "";

    std::string value;
    for (std::size_t i = 0; i < length; i++) {
        if (stream[i] != 0)  // drop 0s
            value += stream[i];
    }

    return value;
}

#endif  //SMA_BLUETOOTH_STREAM_HANDLING_H
