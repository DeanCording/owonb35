# owonb35

Bluetooth Client for Owon B35 Multimeter for newer (post mid 2017) multimeters with the Semic CS7729CN-001 chip.

Older multimeters that use the Fortune Semiconductor FS9922 chip should use [inflex/owon-b35](https://github.com/inflex/owon-b35).

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

By default, measurements are output in the same scale and resolution as displayed by the multimeter.  When using autoranging, this can result in the measurement scale and resolution changing when the multimeter changes ranges.  To avoid this, you can optionally lock the measurement scale.  However, as the multimeter autoranges, it will change the resolution of the measurement value.

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


## Protocol

The multimeter uses the Bluetooth Low Energy Generic Attributes [(BLE GATT)](https://www.bluetooth.com/specifications/gatt/generic-attributes-overview) to transmit measurements.

Measurement data is output as a BLE notification with a 0xfff4 UUID. The packet consists of three uint16_t numbers.

The first number encodes the function, scale, and decimal places.
```
Overload                                1 1 1
Decimal--------------------------------------
                                          | |
DCmV         1 1 1 1 0 0 0 0  0 0 0 1 1 x x x  f0 18
DCV          1 1 1 1 0 0 0 0  0 0 1 0 0 x x x  f0 20
ACmV         1 1 1 1 0 0 0 0  0 1 0 1 1 x x x  f0 58
ACV          1 1 1 1 0 0 0 0  0 1 1 0 0 x x x  f0 60
                                             
DCuA         1 1 1 1 0 0 0 0  1 0 0 1 0 x x x  f0 90
DCmA         1 1 1 1 0 0 0 0  1 0 0 1 1 x x x  f0 98
DCA          1 1 1 1 0 0 0 0  1 0 1 0 0 x x x  f0 a0
                                            
ACuA         1 1 1 1 0 0 0 0  1 1 0 1 0 x x x  f0 d0
ACmA         1 1 1 1 0 0 0 0  1 1 0 1 1 x x x  f0 d8
ACA          1 1 1 1 0 0 0 0  1 1 1 0 0 x x x  f0 e0
                                            
Ohm          1 1 1 1 0 0 0 1  0 0 1 0 0 x x x  f1 20
kOhm         1 1 1 1 0 0 0 1  0 0 1 0 1 x x x  f1 28
MOhm         1 1 1 1 0 0 0 1  0 0 1 1 0 x x x  f1 30
                                            
nF           1 1 1 1 0 0 0 1  0 1 0 0 1 x x x  f1 48 
uF           1 1 1 1 0 0 0 1  0 1 0 1 0 x x x  f1 50
                                            
Hz           1 1 1 1 0 0 0 1  1 0 1 0 0 x x x  f1 a0
Duty         1 1 1 1 0 0 0 1  1 1 1 0 0 x x x  f1 e0
                                            
TempC        1 1 1 1 0 0 1 0  0 0 1 0 0 x x x  f2 20
TempF        1 1 1 1 0 0 1 0  0 1 1 0 0 x x x  f2 60
                                            
Diode        1 1 1 1 0 0 1 0  1 0 1 0 0 x x x  f2 a0
                                            
Continuity   1 1 1 1 0 0 1 0  1 1 1 0 0 x x x  f2 e0
                                            
hFE          1 1 1 1 0 0 1 1  0 0 1 0 0 x x x  f3 20
                                            
             +------+---+--------+-----+-----+
                F     0  Function Scale Decimal
                
                
Function
    0 0 0 0 - DCV
    0 0 0 1 - ACV
    0 0 1 0 - DCA
    0 0 1 1 - ACA
    0 1 0 0 - Ohm
    0 1 0 1 - Cap
    0 1 1 0 - Hz
    0 1 1 1 - Duty
    1 0 0 0 - TempC
    1 0 0 1 - TempF
    1 0 1 0 - Diode
    1 0 1 1 - Continuity
    1 1 0 0 - hFE

Scale
    0 1 0 - micro (u)
    0 1 1 - milli (m)
    1 0 0 - Unit
    1 0 1 - kilo (k)
    1 1 0 - mega (M)
    
Decimal Places
    0 0 0 - 0
    0 0 1 - 1
    0 1 0 - 2
    0 1 1 - 3
    1 0 0 - 4
    1 0 1 - 5
    1 1 1 - Overload
```

The second number bit encodes the reading type.
```
Hold------------------------   01/05 Autorange
Delta--------------------- |   02/06 Autorange
Autorange--------------- | |   04
Min-----------------   | | |   10
Max--------------- |   | | |   20
                 | |   | | |
   00000000  X X X X X X X X
             8 4 2 1 8 4 2 1
```

The third number is the measurement digits as a signed magnitude binary number (msb is sign bit). It is converted to the floating point by using the number of decimal places as specified in the first number.



