/* tool to read power production data for SMA solar power convertors 
   Copyright Wim Hofman 2010 
   Copyright Stephen Collier 2010,2011 

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* compile gcc -lbluetooth -lcurl -lmysqlclient -g -o smatool smatool.c */

#include "smatool.h"

#include <curl/curl.h>
#include <fmt/format.h>
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/xpath.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "almanac.h"
#include "bt_connection.h"
#include "repost.h"
#include "sb_commands.h"
#include "sma_mysql.h"
#include "stream_handling.h"

/*
 * u16 represents an unsigned 16-bit number.  Adjust the typedef for
 * your hardware.
 */
typedef u_int16_t u16;

#define PPPINITFCS16 0xffff /* Initial FCS value    */
#define PPPGOODFCS16 0xf0b8 /* Good final FCS value */
#define ASSERT(x) assert(x)
#define SCHEMA "4" /* Current database schema */

const char *accepted_strings[] = {
    "$END",
    "$ADDR",
    "$TIME",
    "$SERIAL",
    "$CRC",
    "$POW",
    "$DTOT",
    "$ADD2",
    "$CHAN",
    "$ITIME",
    "$TMMI",
    "$TMPL",
    "$TIMESTRING",
    "$TIMEFROM1",
    "$TIMETO1",
    "$TIMEFROM2",
    "$TIMETO2",
    "$TESTDATA",
    "$ARCHIVEDATA1",
    "$PASSWORD",
    "$SIGNAL",
    "$SUSYID",
    "$INVCODE",
    "$ARCHCODE",
    "$INVERTERDATA",
    "$CNT",      /*Counter of sent packets*/
    "$TIMEZONE", /*Timezone seconds +1 from GMT*/
    "$TIMESET",  /*Unknown string involved in time setting*/
    "$DATA",     /*Data string */
    "$MYSUSYID",
    "$MYSERIAL",
    "$LOGIN"};

static u16 fcstab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78};

/*
 * Calculate a new fcs given the current fcs and the new data.
 */
u16 pppfcs16(u16 fcs, void *_cp, int len)
{
    auto *cp = (unsigned char *)_cp;
    /* don't worry about the efficiency of these asserts here.  gcc will
     * recognise that the asserted expressions are constant and remove them.
     * Whether they are usefull is another question. 
     */

    ASSERT(sizeof(u16) == 2);
    ASSERT(((u16)-1) > 0);
    while (len--)
        fcs = (fcs >> 8) ^ fcstab[(fcs ^ *cp++) & 0xff];
    return (fcs);
}

/*
 * Add escapes (7D) as they are required
 */
void add_escapes(unsigned char *cp, int *len)
{
    int i, j;

    for (i = 19; i < (*len); i++) {
        switch (cp[i]) {
            case 0x7d:
            case 0x7e:
            case 0x11:
            case 0x12:
            case 0x13:
                for (j = (*len); j > i; j--) cp[j] = cp[j - 1];
                cp[i + 1] = cp[i] ^ 0x20;
                cp[i] = 0x7d;
                (*len)++;
        }
    }
}

/*
 * Recalculate and update length to correct for escapes
 */
void fix_length_send(FlagType *flag, unsigned char *cp, int len)
{
    if (flag->debug == 1)
        fmt::print("len={:x}, checkbit {:x}, sum={:x}\n", cp[1], cp[3], cp[1] + cp[3]);
    if ((cp[1] != len + 1)) {
        cp[3] = (cp[1] + cp[3]) - (len + 1);
        cp[1] = len + 1;

        cp[3] = cp[0] ^ cp[1] ^ cp[2];

        if (flag->debug == 1)
            fmt::print("corrected len={:x}, checkbit {:x}, sum={:x}\n", cp[1], cp[3], cp[1] + cp[3]);
    }
}

/*
 * Recalculate and update length to correct for escapes
 */
void fix_length_received(FlagType *flag, unsigned char *received, int len)
{
    if (received[1] != len) {
        if (flag->debug == 1)
            fmt::print("length change from 0x{:x} to 0x{:x}\n", received[1], len);

        if ((received[3] != 0x13) && (received[3] != 0x14)) {
            received[1] = len;
            switch (received[1]) {
                case 0x52:
                    received[3] = 0x2c;
                    break;
                case 0x5a:
                    received[3] = 0x24;
                    break;
                case 0x66:
                    received[3] = 0x1a;
                    break;
                case 0x6a:
                    received[3] = 0x14;
                    break;
                default:
                    break;
            }
        }
    }
}

/*
 * How to use the fcs
 */
void tryfcs16(FlagType *flag, unsigned char *cp, int len, unsigned char *fl, int *cc)
{
    u16 trialfcs;
    unsigned char stripped[1024] = {0};

    memcpy(stripped, cp, len);
    /* add on output */
    if (flag->debug == 2) {
        fmt::print("String to calculate FCS\n");

        for (int i = 0; i < len; i++)
            fmt::print("{:02x} ", cp[i]);

        fmt::print("\n\n");
    }
    trialfcs = pppfcs16(PPPINITFCS16, stripped, len);
    trialfcs ^= 0xffff;              /* complement */
    fl[(*cc)] = (trialfcs & 0x00ff); /* least significant byte first */
    fl[(*cc) + 1] = ((trialfcs >> 8) & 0x00ff);
    (*cc) += 2;
    if (flag->debug == 2) {
        fmt::print("FCS = {:x}{:x} {:x}\n", (trialfcs & 0x00ff), ((trialfcs >> 8) & 0x00ff), trialfcs);
    }
}

unsigned char conv(const char *nn)
{
    unsigned char tt = 0, res = 0;
    int i;

    for (i = 0; i < 2; i++) {
        switch (nn[i]) {
            case 65: /*A*/
            case 97: /*a*/
                tt = 10;
                break;

            case 66: /*B*/
            case 98: /*b*/
                tt = 11;
                break;

            case 67: /*C*/
            case 99: /*c*/
                tt = 12;
                break;

            case 68:  /*D*/
            case 100: /*d*/
                tt = 13;
                break;

            case 69:  /*E*/
            case 101: /*e*/
                tt = 14;
                break;

            case 70:  /*F*/
            case 102: /*f*/
                tt = 15;
                break;

            default:
                tt = nn[i] - 48;
        }
        res = res + (tt * pow(16, 1 - i));
    }
    return res;
}

