#ifndef SMA_BLUETOOTH_SMATOOL_H
#define SMA_BLUETOOTH_SMATOOL_H

#include <time.h>

unsigned char *ReadStream(ConfType *, FlagType *, ReadRecordType *, int *, unsigned char *, int *, unsigned char *, int *, unsigned char *, int, int *, int *);
char *return_xml_data(ConfType *, int);
long ConvertStreamtoLong(unsigned char *, int, unsigned long *);
float ConvertStreamtoFloat(unsigned char *, int, float *);
char *ConvertStreamtoString(unsigned char *, int);
time_t ConvertStreamtoTime(unsigned char *stream, int length, time_t *value, int *day, int *month, int *year, int *hour, int *minute, int *second);
int ConvertStreamtoInt(unsigned char *stream, int length, int *value);
unsigned char conv(char *);
char *debugdate();
int select_str(char *s);
int empty_read_bluetooth(FlagType *flag, ReadRecordType *readRecord, int *s, int *rr, unsigned char *received, int *terminated);
int read_bluetooth(ConfType *conf, FlagType *flag, ReadRecordType *readRecord, int *s, int *rr, unsigned char *received, int cc, unsigned char *last_sent, int *terminated);
void tryfcs16(FlagType *flag, unsigned char *cp, int len, unsigned char *fl, int *cc);
void add_escapes(unsigned char *cp, int *len);
void fix_length_send(FlagType *flag, unsigned char *cp, int *len);

#endif  //SMA_BLUETOOTH_SMATOOL_H
