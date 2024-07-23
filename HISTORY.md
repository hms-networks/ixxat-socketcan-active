# ix_active_can

## History

### 2.0.556	(2024-07-23)

- add support for current firmware version 3.22.0.1670 same version as current VCI4111 driver rev 182
  move fifo handling to separate component because new firmware uses FIFO2 communication primitive to send/receive CAN messages
  change transmit/receive path to use either FIFO or FIFO2 depending which is detected
  FIFO access: use double reads to catch misreads of read/write index values
  fix loading of correct firmware depending on FPGA version (Rev 1595 for FPGA < 2.x, Rev 1670 for >= 2.x)
- rename shift constants (use _SHIFT postfix instead of _S)
- ixxat_fifo_write_cmd: align request to 4 byte boundary
- replace ixxat_pci_setup_altera_mailbox() with pci_write_altera_mailbox()
- rewrite ixxat_pci_dma_get_info() according to ECI implementation
- rewrite ixxat_pci_mc_reset() according to ECI implementation (wait for bootmanager ACK signal)
- fix implementation of ixxat_init_fwfifos() (enum and record only CAN specific FIFOs, skip others)
- maintain init state during device/driver initialization and move error handling from ixxat_pci_probe() to separate function ixxat_dev_uninit()
- rename ixxat_pci_convey_dma() to ixxat_pci_init_dma_adresstrans_table()
- add support for IB640 devices
- send a IXXAT_PCI_CMD_GET_DEVINFO to the device to correctly initialize IB640 devices.
  Without this command IB640 devices sometimes have problems to handle the command fifo correctly after a cold boot (Seen on FPGA versions <= 0x01020000).
- fix determine number of DMA page in ixxat_pci_dma_get_info()
- fix set of address space indication flags in ixxat_pci_init_dma_adresstrans_table()
- replace fixed msleep with firmware startup detection code (checks firmware startup for at least IXXAT_PCI_FIRMWARE_STARTUP_PERIOD)
- uninit: remove release of memory regions from state IXXAT_PROBESTATE_DEVICE_REGISTERED as it is already done in state IXXAT_PROBESTATE_PCI_REGIONS_REQUESTED
- unify formatting of error codes in dmesg output to hex format
- fix error result of ixxat_init_fwfifos() in case of FIFO tag checks fail
- ixxat_pci_probe: handle errors during DMA address table init

### 2.0.520	(2024-06-04)

- kernel >= 6.1.0: use can_dev_dropped_skb() instead of can_dropped_invalid_skb() to check skb in ixxat_usb_start_xmit()
- replace kfree_skb() with dev_kfree_skb() calls
- cleanup/restructure echo skb handling in start_xmit functions

### 2.0.492	(2024-04-02)

- add more error output to ixxat_pci_send_cmd()
- fix communication via fifos on arm32 platforms (memcpy_toio/memcpy_fromio does not work on non-prefetchable memory region, and iowrite32_rep/ioread32_rep does not either)
- add fifo resource tag check on initialization
- fix decoding of firmware file on big endian platforms
- disable debug output in release version (removes warning about unused locals)
- read firmware version and support sysfs attributes (serial, firmware_version, hardware, hardware_version, fpga_version)
- use dev_addr_mod helper function to set device address to avoid "Incorrect netdev->dev_addr" warning during driver unload
- remove use of request_mem_region/release_mem_region to map devicd memory and use pci_request_regions/pci_release_regions instead
- fix device information dmesg output
- fix interpretation of hardware ID on active cards as null terminated string instead of GUID
- set device address from hardware ID, init dev_id and dev_port to controller index
- fix build against kernel version 5.12
- prevent error: unused variable ‘intSR’ in release build
- cleanup kernel version dependent code
- replace calls to pci_alloc_consistent/pci_free_consistent with dma_alloc_coherent/dma_free_coherent
- remove unnecessary 4kB page from DMA memory window
- IB2xx: set DMA memory size to 512kB to work around IOMMU issues
- replace calls to netif_napi_add() with netif_napi_add_weight()
- fix warnings for type parameter used in skb functions
- handle different signatures of skb and dlc functions depending on kernel version

### 2.0.377	(2020-03-12)

- initial version
