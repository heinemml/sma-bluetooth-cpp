#include "sb_commands.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <fmt/format.h>
#if __has_include(<fmt/chrono.h>)
#include <fmt/chrono.h>
#else
#include <fmt/time.h>
#endif
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "sma_mysql.h"
#include "sma_struct.h"
#include "smatool.h"

std::string debugdate()
{
    auto now = std::time(nullptr);
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", *std::localtime(&now));
}

/*
 * Update internal running list with live data for later processing
 */
int UpdateLiveList(FlagType *flag, UnitType *unit, const char *format, time_t idate, const char *description, float fvalue, int ivalue, const char *svalue, const char *units, int persistent, LiveDataList &livedatalist)
{
    if (strlen(unit->Inverter) > 0) {
        auto &element = livedatalist.emplace_back();
        element.date = idate;
        strcpy(element.inverter, unit->Inverter);

        unsigned long long inverter_serial = (unit->Serial[0] << 24) + (unit->Serial[1] << 16) + (unit->Serial[2] << 8) + unit->Serial[3];
        element.serial = inverter_serial;
        strcpy(element.Description, description);
        strcpy(element.Units, units);
        if (fvalue >= 0)
            sprintf(element.Value, format, fvalue);
        else if (ivalue > 0)
            sprintf(element.Value, format, ivalue);
        else if (strlen(svalue) > 0) {
            sprintf(element.Value, format, svalue);
        }
        element.Persistent = persistent;
    } else {
        if (flag->debug == 1)
            printf("Don't have inverter details yet\n");
    }
    return 0;
}

