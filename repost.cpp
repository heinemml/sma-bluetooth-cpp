/* tool to read power production data for SMA solar power convertors 
   Copyright Wim Hofman 2010 
   Copyright Stephen Collier 2010,2011 

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* repost is a utility to check pvoutput data and repost any differences */

#include <curl/curl.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "sma_mysql.h"
#include "sma_struct.h"

std::size_t write_data(void *ptr, std::size_t size, std::size_t num_blocks, void *stream)
{
    std::size_t written;

    written = fwrite(ptr, size, num_blocks, reinterpret_cast<FILE *>(stream));
    return written;
}

void sma_repost(ConfType *conf, FlagType *flag)
{
    FILE *fp;
    CURL *curl;
    CURLcode curl_result;
    char buf[1024], buf1[400];
    char SQLQUERY[1000];
    char compurl[400];
    int update_data;
    MYSQL_ROW row;

    float dtotal;
    float power;

    /* Connect to database */
    auto mysql_connection = MySQLConnection(conf->MySqlHost, conf->MySqlUser, conf->MySqlPwd, conf->MySqlDatabase);
    //Get Start of day value
    printf(R"(SELECT DATE_FORMAT( dt1.DateTime, "%%Y%%m%%d" ), round((dt1.ETotalToday*1000-dt2.ETotalToday*1000),0) FROM DayData as dt1 join DayData as dt2 on dt2.DateTime = DATE_SUB( dt1.DateTime, interval 1 day ) WHERE dt1.DateTime LIKE "%%-%%-%% 23:55:00" ' ORDER BY dt1.DateTime DESC)");
    sprintf(SQLQUERY, R"(SELECT DATE_FORMAT( dt1.DateTime, "%%Y%%m%%d" ), round((dt1.ETotalToday*1000-dt2.ETotalToday*1000),0) FROM DayData as dt1 join DayData as dt2 on dt2.DateTime = DATE_SUB( dt1.DateTime, interval 1 day ) WHERE dt1.DateTime LIKE "%%-%%-%% 23:55:00" ORDER BY dt1.DateTime DESC)");
    auto result = mysql_connection.ExecuteQuery(SQLQUERY, flag->debug);
    while ((row = mysql_fetch_row(result.res)))  //if there is a result, update the row
    {
    startforwait:
        fp = fopen("/tmp/curl_output", "w+");
        update_data = 0;
        dtotal = atof(row[1]);
        sleep(2);  //pvoutput limits 1 second output
        sprintf(compurl, "http://pvoutput.org/service/r1/getstatistic.jsp?df=%s&dt=%s&key=%s&sid=%s", row[0], row[0], conf->PVOutputKey, conf->PVOutputSid);
        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, compurl);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            //curl_easy_setopt(curl, CURLOPT_FAILONERROR, compurl);
            curl_result = curl_easy_perform(curl);
            if (flag->debug == 1) printf("result = %d\n", curl_result);
            rewind(fp);
            fgets(buf, sizeof(buf), fp);
            curl_result = static_cast<CURLcode>(sscanf(buf, "Bad request %s has no outputs between the requested period", buf1));
            printf("return=%d buf1=%s\n", curl_result, buf1);
            if (curl_result > 0) {
                update_data = 1;
                printf("test\n");
            } else {
                printf("buf=%s here 1.\n", buf);
                curl_result = static_cast<CURLcode>(sscanf(buf, "Forbidden 403: Exceeded 60 requests %s", buf1));
                if (curl_result > 0) {
                    printf("Too Many requests in 1hr sleeping for 1hr\n");
                    fclose(fp);
                    sleep(3600);
                    goto startforwait;
                }

                printf("return=%d buf1=%s\n", curl_result, buf1);
                if (sscanf(buf, "%f,%s", &power, buf1) > 0) {
                    printf("Power %f\n", power);
                    if (power != dtotal) {
                        printf("Power %f Produced=%f\n", power, dtotal);
                        update_data = 1;
                    }
                }
            }
            curl_easy_cleanup(curl);
            if (update_data == 1) {
                curl = curl_easy_init();
                if (curl) {
                    sprintf(compurl, "http://pvoutput.org/service/r2/addoutput.jsp?d=%s&g=%f&key=%s&sid=%s", row[0], dtotal, conf->PVOutputKey, conf->PVOutputSid);
                    if (flag->debug == 1) printf("url = %s\n", compurl);
                    curl_easy_setopt(curl, CURLOPT_URL, compurl);
                    curl_easy_setopt(curl, CURLOPT_FAILONERROR, compurl);
                    curl_result = curl_easy_perform(curl);
                    sleep(1);
                    if (flag->debug == 1) printf("result = %d\n", curl_result);
                    curl_easy_cleanup(curl);
                    if (curl_result == 0) {
                        sprintf(SQLQUERY, "UPDATE DayData set PVOutput=NOW() WHERE DateTime=\"%s235500\"  ", row[0]);
                        if (flag->debug == 1) printf("%s\n", SQLQUERY);
                        //DoQuery(SQLQUERY);
                    } else
                        break;
                }
            }
        }
        fclose(fp);
    }
}
