# owonb35

Bluetooth Client for Owon B35 Multimeter

The [Owon B35](http://owontme.com/products_owon_3_5%7C6_digital_multimeter_with_bluetooth) series of multimeters use Bluetooth Low Energy (BLE) to transmit readings to remote monitors such as smartphone apps.  This Linux client allows to to caputure measurement data to you computer to record in a file or display on charting software such as gnuplot or KST.

## Requirements

This client requires the [Gattlib](https://github.com/labapart/gattlib) BLE library to be installed.  Compiled packages are available to install with needing to compile.

## Installation

Either download the compiled binary or the source code to compile your own.

Compiling is a simple 'make'.

## Usage

The client is designed to be a simple receiver of measurement data that outputs in formats that can be piped into other tools for processing or display.

```
owonb35 [-t|-T] [-c|-j] [-u|-m|-b|-k|-M] [<device_address>]

        -s               Timestamp measurements in elapsed seconds from first reading
        -S               Timestamp measurements in seconds
        -t               Timestamp measurements in elapsed milliseconds from first reading
        -T               Timestamp measurements in milliseconds
        -d               Timestamp measurements with date/time
        -c               Comma separated values (CSV) output
        -j               JSON output
        -u               Scale measurements to micro units
        -m               Scale measurements to milli units
        -b               Scale measurements to base units
        -k               Scale measurements to kilo units
        -M               Scale measurements to mega units
        -n               Output just the measurement without the units or type for use with feedgnuplot
        <device_address> Address of Owon multimeter to connect
                          otherwise will connect to first meter found if not specified
```

You can provide an optional Bluetooth address for the specific multimeter to connect to or the client will otherwise scan for devices and connect to the first multimeter it finds.  Scanning and connection can be a bit flaky at times.  Note that only one client can connect to the multimeter at a time.

Measurments can be optionally timestamped in actual time or elapsed time since the first measurement was received.  Timestamps can be in seconds, milliseconds, or date-time.  Note that the multimeter transmits measurements approximately every 600ms.

Output format defaults to space seperated values but can also be output in Comma Seperated Values (CSV) or JSON formats.  By default, the measurement unit is output but this can be disabled for feeding applications that can only handle numeric data.

By default, measurements are output in the same format as displayed by the multimeter.  When using autoranging, this can result in the measurement unit and scale changing when the multimeter changes ranges.  You can optionally fix the measurement unit to lock the measurement scale.  However, as the multimeter autoranges, it will change the resolution of the measurement value.

## Interfacing

The client is designed to inteface into other tools using the normal Unix pipe and redirection mechanisms.

### Files

Measurement values can be written directly to file using redirects:

Write timestamped measurements in CSV format - `owonb35 -c -d > measurements.txt`

The `tee` command can be used to write measurement values to a file and also pipe them to another application.

`owonb35 | tee measurements.txt | next_tool`

### gnuplot

[gnuplot](http://www.gnuplot.info) is a very flexible plotting program.  The client can feed measurements to gnuplot in realtime using [feedgnuplot](https://github.com/dkogan/feedgnuplot).

`owonb35 -s -n -b | feedgnuplot --domain --lines --stream  --ymin 0 --ylabel 'Volts' --xlabel 'Seconds' --exit`

It is best to lock the measurement unit and scale to prevent them jumping around if the multimeter autoranges during measurement.  feedgnuplot doesn't handle non-numeric data in its input, so the output of measurement units need to be disabled.  It also cannot handle measurements with less than one second resolution, which means that it can only display elapsed timestamps.  Data written to file and loaded into gnuplot can display actual timestamps.

### MQTT

MQTT is a publish/subscribe messaging system frequently used in Internet of Things networks.  It allows clients to publish data onto a network for other clients to subscribe to.  Data is usually published as single values or in JSON format.

Single value - `omonb35 -n -b | mosquitto_pub -t voltage -l`

JSON format - `omonb35 -T -b -j | mosquitto_pub -t measurement -l`






