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
};

int GetLine(const char *command, FILE *fp);

void InverterCommand(const char *command, ConfType *conf, FlagType *flag, UnitType **unit, FILE *fp, SessionData &session_data);

#endif  //SMA_BLUETOOTH_SB_COMMANDS_H
