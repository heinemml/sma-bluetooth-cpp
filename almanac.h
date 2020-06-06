//
// Created by Michael Nosthoff on 01.06.20.
//

#ifndef SMA_BLUETOOTH_ALMANAC_H
#define SMA_BLUETOOTH_ALMANAC_H

#include "sma_struct.h"

char *sunrise(ConfType *conf, int debug);
char *sunset(ConfType *conf);
int todays_almanac(ConfType *conf, int debug);
void update_almanac(ConfType *conf, char *sunrise, char *sunset, int debug);

#endif  //SMA_BLUETOOTH_ALMANAC_H
