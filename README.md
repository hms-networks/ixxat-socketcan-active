# IXXAT Active PCI Cards Driver

The IXXAT active PCI cards linux driver provides support for the following devices:

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

Congratulations! You can now use the IXXAT active PCI cards.
