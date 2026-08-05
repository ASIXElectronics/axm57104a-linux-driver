# ASIX AXM57104A Quad Port TSN Gigabit Ethernet PCIe NIC Linux driver (`axm57104A.ko`)

This is the official **ASIX AXM57104A Quad Port TSN Gigabit Ethernet PCIe NIC Linux driver** source (module name: `axm57104A.ko`), which is suitable for embedded systems, smart home/office devices, Ethernet hubs, switches, routers and home gateway applications.

---
## ASIX TSN Gigabit Ethernet Switch Solutions
- [AXM57104A Quad Port TSN Gigabit Ethernet PCIe NIC](https://www.asix.com.tw/en/product/IndustrialEthernet/TSN/AXM57104)

The **AXM57104A** is a 4-port TSN Gigabit Ethernet PCIe NIC card, which supports enhanced TSN functions, compliant to IEEE 802.1AS-Rev/AS-2020, 1588V2, 802.1Qav, 802.3br & 802.1Qbu, 802.1Qbv, 802.1Qci and 802.1CB. AXM57104A also supports FPGA hard-core field upgradable via In Application Programming (IAP) for TSN standards evolution. It is a cost-efficient PCIe to TSN solution for Industrial Internet of Things (IIoT) applications to enable TSN functions on industrial automation platforms, Fieldbus over TSN gateways and converge the non-real-time IT network and real-time OT networks which work on different industrial Ethernet protocols such as EtherCAT/PROFINET/EtherNet IP/etc.

---
# Prerequisites

Prepare to build the driver, you need the Linux kernel sources installed on the
build machine, and make sure that the version of the running kernel must match
the installed kernel sources. If you don't have the kernel sources, you can get
it from www.kernel.org or contact to your Linux distributor. If you don't know
how to do, please refer to KERNEL-HOWTO.

# Getting Start

1. Extract the compressed driver source file to your temporary directory by the
   following command:

	[root@localhost template]# tar -xf DRIVER_SOURCE_PACKAGE.tar.bz2

2. Now, the driver source files should be extracted under the current directory.
   Executing the following command to compile the driver and install the driver:
 
	[root@localhost template]# ./install

If you want to remove the driver, just executing the following command:

	[root@localhost anywhere]# ./uninstall
