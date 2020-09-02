//
// Created by Michael Nosthoff on 01.06.20.
//

#ifndef SMA_BLUETOOTH_SB_COMMANDS_H
#define SMA_BLUETOOTH_SB_COMMANDS_H

#include <cstdio>

#include "bt_connection.h"
#include "sma_struct.h"

struct SessionData {
    ArchDataList &archDataList;
    LiveDataList &liveDataList;
    BTConnection &btConnection;
    ConfType &conf;
    FlagType &flags;
    UnitType **unit{nullptr};
    FILE *fp{nullptr};
};

int GetLine(const char *command, FILE *fp);

void InverterCommand(const char *command, SessionData &session_data);

#endif  //SMA_BLUETOOTH_SB_COMMANDS_H
