#include "sma_mysql.h"

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

MySQLConnection::MySQLConnection(const char *server, const char *user, const char *password, const char *database)
{
    m_conn = mysql_init(nullptr);
    /* Connect to database */
    if (!mysql_real_connect(m_conn, server,
                            user, password, database, 0, nullptr, 0)) {
        fmt::print(stderr, "{}\n", mysql_error(m_conn));
        exit(0);
    }
}

MySQLConnection::~MySQLConnection()
{
    mysql_close(m_conn);
}

MySQLResult MySQLConnection::ExecuteQuery(const char *query, bool debug)
{
    if (debug)
        fmt::print("{}\n", query);

    if (mysql_real_query(m_conn, query, strlen(query))) {
        fmt::print(stderr, "{}\n", mysql_error(m_conn));
    }

    return MySQLResult{mysql_store_result(m_conn)};
}

MySQLResult MySQLConnection::ExecuteQuery(const std::string &query, bool debug)
{
    if (debug)
        fmt::print("{}\n", query);

    if (mysql_real_query(m_conn, query.c_str(), query.size())) {
        fmt::print(stderr, "{}\n", mysql_error(m_conn));
    }

    return MySQLResult{mysql_store_result(m_conn)};
}

int install_mysql_tables(ConfType *conf, FlagType *flag, const char *SCHEMA)
/*  Do initial mysql table creationsa */
{
    auto found = false;
    MYSQL_ROW row;

    auto mysql_connection = MySQLConnection(conf->MySqlHost, conf->MySqlUser, conf->MySqlPwd, conf->MySqlDatabase);
    //Get Start of day value
    auto result = mysql_connection.ExecuteQuery("SHOW DATABASES", flag->debug);
    while ((row = mysql_fetch_row(result.res)))  //if there is a result, update the row
    {
        if (strcmp(row[0], conf->MySqlDatabase) == 0) {
            found = true;
            printf("Database exists - exiting");
        }
    }

    if (found) {

        {
            const auto query = fmt::format("CREATE DATABASE IF NOT EXISTS {}", conf->MySqlDatabase);
            mysql_connection.ExecuteQuery(query, flag->debug);
        }

        {
            const auto query = fmt::format("USE  {}", conf->MySqlDatabase);
            mysql_connection.ExecuteQuery(query, flag->debug);
        }

        {
            const auto query =
                "CREATE TABLE `Almanac` ( `id` bigint(20) NOT NULL \
                  AUTO_INCREMENT, \
                  `date` date NOT NULL,\
                  `sunrise` datetime DEFAULT NULL,\
                  `sunset` datetime DEFAULT NULL,\
                  `CHANGETIME` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, \
                   PRIMARY KEY (`id`),\
                   UNIQUE KEY `date` (`date`)\
                   ) ENGINE=MyISAM";

            mysql_connection.ExecuteQuery(query, flag->debug);
        }

        {
            const auto query =
                "CREATE TABLE `DayData` ( \
           `DateTime` datetime NOT NULL, \
           `Inverter` varchar(30) NOT NULL, \
           `Serial` varchar(40) NOT NULL, \
           `CurrentPower` int(11) DEFAULT NULL, \
           `ETotalToday` DECIMAL(10,3) DEFAULT NULL, \
           `Voltage` DECIMAL(10,3) DEFAULT NULL, \
           `PVOutput` datetime DEFAULT NULL, \
           `CHANGETIME` timestamp NOT NULL DEFAULT '0000-00-00 00:00:00' ON UPDATE CURRENT_TIMESTAMP, \
           PRIMARY KEY (`DateTime`,`Inverter`,`Serial`) \
           ) ENGINE=MyISAM";

            mysql_connection.ExecuteQuery(query, flag->debug);
        }

        {
            const auto query =
                "CREATE TABLE `LiveData` ( \
           `id` bigint(20) NOT NULL  AUTO_INCREMENT, \
           `DateTime` datetime NOT NULL, \
           `Inverter` varchar(30) NOT NULL, \
           `Serial` varchar(40) NOT NULL, \
           `Description` varchar(30) NOT NULL, \
           `Value` varchar(30) NOT NULL, \
           `Units` varchar(20) DEFAULT NULL, \
           `CHANGETIME` timestamp NOT NULL DEFAULT '0000-00-00 00:00:00' ON UPDATE CURRENT_TIMESTAMP, \
           PRIMARY KEY (`id`), \
           UNIQUE KEY 'DateTime'(`DateTime`,`Inverter`,`Serial`,`Description`) \
           ) ENGINE=MyISAM";
            mysql_connection.ExecuteQuery(query, flag->debug);
        }

        {
            const auto query =
                "CREATE TABLE `settings` ( \
           `value` varchar(128) NOT NULL, \
           `data` varchar(500) NOT NULL, \
           PRIMARY KEY (`value`) \
           ) ENGINE=MyISAM";

            mysql_connection.ExecuteQuery(query, flag->debug);
        }

        {
            const auto query = fmt::format(R"(INSERT INTO `settings` SET `value` = 'schema', `data` = '%s' )", SCHEMA);

            mysql_connection.ExecuteQuery(query, flag->debug);
        }
    }

    return found;
}

int get_schema_version(MySQLConnection &conn, unsigned int debug)
{
    MYSQL_ROW row;
    if (auto result = conn.ExecuteQuery("SELECT data FROM settings WHERE value=\'schema\'", debug);
        (row = mysql_fetch_row(result.res)))  //if there is a result, update the row
    {
        return atoi(row[0]);
    }

    return 0;
}