int check_send_error(FlagType *flag, const int *s, int *rr, unsigned char *received, const std::string &last_sent, int *terminated, int *already_read)
{
    unsigned char buf[1024]; /*read buffer*/
    unsigned char header[3]; /*read buffer*/
    timeval tv{};
    fd_set readfds;

    tv.tv_sec = 0;  // set timeout of reading
    tv.tv_usec = 5000;
    memset(buf, 0, 1024);

    FD_ZERO(&readfds);
    FD_SET((*s), &readfds);

    select((*s) + 1, &readfds, nullptr, nullptr, &tv);

    (*terminated) = 0;  // Tag to tell if string has 7e termination

    ssize_t result;
    // first read the header to get the record length
    if (FD_ISSET((*s), &readfds)) {                      // did we receive anything within 5 seconds
        result = recv((*s), header, sizeof(header), 0);  //Get length of string
        (*rr) = 0;
        for (unsigned char c : header) {
            received[(*rr)] = c;
            if (flag->debug == 1)
                fmt::print("{:02x} ", received[(*rr)]);
            (*rr)++;
        }
    } else {
        if (flag->verbose == 1)
            fmt::print("Timeout reading bluetooth socket\n");

        (*rr) = 0;
        memset(received, 0, 1024);
        return -1;
    }

    auto len = (uint16_t)header[1];

    if (FD_ISSET((*s), &readfds)) {                         // did we receive anything within 5 seconds
        result = recv((*s), buf, len - sizeof(header), 0);  //Read the length specified by header
    } else {
        if (flag->verbose == 1)
            fmt::print("Timeout reading bluetooth socket\n");
        (*rr) = 0;
        memset(received, 0, 1024);
        return -1;
    }
    if (result > 0) {
        auto bytes_read = static_cast<std::size_t>(result);
        if (flag->debug == 1) {
            fmt::print("\nReceiving\n");
            fmt::print("    {:08x}: .. .. .. .. .. .. .. .. .. .. .. .. ", 0);
            unsigned int j = 12;
            for (unsigned char c : header) {
                if (j % 16 == 0)
                    fmt::print("\n    {:08x}: ", j);
                fmt::print("{:02x} ", c);
                j++;
            }
            for (ssize_t i = 0; i < bytes_read; i++) {
                if (j % 16 == 0)
                    fmt::print("\n    {:08x} ", j);
                fmt::print("{:02x} ", buf[i]);
                j++;
            }
            fmt::print(" rr={}", (bytes_read + (*rr)));
            fmt::print("\n\n");
        }
        if ((last_sent.size() == bytes_read) && (memcmp(received, last_sent.c_str(), last_sent.size()) == 0)) {
            fmt::print(stderr, "ERROR received what we sent!");
            getchar();
            //Need to do something
        }
        if (buf[bytes_read - 1] == 0x7e)
            (*terminated) = 1;
        else
            (*terminated) = 0;
        for (ssize_t i = 0; i < bytes_read; i++) {  //start copy the rec buffer in to received
            if (buf[i] == 0x7d) {                   //did we receive the escape char
                switch (buf[i + 1]) {               // act depending on the char after the escape char

                    case 0x5e:
                        received[(*rr)] = 0x7e;
                        break;

                    case 0x5d:
                        received[(*rr)] = 0x7d;
                        break;

                    default:
                        received[(*rr)] = buf[i + 1] ^ 0x20;
                        break;
                }
                i++;
            } else {
                received[(*rr)] = buf[i];
            }
            if (flag->debug == 1)
                fmt::print("{:02x} ", received[(*rr)]);

            (*rr)++;
        }
        fix_length_received(flag, received, *rr);
        if (flag->debug == 1) {
            printf("\n");
            for (int i = 0; i < (*rr); i++)
                fmt::print("{:02x} ", received[(i)]);
        }
        if (flag->debug == 1)
            fmt::print("\n\n");
        (*already_read) = 1;
    }
    return 0;
}

