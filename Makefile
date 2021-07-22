mod-name += ix_active_can

MOD_DIR             := kernel/drivers/net/can/ixxat_pci_active
KBUILD_DIR          := /lib/modules/$(shell uname -r)/build
SRC_DIR             := $(shell pwd)/$(MOD_DIR)
DEST_DIR            := /lib/modules/$(shell uname -r)/$(MOD_DIR)
FW_DIR              := /lib/firmware/ixxat
FW_FILE             := ixx_active_can.fw
MOD_FILE            := $(mod-name).ko

#
# the Kernel Makefile is used !
#

.PHONY: all clean install uninstall

all:
	make -C $(KBUILD_DIR) M=$(SRC_DIR) CONFIG_CAN_IXXAT_PCI_ACTIVE=m modules

clean:
	make -C $(KBUILD_DIR) M=$(SRC_DIR) CONFIG_CAN_IXXAT_PCI_ACTIVE=m clean

install: all
	mkdir -p "$(FW_DIR)"
	cp "$(SRC_DIR)/$(FW_FILE)" "$(FW_DIR)"
	install -d "$(DEST_DIR)"
	install "$(SRC_DIR)/$(MOD_FILE)" "$(DEST_DIR)"
	depmod -a
	modprobe $(mod-name)

uninstall:
	modprobe -r $(mod-name)
	depmod -a
	rm -f "$(DEST_DIR)/$(MOD_FILE)"
	rm -f "$(FW_DIR)/$(FW_FILE)"
