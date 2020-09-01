#ifndef SMA_BLUETOOTH_SMATOOL_H
#define SMA_BLUETOOTH_SMATOOL_H

#include <cmath>
#include <ctime>
#include <string>

#include "sma_struct.h"

unsigned char *ReadStream(ConfType *, FlagType *, ReadRecordType *, int bt_sock, unsigned char *, int *, unsigned char *, int *, unsigned char *, int, int *, int *);
char *return_xml_data(int index);
unsigned char conv(const char *);
int select_str(char *s);
int empty_read_bluetooth(FlagType *flag, ReadRecordType *readRecord, int bt_sock, int *rr, unsigned char *received, int *terminated);
int read_bluetooth(ConfType *conf, FlagType *flag, ReadRecordType *readRecord, int bt_sock, int *rr, unsigned char *received, int cc, unsigned char *last_sent, int *terminated);
void tryfcs16(FlagType *flag, unsigned char *cp, int len, unsigned char *fl, int *cc);
void add_escapes(unsigned char *cp, int *len);
void fix_length_send(FlagType *flag, unsigned char *cp, int len);

bool IsNullValue(const unsigned char *stream, std::size_t length);

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

#endif  //SMA_BLUETOOTH_SMATOOL_H
