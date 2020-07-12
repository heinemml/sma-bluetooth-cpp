//
// Created by Michael Nosthoff on 01.06.20.
//

#ifndef SMA_BLUETOOTH_SB_COMMANDS_H
#define SMA_BLUETOOTH_SB_COMMANDS_H

#include <cstdio>

#include "sma_struct.h"

int ConnectSocket(ConfType *conf);
int GetLine(const char *command, FILE *fp);

void InverterCommand(const char *command, ConfType *conf, FlagType *flag, UnitType **unit, int *s, FILE *fp, ArchDataList &archdatalist, LiveDataList &livedatalist);

#endif  //SMA_BLUETOOTH_SB_COMMANDS_H
