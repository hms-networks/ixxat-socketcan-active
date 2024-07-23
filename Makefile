mod-name += ix_active_can

KERNEL_SRC          ?= /lib/modules/$(shell uname -r)/build
MOD_DIR             := kernel/drivers/net/can/ixxat_pci_active
SRC_DIR             := $(shell pwd)/$(MOD_DIR)
DEST_DIR            := /lib/modules/$(shell uname -r)/$(MOD_DIR)
FW_DIR              := /lib/firmware/ixxat
FW_FILE             := ixx_active_can.fw
FW_FILE_V2          := ixx_active_can_2.fw
FW_FILE_IB640       := ixx_ib640_canfd.fw
MOD_FILE            := $(mod-name).ko

#
# the Kernel $(MAKE)file is used !
#

.PHONY: all clean modules_install install uninstall

all:
	$(MAKE) -C "$(KERNEL_SRC)" M=$(SRC_DIR) CONFIG_CAN_IXXAT_PCI_ACTIVE=m modules

clean:
	$(MAKE) -C "$(KERNEL_SRC)" M=$(SRC_DIR) CONFIG_CAN_IXXAT_PCI_ACTIVE=m clean

modules_install:
	$(MAKE) -C "$(KERNEL_SRC)" M=$(SRC_DIR) CONFIG_CAN_IXXAT_PCI_ACTIVE=m modules_install

install: all
	mkdir -p "$(FW_DIR)"
	cp "$(SRC_DIR)/$(FW_FILE)" "$(FW_DIR)"
	cp "$(SRC_DIR)/$(FW_FILE_V2)" "$(FW_DIR)"
	cp "$(SRC_DIR)/$(FW_FILE_IB640)" "$(FW_DIR)"
	install -d "$(DEST_DIR)"
	install "$(SRC_DIR)/$(MOD_FILE)" "$(DEST_DIR)"
	depmod -a
	modprobe $(mod-name)

uninstall:
	modprobe -r $(mod-name)
	depmod -a
	rm -f "$(DEST_DIR)/$(MOD_FILE)"
	rm -f "$(FW_DIR)/$(FW_FILE)"
	rm -f "$(FW_DIR)/$(FW_FILE_V2)"
	rm -f "$(FW_DIR)/$(FW_FILE_IB640)"