int ProcessCommand(ConfType *conf, FlagType *flag, UnitType **unit, int bt_sock, FILE *fp, int *linenum, ArchDataList &archdatalist, LiveDataList &livedatalist)
{
    ssize_t read = 0;
    int cc = 0, rr = 0;
    int datalen = 0;
    int failedbluetooth = 0;
    int error = 0;
    int togo = 0;
    int finished;
    time_t fromtime;
    time_t totime;
    unsigned char tzhex[2] = {0};
    unsigned char timeset[4] = {0x30, 0xfe, 0x7e, 0x00};
    tm tm{};
    unsigned char fl[1024] = {0};
    unsigned char received[1024];
    unsigned char datarecord[1024];
    unsigned char dest_address[6] = {0};
    unsigned char timestr[25] = {0};
    ReadRecordType readRecord;
    char *lineread;
    unsigned char *data;
    char BTAddressBuf[20];
    char tt[10] = {48, 48, 48, 48, 48, 48, 48, 48, 48, 48};
    char ti[3];
    char *datastring;
    float currentpower_total = 0.0;
    float dtotal = 0.0;
    float gtotal = 0.0;
    float ptotal = 0.0;
    float strength = 0.0;
    int already_read = 0, terminated = 0;
    int gap = 0, return_key = 0, datalength = 0;
    int pass_i = 0, send_count = 0;
    int persistent = 0;
    int index = 0;
    unsigned long long inverter_serial = 0;

    //convert address
    strncpy(BTAddressBuf, conf->BTAddress, 20);
    dest_address[5] = conv(strtok(BTAddressBuf, ":"));
    dest_address[4] = conv(strtok(nullptr, ":"));
    dest_address[3] = conv(strtok(nullptr, ":"));
    dest_address[2] = conv(strtok(nullptr, ":"));
    dest_address[1] = conv(strtok(nullptr, ":"));
    dest_address[0] = conv(strtok(nullptr, ":"));
    /* get the report time - used in various places */
    auto reporttime = time(nullptr);  //get time in seconds since epoch (1/1/1970)

    char *line = nullptr;
    std::size_t len = 0;
    while ((read = getline(&line, &len, fp)) != -1) {  //read line from sma.in

        (*linenum)++;
        auto *last_sent = (unsigned char *)malloc(sizeof(unsigned char));
        if (last_sent == nullptr) {
            printf("\nOut of memory\n");
            return (-1);
        }

        lineread = strtok(line, " ;");
        if (lineread[0] == ':') {  //See if line is something we need to receive
            if (flag->debug == 1)
                printf("\nCommand sequence finished\n");
            break;
        }

        if (!strcmp(lineread, "R")) {  //See if line is something we need to receive
            if (flag->debug == 1) printf("[%d] %s Waiting for string\n", (*linenum), debugdate().c_str());
            cc = 0;
            do {
                lineread = strtok(nullptr, " ;");
                switch (select_str(lineread)) {
                    case 0:  // $END
                        //do nothing
                        break;

                    case 1:  // $ADDR
                        for (int i = 0; i < 6; i++) {
                            fl[cc] = dest_address[i];
                            cc++;
                        }
                        break;

                    case 3:  // $SERIAL
                        for (int i = 0; i < 4; i++) {
                            fl[cc] = unit[0]->Serial[i];
                            cc++;
                        }
                        break;

                    case 7:  // $ADD2
                        for (int i = 0; i < 6; i++) {
                            fl[cc] = conf->MyBTAddress[i];
                            cc++;
                        }
                        break;

                    default:
                        fl[cc] = conv(lineread);
                        cc++;
                }

            } while (strcmp(lineread, "$END") != 0);
            if (flag->debug == 1) {
                printf("[%d] %s waiting for: ", (*linenum), debugdate().c_str());
                for (int i = 0; i < cc; i++) printf("%02x ", fl[i]);
                printf("\n\n");
            }
            if (flag->debug == 1) printf("[%d] %s Waiting for data on rfcomm\n", (*linenum), debugdate().c_str());
            bool data_found{false};
            while (!data_found) {
                if (already_read == 0)
                    rr = 0;
                if ((already_read == 0) && (read_bluetooth(conf, flag, &readRecord, bt_sock, &rr, received, cc, last_sent, &terminated) != 0)) {
                    already_read = 0;
                    strcpy(lineread, "");
                    sleep(10);
                    failedbluetooth++;
                    if (failedbluetooth > 3)
                        return (-1);
                } else {
                    already_read = 0;

                    if (flag->debug == 1) {
                        printf("[%d] %s looking for: ", (*linenum), debugdate().c_str());
                        for (int i = 0; i < cc; i++) printf("%02x ", fl[i]);
                        printf("\n");
                        printf("[%d] %s received:    ", (*linenum), debugdate().c_str());
                        for (int i = 0; i < rr; i++) printf("%02x ", received[i]);
                        printf("\n\n");
                    }

                    if (memcmp(fl + 4, received + 4, cc - 4) == 0) {
                        data_found = true;
                        if (flag->debug == 1) printf("[%d] %s Found string we are waiting for\n", (*linenum), debugdate().c_str());
                    } else {
                        if (flag->debug == 1) printf("[%d] %s Did not find string\n", (*linenum), debugdate().c_str());
                    }
                }
            }
            if (flag->debug == 2) {
                for (int i = 0; i < cc; i++)
                    printf("%02x ", fl[i]);
                printf("\n\n");
            }
        }
        if (!strcmp(lineread, "S")) {  //See if line is something we need to send
            //Empty the receive data ready for new command
            while (((*linenum) > 22) && (empty_read_bluetooth(flag, &readRecord, bt_sock, &rr, received, &terminated) >= 0))
                ;
            if (flag->debug == 1) printf("[%d] %s Sending\n", (*linenum), debugdate().c_str());
            cc = 0;
            do {
                lineread = strtok(nullptr, " ;");
                switch (select_str(lineread)) {
                    case 0:  // $END
                        //do nothing
                        break;

                    case 1:  // $ADDR
                        for (std::size_t i = 0; i < 6; i++) {
                            fl[cc] = dest_address[i];
                            cc++;
                        }
                        break;

                    case 3:  // $SERIAL
                        for (std::size_t i = 0; i < 4; i++) {
                            fl[cc] = unit[0]->Serial[i];
                            cc++;
                        }
                        break;

                    case 7:  // $ADD2
                        for (std::size_t i = 0; i < 6; i++) {
                            fl[cc] = conf->MyBTAddress[i];
                            cc++;
                        }
                        break;

                    case 2:  // $TIME
                        // get report time and convert
                        sprintf(tt, "%x", (int)reporttime);  //convert to a hex in a string
                        for (int i = 7; i > 0; i = i - 2) {  //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 11:                                         // $TMPLUS
                                                                     // get report time and convert
                        sprintf(tt, "%x", (int)reporttime + 1);      //convert to a hex in a string
                        for (std::size_t i = 7; i > 0; i = i - 2) {  //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 10:  // $TMMINUS
                        // get report time and convert
                        sprintf(tt, "%x", (int)reporttime - 1);      //convert to a hex in a string
                        for (std::size_t i = 7; i > 0; i = i - 2) {  //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 4:  //$crc
                        tryfcs16(flag, fl + 19, cc - 19, fl, &cc);
                        add_escapes(fl, &cc);
                        fix_length_send(flag, fl, cc);
                        break;

                    case 12:  // $TIMESTRING
                        for (std::size_t i = 0; i < 25; i++) {
                            fl[cc] = timestr[i];
                            cc++;
                        }
                        break;

                    case 13:  // $TIMEFROM1
                              // get report time and convert
                        if (flag->daterange == 1) {
                            if (strptime(conf->datefrom, "%Y-%m-%d %H:%M:%S", &tm) == nullptr) {
                                if (flag->debug == 1) printf("datefrom %s\n", conf->datefrom);
                                printf("Time Coversion Error\n");
                                error = 1;
                                exit(-1);
                            }
                            tm.tm_isdst = -1;
                            fromtime = mktime(&tm);
                            if (fromtime == -1) {
                                // Error we need to do something about it
                                printf("%03x", (int)fromtime);
                                getchar();
                                printf("\n%03x", (int)fromtime);
                                getchar();
                                fromtime = 0;
                                printf("bad from");
                                getchar();
                            }
                        } else {
                            printf("no from");
                            getchar();
                            fromtime = 0;
                        }
                        sprintf(tt, "%03x", (int)fromtime - 300);  //convert to a hex in a string and start 5 mins before for dummy read.
                        for (int i = 7; i > 0; i = i - 2) {        //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 14:  // $TIMETO1
                        if (flag->daterange == 1) {
                            if (strptime(conf->dateto, "%Y-%m-%d %H:%M:%S", &tm) == nullptr) {
                                if (flag->debug == 1) printf("dateto %s\n", conf->dateto);
                                printf("Time Conversion Error\n");
                                error = 1;
                                exit(-1);
                            }
                            tm.tm_isdst = -1;
                            totime = mktime(&tm);
                            if (totime == -1) {
                                // Error we need to do something about it
                                printf("%03x", (int)totime);
                                getchar();
                                printf("\n%03x", (int)totime);
                                getchar();
                                totime = 0;
                                printf("bad to");
                                getchar();
                            }
                        } else
                            totime = 0;
                        sprintf(tt, "%03x", (int)totime);    //convert to a hex in a string
                                                             // get report time and convert
                        for (int i = 7; i > 0; i = i - 2) {  //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 15:  // $TIMEFROM2
                        if (flag->daterange == 1) {
                            strptime(conf->datefrom, "%Y-%m-%d %H:%M:%S", &tm);
                            tm.tm_isdst = -1;
                            fromtime = mktime(&tm) - 86400;
                            if (fromtime == -1) {
                                // Error we need to do something about it
                                printf("%03x", (int)fromtime);
                                getchar();
                                printf("\n%03x", (int)fromtime);
                                getchar();
                                fromtime = 0;
                                printf("bad from");
                                getchar();
                            }
                        } else {
                            printf("no from");
                            getchar();
                            fromtime = 0;
                        }
                        sprintf(tt, "%03x", (int)fromtime);  //convert to a hex in a string
                        for (int i = 7; i > 0; i = i - 2) {  //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 16:  // $TIMETO2
                        if (flag->daterange == 1) {
                            strptime(conf->dateto, "%Y-%m-%d %H:%M:%S", &tm);

                            tm.tm_isdst = -1;
                            totime = mktime(&tm) - 86400;
                            if (totime == -1) {
                                // Error we need to do something about it
                                printf("%03x", (int)totime);
                                getchar();
                                printf("\n%03x", (int)totime);
                                getchar();
                                fromtime = 0;
                                printf("bad from");
                                getchar();
                            }
                        } else
                            totime = 0;
                        sprintf(tt, "%03x", (int)totime);    //convert to a hex in a string
                        for (int i = 7; i > 0; i = i - 2) {  //change order and convert to integer
                            ti[1] = tt[i];
                            ti[0] = tt[i - 1];
                            ti[2] = '\0';
                            fl[cc] = conv(ti);
                            cc++;
                        }
                        break;

                    case 19:  // $PASSWORD
                    {
                        std::size_t j = 0;
                        for (std::size_t i = 0; i < 12; i++) {
                            if (conf->Password[j] == '\0')
                                fl[cc] = 0x88;
                            else {
                                pass_i = conf->Password[j];
                                fl[cc] = ((pass_i + 0x88) % 0xff);
                                j++;
                            }
                            cc++;
                        }
                        break;
                    }
                    case 21:  // $SUSyID
                        for (std::size_t i = 0; i < 2; i++) {
                            fl[cc] = unit[0]->SUSyID[i];
                            cc++;
                        }
                        break;

                    case 22:  // $INVCODE
                        fl[cc] = conf->NetID;
                        cc++;
                        break;

                    case 25:  // $CNT send counter
                        send_count++;
                        fl[cc] = send_count;
                        cc++;
                        break;

                    case 26:  // $TIMEZONE timezone in seconds, reverse endian
                        fl[cc] = tzhex[0];
                        fl[cc + 1] = tzhex[1];
                        cc += 2;
                        break;

                    case 27:  // $TIMESET unknown setting
                        for (int i = 0; i < 4; i++) {
                            fl[cc] = timeset[i];
                            cc++;
                        }
                        break;

                    case 29:  // $MYSUSYID
                        for (int i = 0; i < 2; i++) {
                            fl[cc] = conf->MySUSyID[i];
                            cc++;
                        }
                        break;

                    case 30:  // $MYSERIAL
                        for (int i = 0; i < 4; i++) {
                            fl[cc] = conf->MySerial[i];
                            cc++;
                        }
                        printf("\n");
                        break;

                    default:
                        fl[cc] = conv(lineread);
                        cc++;
                }

            } while (strcmp(lineread, "$END") != 0);
            if (flag->debug == 1) {
                int last_decoded = 0;
                int j = 1;

                printf(" cc=%d", cc);
                printf("\n\n");
                printf("\n-----------------------------------------------------------");
                printf("\nSEND:");
                //Start byte
                printf("\n7e ");
                //Size and checkbit
                auto len = (uint16_t)fl[j];
                printf("%02x %02x                    size:              %d", fl[j], fl[j + 1], len);
                ++j;
                printf("\n   ");
                printf("%02x ", fl[++j]);
                printf("\n   ");
                printf("%02x ", fl[++j]);
                printf("                      checkbit:          %d", fl[j]);
                printf("\n   ");
                //Source Address
                for (int i = ++j; i < cc; i++) {
                    if (i > j + 5) break;
                    printf("%02x ", fl[i]);
                }
                printf("       source:            %02x:%02x:%02x:%02x:%02x:%02x", fl[j + 5], fl[j + 4], fl[j + 3], fl[j + 2], fl[j + 1], fl[j]);
                j = j + 5;
                printf("\n   ");
                //Destination Address
                for (int i = ++j; i < cc; i++) {
                    if (i > j + 5) break;
                    printf("%02x ", fl[i]);
                }
                printf("       destination:       %02x:%02x:%02x:%02x:%02x:%02x", fl[j + 5], fl[j + 4], fl[j + 3], fl[j + 2], fl[j + 1], fl[j]);
                j = j + 5;
                printf("\n   ");
                //Destination Address
                for (int i = ++j; i < cc; i++) {
                    if (i > j + 1) break;
                    printf("%02x ", fl[i]);
                }
                printf("                   control:           %02x%02x", fl[j + 1], fl[j]);
                j++;
                last_decoded = j + 1;
                j++;
                if (memcmp(fl + j, "\x7e\xff\x03\x60\x65", 5) == 0) {
                    printf("\n");
                    for (int i = j; i < cc; i++) {
                        if (i > j + 4) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("             SMA Data2+ header: %02x:%02x:%02x:%02x:%02x", fl[j + 4], fl[j + 3], fl[j + 2], fl[j + 1], fl[j]);
                    j += 5;
                    printf("\n   ");
                    for (int i = ++j; i < cc; i++) {
                        if (i > j) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("                      data packet size:  %02d", fl[j]);
                    printf("\n   ");
                    for (int i = ++j; i < cc; i++) {
                        if (i > j) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("                      SUSYId:            %02x %02x", fl[j], fl[j + 1]);
                    j++;
                    printf("\n   ");
                    for (int i = ++j; i < cc; i++) {
                        if (i > j + 3) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("             Serial:            %02x:%02x:%02x:%02x", fl[j + 3], fl[j + 2], fl[j + 1], fl[j]);
                    j = j + 3;
                    printf("\n   ");
                    for (int i = ++j; i < cc; i++) {
                        if (i > j + 1) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("                   unknown:           %02x %02x", fl[j + 1], fl[j]);
                    j++;
                    printf("\n   ");
                    for (int i = ++j; i < cc; i++) {
                        if (i > j + 1) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("                   MySUSId:           %02x:%02x", fl[j + 1], fl[j]);
                    printf("\n   ");
                    for (int i = ++j; i < cc; i++) {
                        if (i > j + 3) break;
                        printf("%02x ", fl[i]);
                    }
                    printf("             MySerial:          %02x:%02x:%02x:%02x", fl[j + 3], fl[j + 2], fl[j + 1], fl[j]);
                    printf("\n   ");
                    j++;
                    last_decoded = j + 1;
                }
                printf("\n   ");
                j = 0;
                for (int i = last_decoded; i < cc; i++) {
                    if (j % 16 == 0)
                        printf("\n   %08x: ", j);
                    printf("%02x ", fl[i]);
                    j++;
                }
                printf(" rr=%d", (cc + 3));
                printf("\n\n");
            }
            last_sent = (unsigned char *)realloc(last_sent, sizeof(unsigned char) * (cc));
            memcpy(last_sent, fl, cc);
            write((bt_sock), fl, cc);
            already_read = 0;
            //check_send_error( &conf, &s, &rr, received, cc, last_sent, &terminated, &already_read );
        }

        if (!strcmp(lineread, "E")) {  //See if line is something we need to extract
            if (readRecord.Status[0] == 0xe0) {
                if (flag->debug == 1) printf("\n%s There is no data to extract, waiting", debugdate().c_str());
                // Read the rest of the records
                while (read_bluetooth(conf, flag, &readRecord, bt_sock, &rr, received, cc, last_sent, &terminated) == 0) {
                    if (flag->debug == 1) printf(".");
                }
                if (flag->debug == 1) printf("\n");
            } else {
                if (flag->debug == 1) printf("[%d] %s Extracting\n", (*linenum), debugdate().c_str());
                cc = 0;
                do {
                    lineread = strtok(nullptr, " ;");
                    //printf( "\nselect=%d", select_str(lineread));
                    switch (select_str(lineread)) {
                        case 9:  // extract Time from Inverter
                        {
                            auto timestamp = ConvertStreamTo<time_t>(received + 66, 4);
                            fmt::print("Date power = {:%Y-%m-%d %H:%M:%S}\n", *std::localtime(&timestamp));
                            //currentpower = (received[72] * 256) + received[71];
                            //printf("Current power = %i Watt\n",currentpower);
                            break;
                        }
                        case 5:  // extract current power $POW
                            if ((data = ReadStream(conf, flag, &readRecord, bt_sock, received, &rr, data, &datalen, last_sent, cc, &terminated, &togo)) != nullptr) {
                                //printf( "\ndata=%02x:%02x:%02x:%02x:%02x:%02x\n", data[0], (data+1)[0], (data+2)[0], (data+3)[0], (data+4)[0], (data+5)[0] );
                                if ((data + 3)[0] == 0x08)
                                    gap = 40;
                                if ((data + 3)[0] == 0x10)
                                    gap = 40;
                                if ((data + 3)[0] == 0x40)
                                    gap = 28;
                                if ((data + 3)[0] == 0x00)
                                    gap = 28;
                                for (int i = 0; i < datalen; i += gap) {
                                    auto timestamp = ConvertStreamTo<time_t>(data + i + 4, 4);
                                    currentpower_total = ConvertStreamTo<float>(data + i + 8, 3);
                                    return_key = -1;
                                    for (std::size_t j = 0; j < conf->num_return_keys; j++) {
                                        if (((data + i + 1)[0] == conf->returnkeylist[j].key1) && ((data + i + 2)[0] == conf->returnkeylist[j].key2)) {
                                            return_key = j;
                                            break;
                                        }
                                    }
                                    if (return_key >= 0) {
                                        fmt::print("{:%Y-%m-%d %H:%M:%S} {:<20} = {:.0f} {:<20}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                        inverter_serial = (unit[0]->Serial[3] << 24) + (unit[0]->Serial[2] << 16) + (unit[0]->Serial[1] << 8) + unit[0]->Serial[0];
                                    } else if ((data + 0)[0] > 0) {
                                        fmt::print("{:%Y-%m-%d %H:%M:%S} NO DATA for {:02x} {:02x} = {:.0f} NO UNITS\n", *std::localtime(&timestamp), (data + i + 1)[0], (data + i + 1)[1], currentpower_total);
                                    }
                                }
                                free(data);
                            }
                            //An Error has occurred
                            break;

                        case 6:  // extract total energy collected today

                            gtotal = (received[69] * 65536) + (received[68] * 256) + received[67];
                            gtotal = gtotal / 1000;
                            printf("G total so far = %.2f Kwh\n", gtotal);
                            dtotal = (received[84] * 256) + received[83];
                            dtotal = dtotal / 1000;
                            printf("E total today = %.2f Kwh\n", dtotal);
                            break;

                        case 7:  // extract 2nd address
                            for (std::size_t i = 0; i < 6; i++) {
                                conf->MyBTAddress[i] = received[26 + i];
                            }
                            if (flag->debug == 1)
                                printf("\nMyBTAddress = %02x:%02x:%02x:%02x:%02x:%02x \n", conf->MyBTAddress[5], conf->MyBTAddress[4], conf->MyBTAddress[3], conf->MyBTAddress[2], conf->MyBTAddress[1], conf->MyBTAddress[0]);
                            break;

                        case 12:  // extract time strings $TIMESTRING
                            if (flag->debug == 1) printf("received[60]=0x%0X - Expected 0x6D\n", received[60]);
                            if (flag->debug == 1) printf("received[60]=0x%0X - Expected 0x6D\n", received[60]);
                            if ((received[60] == 0x6d) && (received[61] == 0x23)) {
                                memcpy(timestr, received + 63, 24);
                                if (flag->debug == 1) printf("extracting timestring\n");
                                memcpy(timeset, received + 79, 4);
                                auto timestamp = ConvertStreamTo<time_t>(received + 63, 4);
                                /* Allow delay for inverter to be slow */
                                if (reporttime > timestamp) {
                                    if (flag->debug == 1)
                                        printf("delay=%d\n", (int)(reporttime - timestamp));
                                    //sleep( reporttime - idate );
                                    sleep(5);  //was sleeping for > 1min excessive
                                }
                            } else {
                                if (received[61] == 0x7e) {
                                    printf("$TIMESTRING extraction failed. Check password!!!\n");
                                } else {
                                    memcpy(timestr, received + 63, 24);
                                    if (flag->debug == 1) printf("bad extracting timestring\n");
                                }
                                already_read = 0;
                                strcpy(lineread, "");
                                failedbluetooth++;
                                if (failedbluetooth > 60)
                                    exit(-1);
                                //exit(-1);
                            }

                            break;

                        case 17:  // Test data
                            if ((data = ReadStream(conf, flag, &readRecord, bt_sock, received, &rr, data, &datalen, last_sent, cc, &terminated, &togo)) != nullptr) {
                                printf("\n");

                                free(data);
                                break;
                            } else
                                //An Error has occurred
                                break;

                        case 18:  // $ARCHIVEDATA1
                        {
                            finished = 0;
                            ptotal = 0;
                            time_t timestamp = 0;
                            time_t timestamp_prev = 0;
                            printf("\n");
                            while (finished != 1) {
                                if ((data = ReadStream(conf, flag, &readRecord, bt_sock, received, &rr, data, &datalen, last_sent, cc, &terminated, &togo)) != nullptr) {
                                    std::size_t j = 0;
                                    for (int i = 0; i < datalen; i++) {
                                        datarecord[j] = data[i];
                                        j++;
                                        if (j > 11) {
                                            if (timestamp > 0)
                                                timestamp_prev = timestamp;
                                            else
                                                timestamp_prev = 0;
                                            timestamp = ConvertStreamTo<time_t>(datarecord, 4);
                                            if (timestamp_prev == 0)
                                                timestamp_prev = timestamp - 300;

                                            gtotal = ConvertStreamTo<float>(datarecord + 4, 8);
                                            if (archdatalist.empty())
                                                ptotal = gtotal;

                                            fmt::print("\n{:%Y-%m-%d %H:%M:%S}  total={:.3f} Kwh current={:.0f} Watts togo={} i={}\n", *std::localtime(&timestamp), gtotal / 1000, (gtotal - ptotal) * 12, togo, i);
                                            if (timestamp != timestamp_prev + 300) {
                                                printf("Date Error! prev=%d current=%d\n", (int)timestamp_prev, (int)timestamp);
                                                error = 1;
                                                // break;
                                            }

                                            auto &element = archdatalist.emplace_back();
                                            element.date = timestamp;
                                            strcpy(element.inverter, unit[0]->Inverter);
                                            inverter_serial = (unit[0]->Serial[0] << 24) + (unit[0]->Serial[1] << 16) + (unit[0]->Serial[2] << 8) + unit[0]->Serial[3];
                                            element.serial = inverter_serial;
                                            element.accum_value = gtotal / 1000;
                                            element.current_value = (gtotal - ptotal) * 12;
                                            ptotal = gtotal;
                                            j = 0;  //get ready for another record
                                        }
                                    }
                                    if (togo == 0)
                                        finished = 1;
                                    else if (read_bluetooth(conf, flag, &readRecord, bt_sock, &rr, received, cc, last_sent, &terminated) != 0) {
                                        strcpy(lineread, "");
                                        sleep(10);
                                        failedbluetooth++;
                                        if (failedbluetooth > 3)
                                            exit(-1);
                                    }
                                } else
                                    //An Error has occurred
                                    break;
                            }
                            free(data);
                            printf("\n");

                            break;
                        }
                        case 20:  // SIGNAL signal strength

                            strength = (received[22] * 100.0) / 0xff;
                            if (flag->verbose == 1) {
                                printf("bluetooth signal = %.0f%%\n", strength);
                            }
                            break;
                        case 21:  // extract $SUSID
                            unit[0]->SUSyID[0] = received[24];
                            unit[0]->SUSyID[1] = received[25];
                            if (flag->debug == 1) printf("extracting SUSyID=%02x:%02x\n", unit[0]->SUSyID[0], unit[0]->SUSyID[1]);
                            break;
                        case 22:  // extract time strings $INVCODE
                            conf->NetID = received[22];
                            if (flag->debug == 1) printf("extracting invcode=%02x\n", conf->NetID);

                            break;
                        case 24:  // Inverter data $INVERTERDATA

                            if ((data = ReadStream(conf, flag, &readRecord, bt_sock, received, &rr, data, &datalen, last_sent, cc, &terminated, &togo)) != nullptr) {
                                if (flag->debug == 1) printf("data=%02x\n", (data + 3)[0]);
                                if ((data + 3)[0] == 0x08)
                                    gap = 40;
                                if ((data + 3)[0] == 0x10)
                                    gap = 40;
                                if ((data + 3)[0] == 0x40)
                                    gap = 28;
                                if ((data + 3)[0] == 0x00)
                                    gap = 28;
                                for (int i = 0; i < datalen; i += gap) {
                                    auto timestamp = ConvertStreamTo<time_t>(data + i + 4, 4);
                                    currentpower_total = ConvertStreamTo<float>(data + i + 8, 3);
                                    return_key = -1;
                                    for (std::size_t j = 0; j < conf->num_return_keys; j++) {
                                        if (((data + i + 1)[0] == conf->returnkeylist[j].key1) && ((data + i + 2)[0] == conf->returnkeylist[j].key2)) {
                                            return_key = j;
                                            break;
                                        }
                                    }
                                    if (return_key >= 0) {
                                        if (i == 0)
                                            fmt::print("{:%Y-%m-%d %H:%M:%S} {:s}\n", *std::localtime(&timestamp), (data + i + 8));

                                        fmt::print("{:%Y-%m-%d %H:%M:%S} {:>20s} = {:.0f} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                    } else if (data[0] > 0)
                                        fmt::print("{:%Y-%m-%d %H:%M:%S} NO DATA for {:02x} {:02x} = {:.0f} NO UNITS \n", *std::localtime(&timestamp), (data + i + 1)[0], (data + i + 1)[0], currentpower_total);
                                }
                                free(data);
                            }
                            break;
                        case 28:  // extract data $DATA
                            if ((data = ReadStream(conf, flag, &readRecord, bt_sock, received, &rr, data, &datalen, last_sent, cc, &terminated, &togo)) != nullptr) {
                                gap = 0;
                                return_key = -1;
                                for (std::size_t j = 0; j < conf->num_return_keys; j++) {
                                    if (((data + 1)[0] == conf->returnkeylist[j].key1) && ((data + 2)[0] == conf->returnkeylist[j].key2)) {
                                        return_key = j;
                                        break;
                                    }
                                }
                                if (return_key >= 0) {
                                    gap = conf->returnkeylist[return_key].recordgap;
                                    datalength = conf->returnkeylist[return_key].datalength;
                                } else if (datalen > 0)
                                    printf("\nFailed to find key %02x:%02x", (data + 1)[0], (data + 2)[0]);

                                for (int i = 0; i < datalen; i += gap) {
                                    auto timestamp = ConvertStreamTo<time_t>(data + i + 4, 4);
                                    return_key = -1;
                                    for (std::size_t j = 0; j < conf->num_return_keys; j++) {
                                        if (((data + i + 1)[0] == conf->returnkeylist[j].key1) && ((data + i + 2)[0] == conf->returnkeylist[j].key2)) {
                                            return_key = j;
                                            break;
                                        }
                                    }
                                    if (return_key >= 0) {
                                        switch (conf->returnkeylist[return_key].decimal) {
                                            case 0:
                                                currentpower_total = ConvertStreamTo<float>(data + i + 8, datalength);
                                                if (currentpower_total == 0)
                                                    persistent = 1;
                                                else
                                                    persistent = conf->returnkeylist[return_key].persistent;
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>20s} = {:.0f} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%.0f", timestamp, conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, -1, (char *)nullptr, conf->returnkeylist[return_key].units, persistent, livedatalist);
                                                break;
                                            case 1:
                                                currentpower_total = ConvertStreamTo<float>(data + i + 8, datalength);
                                                if (currentpower_total == 0)
                                                    persistent = 1;
                                                else
                                                    persistent = conf->returnkeylist[return_key].persistent;
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>30s} = {:.1f} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%.1f", timestamp, conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, -1, (char *)nullptr, conf->returnkeylist[return_key].units, persistent, livedatalist);
                                                break;
                                            case 2:
                                                currentpower_total = ConvertStreamTo<float>(data + i + 8, datalength);
                                                if (currentpower_total == 0)
                                                    persistent = 1;
                                                else
                                                    persistent = conf->returnkeylist[return_key].persistent;
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>30s} = {:.2f} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%.2f", timestamp, conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, -1, (char *)nullptr, conf->returnkeylist[return_key].units, persistent, livedatalist);
                                                break;
                                            case 3:
                                                currentpower_total = ConvertStreamTo<float>(data + i + 8, datalength);
                                                if (currentpower_total == 0)
                                                    persistent = 1;
                                                else
                                                    persistent = conf->returnkeylist[return_key].persistent;
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>30s} = {:.3f} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%.3f", timestamp, conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, -1, (char *)nullptr, conf->returnkeylist[return_key].units, persistent, livedatalist);
                                                break;
                                            case 4:
                                                currentpower_total = ConvertStreamTo<float>(data + i + 8, datalength);
                                                if (currentpower_total == 0)
                                                    persistent = 1;
                                                else
                                                    persistent = conf->returnkeylist[return_key].persistent;
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>30s} = {:.4f} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%.4f", timestamp, conf->returnkeylist[return_key].description, currentpower_total / conf->returnkeylist[return_key].divisor, -1, (char *)nullptr, conf->returnkeylist[return_key].units, persistent, livedatalist);
                                                break;
                                            case 97: {
                                                fmt::print("                    {:>30s} = {:%Y-%m-%d %H:%M:%S}\n", conf->returnkeylist[return_key].description, *std::localtime(&timestamp));
                                                auto vbuf = fmt::format("{:%Y-%m-%d %H:%M:%S}", *std::localtime(&timestamp));
                                                UpdateLiveList(flag, unit[0], "%s", timestamp, conf->returnkeylist[return_key].description, -1.0, -1, vbuf.c_str(), conf->returnkeylist[return_key].units, conf->returnkeylist[return_key].persistent, livedatalist);

                                                break;
                                            }
                                            case 98:

                                                index = ConvertStreamTo<int>(data + i + 8, 2);
                                                datastring = return_xml_data(index);
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>30s} = {:s} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, datastring, conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%s", timestamp, conf->returnkeylist[return_key].description, -1.0, -1, datastring, conf->returnkeylist[return_key].units, conf->returnkeylist[return_key].persistent, livedatalist);
                                                if ((data + i + 1)[0] == 0x20 && (data + i + 2)[0] == 0x82) {
                                                    strcpy(unit[0]->Inverter, datastring);
                                                }
                                                free(datastring);
                                                break;

                                            case 99: {
                                                auto data_string = ConvertStreamTo<std::string>(data + i + 8, datalength);
                                                fmt::print("{:%Y-%m-%d %H:%M:%S} {:>30s} = {:s} {:>20s}\n", *std::localtime(&timestamp), conf->returnkeylist[return_key].description, data_string.c_str(), conf->returnkeylist[return_key].units);
                                                UpdateLiveList(flag, unit[0], "%s", timestamp, conf->returnkeylist[return_key].description, -1.0, -1, data_string.c_str(), conf->returnkeylist[return_key].units, conf->returnkeylist[return_key].persistent, livedatalist);
                                                break;
                                            }
                                        }
                                        //       live_mysql( &conf, year, month, day, hour, minute, second, conf.Inverter, inverter_serial, returnkeylist[return_key].description, currentpower_total/returnkeylist[return_key].divisor, returnkeylist[return_key].units, debug );
                                    } else {
                                        if (data[0] > 0)
                                            fmt::print("{:%Y-%m-%d %H:%M:%S} NO DATA for {:02x} {:02x} = {:.0f} NO UNITS \n", *std::localtime(&timestamp), (data + i + 1)[0], (data + i + 1)[0], currentpower_total);
                                        break;
                                    }
                                }
                                free(data);
                                break;
                            } else {
                                //An Error has occurred
                                break;
                            }
                        case 31:  // LOGIN Data
                            auto date = ConvertStreamTo<time_t>(received + 59, 4);
                            if (flag->debug == 1) fmt::print("Date power = {:%Y-%m-%d %H:%M:%S}\n", *std::localtime(&date));
                            if (flag->debug == 1) printf("extracting SUSyID=%02x:%02x\n", received[33], received[34]);
                            unit[0]->Serial[3] = received[35];
                            unit[0]->Serial[2] = received[36];
                            unit[0]->Serial[1] = received[37];
                            unit[0]->Serial[0] = received[38];
                            if (flag->verbose == 1) printf("serial=%02x:%02x:%02x:%02x\n", unit[0]->Serial[3] & 0xff, unit[0]->Serial[2] & 0xff, unit[0]->Serial[1] & 0xff, unit[0]->Serial[0] & 0xff);
                            //This is where we poll for other inverters
                            inverter_serial = (unit[0]->Serial[0] << 24) + (unit[0]->Serial[1] << 16) + (unit[0]->Serial[2] << 8) + unit[0]->Serial[3];
                            sprintf(unit[0]->SerialStr, "%llu", inverter_serial);
                            //This is where we poll for other inverters
                            unit[0]->SUSyID[0] = received[33];
                            unit[0]->SUSyID[1] = received[34];
                            //This is where we poll for other inverters
                            break;
                    }
                }

                while (strcmp(lineread, "$END") != 0);
            }
        }
    }
    return error;
}
/*
 * Run a command on an inverter
 *
 */
void InverterCommand(const char *command, ConfType *conf, FlagType *flag, UnitType **unit, int bt_sock, FILE *fp, ArchDataList &archdatalist, LiveDataList &livedatalist)
{
    if (fseek(fp, 0L, 0) < 0)
        printf("\nError");

    printf("================\n");
    printf("Command: %s\n", command);
    printf("================\n");

    if (auto line_num = GetLine(command, fp); line_num > 0) {
        if (ProcessCommand(conf, flag, unit, bt_sock, fp, &line_num, archdatalist, livedatalist) < 0) {
            printf("\nError need to do something");
            getchar();
        }
    } else {
        printf("\nCommand %s not found in '.in' file", command);
    }
}

/*
 * Get Line number of the command required
 * return line number on success 0 on failure
 */
int GetLine(const char *command, FILE *fp)
{
    char *line = nullptr;
    std::size_t len = 0;
    int line_num = 0;
    int found_line = 0;

    while (getline(&line, &len, fp) != -1) {  //read line from sma.in
        line_num++;
        auto *splitted_line = strtok(line, " ;");  // cuts ":command $END" to ":command"
        if (splitted_line[0] == ':') {             // See if line is command we are looking for
            if (strcmp(splitted_line + 1, command) == 0) {
                found_line = line_num;
                break;
            }
        }
    }

    free(line);

    return found_line;
}
