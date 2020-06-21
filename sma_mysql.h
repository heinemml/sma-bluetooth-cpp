#include <mysql/mysql.h>

#include "sma_struct.h"

extern MYSQL *conn;
extern MYSQL_RES *res;
extern MYSQL_RES *res1;
extern MYSQL_RES *res2;

void OpenMySqlDatabase(const char *, const char *, const char *, const char *);
void CloseMySqlDatabase();
int DoQuery(const char *);
int DoQuery1(const char *);
int DoQuery2(const char *);
int install_mysql_tables(ConfType *, FlagType *, const char *);
void update_mysql_tables(ConfType *, FlagType *);
int check_schema(ConfType *, FlagType *, const char *);
void live_mysql(ConfType, FlagType, LiveDataType *, int);
