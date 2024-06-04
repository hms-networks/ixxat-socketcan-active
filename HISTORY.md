# ix_active_can

## History

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
