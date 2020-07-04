#include <mysql/mysql.h>

#include "sma_struct.h"

extern MYSQL *conn;

struct MySQLResult {
    MySQLResult(MYSQL_RES *res) : res(res) {}
    ~MySQLResult()
    {
        if (res)
            mysql_free_result(res);
    }
    MYSQL_RES *res;
};

void OpenMySqlDatabase(const char *, const char *, const char *, const char *);
void CloseMySqlDatabase();
MySQLResult ExecuteQuery(const char *query, bool debug = false);
int install_mysql_tables(ConfType *, FlagType *, const char *);
void update_mysql_tables(ConfType *, FlagType *);
int check_schema(ConfType *, FlagType *, const char *);
void live_mysql(ConfType, bool debug, LiveDataType *, int);
