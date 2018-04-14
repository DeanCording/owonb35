/*
 *
 *  owonb35 - Owon B35 Bluetooth client for newer meters with the Semic CS7729CN-001 chip
 *  instead of the earlier Fortune Semiconductor FS9922 chip
 *
 *  Copyright (C) 2018  Dean Cording <dean@cording.id.au>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

#include <gattlib.h>

#define VERSION "1.2.0"

int quiet = FALSE;

#define BLE_SCAN_TIMEOUT   4

GMainLoop *loop;

// Measurement UUID
uuid_t g_command_uuid = CREATE_UUID16(0xfff1);
const uuid_t g_measurement_uuid = CREATE_UUID16(0xfff4);


char *address = NULL;

const char BDM[] = "BDM";

#define DATE_CMD    "*DATe"
#define RECORD_CMD  "*RECOrd,"
#define READLEN_CMD "*READlen?"
#define READ_CMD    "*READ1?"

#define MAX_MEASUREMENTS 10000

uint32_t interval = 0;
uint32_t num_measurements = 0;


enum {space, csv, json} format = space;

enum {none, elapsed_sec, actual_sec, elapsed_milli, actual_milli, date} timestamp = none;
int units = 0;

int show_units = TRUE;

int low_battery = FALSE;

unsigned long start_time = 0;

void print_timestamp() {
    struct timeval now;
    char date_now[30];

    if (timestamp == none) return;

    gettimeofday(&now,NULL);

    switch (timestamp) {
        case none:
            break;

        case elapsed_sec:
            if (start_time == 0) {
                printf("0.0");
                start_time = now.tv_sec*1000 + now.tv_usec/1000;
            } else {
                printf("%.1f", (float)((now.tv_sec*1000 + now.tv_usec/1000) - start_time)/1000);
            }
            break;

        case actual_sec:
            printf("%ld.%ld", now.tv_sec, now.tv_usec/100000);
            break;

        case elapsed_milli:
            if (start_time == 0) {
                printf("0");
                start_time = now.tv_sec*1000 + now.tv_usec/1000;
            } else {
                printf("%ld", ((now.tv_sec*1000 + now.tv_usec/1000) - start_time));
            }
            break;

        case actual_milli:
            printf("%ld", now.tv_sec*1000 + now.tv_usec/1000);
            break;

        case date:
            strftime(date_now,30, "%F %H:%M:%S", localtime(&now.tv_sec));
            printf("%s.%ld", date_now, now.tv_usec/100000);
            break;

    }

}

void print_measurement(float measurement, int decimal, int scale) {

    if (decimal > 3) {
        printf("Overload");
    } else {

        if (units && (units != scale)) {

            measurement = measurement * pow(10.0, (scale-units)*3);

            decimal = decimal - (scale-units)*3;

        }

        printf("% .*f", decimal, measurement);

    }

}

void print_units(int scale, int function) {

    if (units) scale = units;

    switch (scale) {
        case 1:
            printf("n");
            break;

        case 2:
            printf("u");
            break;

        case 3:
            printf("m");
            break;

        case 5:
            printf("k");
            break;

        case 6:
            printf("M");
            break;
    }

    switch (function) {

        case 0:
            printf("Vdc");
            break;

        case 1:
            printf("Vac");
            break;

        case 2:
            printf("Adc");
            break;

        case 3:
            printf("Aac");
            break;

        case 4:
            printf("Ohms");
            break;

        case 5:
            printf("F");
            break;

        case 6:
            printf("Hz");
            break;

        case 7:
            printf("%%");
            break;

        case 8:
            printf("°C");
            break;

        case 9:
            printf("°F");
            break;

        case 10:
            printf("V");
            break;

        case 11:
            printf("Ohms");
            break;

        case 12:
            printf("hFE");
            break;

    }

}

void print_type(uint16_t type) {

    if (type & 0x02) printf("Δ ");
    if (type & 0x10) printf("min");
    if (type & 0x20) printf("max");
    if (type & 0x01) printf("hold");

}




void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {
    int i;

    uint16_t* reading;

    int function, scale, decimal;

    float measurement;

    if ((data_length !=6) && (data[1] < 240)) {

        fprintf(stderr, "Unrecognized packet: ");

        for (i = 0; i < data_length; i++) {
            fprintf(stderr, "%02x ", data[i]);
        }

        fprintf(stderr, "\n");

    } else {

        reading = (uint16_t*)data;

        function = (reading[0] >> 6) & 0x0f;
        scale = (reading[0] >> 3) & 0x07;
        decimal = reading[0] & 0x07;

        if (reading[2] < 0x7fff) {
            measurement = (float)reading[2] / pow(10.0, decimal);
        } else {
            measurement = -1 * (float)(reading[2] & 0x7fff) / pow(10.0, decimal);
        }

        if (reading[1] & 0x08) {
            if (!low_battery) {
                fprintf(stderr, "LOW BATTERY\n");
            }

            if (low_battery++ > 17) low_battery = 0;

        } else {
            low_battery = FALSE;
        }

        switch (format) {
            case space:
            case csv:

                if (timestamp) {
                    print_timestamp();
                    printf("%c", (format?',':' '));
                }

                print_measurement(measurement, decimal, scale);

                if (show_units) {
                    printf("%c", (format?',':' '));
                    print_units(scale, function);
                    printf("%c", (format?',':' '));
                    print_type(reading[1]);
                }
                printf("\n");

                break;

            case json:

                printf("{");

                if (timestamp) {
                    printf("\"timestamp\":");
                    if (timestamp == date) printf("\"");
                    print_timestamp();
                    if (timestamp == date) printf("\"");
                    printf(", ");
                }

                printf("\"measurement\":");
                print_measurement(measurement, decimal, scale);

                if (show_units) {
                    printf(", \"units\":\"");
                    print_units(scale, function);
                    printf("\", \"type\":\"");
                    print_type(reading[1]);
                    printf("\"");
                }
                printf(" }\n");

                break;

        }

        fflush(stdout);
    }
}

static void usage(char *argv[]) {
    printf("%s [-s|-S|-t|-T|-d] [-c|-j] [-n|-u|-m|-b|-k|-M] [-x] [-R] [-q] [<device_address>]\n", argv[0]);
    printf("%s -R <seconds per measurement> <number of measurements> [<device_address>]\n", argv[0]);
    printf("%s -h\n", argv[0]);
    printf("%s -V\n", argv[0]);
    printf("\n\tReceives measurement data from Owon B35+/B35T+ digital multimeters using bluetooth.\n\n");
    printf("\t-s\t\t Timestamp measurements in elapsed seconds from first reading\n");
    printf("\t-S\t\t Timestamp measurements in seconds\n");
    printf("\t-t\t\t Timestamp measurements in elapsed milliseconds from first reading\n");
    printf("\t-T\t\t Timestamp measurements in milliseconds\n");
    printf("\t-d\t\t Timestamp measurements with date/time\n");
    printf("\t-c\t\t Comma separated values (CSV) output\n");
    printf("\t-j\t\t JSON output\n");
    printf("\t-n\t\t Scale measurements to nano units\n");
    printf("\t-u\t\t Scale measurements to micro units\n");
    printf("\t-m\t\t Scale measurements to milli units\n");
    printf("\t-b\t\t Scale measurements to base units\n");
    printf("\t-k\t\t Scale measurements to kilo units\n");
    printf("\t-M\t\t Scale measurements to mega units\n");
    printf("\t-x\t\t Output just the measurement without the units or type for use with feedgnuplot\n");
    printf("\t<device_address> Address of Owon multimeter to connect\n");
    printf("\t-q\t\t Quiet - no status output\n");
    printf("\t-h\t\t Display this help\n");
    printf("\t-V\t\t Display version\n");
    printf("\t\t\t  otherwise will connect to first meter found if not specified\n");

}


static void ble_discovered_device(const char* addr, const char* name) {

    if ((name != NULL) && (strcmp(BDM, name) == 0) && (address == NULL)) {

        if (!quiet) fprintf(stderr, "Connecting to %s\n", addr);

        address = malloc(18);
        strcpy(address,addr);
   }

}

void signal_handler(int signal){

    g_main_loop_quit(loop);
}

int main(int argc, char *argv[]) {
    int ret;
    gatt_connection_t* connection;

    int scan = 1;

    int argi;

    const char* adapter_name = NULL;
    void* adapter = NULL;


    if ((argc > 3) && (argv[1][0] == '-') && (argv[1][1] == 'R')) {

        interval = strtoul(argv[2], NULL, 0);
        if (interval < 1) {
            fprintf(stderr, "Measurement interval must be 1 or more seconds.\n");
            return 1;
        }


        num_measurements = strtoul(argv[3], NULL, 0);
        if ((num_measurements < 1) || (num_measurements > MAX_MEASUREMENTS)) {
            fprintf(stderr, "Number of measurements must be between 0 and %d.\n", MAX_MEASUREMENTS);
            return 1;
        }

        if (argc == 5) {
printf("Huh\n");
            address = argv[4];
            scan = FALSE;
        }
    } else {

        for (argi = 1; argi < argc; argi++) {
            if (argv[argi][0] == '-') {
                switch (argv[argi][1]) {
                    case 's':
                        timestamp = elapsed_sec;
                        break;

                    case 'S':
                        timestamp = actual_sec;
                        break;

                    case 't':
                        timestamp = elapsed_milli;
                        break;

                    case 'T':
                        timestamp = actual_milli;
                        break;

                    case 'd':
                        timestamp = date;
                        break;

                    case 'c':
                        format = csv;
                        break;

                    case 'j':
                        format = json;
                        break;

                    case 'n':
                        units = 1;
                        break;

                    case 'u':
                        units = 2;
                        break;

                    case 'm':
                        units = 3;
                        break;

                    case 'b':
                        units = 4;
                        break;

                    case 'k':
                        units = 5;
                        break;

                    case 'M':
                        units = 6;
                        break;

                    case 'x':
                        show_units = FALSE;
                        break;

                    case 'h':
                        usage(argv);
                        return 0;

                    case 'q':
                        quiet = TRUE;
                        break;

                    case 'V':
                        printf("%s version ", argv[0]);
                        printf(VERSION);
                        printf("\n");
                        return 0;

                    default:
                        fprintf(stderr, "Unknown option %s\n\n", argv[argi]);
                        usage(argv);
                        return 1;

                }
            } else {
                address = argv[argi];
                scan = FALSE;
            }
        }
    }

    if (scan) {

        do {
            if (!quiet) fprintf(stderr, "Scanning...\n");

            ret = gattlib_adapter_open(adapter_name, &adapter);
            if (ret) {
                fprintf(stderr, "ERROR: Failed to open adapter.\n");
                return 1;
            }

            ret = gattlib_adapter_scan_enable(adapter, ble_discovered_device, BLE_SCAN_TIMEOUT);
            if (ret) {
                fprintf(stderr, "ERROR: Failed to scan.\n");
                return 1;
            }
            gattlib_adapter_scan_disable(adapter);

            gattlib_adapter_close(adapter);

            if (address == NULL) {
                if (!quiet) fprintf(stderr, "Multimeter device not found.\n");
                sleep(2);
            }

        } while (address == NULL);

    }

    if (address == NULL) {
        usage(argv);
        return 1;
    }

    connection = gattlib_connect(NULL, address, BDADDR_LE_PUBLIC, BT_SEC_LOW, 0, 0);
    if (connection == NULL) {
        fprintf(stderr, "Fail to connect to the multimeter bluetooth device.\n");
        return 1;
    }

    if (interval) {

        char *index;

        uint8_t buffer[16];

        struct tm *date;
        time_t now;

        memset(buffer, 0, sizeof(buffer));

        index = stpcpy((char *)buffer, DATE_CMD);

        now = time(NULL);
        date = localtime(&now);

        index[0] = (uint8_t)(date->tm_year/100);
        index[1] = (uint8_t)(date->tm_year - date->tm_year/100);
        index[2] = (uint8_t)(date->tm_mon + 1);
        index[3] = (uint8_t)(date->tm_mday);
        index[4] = (uint8_t)(date->tm_hour);
        index[5] = (uint8_t)(date->tm_min);
        index[6] = (uint8_t)(date->tm_sec);

		ret = gattlib_write_char_by_uuid(connection, &g_command_uuid, buffer, sizeof(buffer));
        if (ret) {
            fprintf(stderr, "Fail to write date.\n");
            return 1;
        }

        memset(buffer, 0, sizeof(buffer));

        index = stpcpy((char *)buffer, RECORD_CMD);

        ((uint32_t *)index)[0] = interval;
        ((uint32_t *)index)[1] = num_measurements;
printf("num_measurements %u\n", num_measurements);
		ret = gattlib_write_char_by_uuid(connection, &g_command_uuid, buffer, sizeof(buffer));
        if (ret) {
            fprintf(stderr, "Fail to write record command.\n");
            return 1;
        }

        if (!quiet) fprintf(stderr, "Recording started\n");

    } else {

        gattlib_register_notification(connection, notification_handler, NULL);

        ret = gattlib_notification_start(connection, &g_measurement_uuid);
        if (ret) {
            fprintf(stderr, "Fail to start listener.\n");
            return 1;
        }


        loop = g_main_loop_new(NULL, 0);

        signal(SIGINT, signal_handler);

        g_main_loop_run(loop);

        g_main_loop_unref(loop);

    }

    gattlib_disconnect(connection);
    if (!quiet) fprintf(stderr,"Disconnected\n");
    return 0;
}
