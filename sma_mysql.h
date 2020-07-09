#include <mysql/mysql.h>

#include <string>

#include "sma_struct.h"

struct MySQLResult {
    explicit MySQLResult(MYSQL_RES *res) : res(res) {}
    ~MySQLResult()
    {
        if (res)
            mysql_free_result(res);
    }
    MYSQL_RES *res;
};

class MySQLConnection
{
public:
    MySQLConnection(const char *server, const char *user, const char *password, const char *database);
    ~MySQLConnection();
    MySQLResult ExecuteQuery(const char *query, bool debug);
    MySQLResult ExecuteQuery(const std::string &query, bool debug);

private:
    MYSQL *m_conn;
};

int install_mysql_tables(ConfType *, FlagType *, const char *);
void update_mysql_tables(ConfType *, FlagType *);
int check_schema(ConfType *, FlagType *, const char *);
void live_mysql(ConfType, bool debug, LiveDataType *, int);
