# IXXAT Active PCI Device Driver

The IXXAT active PCI device Linux driver provides support for the following devices:

* IXXAT CAN-IB200/PCIe     (CAN)
* IXXAT CAN-IB210/PCIe XMC (CAN)
* IXXAT CAN-IB230/PCIe 104 (CAN)
* IXXAT CAN-IB400/PCI      (CAN)
* IXXAT CAN-IB410/PCI  PMC (CAN)
* IXXAT CAN-IB600/PCIe     (CAN FD)
* IXXAT CAN-IB610/PCIe XMC (CAN FD)
* IXXAT CAN-IB630/PCIe 104 (CAN FD)
* IXXAT CAN-IB810/PCI  PMC (CAN FD)
* IXXAT CAN-IB640/PCI      (CAN FD)

## Install

The installation of the linux driver requires that the linux kernel header files
and the necessary build tools are installed on your system. You can install them as follows:

Debian based systems:

```
$ sudo apt install linux-headers-$(uname -r)
$ sudo apt install --reinstall build-essential
```

Raspbian:
```
$ sudo apt-get install raspberrypi-kernel-headers
```

Fedora and Red Hat:

```
$ su -
# yum -y install kernel-devel kernel-headers
# yum -y groupinstall 'Development Tools'
```

You can check if the headers are installed by running the following command:

```
$ ls /usr/src/linux-headers-$(uname -r)
```

Compile the kernel module by running make:

```
$ make all
```

You can then install the module by running:

Debian based systems:

```
$ sudo make install
```

Fedora and Red Hat:

```
$ su -
# make install
```

This will build your modules, install them in a shared directory and load
them into the kernel.

Congratulations! You can now use your IXXAT active PCI devices.


## Get information about available SocketCAN interfaces

SocketCAN devices are shown as network interfaces and can be listed via the ip command, e.g:

```
$ ip a
```

lists all network devices and SocketCAN devices usually follow the name pattern "can?":

```
...
19: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 16 qdisc pfifo_fast state UP group default qlen 10
    link/can 48:57:34:38:32:34:34:36:00:00:00:00:00:00:00:00 brd 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
20: can1: <NOARP,UP,LOWER_UP,ECHO> mtu 16 qdisc pfifo_fast state UP group default qlen 10
    link/can 48:57:34:38:32:34:34:36:00:00:00:00:00:00:00:00 brd 00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00
```

You can use the json interface of the ip command to do more sophisticated lookup operations to search for specific devices.
This command dumps info of all interface as prettified json.

```
$ ip -j a | jq
```

The jq command can be used to search for specific interfaces, e.g. to list all SocketCAN interfaces use

```
$ ip -j addr show | jq ".[]|select(.link_type==\"can\")"
```

Replace the term "can" in the above with "vcan" to list virtual SocketCAN interfaces.
To list only the ifnames of all SocketCAN interfaces use the command

```
$ ip -j addr show | jq ".[]|select(.link_type==\"can\")|.ifname"
```

For further information lookup the description of the jq command.

## More interface information

You could use the ip command to read more interface information

```
$ ip -details link show can0
$ ip -details -statistics link show can0
```

and the driver supports sysfs access to read more device information. 
To list these for the first SocketCAN interface you can use

```
$ for c in /sys/class/net/can0/*; do echo -n "`basename $c`: "; cat $c; done
```

Useful information bits are

* address, addr_len (usually the address contains the serial number)
* dev_id/dev_port (zero based controller index, devices can support multiple CAN controllers)
* firmware_version
* fpga_version
* hardware (holds the device class string, e.g. "USB-to-CAN_FD")
* hardware_version
* operation state (up or down)
* serial (device id as string, usually the serial number)
* statistics (statistic counters)
* tx_queue_len (length of tx queue)

You can combine all of this information e.g. to search for SocketCAN interfaces which belong to a specific hwid.

```
$ for dev in $(ip -j addr show | jq ".[]|select(.link_type==\"can\")|.ifname"); do if [ "HW482446" == $(cat /sys/class/net/can1
/serial) ]; then echo $dev; fi; done
```

Each interface supports a bunch of statistic counters which can be dumped via:

```
$ for c in /sys/class/net/can0/statistics/*; do echo -n "`basename $c`: "; cat $c; done
collisions: 0
multicast: 0
rx_bytes: 0
rx_compressed: 0
rx_crc_errors: 0
rx_dropped: 0
rx_errors: 0
rx_fifo_errors: 0
rx_frame_errors: 0
rx_length_errors: 0
rx_missed_errors: 0
rx_nohandler: 0
rx_over_errors: 1
rx_packets: 0
tx_aborted_errors: 0
tx_bytes: 0
tx_carrier_errors: 0
tx_compressed: 0
tx_dropped: 0
tx_errors: 0
tx_fifo_errors: 0
tx_heartbeat_errors: 0
tx_packets: 0
tx_window_errors: 0
```

Instead of reading this direct via sysfs you could access this 
information with the ip command, too:

```
$ ip -j -details -statistics link show can0 | jq
```

## Basic usage

This chapter is only a short introduction to SocketCAN usage.
For more information see [https://www.kernel.org/doc/Documentation/networking/can.txt](https://www.kernel.org/doc/Documentation/networking/can.txt).

To be able to use these interfaces, you have to configure them with a valid bitrate.
You can initialize an interface with the commands:

```
$ sudo ip link set can0 type can bitrate 1000000
$ sudo ip link set can0 up
```

or do the same in a loop for multiple interfaces and combine bitrate setting with the up command:

```
for c in $(seq 0 1); do sudo ip link set can$c up type can bitrate 1000000; done
```

For CAN-FD bitrates you have to specify two bitrates (arbitration and data bitrate) and enable CAN-FD:

```
$ sudo ip link set can0 up type can bitrate 500000 sample-point 0.75 dbitrate 4000000 dsample-point 0.8 fd on
```

## Transmit/Receive messages


For basic operations on SocketCAN devices you need to install the can-utils package:

```
$ sudo apt install can-utils
```

After that you can use the commands cansend, candump and cangen to work with CAN messages.

```
$ cansend can0 123#112233
```

sends a message in can0.

```
$ candump can1
```

receives messages from can1.

```
$ cangen can0
```

generates CAN traffic on can0, see the command line interface on how to shape the 
traffic content.


## Remove the driver

Finally, if you want to uninstall the driver you could either use the rmmod command or
use the makefile, which supports driver removal, via

```
$ sudo make uninstall
```
