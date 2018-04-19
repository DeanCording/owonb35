/*
 *
 *  owonb35 - Owon B35 Bluetooth client for newer meters with the Semic CS7729CN-001 chip
 *  instead of the earlier Fortune Semiconductor FS9922 chip
 *
 *  Copyright (C) 2018  Dean Cording <dean@cording.id.au>
 *
 *  https://github.com/DeanCording/owonb35
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
#include <termios.h>

#include <gattlib.h>

#define VERSION "1.4"

_Bool quiet = FALSE;

#define BLE_SCAN_TIMEOUT   3

GMainLoop *loop;

// BLE GATT UUID
uuid_t g_command_uuid = CREATE_UUID16(0xfff1);
uuid_t g_control_uuid = CREATE_UUID16(0xfff3);
const uuid_t g_measurement_uuid = CREATE_UUID16(0xfff4);

gatt_connection_t* connection;
char *address = NULL;
const char BDM[] = "BDM";

// Offline recording
#define DATE_CMD    "*DATe"
#define RECORD_CMD  "*RECOrd,"
#define READLEN_CMD "*READlen?"
#define READ_CMD    "*READ1?"

#define MAX_MEASUREMENTS 10000

uint32_t interval = 0;
uint32_t num_measurements = 0;

_Bool offline = FALSE;
uint16_t offline_function = 0;
time_t offline_time = 0;
uint32_t offline_interval;


// Interactive controls
_Bool interactive = FALSE;
struct termios orig_termios;

#define SELECT          0x0101
#define AUTO            0x0002
#define RANGE           0x0102
#define LIGHT           0x0003
#define HOLD            0x0103
#define BLUETOOTH_OFF   0x0004
#define RELATIVE        0x0104
#define HZ              0x0105
#define NORMAL          0x0006
#define MIN_MAX         0x0106


// Output options
enum {space, csv, json} format = space;

enum {none, elapsed_sec, actual_sec, elapsed_milli, actual_milli, date} timestamp = none;
int units = 0;

_Bool show_units = TRUE;

int low_battery = FALSE;

unsigned long start_time = 0;

// Watchdog flag
_Bool active = FALSE;

// Outputs the measurement timestamp
void print_timestamp() {

    struct timeval now;
    char date_now[30];

    if (timestamp == none) return;

    if (offline_time) {
        now.tv_sec = offline_time;
        now.tv_usec = 0;
    } else {
        gettimeofday(&now,NULL);
    }

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


// Outputs the measurement value
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

// Outputs the measurement units
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

// Outputs the measurement type
void print_type(uint16_t type) {

    if (type & 0x02) printf("Δ ");
    if (type & 0x10) printf("min");
    if (type & 0x20) printf("max");
    if (type & 0x01) printf("hold");

}

// Outputs the measurement
void display_reading(uint16_t* reading) {

    int function, scale, decimal;

    float measurement;

    // Extract data items from first number
    function = (reading[0] >> 6) & 0x0f;
    scale = (reading[0] >> 3) & 0x07;
    decimal = reading[0] & 0x07;

    // Extract and convert measurement value
    if (reading[2] < 0x7fff) {
        measurement = (float)reading[2] / pow(10.0, decimal);
    } else {
        measurement = -1 * (float)(reading[2] & 0x7fff) / pow(10.0, decimal);
    }


    // Check for low battery condition
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

    // Flush output for realtime displays
    fflush(stdout);
}


// Handler for BLE notification events
void notification_handler(const uuid_t* uuid, const uint8_t* data, size_t data_length, void* user_data) {

    uint16_t reading[3];
    int index;

    // Reset watchdog flag
    active = TRUE;

    if (offline) {
        // Process offline recording dump packet

        if (!offline_function && (data_length < 20)) return;

        if (!offline_function && (data[0] == 0xff)) return;  // skip lead-in

        index = 0;

        if (!offline_function) {
            // Read header

            // Extract recording start timestamp
            struct tm brokentime;

            brokentime.tm_year = data[0] * 100 + data[1];
            brokentime.tm_mon = data[2] - 1;
            brokentime.tm_mday = data[3];
            brokentime.tm_hour = data[4];
            brokentime.tm_min = data[5];
            brokentime.tm_sec = data[6];

            offline_time = mktime(&brokentime);

            // Extract measurement interval
            offline_interval = *((uint32_t *)(data+8));

            // Extract measurement function and units
            offline_function = *((uint16_t *)(data+16));

            index = 18;

        }

        reading[0] = offline_function;
        reading[1] = 0;

        for(;index < 20; index+=2) {

            if (*((uint16_t *)(data+index)) == 0xffff) {
                g_main_loop_quit(loop);
                return;
            }

            reading[2] = *((uint16_t *)(data+index));

            display_reading(reading);

            offline_time += offline_interval;
        }

    } else if ((data_length == 6) && (data[1] >= 0xf0)) {

        // Realtime measurement packet

        display_reading((uint16_t*)data);

    } else {

        fprintf(stderr, "Unrecognized packet: ");

        for (int i = 0; i < data_length; i++) {
            fprintf(stderr, "%02x ", data[i]);
        }

        fprintf(stderr, "\n");

    }


}

static void usage(char *argv[]) {
    printf("%s [-s|-S|-t|-T|-d] [-c|-j] [-n|-u|-m|-b|-k|-M] [-x] [-r] [-q] [-h|-V] [<device_address>]\n", argv[0]);
    printf("\tMeasurement collection\n\n");
    printf("%s -R <seconds per measurement> <number of measurements> [<device_address>]\n", argv[0]);
    printf("\tStart offline measurement recording\n\n");
    printf("\tClient for Owon B35/B35+/B35T+ digital multimeters using bluetooth.\n\n");
    printf("\t-i\t\t Interactive remote control\n");
    printf("\t-s\t\t Timestamp measurements in elapsed seconds from first reading\n");
    printf("\t-S\t\t Timestamp measurements in Unix epoch seconds\n");
    printf("\t-t\t\t Timestamp measurements in elapsed milliseconds from first reading\n");
    printf("\t-T\t\t Timestamp measurements in Javascript epoch milliseconds\n");
    printf("\t-d\t\t Timestamp measurements with ISO date/time\n");
    printf("\t-c\t\t Comma separated values (CSV) output\n");
    printf("\t-j\t\t JSON output\n");
    printf("\t-n\t\t Scale measurements to nano units\n");
    printf("\t-u\t\t Scale measurements to micro units\n");
    printf("\t-m\t\t Scale measurements to milli units\n");
    printf("\t-b\t\t Scale measurements to base units\n");
    printf("\t-k\t\t Scale measurements to kilo units\n");
    printf("\t-M\t\t Scale measurements to mega units\n");
    printf("\t-x\t\t Output measurement value without units or type for use with feedgnuplot\n");
    printf("\t-R\t\t Start offline measurement recording\n");
    printf("\t-r\t\t Download offline measurement recording\n");
    printf("\t-q\t\t Quiet - no status output\n");
    printf("\t-h\t\t Display this help and exit\n");
    printf("\t-V\t\t Display version and exit\n");
    printf("\t<device_address> Address of Owon multimeter to connect\n");
    printf("\t\t\t  otherwise will connect to first meter found if not specified\n");
    printf("\n\tInteractive controls:\n");
    printf("\t\ts - Select\n");
    printf("\t\ta - Auto\n");
    printf("\t\tr - Range\n");
    printf("\t\tl - Backlight\n");
    printf("\t\th - Hold\n");
    printf("\t\tb - Turn off Bluetooth\n");
    printf("\t\td - Delta (Relative)\n");
    printf("\t\tf - Fequency Hz/Duty\n");
    printf("\t\tm - Min/Max\n");
    printf("\t\tn - Normal display\n");

}

// Event handler for interactive controls
static gboolean interactive_read(GIOChannel *chan, GIOCondition cond,
							gpointer user_data) {

    gchar buffer;
    gsize chars_read;

    uint16_t control;

	if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		g_io_channel_unref(chan);
		return FALSE;
	}

	g_io_channel_read_chars(chan, &buffer, 1, &chars_read, NULL);

    switch (buffer) {
        case 's':
            control = SELECT;
            break;

        case 'a':
            control = AUTO;
            break;

        case 'r':
            control = RANGE;
            break;

        case 'l':
            control = LIGHT;
            break;

        case 'h':
            control = HOLD;
            break;

        case 'b':
            control = BLUETOOTH_OFF;
            break;

        case 'd':
            control = RELATIVE;
            break;

        case 'f':
            control = HZ;
            break;

        case 'm':
            control = MIN_MAX;
            break;

        case 'n':
            control = NORMAL;
            break;

        default:
            return TRUE;
    }


    if (gattlib_write_char_by_uuid(connection, &g_control_uuid, &control, sizeof(control))) {
        fprintf(stderr, "Failed to send control.\n");
    }


	return TRUE;
}

// Handler for new device discovery
static void ble_discovered_device(const char* addr, const char* name) {

    if ((name != NULL) && (strcmp(BDM, name) == 0) && (address == NULL)) {

        if (!quiet) fprintf(stderr, "Connecting to %s\n", addr);

        address = malloc(18);
        strcpy(address,addr);
   }

}


// Connect to bluetooth multimeter
void connect_device() {

    do {
        connection = gattlib_connect(NULL, address, BDADDR_LE_PUBLIC, BT_SEC_LOW, 0, 0);
        if (connection == NULL) {
            fprintf(stderr, "Fail to connect to the multimeter bluetooth device.\n");
            sleep(1);
        }
    } while (connection == NULL);

}

// Start the notification listener
void start_listener() {
    gattlib_register_notification(connection, notification_handler, NULL);

    int ret = gattlib_notification_start(connection, &g_measurement_uuid);
    if (ret) {
        fprintf(stderr, "Fail to start listener.\n");
        exit(1);
    }
}

// Attempt to reconnect to the bluetooth multimeter
void reconnect_device() {

    gattlib_disconnect(connection);
    connect_device();
    start_listener();

}


// Connection watchdog

guint timeout_sec = 5;

gboolean watchdog_check(gpointer data) {

    if (!active) {
        if (!quiet) fprintf(stderr, "Timeout\n");

        reconnect_device();

    }

    active = FALSE;

    return TRUE;

}
// SIGINT handler for clean shutdown
void signal_handler(int signal){

    g_main_loop_quit(loop);
}

int main(int argc, char *argv[]) {
    int ret;
    GIOChannel *pchan;

    _Bool scan = TRUE;

    const char* adapter_name = NULL;
    void* adapter = NULL;


    if ((argc > 3) && (argv[1][0] == '-') && (argv[1][1] == 'R')) {

        // Offline recording
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
            address = argv[4];
            scan = FALSE;
        }
    } else {

        for (int argi = 1; argi < argc; argi++) {
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

                    case 'r':
                        offline = TRUE;
                        break;

                    case 'h':
                        usage(argv);
                        return 0;

                    case 'q':
                        quiet = TRUE;
                        break;

                    case 'i':
                        interactive = TRUE;
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

    connect_device();

    if (interval) {

        // Start offline recording

        char *index;

        uint8_t buffer[16];

        struct tm *date;
        time_t now;

        memset(buffer, 0, sizeof(buffer));

        // Send current date/time
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

        //  Send recording parameters
        memset(buffer, 0, sizeof(buffer));

        index = stpcpy((char *)buffer, RECORD_CMD);

        ((uint32_t *)index)[0] = interval;
        ((uint32_t *)index)[1] = num_measurements;
		ret = gattlib_write_char_by_uuid(connection, &g_command_uuid, buffer, sizeof(buffer));
        if (ret) {
            fprintf(stderr, "Failed to write record command.\n");
            return 1;
        }

        if (!quiet) fprintf(stderr, "Recording started\n");

    } else {

        start_listener();

        if (offline) {
            // Request offline recording download
            uint8_t buffer[16];
            size_t len;

            // Check number of measurements available
            memset(buffer, 0, sizeof(buffer));

            stpcpy((char *)buffer, READLEN_CMD);

            ret = gattlib_write_char_by_uuid(connection, &g_command_uuid, buffer, sizeof(buffer));
            if (ret) {
                fprintf(stderr, "Fail to request length of offline recorded measurements.\n");
                return 1;
            }


            len = sizeof(buffer);
            ret = gattlib_read_char_by_uuid(connection, &g_command_uuid, buffer, &len);
            if (ret) {
                fprintf(stderr, "Failed to read length of offline recorded measurements.\n");
                return 1;
            }

            if (*((uint32_t *)buffer) == 0) {
                fprintf(stderr, "No offline recorded measurements available.\n");
            } else {
                if (!quiet) fprintf(stderr, "Downloading %u offline recorded measurements.\n",
                    (*((uint32_t *)buffer)-2)/2);
            }

            // Request measurement data
            memset(buffer, 0, sizeof(buffer));

            stpcpy((char *)buffer, READ_CMD);

            ret = gattlib_write_char_by_uuid(connection, &g_command_uuid, buffer, sizeof(buffer));
            if (ret) {
                fprintf(stderr, "Failed to request offline recorded measurements.\n");
                return 1;
            }
        }

        g_timeout_add_seconds(timeout_sec, watchdog_check, NULL);

        loop = g_main_loop_new(NULL, 0);

        signal(SIGINT, signal_handler);

        if (interactive) {

            // Disable terminal buffering
            struct termios new_termios;

            tcgetattr(0, &orig_termios);
            memcpy(&new_termios, &orig_termios, sizeof(new_termios));
            new_termios.c_lflag &= ~(ECHO | ECHONL | ICANON);

            tcsetattr(0, TCSANOW, &new_termios);

            // Set up input event handler
            pchan = g_io_channel_unix_new(fileno(stdin));
            g_io_channel_set_close_on_unref(pchan, TRUE);
            gint events = G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL;
            g_io_add_watch(pchan, events, interactive_read, NULL);
        }

        g_main_loop_run(loop);

        g_main_loop_unref(loop);
    }

    gattlib_disconnect(connection);
    if (!quiet) fprintf(stderr,"Disconnected\n");

    if (interactive)
        tcsetattr(0, TCSANOW, &orig_termios);

    return 0;
}
