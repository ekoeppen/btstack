# BTstack Apple Newton Port

This is a port of the [BTstack](https://github.com/bluekitchen/btstack) Bluetooth stack to the Apple Newton MP2x00.

# Welcome to BTstack

BTstack is [BlueKitchen's](https://bluekitchen-gmbh.com) implementation of the official Bluetooth stack.
It is well suited for small, resource-constraint devices
such as 8 or 16 bit embedded systems as it is highly configurable and comes with an ultra small memory footprint.

Targeting a variety of platforms is as simple as providing the necessary UART, CPU, and CLOCK implementations. BTstack is currently capable of connecting to Bluetooth-modules via: (H2) HCI USB, (H4) HCI UART + TI's eHCILL, and (H5) HCI Three-Wire UART.

On smaller embedded systems, a minimal run loop implementation allows to use BTstack without a Real Time OS (RTOS).
If a RTOS is already provided, BTstack can be integrated and run as a single thread.

On larger systems, BTstack provides a server that connects to a Bluetooth module.
Multiple applications can communicate with this server over different inter-process communication methods. As sockets are used for client/server communication, it's easy to interact via higher-level level languages, e.g. there's already a Java binding for use in desktop environments.

BTstack supports the Central and the Peripheral Role of Bluetooth 5 Low Energy specification incl. LE Secure Connections, LE Data Channels, and LE Data Length Extension. It can be configured to run as either single-mode stack or a dual-mode stack.

BTstack is free for non-commercial use. However, for commercial use, <a href="mailto:contact@bluekitchen-gmbh.com">tell us</a> a bit about your project to get a quote.

**Documentation:** [HTML](http://bluekitchen-gmbh.com/btstack/), [PDF](http://bluekitchen-gmbh.com/btstack.pdf)

**Third-party libraries (FOSS):** [List of used libraries and their licenses](https://github.com/bluekitchen/btstack/blob/develop/3rd-party/README.md)

**Discussion and Community Support:** [BTstack Google Group](https://groups.google.com/group/btstack-dev)


### Supported Protocols and Profiles

**Protocols:** L2CAP (incl. LE Data Channels), RFCOMM, SDP, BNEP, AVDTP, AVCTP, ATT, SM (incl. LE Secure Connections).

**Profiles:** GAP, IOP, HFP, HSP, SPP, PAN, A2DP, AVRCP incl. Browsing, GATT.

**GATT Services:** Battery, Cycling Power, Cycling Speed and Cadence, Device Information, Heart Rate, HID over GATT (HOG), Mesh Provisioning, Mesh Proxy, Nordic SPP, u-Blox SPP. 

GATT Services are in general easy to implement and require short development time. For more GATT Services please contact us, or follow the [implementation guidelines](http://bluekitchen-gmbh.com/btstack/profiles/#gatt-generic-attribute-profile).  

**Beta Stage:** HID, HOGP, PBAP.

**In Development:** BLE Mesh and more.

It has been qualified with the Bluetooth SIG (QDID 110883) for GAP 1.1, IOP, HFP 1.7, HSP 1.2, SPP 1.2, PAN 1.0, A2DP 1.3, AVRCP 1.6 profiles and
GATT, SM of the Bluetooth Core 5.0 specification. For information on MFi/iAP2 support, please <a href="mailto:contact@bluekitchen-gmbh.com">contact us</a>.

## Source Tree Overview
Path				| Description
--------------------|---------------
chipset             | Support for individual Bluetooth chipsets
doc                 | Sources for BTstack documentation
example             | Example applications available for all ports
platform            | Support for special OSs and/or MCU architectures
port                | Complete port for a MCU + Chipset combinations
src                 | Bluetooth stack implementation
test                | Unit and PTS tests
tool                | Helper tools for BTstack