int empty_read_bluetooth(FlagType *flag, ReadRecordType *readRecord, const int bt_sock, int *rr, unsigned char *received, int *terminated)
{
    ssize_t bytes_read = 0;
    int last_decoded = 0;
    int j = 0;
    unsigned char buf[1024]; /*read buffer*/
    unsigned char header[4]; /*read buffer*/
    timeval tv{};
    fd_set readfds;

    tv.tv_sec = 1;  // set timeout of reading
    tv.tv_usec = 0;
    memset(buf, 0, 1024);

    FD_ZERO(&readfds);
    FD_SET(bt_sock, &readfds);

    if (select(bt_sock + 1, &readfds, nullptr, nullptr, &tv) < 0) {
        fmt::print("select error has occurred");
        getchar();
    }

    (*terminated) = 0;  // Tag to tell if string has 7e termination
    // first read the header to get the record length
    if (FD_ISSET(bt_sock, &readfds)) {                          // did we receive anything within 5 seconds
        bytes_read = recv(bt_sock, header, sizeof(header), 0);  //Get length of string
        (*rr) = 0;
        for (unsigned int i = 0; i < sizeof(header); i++) {
            received[(*rr)] = header[i];
            if (flag->debug == 2)
                fmt::print("{:02x} ", received[i]);
            (*rr)++;
        }
    } else {
        memset(received, 0, 1024);
        (*rr) = 0;
        return -1;
    }

    auto len = (uint16_t)header[1];

    if (FD_ISSET(bt_sock, &readfds)) {                             // did we receive anything within 5 seconds
        bytes_read = recv(bt_sock, buf, len - sizeof(header), 0);  //Read the length specified by header
    } else {
        memset(received, 0, 1024);
        (*rr) = 0;
        return -1;
    }
    readRecord->Status[0] = 0;
    readRecord->Status[1] = 0;

    if (bytes_read > 0) {
        if (flag->debug == 1) {
            fmt::print("\n-----------------------------------------------------------");
            fmt::print("\nREAD:");
            //Start byte
            fmt::print("\n7e ");
            j++;
            //Size and checkbit
            fmt::print("{:02x} {:02x}                 size:              {}", header[1], header[2], len);
            fmt::print("\n   ");
            fmt::print("{:02x} ", header[3]);
            fmt::print("                      checkbit:          {}", header[3]);
            fmt::print("\n   ");
            //Source Address
            for (ssize_t i = 0; i < bytes_read; i++) {
                if (i > 5) break;
                fmt::print("{:02x} ", buf[i]);
            }
            fmt::print("       source:            {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[5], buf[4], buf[3], buf[2], buf[1], buf[0]);
            fmt::print("\n   ");
            //Destination Address
            for (ssize_t i = 6; i < bytes_read; i++) {
                if (i > 11) break;
                fmt::print("{:02x} ", buf[i]);
            }
            fmt::print("       destination:       {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[11], buf[10], buf[9], buf[8], buf[7], buf[6]);
            fmt::print("\n   ");
            //Destination Address
            for (ssize_t i = 12; i < bytes_read; i++) {
                if (i > 13) break;
                fmt::print("{:02x} ", buf[i]);
            }
            fmt::print("                   control:           {:02x}{:02x}", buf[13], buf[12]);
            readRecord->Control[0] = buf[12];
            readRecord->Control[1] = buf[13];

            last_decoded = 14;
            if (memcmp(buf + 14, "\x7e\xff\x03\x60\x65", 5) == 0) {
                fmt::print("\n");
                for (ssize_t i = 14; i < bytes_read; i++) {
                    if (i > 18) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("             SMA Data2+ header: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[18], buf[17], buf[16], buf[15], buf[14]);
                fmt::print("\n   ");
                for (ssize_t i = 19; i < bytes_read; i++) {
                    if (i > 19) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                      data packet size:  {:02d}", buf[19]);
                fmt::print("\n   ");
                for (ssize_t i = 20; i < bytes_read; i++) {
                    if (i > 20) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                      control:           {:02x}", buf[20]);
                fmt::print("\n   ");
                for (ssize_t i = 21; i < bytes_read; i++) {
                    if (i > 26) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("       source:            {:02x} {:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[21], buf[26], buf[25], buf[24], buf[23], buf[22]);
                fmt::print("\n   ");
                for (ssize_t i = 27; i < bytes_read; i++) {
                    if (i > 28) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                   read status:       {:02x} {:02x}", buf[28], buf[27]);
                fmt::print("\n   ");
                for (ssize_t i = 29; i < bytes_read; i++) {
                    if (i > 30) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                readRecord->Status[0] = buf[28];
                readRecord->Status[1] = buf[27];
                fmt::print("                   count up:          {:02d} {:02x}:{:02x}", buf[29] + buf[30] * 256, buf[30], buf[29]);
                fmt::print("\n   ");
                for (ssize_t i = 31; i < bytes_read; i++) {
                    if (i > 32) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                   count down:        {:02d} {:02x}:{:02x}", buf[31] + buf[32] * 256, buf[32], buf[31]);
                fmt::print("\n   ");
                last_decoded = 33;
            }
            printf("\n   ");
            j = 0;
            for (int i = last_decoded; i < bytes_read; i++) {
                if (j % 16 == 0)
                    fmt::print("\n   {:08x}: ", j);
                fmt::print("{:02x} ", buf[i]);
                j++;
            }
            fmt::print(" rr={:d}", (bytes_read + 3));
            fmt::print("\n\n");
        }
    }
    (*rr) = 0;
    memset(received, 0, 1024);
    return 0;
}

int read_bluetooth(ConfType *conf, FlagType *flag, ReadRecordType *readRecord, const int bt_sock, int *rr, unsigned char *received, const std::string &last_sent, int *terminated)
{
    int bytes_read = 0;
    unsigned char buf[1024]; /*read buffer*/
    unsigned char header[4]; /*read buffer*/
    unsigned char checkbit;
    timeval tv{};
    fd_set readfds;

    tv.tv_sec = conf->bt_timeout;  // set timeout of reading
    tv.tv_usec = 0;
    memset(buf, 0, 1024);

    FD_ZERO(&readfds);
    FD_SET(bt_sock, &readfds);

    if (select(bt_sock + 1, &readfds, nullptr, nullptr, &tv) < 0) {
        fmt::print(stderr, "select error has occurred");
        getchar();
    }

    if (flag->verbose == 1)
        fmt::print("Reading bluetooth packet\n");

    (*terminated) = 0;  // Tag to tell if string has 7e termination
    // first read the header to get the record length
    if (FD_ISSET(bt_sock, &readfds)) {                          // did we receive anything within 5 seconds
        bytes_read = recv(bt_sock, header, sizeof(header), 0);  //Get length of string
        (*rr) = 0;
        for (const auto &c : header) {
            received[(*rr)++] = c;
            if (flag->debug == 2)
                fmt::print("{:02x} ", c);
        }
    } else {
        if (flag->verbose == 1)
            fmt::print("Timeout reading bluetooth socket\n");
        (*rr) = 0;
        memset(received, 0, 1024);
        return -1;
    }

    auto len = (uint16_t)header[1];

    if (FD_ISSET(bt_sock, &readfds)) {                             // did we receive anything within 5 seconds
        bytes_read = recv(bt_sock, buf, len - sizeof(header), 0);  //Read the length specified by header
        fmt::print("length defined by header: {}, actually read: {}\n", len, bytes_read);
    } else {
        if (flag->verbose == 1)
            fmt::print("Timeout reading bluetooth socket\n");
        (*rr) = 0;
        memset(received, 0, 1024);
        return -1;
    }
    readRecord->Status[0] = 0;
    readRecord->Status[1] = 0;
    if (bytes_read > 0) {
        if (flag->debug == 1) {
            int i = 0;
            fmt::print("\n-----------------------------------------------------------");
            fmt::print("\nREAD:");
            //Start byte
            fmt::print("\n7e ");
            //Size and checkbit
            fmt::print("{:02x} {:02x} ", header[1], header[2]);
            fmt::print("                   size:              {}", len);
            fmt::print("\n   ");
            fmt::print("{:02x} ", header[3]);
            fmt::print("                      checkbit:          {:d}", header[3]);
            fmt::print("\n   ");
            //Source Address
            for (i = 0; i < bytes_read; i++) {
                if (i > 5) break;
                fmt::print("{:02x} ", buf[i]);
            }
            fmt::print("       source:            {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[5], buf[4], buf[3], buf[2], buf[1], buf[0]);
            fmt::print("\n   ");
            //Destination Address
            for (i = 6; i < bytes_read; i++) {
                if (i > 11) break;
                fmt::print("{:02x} ", buf[i]);
            }
            fmt::print("       destination:       {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[11], buf[10], buf[9], buf[8], buf[7], buf[6]);
            fmt::print("\n   ");
            //Destination Address
            for (i = 12; i < bytes_read; i++) {
                if (i > 13) break;
                fmt::print("{:02x} ", buf[i]);
            }
            fmt::print("                   control:           {:02x}{:02x}", buf[13], buf[12]);
            readRecord->Control[0] = buf[12];
            readRecord->Control[1] = buf[13];

            if (memcmp(buf + 14, "\x7e\xff\x03\x60\x65", 5) == 0) {
                fmt::print("\n");
                for (i = 14; i < bytes_read; i++) {
                    if (i > 18) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("             SMA Data2+ header: {:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[18], buf[17], buf[16], buf[15], buf[14]);
                fmt::print("\n   ");
                for (i = 19; i < bytes_read; i++) {
                    if (i > 19) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                      data packet size:  {:02d}", buf[19]);
                fmt::print("\n   ");
                for (i = 20; i < bytes_read; i++) {
                    if (i > 20) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                      control:           {:02x}", buf[20]);
                fmt::print("\n   ");
                for (i = 21; i < bytes_read; i++) {
                    if (i > 26) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("       source:            {:02x} {:02x}:{:02x}:{:02x}:{:02x}:{:02x}", buf[21], buf[26], buf[25], buf[24], buf[23], buf[22]);
                fmt::print("\n   ");
                for (i = 27; i < bytes_read; i++) {
                    if (i > 28) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                   read status:       {:02x} {:02x}", buf[28], buf[27]);
                fmt::print("\n   ");
                for (i = 29; i < bytes_read; i++) {
                    if (i > 30) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                readRecord->Status[0] = buf[28];
                readRecord->Status[1] = buf[27];
                fmt::print("                   count up:          {:02d} {:02x}:{:02x}", buf[29] + buf[30] * 256, buf[30], buf[29]);
                fmt::print("\n   ");
                for (i = 31; i < bytes_read; i++) {
                    if (i > 32) break;
                    fmt::print("{:02x} ", buf[i]);
                }
                fmt::print("                   count down:        {:02d} {:02x}:{:02x}", buf[31] + buf[32] * 256, buf[32], buf[31]);
                fmt::print("\n   ");
            }
            fmt::print("\n   ");
            std::size_t j = 0;
            for (; i < bytes_read; i++) {
                if (j % 16 == 0)
                    fmt::print("\n   {:08x}: ", j);
                fmt::print("{:02x} ", buf[i]);
                j++;
            }
            fmt::print(" rr={:d}", (bytes_read + 3));
            fmt::print("\n\n");
        }

        if ((last_sent.size() == bytes_read) && (memcmp(received, last_sent.c_str(), last_sent.size()) == 0)) {
            fmt::print("ERROR received what we sent!");
            getchar();
            //Need to do something
        }
        // Check check bit
        checkbit = header[0] ^ header[1] ^ header[2];
        if (checkbit != header[3]) {
            fmt::print("\nCheckbit Error! {:02x}!={:02x}\n", header[0] ^ header[1] ^ header[2], header[3]);
            (*rr) = 0;
            memset(received, 0, 1024);
            return -1;
        }
        if (buf[bytes_read - 1] == 0x7e)
            (*terminated) = 1;
        else
            (*terminated) = 0;
        for (int i = 0; i < bytes_read; i++) {  //start copy the rec buffer in to received
            if (buf[i] == 0x7d) {               //did we receive the escape char
                switch (buf[i + 1]) {           // act depending on the char after the escape char

                    case 0x5e:
                        received[(*rr)] = 0x7e;
                        break;

                    case 0x5d:
                        received[(*rr)] = 0x7d;
                        break;

                    default:
                        received[(*rr)] = buf[i + 1] ^ 0x20;
                        break;
                }
                i++;
            } else {
                received[(*rr)] = buf[i];
            }
            if (flag->debug == 2)
                fmt::print("{:02x} ", received[(*rr)]);
            (*rr)++;
        }
        fix_length_received(flag, received, *rr);
        if (flag->debug == 2) {
            printf("\n");
            for (int i = 0; i < (*rr); i++)
                fmt::print("{:02x} ", received[(i)]);
        }
        if (flag->debug == 1)
            fmt::print("\n\n");
    }
    return 0;
}

int select_str(char *s)
{
    for (std::size_t i = 0; i < sizeof(accepted_strings) / sizeof(*accepted_strings); ++i) {
        //printf( "\ni=%d accepted=%s string=%s", i, accepted_strings[i], s );
        if (!strcmp(s, accepted_strings[i])) return i;
    }
    return -1;
}

unsigned char *get_timezone_in_seconds(FlagType *flag, unsigned char *tzhex)
{
    time_t curtime;
    struct tm *loctime;
    struct tm *utctime;
    int day, month, year, hour, minute, isdst;

    float localOffset;
    int tzsecs;

    curtime = time(nullptr);  //get time in seconds since epoch (1/1/1970)
    loctime = localtime(&curtime);
    day = loctime->tm_mday;
    month = loctime->tm_mon + 1;
    year = loctime->tm_year + 1900;
    hour = loctime->tm_hour;
    minute = loctime->tm_min;
    isdst = loctime->tm_isdst;
    utctime = gmtime(&curtime);

    if (flag->debug == 1)
        fmt::print("utc={:04d}-{:02d}-{:02d} {:02d}:{:02d} local={:04d}-{:02d}-{:02d} {:02d}:{:02d} diff {} hours\n", utctime->tm_year + 1900, utctime->tm_mon + 1, utctime->tm_mday, utctime->tm_hour, utctime->tm_min, year, month, day, hour, minute, hour - utctime->tm_hour);
    localOffset = (hour - utctime->tm_hour) + (float)(minute - utctime->tm_min) / 60;
    if (flag->debug == 1)
        fmt::print("localOffset={}\n", localOffset);
    if ((year > utctime->tm_year + 1900) || (month > utctime->tm_mon + 1) || (day > utctime->tm_mday))
        localOffset += 24;
    if ((year < utctime->tm_year + 1900) || (month < utctime->tm_mon + 1) || (day < utctime->tm_mday))
        localOffset -= 24;
    if (flag->debug == 1)
        fmt::print("localOffset={} isdst={}\n", localOffset, isdst);
    if (isdst > 0)
        localOffset = localOffset - 1;
    tzsecs = (localOffset)*3600 + 1;
    if (tzsecs < 0)
        tzsecs = 65536 + tzsecs;
    if (flag->debug == 1)
        fmt::print("tzsecs={:x} {:d}\n", tzsecs, tzsecs);
    tzhex[1] = tzsecs / 256;
    tzhex[0] = tzsecs - (tzsecs / 256) * 256;
    if (flag->debug == 1)
        fmt::print("tzsecs={:02x} {:02x}\n", tzhex[1], tzhex[0]);

    return tzhex;
}

int auto_set_dates(ConfType *conf, FlagType *flag)
/*  If there are no dates set - get last updated date and go from there to NOW */
{
    if (flag->mysql == 1) {
        auto mysql_connection = MySQLConnection(conf->MySqlHost, conf->MySqlUser, conf->MySqlPwd, conf->MySqlDatabase);
        //Get last updated value
        const auto query = "SELECT DATE_FORMAT( DateTime, \"%Y-%m-%d %H:%i:%S\" ) FROM DayData WHERE 1 ORDER BY DateTime DESC LIMIT 1";

        MYSQL_ROW row;
        if (auto result = mysql_connection.ExecuteQuery(query, flag->debug);
            (row = mysql_fetch_row(result.res)))  //if there is a result, update the row
        {
            strcpy(conf->datefrom, row[0]);
        }
    }

    time_t curtime = time(nullptr);  //get time in seconds since epoch (1/1/1970)
    struct tm *loctime = localtime(&curtime);
    int day = loctime->tm_mday;
    int month = loctime->tm_mon + 1;
    int year = loctime->tm_year + 1900;
    int hour = loctime->tm_hour;
    int minute = loctime->tm_min;
    snprintf(conf->dateto, DATELENGTH, "%04d-%02d-%02d %02d:%02d:00", year, month, day, hour, minute);

    if (strlen(conf->datefrom) == 0)
        snprintf(conf->datefrom, DATELENGTH, "%04d-%02d-%02d 00:00:00", year, month, day);

    flag->daterange = 1;
    //if (flag->verbose == 1)
    fmt::print("Auto set dates from {} to {}\n", conf->datefrom, conf->dateto);

    return 1;
}

int is_light(ConfType *conf, FlagType *flag)
/*  Check if all data done and past sunset or before sunrise */
{
    bool light = true;

    auto mysql_connection = MySQLConnection(conf->MySqlHost, conf->MySqlUser, conf->MySqlPwd, conf->MySqlDatabase);
    //Get Start of day value

    MYSQL_ROW row;
    if (auto result = mysql_connection.ExecuteQuery("SELECT if(sunrise < NOW(),1,0) FROM Almanac WHERE date= DATE_FORMAT( NOW(), \"%Y-%m-%d\")", flag->debug);
        (row = mysql_fetch_row(result.res)))  //if there is a result, update the row
    {
        if (atoi((char *)row[0]) == 0) light = false;
    }

    if (light) {
        const auto query = "SELECT if( dd.datetime > al.sunset,1,0) FROM DayData as dd left join Almanac as al on al.date=DATE(dd.datetime) and al.date=DATE(NOW()) WHERE 1 ORDER BY dd.datetime DESC LIMIT 1";

        if (auto result = mysql_connection.ExecuteQuery(query, flag->debug); (row = mysql_fetch_row(result.res)))  //if there is a result, update the row
        {
            if (atoi((char *)row[0]) == 1) light = false;
        }
    }
    if (flag->debug == 1)
        fmt::print("is light {}\n", light);

    return light ? 1 : 0;
}

//Set a value depending on inverter
void SetInverterType(ConfType *conf, UnitType **unit)
{
    srand(time(nullptr));
    unit[0]->SUSyID[0] = 0xFF;
    unit[0]->SUSyID[1] = 0xFF;
    conf->MySUSyID[0] = rand() % 254;
    conf->MySUSyID[1] = rand() % 254;
    conf->MySerial[0] = rand() % 254;
    conf->MySerial[1] = rand() % 254;
    conf->MySerial[2] = rand() % 254;
    conf->MySerial[3] = rand() % 254;
}

// Set switches to save lots of strcmps
void SetSwitches(ConfType *conf, FlagType *flag)
{
    //Check if all location variables are set
    if ((conf->latitude_f <= 180) && (conf->longitude_f <= 180))
        flag->location = 1;
    else
        flag->location = 0;
    //Check if all Mysql variables are set
    if ((strlen(conf->MySqlUser) > 0) && (strlen(conf->MySqlPwd) > 0) && (strlen(conf->MySqlHost) > 0) && (strlen(conf->MySqlDatabase) > 0) && (flag->test == 0))
        flag->mysql = 1;
    else
        flag->mysql = 0;
    //Check if all File variables are set
    if (strlen(conf->File) > 0)
        flag->file = 1;
    else
        flag->file = 0;
    //Check if all PVOutput variables are set
    if ((strlen(conf->PVOutputURL) > 0) && (strlen(conf->PVOutputKey) > 0) && (strlen(conf->PVOutputSid) > 0))
        flag->post = 1;
    else
        flag->post = 0;
    if ((strlen(conf->datefrom) > 0) && (strlen(conf->dateto) > 0))
        flag->daterange = 1;
    else
        flag->daterange = 0;
}

unsigned char *
ReadStream(ConfType *conf, FlagType *flag, ReadRecordType *readRecord, int bt_sock, unsigned char *stream, int *streamlen, unsigned char *datalist, int *datalen, const std::string &last_sent, int *terminated, int *togo)
{
    int finished;
    int finished_record;
    int i, j = 0;

    (*togo) = ConvertStreamTo<int>(stream + 43, 2);
    if (flag->debug == 1) printf("togo=%d\n", (*togo));
    i = 59;  //Initial position of data stream
    (*datalen) = 0;
    datalist = (unsigned char *)malloc(sizeof(char));
    finished = 0;
    finished_record = 0;
    while (finished != 1) {
        datalist = (unsigned char *)realloc(datalist, sizeof(char) * ((*datalen) + (*streamlen) - i));
        while (finished_record != 1) {
            if (i > 500) break;  //Somthing has gone wrong

            if ((i < (*streamlen)) && (((*terminated) != 1) || (i + 3 < (*streamlen)))) {
                datalist[j] = stream[i];
                j++;
                (*datalen) = j;
                i++;
            } else
                finished_record = 1;
        }
        finished_record = 0;
        if ((*terminated) == 0) {
            if (read_bluetooth(conf, flag, readRecord, bt_sock, streamlen, stream, last_sent, terminated) != 0) {
                free(datalist);
                datalist = nullptr;
            }

            if (j > 0) i = 18;
        } else
            finished = 1;
    }

    if (flag->debug == 1) {
        fmt::print("len={} data=", (*datalen));
        for (i = 0; i < (*datalen); i++)
            fmt::print("{:02x} ", datalist[i]);
        fmt::print("\n");
    }
    return datalist;
}

//read return value data from init file
ReturnType *
InitReturnKeys(ConfType *conf)
{
    FILE *fp;
    char line[400];
    ReturnType tmp;
    ReturnType *returnkeylist;
    int num_return_keys = 0;
    int data_follows;

    data_follows = 0;

    fp = fopen(conf->File, "r");
    if (fp == nullptr) {
        fmt::print(stderr, "\nCouldn't open file {}", conf->File);
        fmt::print(stderr, "\nerror={}\n", strerror(errno));
        exit(1);
    } else {
        while (!feof(fp)) {
            if (fgets(line, 400, fp) != nullptr) {  //read line from smatool.conf
                if (line[0] != '#') {
                    if (strncmp(line, ":unit conversions", 17) == 0)
                        data_follows = 1;
                    if (strncmp(line, ":end unit conversions", 21) == 0)
                        data_follows = 0;
                    if (data_follows == 1) {
                        tmp.key1 = 0x0;
                        tmp.key2 = 0x0;
                        strcpy(tmp.description, "");  //Null out value
                        strcpy(tmp.units, "");        //Null out value
                        tmp.divisor = 0;
                        tmp.decimal = 0;
                        tmp.datalength = 0;
                        tmp.recordgap = 0;
                        tmp.persistent = 1;

                        if (sscanf(line, R"(%x %x "%[^"]" "%[^"]" %d %d %d %d)", &tmp.key1, &tmp.key2, tmp.description, tmp.units, &tmp.decimal, &tmp.recordgap, &tmp.datalength, &tmp.persistent) == 8) {
                            if (num_return_keys == 0)
                                returnkeylist = (ReturnType *)malloc(sizeof(ReturnType));
                            else
                                returnkeylist = (ReturnType *)realloc(returnkeylist, sizeof(ReturnType) * (num_return_keys + 1));

                            (returnkeylist + num_return_keys)->key1 = tmp.key1;
                            (returnkeylist + num_return_keys)->key2 = tmp.key2;
                            strcpy((returnkeylist + num_return_keys)->description, tmp.description);
                            strcpy((returnkeylist + num_return_keys)->units, tmp.units);
                            (returnkeylist + num_return_keys)->decimal = tmp.decimal;
                            (returnkeylist + num_return_keys)->divisor = (float)pow(10, tmp.decimal);
                            (returnkeylist + num_return_keys)->datalength = tmp.datalength;
                            (returnkeylist + num_return_keys)->recordgap = tmp.recordgap;
                            (returnkeylist + num_return_keys)->persistent = tmp.persistent;
                            ++num_return_keys;
                        } else {
                            if (line[0] != ':') {
                                fmt::print(stderr, "\nWarning Data Scan Failure\n {}\n", line);
                                getchar();
                            }
                        }
                    }
                }
            }
        }
        fclose(fp);
    }
    conf->num_return_keys = num_return_keys;
    conf->returnkeylist = returnkeylist;
    return returnkeylist;
}

/* Init Config to default values */
void InitConfig(ConfType *conf)
{
    strcpy(conf->Config, "./smatool.conf");
    strcpy(conf->BTAddress, "");
    conf->bt_timeout = 30;
    strcpy(conf->Password, "0000");
    strcpy(conf->File, "sma.in");
    strcpy(conf->Xml, "smatool.xml");
    conf->latitude_f = 999;
    conf->longitude_f = 999;
    strcpy(conf->MySqlHost, "localhost");
    strcpy(conf->MySqlDatabase, "smatool");
    strcpy(conf->MySqlUser, "");
    strcpy(conf->MySqlPwd, "");
    strcpy(conf->PVOutputURL, "http://pvoutput.org/service/r2/addstatus.jsp");
    strcpy(conf->PVOutputKey, "");
    strcpy(conf->PVOutputSid, "");
    strcpy(conf->datefrom, "");
    strcpy(conf->dateto, "");
}

/* Init Flagsg to default values */
void InitFlag(FlagType *flag)
{
    flag->debug = 0;     /* debug flag */
    flag->verbose = 0;   /* verbose flag */
    flag->daterange = 0; /* is system using a daterange */
    flag->location = 0;  /* is system using a daterange */
    flag->test = 0;      /* is system using a daterange */
    flag->mysql = 0;     /* is system using a daterange */
    flag->file = 0;      /* is system using a daterange */
    flag->post = 0;      /* is system using a daterange */
    flag->repost = 0;    /* is system using a daterange */
}

/* read Config from file */
int GetConfig(ConfType *conf, FlagType *flag)
{
    FILE *fp;
    char line[400];
    char variable[400];
    char value[400];

    if (strlen(conf->Config) > 0) {
        if ((fp = fopen(conf->Config, "r")) == (FILE *)nullptr) {
            fmt::print(stderr, "Error! Could not open file {}\n", conf->Config);
            return (-1);  //Could not open file
        }
    } else {
        if ((fp = fopen("./smatool.conf", "r")) == (FILE *)nullptr) {
            fmt::print(stderr, "Error! Could not open file ./smatool.conf\n");
            return (-1);  //Could not open file
        }
    }
    while (!feof(fp)) {
        if (fgets(line, 400, fp) != nullptr) {  //read line from smatool.conf
            if (line[0] != '#') {
                strcpy(value, "");  //Null out value
                sscanf(line, "%s %s", variable, value);
                if (flag->debug == 1)
                    fmt::print("'{}'='{}'\n", variable, value);
                if (value[0] != '\0') {
                    if (strcmp(variable, "BTAddress") == 0)
                        strcpy(conf->BTAddress, value);
                    if (strcmp(variable, "BTTimeout") == 0)
                        conf->bt_timeout = atoi(value);
                    if (strcmp(variable, "Password") == 0)
                        strcpy(conf->Password, value);
                    if (strcmp(variable, "File") == 0)
                        strcpy(conf->File, value);
                    if (strcmp(variable, "Latitude") == 0)
                        conf->latitude_f = atof(value);
                    if (strcmp(variable, "Longitude") == 0)
                        conf->longitude_f = atof(value);
                    if (strcmp(variable, "MySqlHost") == 0)
                        strcpy(conf->MySqlHost, value);
                    if (strcmp(variable, "MySqlDatabase") == 0)
                        strcpy(conf->MySqlDatabase, value);
                    if (strcmp(variable, "MySqlUser") == 0)
                        strcpy(conf->MySqlUser, value);
                    if (strcmp(variable, "MySqlPwd") == 0)
                        strcpy(conf->MySqlPwd, value);
                    //if( strcmp( variable, "PVOutputURL" ) == 0 )
                    //  strcpy( conf->PVOutputURL, value );
                    if (strcmp(variable, "PVOutputKey") == 0)
                        strcpy(conf->PVOutputKey, value);
                    if (strcmp(variable, "PVOutputSid") == 0)
                        strcpy(conf->PVOutputSid, value);
                }
            }
        }
    }
    fclose(fp);
    return (0);
}

xmlDocPtr
getdoc(char *docname)
{
    xmlDocPtr doc;
    doc = xmlParseFile(docname);

    if (doc == nullptr) {
        fmt::print(stderr, "Document not parsed successfully. \n");
        return nullptr;
    }

    return doc;
}

xmlXPathObjectPtr
getnodeset(xmlDocPtr doc, xmlChar *xpath)
{
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == nullptr) {
        fmt::print(stderr, "Error in xmlXPathNewContext\n");
        return nullptr;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == nullptr) {
        fmt::print(stderr, "Error in xmlXPathEvalExpression\n");
        return nullptr;
    }
    if (xmlXPathNodeSetIsEmpty(result->nodesetval)) {
        xmlXPathFreeObject(result);
        fmt::print("No result\n");
        return nullptr;
    }
    return result;
}

void setup_xml_xpath(xmlChar *xpath, char *docname, int index)
{
    sprintf((char *)xpath, "//Datamap/Map[@index='%d']", index);
    sprintf(docname, "%s", "smatool.xml");
}

char *return_xml_data(int index)
{
    xmlDocPtr doc;
    xmlNodeSetPtr nodeset;
    xmlNodePtr cur;
    xmlXPathObjectPtr result;
    xmlChar xpath[30];
    char docname[60];
    char *return_string = (char *)nullptr;
    xmlChar *keyword;

    setup_xml_xpath(xpath, docname, index);
    doc = getdoc(docname);
    result = getnodeset(doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (int i = 0; i < nodeset->nodeNr; i++) {
            cur = nodeset->nodeTab[i]->xmlChildrenNode;
            while (cur != nullptr) {
                if (xmlStrEqual(cur->name, (const xmlChar *)"Value")) {
                    keyword = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
                    return_string = static_cast<char *>(malloc(sizeof(char) * strlen((char *)keyword) + 1));
                    strcpy(return_string, (char *)keyword);
                    xmlFree(keyword);
                }
                cur = cur->next;
            }
        }
        xmlXPathFreeObject(result);
    } else {
        fmt::print(stderr, "\nfailed to getnodeset with xpath '{}'\n", xpath);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();

    return (char *)return_string;
}

/* Print a help message */
void PrintHelp()
{
    fmt::print("Usage: smatool [OPTION]\n");
    fmt::print("  -v,  --verbose                           Give more verbose output\n");
    fmt::print("  -d,  --debug                             Show debug\n");
    fmt::print("  -c,  --config CONFIGFILE                 Set config file default smatool.conf\n");
    fmt::print("       --test                              Run in test mode - don't update data\n");
    fmt::print("\n");
    fmt::print("Dates are no longer required - defaults to last update if using mysql\n");
    fmt::print("or 2000 to now if not using mysql\n");
    fmt::print("  -from  --datefrom YYYY-MM-DD HH:MM:00    Date range from date\n");
    fmt::print("  -to  --dateto YYYY-MM-DD HH:MM:00        Date range to date\n");
    fmt::print("\n");
    fmt::print("The following options are in config file but may be overridden\n");
    fmt::print("  -i,  --inverter INVERTER_MODEL           inverter model\n");
    fmt::print("  -a,  --address INVERTER_ADDRESS          inverter BT address\n");
    fmt::print("  -t,  --timeout TIMEOUT                   bluetooth timeout (secs) default 5\n");
    fmt::print("  -p,  --password PASSWORD                 inverter user password default 0000\n");
    fmt::print("  -f,  --file FILENAME                     command file default sma.in\n");
    fmt::print("Location Information to calculate sunset and sunrise so inverter is not\n");
    fmt::print("queried in the dark\n");
    fmt::print("  -lat,  --latitude LATITUDE               location latitude -180 to 180 deg\n");
    fmt::print("  -lon,  --longitude LONGITUDE             location longitude -90 to 90 deg\n");
    fmt::print("Mysql database information\n");
    fmt::print("  -H,  --mysqlhost MYSQLHOST               mysql host default localhost\n");
    fmt::print("  -D,  --mysqldb MYSQLDATBASE              mysql database default smatool\n");
    fmt::print("  -U,  --mysqluser MYSQLUSER               mysql user\n");
    fmt::print("  -P,  --mysqlpwd MYSQLPASSWORD            mysql password\n");
    fmt::print("Mysql tables can be installed using INSTALL you may have to use a higher \n");
    fmt::print("privelege user to allow the creation of databases and tables, use command line \n");
    fmt::print("       --INSTALL                           install mysql data tables\n");
    fmt::print("       --UPDATE                            update mysql data tables\n");
    fmt::print("PVOutput.org (A free solar information system) Configs\n");
    fmt::print("  -url,  --pvouturl PVOUTURL               pvoutput.org live url\n");
    fmt::print("  -key,  --pvoutkey PVOUTKEY               pvoutput.org key\n");
    fmt::print("  -sid,  --pvoutsid PVOUTSID               pvoutput.org sid\n");
    fmt::print("  -repost                                  verify and repost data if different\n");
    fmt::print("\n\n");
}

/* Init Config to default values */
int ReadCommandConfig(ConfType *conf, FlagType *flag, int argc, char **argv, int *no_dark, int *install, int *update)
{
    // these need validation checking at some stage TODO
    for (int i = 1; i < argc; i++)  //Read through passed arguments
    {
        if ((strcmp(argv[i], "-v") == 0) || (strcmp(argv[i], "--verbose") == 0)) {
            flag->verbose = 1;
        } else if ((strcmp(argv[i], "-d") == 0) || (strcmp(argv[i], "--debug") == 0)) {
            flag->debug = 1;
            flag->verbose = 1;
        } else if ((strcmp(argv[i], "-c") == 0) || (strcmp(argv[i], "--config") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->Config, argv[i]);
            }
        } else if (strcmp(argv[i], "--test") == 0) {
            flag->test = 1;
        } else if ((strcmp(argv[i], "-from") == 0) || (strcmp(argv[i], "--datefrom") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->datefrom, argv[i]);
            }
        } else if ((strcmp(argv[i], "-to") == 0) || (strcmp(argv[i], "--dateto") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->dateto, argv[i]);
            }
        } else if (strcmp(argv[i], "-repost") == 0) {
            i++;
            flag->repost = 1;
        } else if ((strcmp(argv[i], "-a") == 0) || (strcmp(argv[i], "--address") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->BTAddress, argv[i]);
            }
        } else if ((strcmp(argv[i], "-t") == 0) || (strcmp(argv[i], "--timeout") == 0)) {
            i++;
            if (i < argc) {
                conf->bt_timeout = atoi(argv[i]);
            }
        } else if ((strcmp(argv[i], "-p") == 0) || (strcmp(argv[i], "--password") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->Password, argv[i]);
            }
        } else if ((strcmp(argv[i], "-f") == 0) || (strcmp(argv[i], "--file") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->File, argv[i]);
            }
        } else if ((strcmp(argv[i], "-n") == 0) || (strcmp(argv[i], "--nodark") == 0)) {
            (*no_dark) = 1;
        } else if ((strcmp(argv[i], "-lat") == 0) || (strcmp(argv[i], "--latitude") == 0)) {
            i++;
            if (i < argc) {
                conf->latitude_f = atof(argv[i]);
            }
        } else if ((strcmp(argv[i], "-long") == 0) || (strcmp(argv[i], "--longitude") == 0)) {
            i++;
            if (i < argc) {
                conf->longitude_f = atof(argv[i]);
            }
        } else if ((strcmp(argv[i], "-H") == 0) || (strcmp(argv[i], "--mysqlhost") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->MySqlHost, argv[i]);
            }
        } else if ((strcmp(argv[i], "-D") == 0) || (strcmp(argv[i], "--mysqlcwdb") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->MySqlDatabase, argv[i]);
            }
        } else if ((strcmp(argv[i], "-U") == 0) || (strcmp(argv[i], "--mysqluser") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->MySqlUser, argv[i]);
            }
        } else if ((strcmp(argv[i], "-P") == 0) || (strcmp(argv[i], "--mysqlpwd") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->MySqlPwd, argv[i]);
            }
        } else if ((strcmp(argv[i], "-url") == 0) || (strcmp(argv[i], "--pvouturl") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->PVOutputURL, argv[i]);
            }
        } else if ((strcmp(argv[i], "-key") == 0) || (strcmp(argv[i], "--pvoutkey") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->PVOutputKey, argv[i]);
            }
        } else if ((strcmp(argv[i], "-sid") == 0) || (strcmp(argv[i], "--pvoutsid") == 0)) {
            i++;
            if (i < argc) {
                strcpy(conf->PVOutputSid, argv[i]);
            }
        } else if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0)) {
            PrintHelp();
            return (-1);
        } else if (strcmp(argv[i], "--INSTALL") == 0) {
            (*install) = 1;
        } else if (strcmp(argv[i], "--UPDATE") == 0) {
            (*update) = 1;
        } else {
            printf("Bad Syntax\n\n");
            for (i = 0; i < argc; i++)
                printf("%s ", argv[i]);
            printf("\n\n");

            PrintHelp();
            return (-1);
        }
    }
    return (0);
}

bool post_pvoutput(const std::string &batch_string, std::size_t batch_count, MySQLConnection &mysql_connection, const std::string &PVOutputKey, const std::string &PVOutputSid, bool debug)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;

    const auto compurl = fmt::format("http://pvoutput.org/service/r2/addbatchstatus.jsp?data={}&key={}&sid={}", batch_string, PVOutputKey, PVOutputSid);
    if (debug == 1)
        fmt::print("url = {}\n", compurl);
    curl_easy_setopt(curl, CURLOPT_URL, compurl.c_str());
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, compurl.c_str());
    CURLcode curl_result = curl_easy_perform(curl);
    sleep(1);
    if (debug == 1)
        fmt::print("result = {}\n", curl_result);
    curl_easy_cleanup(curl);
    if (curl_result != 0)
        return false;

    const auto query_date = fmt::format(R"(SELECT DATE_FORMAT(dd1.DateTime,'%Y%m%d'), DATE_FORMAT(dd1.DateTime,'%H:%i'), ROUND((dd1.ETotalToday-dd2.EtotalToday)*1000), dd1.CurrentPower, dd1.DateTime FROM DayData as dd1 join DayData as dd2 on dd2.DateTime=DATE_FORMAT(dd1.DateTime,'%Y-%m-%d 00:00:00') WHERE dd1.DateTime>=Date_Sub(CURDATE(),INTERVAL 13 DAY) and dd1.PVOutput IS NULL and dd1.CurrentPower>0 ORDER BY dd1.DateTime ASC limit {})", batch_count);
    auto result_to_update = mysql_connection.ExecuteQuery(query_date, debug);

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result_to_update.res)))  //Need to update these
    {
        const auto query_update = fmt::format("UPDATE DayData set PVOutput=NOW() WHERE DateTime=\"{}\"", row[4]);
        mysql_connection.ExecuteQuery(query_update, debug);
    }

    return true;
}

int main(int argc, char **argv)
{
    FILE *fp;
    ConfType conf{};
    FlagType flag{};
    int maximumUnits = 1;
    UnitType *unit;
    unsigned char received[1024];
    int install = 0, update = 0, no_dark = 0;
    int error = 0;
    int max_output;
    unsigned char tzhex[2] = {0};
    MYSQL_ROW row;
    ArchDataList archdatalist{};
    LiveDataList livedatalist{};

    char sunrise_time[6], sunset_time[6];

    unit = (UnitType *)malloc(sizeof(UnitType) * maximumUnits);
    if (unit == nullptr) {
        fmt::print(stderr, "Error allocating memory for line buffer.");
        exit(1);
    }
    memset(received, 0, sizeof(received));

    // set config to defaults
    InitConfig(&conf);
    InitFlag(&flag);
    // read command arguments needed so can get config
    if (ReadCommandConfig(&conf, &flag, argc, argv, &no_dark, &install, &update) < 0)
        exit(0);
    // read Config file
    if (GetConfig(&conf, &flag) < 0)
        exit(-1);
    // read command arguments  again - they overide config
    if (ReadCommandConfig(&conf, &flag, argc, argv, &no_dark, &install, &update) < 0)
        exit(0);
    // read Inverter Setting file
    //if( GetInverterSetting( &conf ) < 0 )
    //  exit(-1);
    // set switches used through the program
    SetSwitches(&conf, &flag);
    if ((install == 1) && (flag.mysql == 1)) {
        install_mysql_tables(&conf, &flag, SCHEMA);
        exit(0);
    }
    if ((update == 1) && (flag.mysql == 1)) {
        update_mysql_tables(&conf, &flag);
        exit(0);
    }
    // Get Return Value lookup from file
    InitReturnKeys(&conf);
    // Set value for inverter type

    SetInverterType(&conf, &unit);
    // Get Local Timezone offset in seconds
    get_timezone_in_seconds(&flag, tzhex);
    // Location based information to avoid querying Inverter in the dark
    if ((flag.location == 1) && (flag.mysql == 1)) {
        if (flag.debug == 1) fmt::print("Before todays Almanac\n");
        if (!todays_almanac(&conf, flag.debug)) {
            sprintf(sunrise_time, "%s", sunrise(&conf, flag.debug));
            sprintf(sunset_time, "%s", sunset(&conf));
            if (flag.verbose == 1) fmt::print("sunrise={} sunset={}\n", sunrise_time, sunset_time);
            update_almanac(&conf, sunrise_time, sunset_time, flag.debug);
        }
    }

    if (flag.mysql == 1) {
        if (flag.debug == 1) fmt::print("Before Check Schema\n");
        if (check_schema(&conf, &flag, SCHEMA) != 1)
            exit(-1);
    }

    if (flag.daterange == 0) {  //auto set the dates
        if (flag.debug == 1) fmt::print("auto_set_dates\n");
        auto_set_dates(&conf, &flag);
    }

    if (flag.verbose == 1)
        fmt::print("QUERY RANGE    from {} to {}\n", conf.datefrom, conf.dateto);

    if ((flag.daterange == 1) && ((flag.location = 0) || (flag.mysql == 0) || no_dark == 1 || is_light(&conf, &flag))) {
        if (flag.file == 1)
            fp = fopen(conf.File, "r");
        else
            fp = fopen("sma.in", "r");

        fmt::print("Connecting to {}\n", conf.BTAddress);
        //Connect to Inverter
        BTConnection bt_conn{conf.BTAddress};

        SessionData session_data{archdatalist, livedatalist, bt_conn, conf, flag, &unit, fp};

        InverterCommand("init", session_data);
        InverterCommand("login", session_data);
        InverterCommand("typelabel", session_data);
        InverterCommand("startuptime", session_data);
        InverterCommand("getacvoltage", session_data);
        InverterCommand("getenergyproduction", session_data);
        InverterCommand("getspotdcpower", session_data);
        InverterCommand("getspotdcvoltage", session_data);
        InverterCommand("getspotacpower", session_data);
        InverterCommand("getgridfreq", session_data);
        InverterCommand("maxACPower", session_data);
        InverterCommand("maxACPowerTotal", session_data);
        InverterCommand("ACPowerTotal", session_data);
        InverterCommand("DeviceStatus", session_data);
        InverterCommand("getrangedata", session_data);
        InverterCommand("logoff", session_data);
    }

    if ((flag.mysql == 1) && (error == 0)) {
        /* Connect to database */
        auto mysql_connection = MySQLConnection(conf.MySqlHost, conf.MySqlUser, conf.MySqlPwd, conf.MySqlDatabase);
        for (const auto &data : archdatalist)  //Start at 1 as the first record is a dummy
        {
            const auto query = fmt::format("INSERT INTO DayData ( DateTime, Inverter, Serial, CurrentPower, EtotalToday ) VALUES ( FROM_UNIXTIME({}),\'{}\',{},{:.0f}, {:.3f} ) ON DUPLICATE KEY UPDATE DateTime=Datetime, Inverter=VALUES(Inverter), Serial=VALUES(Serial), CurrentPower=VALUES(CurrentPower), EtotalToday=VALUES(EtotalToday)",
                                           data.date, data.inverter, data.serial, data.current_value, data.accum_value);
            mysql_connection.ExecuteQuery(query, flag.debug);
        }

        if (flag.post == 1) {

            //Update Mysql with live data
            live_mysql(conf, flag.debug, livedatalist);
            printf("\nbefore update to PVOutput");
            getchar();
            {
                unsigned long long inverter_serial = (unit[0].Serial[0] << 24) + (unit[0].Serial[1] << 16) + (unit[0].Serial[2] << 8) + unit[0].Serial[3];
                const auto query = fmt::format(R"(SELECT Value FROM LiveData WHERE Inverter = '{}' and Serial='{}' and Description='Max Phase 1' ORDER BY DateTime DESC LIMIT 1)", unit[0].Inverter, inverter_serial);
                if (auto result = mysql_connection.ExecuteQuery(query, flag.debug); mysql_num_rows(result.res) == 1) {
                    if ((row = mysql_fetch_row(result.res))) {
                        max_output = atoi(row[0]) * 1.2;
                    }
                }
            }

            const auto query = fmt::format(R"(SELECT DATE_FORMAT(dd1.DateTime,'%Y%m%d'), DATE_FORMAT(dd1.DateTime,'%H:%i'), ROUND((dd1.ETotalToday-dd2.EtotalToday)*1000), if( dd1.CurrentPower < {0}, dd1.CurrentPower, {0} ), dd1.DateTime FROM DayData as dd1 join DayData as dd2 on dd2.DateTime=DATE_FORMAT(dd1.DateTime,'%Y-%m-%d 00:00:00') WHERE dd1.DateTime>=Date_Sub(CURDATE(),INTERVAL 13 DAY) and dd1.PVOutput IS NULL and dd1.CurrentPower>0 ORDER BY dd1.DateTime ASC)", max_output);

            auto result = mysql_connection.ExecuteQuery(query, flag.debug);
            if (mysql_num_rows(result.res) == 1) {
                if ((row = mysql_fetch_row(result.res)))  //Need to update these
                {
                    const auto compurl = fmt::format("{}?d={}&t={}&v1={}&v2={}&key={}&sid={}", conf.PVOutputURL, row[0], row[1], row[2], row[3], conf.PVOutputKey, conf.PVOutputSid);
                    if (flag.debug == 1)
                        fmt::print("url = {}\n", compurl);
                    {
                        CURL *curl = curl_easy_init();
                        if (curl) {
                            curl_easy_setopt(curl, CURLOPT_URL, compurl.c_str());
                            curl_easy_setopt(curl, CURLOPT_FAILONERROR, compurl.c_str());
                            CURLcode curl_result = curl_easy_perform(curl);
                            if (flag.debug == 1)
                                fmt::print("result = {}\n", curl_result);
                            curl_easy_cleanup(curl);
                            if (curl_result == 0) {
                                const auto query_update = fmt::format("UPDATE DayData  set PVOutput=NOW() WHERE DateTime=\"{}\"", row[4]);
                                mysql_connection.ExecuteQuery(query_update, flag.debug);
                            }
                        }
                    }
                }
            } else {
                std::string batch_string{};
                std::size_t batch_count{0};
                while ((row = mysql_fetch_row(result.res)))  //Need to update these
                {
                    sleep(2);
                    if (!batch_string.empty())
                        batch_string.append(";");

                    batch_string.append(fmt::format("{},{},{},{}", row[0], row[1], row[2], row[3]));
                    ++batch_count;
                    if (batch_count == 30) {
                        auto success = post_pvoutput(batch_string, batch_count, mysql_connection, conf.PVOutputKey, conf.PVOutputSid, flag.debug);
                        if (!success)
                            break;

                        batch_count = 0;
                        batch_string.clear();
                    }
                }
                if (batch_count > 0) {
                    post_pvoutput(batch_string, batch_count, mysql_connection, conf.PVOutputKey, conf.PVOutputSid, flag.debug);
                }
            }
        }
    }

    if ((flag.repost == 1) && (error == 0)) {
        fmt::print("\nrepost\n");  //getchar();
        sma_repost(&conf, &flag);
    }

    return 0;
}