void update_mysql_tables(ConfType *conf, FlagType *flag)
/*  Do mysql table schema updates */
{
    auto mysql_connection = MySQLConnection(conf->MySqlHost, conf->MySqlUser, conf->MySqlPwd, conf->MySqlDatabase);

    /*Check current schema value*/
    int schema_value = get_schema_version(mysql_connection, flag->debug);

    if (schema_value == 1) {  //Upgrade from 1 to 2
        mysql_connection.ExecuteQuery("ALTER TABLE `DayData` CHANGE `ETotalToday` `ETotalToday` DECIMAL(10,3) NULL DEFAULT NULL", flag->debug);
        mysql_connection.ExecuteQuery("UPDATE `settings` SET `value` = \'schema\', `data` = 2", flag->debug);

        schema_value = get_schema_version(mysql_connection, flag->debug);
    }

    if (schema_value == 2) {  //Upgrade from 2 to 3
        mysql_connection.ExecuteQuery(
            "CREATE TABLE `LiveData` (\
            `id` BIGINT NOT NULL AUTO_INCREMENT , \
           	`DateTime` datetime NOT NULL, \
           	`Inverter` varchar(10) NOT NULL, \
           	`Serial` varchar(40) NOT NULL, \
            `Description` char(20) NOT NULL , \
            `Value` INT NOT NULL , \
            `Units` char(20) NOT NULL , \
           	`CHANGETIME` timestamp NOT NULL DEFAULT '0000-00-00 00:00:00' ON UPDATE CURRENT_TIMESTAMP, \
           	UNIQUE KEY (`DateTime`,`Inverter`,`Serial`,`Description`), \
		    PRIMARY KEY ( `id` ) \
		    ) ENGINE = MYISAM",
            flag->debug);
        mysql_connection.ExecuteQuery("UPDATE `settings` SET `value` = \'schema\', `data` = 3", flag->debug);

        schema_value = get_schema_version(mysql_connection, flag->debug);
    }

    if (schema_value == 3) {  //Upgrade from 3 to 4

        mysql_connection.ExecuteQuery(
            "ALTER TABLE `DayData` CHANGE `Inverter` `Inverter` varchar(30) NOT NULL,"
            "CHANGE `Serial` `Serial` varchar(40) NOT NULL",
            flag->debug);

        mysql_connection.ExecuteQuery(
            "ALTER TABLE `LiveData` CHANGE `Inverter` `Inverter` varchar(30) NOT NULL,"
            " CHANGE `Serial` `Serial` varchar(40) NOT NULL, CHANGE `Description` `Description` varchar(30) NOT NULL,"
            " CHANGE `Value` `Value` varchar(30), CHANGE `Units` `Units` varchar(20) NULL DEFAULT NULL ",
            flag->debug);

        mysql_connection.ExecuteQuery("UPDATE `settings` SET `value` = \'schema\', `data` = 4", flag->debug);
    }
}

int check_schema(ConfType *conf, FlagType *flag, const char *SCHEMA)
/*  Check if using the correct database schema */
{
    int found = 0;
    MYSQL_ROW row;

    auto mysql_connection = MySQLConnection(conf->MySqlHost, conf->MySqlUser, conf->MySqlPwd, conf->MySqlDatabase);
    //Get Start of day value

    if (auto result = mysql_connection.ExecuteQuery("SELECT data FROM settings WHERE value=\'schema\'",
                                                    flag->debug);
        (row = mysql_fetch_row(result.res)))  //if there is a result, update the row
    {
        if (strcmp(row[0], SCHEMA) == 0)
            found = 1;
    }

    if (found != 1) {
        fmt::print("Please Update database schema use --UPDATE\n");
    }
    return found;
}

void live_mysql(ConfType conf, bool debug, const LiveDataList &livedatalist)
/* Live inverter values mysql update */
{
    struct tm *loctime;
    int day, month, year, hour, minute, second;

    auto mysql_connection = MySQLConnection(conf.MySqlHost, conf.MySqlUser, conf.MySqlPwd, conf.MySqlDatabase);
    for (const auto &data : livedatalist) {

        loctime = localtime(&data.date);
        day = loctime->tm_mday;
        month = loctime->tm_mon + 1;
        year = loctime->tm_year + 1900;
        hour = loctime->tm_hour;
        minute = loctime->tm_min;
        second = loctime->tm_sec;

        auto live_data = true;
        if (data.Persistent == 1) {
            const auto query = fmt::format(R"(SELECT IF (Value = "{}",NULL,Value) FROM LiveData where Inverter="{}" and Serial={} and Description="{}" ORDER BY DateTime DESC LIMIT 1)",
                                           data.Value, data.inverter, data.serial, data.Description);

            MYSQL_ROW row;
            if (auto result = mysql_connection.ExecuteQuery(query, debug); (row = mysql_fetch_row(result.res)))  //if there is a result, update the row
            {
                if (row[0] == nullptr) {
                    live_data = false;
                }
            }
        }

        if (live_data) {
            const auto datetime = fmt::format("{}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", year, month, day, hour, minute, second);
            const auto query = fmt::format(R"(INSERT INTO LiveData ( DateTime, Inverter, Serial, Description, Value, Units ) VALUES ( '{}', '{}', {}, '{}', '{}', '{}'  ) ON DUPLICATE KEY UPDATE DateTime=Datetime, Inverter=VALUES(Inverter), Serial=VALUES(Serial), Description=VALUES(Description), Description=VALUES(Description), Value=VALUES(Value), Units=VALUES(Units))",
                                           datetime, data.inverter, data.serial, data.Description, data.Value, data.Units);
            mysql_connection.ExecuteQuery(query, debug);
        }
    }
}
