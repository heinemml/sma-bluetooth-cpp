#ifndef SMA_BLUETOOTH_SMATOOL_H
#define SMA_BLUETOOTH_SMATOOL_H

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

#endif  //SMA_BLUETOOTH_SMATOOL_H
