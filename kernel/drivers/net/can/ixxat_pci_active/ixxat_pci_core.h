/* SPDX-License-Identifier: GPL-2.0 */

/* CAN driver base for IXXAT PCI-to-CAN
 *
 * Copyright (C) 2018-2024 HMS Industrial Networks <socketcan@hms-networks.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef IXXAT_PCI_CORE_H_
#define IXXAT_PCI_CORE_H_

#include "ixxat_fifo.h"

#define IXXAT_FIRMWARE_FPGA_V1		"ixxat/ixx_active_can.fw"
#define IXXAT_FIRMWARE_FPGA_V2		"ixxat/ixx_active_can_2.fw"
#define IXXAT_FIRMWARE_IB640		"ixxat/ixx_ib640_canfd.fw"

#define IXXAT_PCI_VENDOR_ID		0x1BEE

#define IXXAT_PCI_CLOCK			80000000

/* CAN */
#define CAN_IB200_PRODUCT_ID		0x0003
#define CAN_IB210_PRODUCT_ID		0x0014
#define CAN_IB230_PRODUCT_ID		0x0006
#define CAN_IB400_PRODUCT_ID		0x0011
#define CAN_IB410_PRODUCT_ID		0x0016
/* CAN FD */
#define CAN_IB600_PRODUCT_ID		0x000f
#define CAN_IB610_PRODUCT_ID		0x0018
#define CAN_IB640_PRODUCT_ID		0x002e
#define CAN_IB800_PRODUCT_ID		0x001b
#define CAN_IB810_PRODUCT_ID		0x001c

#define IXXAT_PCI_BUS_TYPE(busctrl)	((u8)(((busctrl) >> 8) & 0x00FF))

#define CAN_IB_IFI_CAN_BASE		0x2000
#define CAN_IB_IFI_CAN_SIZE		0x1000

#define IXXAT_PCI_CARDNAME_SIZE		0x0010
#define IXXAT_PCI_HWSERIAL_SIZE		0x0010

#define IXXAT_PCI_STATE_CONNECTED	BIT(0)
#define IXXAT_PCI_STATE_RUNNING		BIT(1)

#define PCIE_ALTERA_LCR_INTCSR		0x0040
#define PCIE_ALTERALCR_A2P_INTENA	0x0050

#define IXXAT_PCI_MAX_TX_TRANS		0x10

#define IXXAT_PCI_BUS_CAN		1

#define IXXAT_PCI_BTMODE_NAT		BIT(0)
#define IXXAT_PCI_BTMODE_TSM		BIT(1)

#define IXXAT_PCI_STOP_ACTION_CLEARALL	0x3

#define IXXAT_PCI_RESET			0x00000001
#define IXXAT_PCI_RESET_PERIOD		10000
#define IXXAT_PCI_RESET_DELAY		200000
#define IXXAT_PCI_BOOTM_STARTUP_PERIOD	1000000

#define IXXAT_PCI_FIRMWARE_STARTUP_PERIOD	1000000

// Boot manager mailbox start acknowledge ("BOOT")
#define IXXAT_PCI_MBX_ACK_START_BOOT	0x544f4f42
// Firmware mailbox start acknowledge ("FIRM")
#define IXXAT_PCI_MBX_ACK_START_FIRM	0x4d524946

#define IXXAT_PCI_ALTERA_P2A_MBX_OFF	(0x200 * 4)
#define IXXAT_PCI_ALTERA_P2A_MBX_COUNT	8

#define IXXAT_PCI_ALTERA_A2P_MBX_OFF	(0x240 * 4)
#define IXXAT_PCI_ALTERA_A2P_MBX_COUNT	8

#define IXXAT_PCI_ALTERA_ADRTRANSSTART_OFF	0x1000
#define IXXAT_PCI_ALTERA_ADRTRANSEND_OFF	0x2000
#define IXXAT_PCI_ALTERA_RESET_CARD_OFF		0x4000

#define IXXAT_PCI_SFF_ID		0x0003FFFF
#define IXXAT_PCI_SFF_SHIFT		11
#define IXXAT_PCI_EFF_ID		0x1FFC0000
#define IXXAT_PCI_EFF_SHIFT		18

#define IXXAT_PCI_MSG_TYPE_VCI		0x00
#define IXXAT_PCI_MSG_TYPE_IFI		0x01
#define IXXAT_PCI_MSG_FLAGS_TYPE	0x000000FF

#define IXXAT_PCI_OPMODE_STANDARD	BIT(0)
#define IXXAT_PCI_OPMODE_EXTENDED	BIT(1)
#define IXXAT_PCI_OPMODE_ERRFRAME	BIT(2)
#define IXXAT_PCI_OPMODE_LISTONLY	BIT(3)

#define IXXAT_PCI_EXMODE_EXTDATA	BIT(0)
#define IXXAT_PCI_EXMODE_FASTDATA	BIT(1)
#define IXXAT_PCI_EXMODE_ISOFD		BIT(2)

#define IXXAT_PCI_CAN_STATUS_OK		0x00000000
#define IXXAT_PCI_CAN_STATUS_OVRRUN	BIT(1)
#define IXXAT_PCI_CAN_STATUS_ERRLIM	BIT(2)
#define IXXAT_PCI_CAN_STATUS_BUSOFF	BIT(3)
#define IXXAT_PCI_CAN_STATUS_ERR_PAS	BIT(13)

#define IXXAT_PCI_CAN_ERROR_COUNTER_RX	3
#define IXXAT_PCI_CAN_ERROR_COUNTER_TX	4

#define IXXAT_PCI_CAN_ERROR_STUFF	1
#define IXXAT_PCI_CAN_ERROR_FORM	2
#define IXXAT_PCI_CAN_ERROR_ACK		3
#define IXXAT_PCI_CAN_ERROR_BIT		4
#define IXXAT_PCI_CAN_ERROR_CRC		6

#define IXXAT_PCI_CAN_INFO		0x01
#define IXXAT_PCI_CAN_ERROR		0x02
#define IXXAT_PCI_CAN_STATUS		0x03
#define IXXAT_PCI_CAN_WAKEUP		0x04
#define IXXAT_PCI_CAN_TIMEOVR		0x05
#define IXXAT_PCI_CAN_TIMERST		0x06

#define IXXAT_PCI_ENABLE_INT		0x00FF0000
#define IXXAT_PCI_DISABLE_INT		0xFF00FFFF
#define IXXAT_PCI_STAT_MASK		0x00FF0080

#define IXXAT_PCI_DMA_ADD_HIGH		0xFFFFFFFF
#define IXXAT_PCI_DMA_ADD_LOW		0xFFFFFFFC
#define IXXAT_PCI_DMA_OFFSET_HIGH	32

#define IXXAT_PCI_NUM_CMD_FIFO		2

#define IXXAT_PCI_DMA_TEST_ADDR		0x80100000
#define IXXAT_PCI_DMA_TEST_CONTENT	"DMA Memory Check"

#define IXXAT_PCI_CMD_MAX_SIZE		240
#define IXXAT_PCI_CMD_LB_SIZE		228
#define IXXAT_PCI_CMD_TIMEOUT_NS	500000000

#define IXXAT_PCI_CMD_START		0x326
#define IXXAT_PCI_CMD_STOP		0x327
#define IXXAT_PCI_CMD_INIT_V2		0x337

#define IXXAT_PCI_CMD_GET_FWINFO	0x400
#define IXXAT_PCI_CMD_GET_DEVCAPS	0x401
#define IXXAT_PCI_CMD_GET_DEVINFO	0x402
#define IXXAT_PCI_CMD_GET_FWINFO2	0x403

#define IXXAT_PCI_CMD_ADRTABLE_ESTABLISH 0x440
#define IXXAT_PCI_CMD_ADRTABLE_SIZE	0x441
#define IXXAT_PCI_CMD_LOOPBACK		0x500
#define IXXAT_PCI_CMD_STARTFIRMWARE	0x501
#define IXXAT_PCI_CMD_TRIGGER_INT	0x502
#define IXXAT_PCI_CMD_READ_BLOCK	0x503
#define IXXAT_PCI_CMD_WRITE_BLOCK	0x504

#define IXXAT_PCI_RESDIR_HTOD		0x0001
#define IXXAT_PCI_RESDIR_DTOH		0x0002

#define IXXAT_PCI_VER_SIZE		0x10
#define IXXAT_PCI_RES_VER_OFF		IXXAT_PCI_VER_SIZE
#define IXXAT_PCI_RES_MAX_OBJ_SIZE	252
#define IXXAT_PCI_RES_HEADER_SIZE	28

#define IXXAT_PCI_CAN_ERROR_LEN		5


/* size of available mapped DMA (after possible page shifting) */
#define IXXAT_PCI_DMA_SIZE		0x80000
/* Size of DMA Buffer 512kiB required */
#define IXXAT_PCI_DMA_LEN		(512 * 1024)
#define IXXAT_PCI_ADDMEM_LEN		PAGE_SIZE

#define IX_LOOP_DIS			0x00	//disable self reception
#define IX_LOOP_SELF_RX			0x01	//enable self reception
#define IX_LOOPBACK			0x02	//pass on message to application

#define IXXAT_MAX_CANCTRL_COUNT         32

// command used by bootmanager on IB200 < FPGA 1.3 (BM version 3.0.4.0)
struct ixxat_intf_info_1_3 {
	char intf_name[IXXAT_PCI_CARDNAME_SIZE];	/* device name */
	char intf_id[IXXAT_PCI_HWSERIAL_SIZE];		/* unique device id */
	u16 intf_version;				/* device version ( 0, 1, ...) */
	u32 intf_fpga_version;				/* device version of FPGA design */
} __packed;

struct ixxat_intf_info {
	char intf_name[IXXAT_PCI_CARDNAME_SIZE];	/* device name */
	char intf_id[IXXAT_PCI_HWSERIAL_SIZE];		/* unique device id */
	u16 intf_version;				/* device version ( 0, 1, ...) */
	u32 intf_fpga_version;				/* device version of FPGA design */
	u16 reserved;
} __packed;

#define IX_FPGAVERSION_MASK		0x00FFFFFF
#define IX_FPGAVERSION_V1_3		0x00010300
#define IX_FPGAVERSION_V2		0x00020000

struct ixxat_intf_firmware_info {
	u32 firmware_type;		/* type of currently running firmware */
	u16 reserved;			/* reserved */
	u16 major_version;		/* major firmware version number */
	u16 minor_version;		/* minor firmware version number */
	u16 build_version;		/* build firmware version number */
} __packed;

struct ixxat_intf_firmware_info2 {
	u32 firmware_type;		/* type of currently running firmware */
	u16 reserved;			/* reserved */
	u16 major_version;		/* major firmware version number */
	u16 minor_version;		/* minor firmware version number */
	u16 build_version;		/* build firmware version number */
	u32 revision;			/* revision number */
} __packed;

struct ixxat_intf_caps {
	u16 bus_ctrl_count;
	u16 bus_ctrl_types[32];
	u16 reserved;
} __packed;

struct ixxat_pci_fw {
	__le16 length;
	__le32 address;
	u8 data[];
} __packed;

struct ixxat_pci_fwHdr {
	char   caIdent[10];
	__le32 type;
	__le32 maxlen;
} __packed;

/**
 * struct ixxat_can_msg_base - IXXAT CAN message base (CL1/CL2)
 * @size: Message size (this field excluded)
 * @time: Message timestamp
 * @msg_id: Message ID
 * @flags: Message flags
 *
 * Contains the common fields of an IXXAT CAN message on both CL1 and CL2
 * devices
 */
struct ixxat_can_msg_base {
	__le32 time;
	__le32 msg_id;
	__le32 flags;
} __packed;

/**
 * struct ixxat_can_msg_cl1 - IXXAT CAN message (CL1)
 * @data: Message data (standard CAN frame)
 *
 * Contains the fields of an IXXAT CAN message on CL1 devices
 */
struct ixxat_can_msg_cl1 {
	u8 data[CAN_MAX_DLEN];
} __packed;

/**
 * struct ixxat_can_msg_cl2 - IXXAT CAN message (CL2)
 * @client_id: Client ID
 * @data: Message data (CAN FD frame)
 *
 * Contains the fields of an IXXAT CAN message on CL2 devices
 */
struct ixxat_can_msg_cl2 {
	__le32 client_id;
	u8 data[CANFD_MAX_DLEN];
} __packed;

/**
 * struct ixxat_can_msg - IXXAT CAN message
 * @base: Base message
 * @cl1: Cl1 message
 * @cl2: Cl2 message
 *
 * Contains an IXXAT CAN message
 */
struct ixxat_can_msg {
	struct ixxat_can_msg_base base;
	union {
		struct ixxat_can_msg_cl1 cl1;
		struct ixxat_can_msg_cl2 cl2;
	};
} __packed;

struct ixxat_time_ref {
	ktime_t kt_host_0;
	u32 ts_dev_0;
	u32 ts_dev_last;
};

struct ixxat_canbtp {
	__le32 mode;
	__le32 bps;
	__le16 ts1;
	__le16 ts2;
	__le16 sjw;
	__le16 tdo;
} __packed;

/* request packet header  */
struct ixxat_pci_dal_req {
	__le32 size;
	__le16 port;
	__le16 socket;
	__le32 code;
} __packed;

/* response packet header */
struct ixxat_pci_dal_res {
	__le32 res_size;
	__le32 ret_size;
	__le32 ret_code;
} __packed;

struct ixxat_pci_write_req {
	struct ixxat_pci_dal_req dal_req;
	__le32 addr;				/* address */
	u8 data[];				/* data to be written */
};

struct ixxat_pci_write_res {
	struct ixxat_pci_dal_res dal_res;
};

struct ixxat_pci_read_req {
	struct ixxat_pci_dal_req dal_req;
	__le32 addr;				/* address */
};

struct ixxat_pci_read_res {
	struct ixxat_pci_dal_res dal_res;
	u8 data[];				/* buffer for data bytes */
};

struct ixxat_pci_loopback_cmd {
	struct ixxat_pci_dal_req req;
	u8 req_data[IXXAT_PCI_CMD_LB_SIZE];	/* buffer for data UINT8s */
	struct ixxat_pci_dal_res res;
	u8 res_data[IXXAT_PCI_CMD_LB_SIZE];	/* buffer for data UINT8s */
} __packed;

struct ixxat_pci_start_fw_cmd {
	struct ixxat_pci_dal_req req;
	__le32 reserved;			/* unused variable */
	struct ixxat_pci_dal_res res;
} __packed;

struct ixxat_pci_adrtable_size_cmd {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	__le32 mem_size;
} __packed;

struct ixxat_pci_adrtable_establish_cmd {
	struct ixxat_pci_dal_req req;
	__le32 mem_offset;
	__le32 mem_len;
	__le32 msi_offset;
	__le32 msi_len;
	__le32 msi_num;
	struct ixxat_pci_dal_res res;
} __packed;

struct ixxat_pci_trigger_int_cmd {
	struct ixxat_pci_dal_req req;
	__le32 mailbox;
	__le32 content;
	struct ixxat_pci_dal_res res;
} __packed;

struct ixxat_pci_fwinfo_cmd {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	struct ixxat_intf_firmware_info info;
} __packed;

struct ixxat_pci_fwinfo2_cmd {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	struct ixxat_intf_firmware_info2 info;
} __packed;

struct ixxat_pci_intf_info_cmd_1_3 {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	struct ixxat_intf_info_1_3 info; /* device capabilities */
} __packed;

struct ixxat_pci_intf_info_cmd {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	struct ixxat_intf_info info; /* device capabilities */
} __packed;

struct ixxat_pci_intf_caps_cmd {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	struct ixxat_intf_caps intf_caps;
} __packed;

struct ixxat_pci_init_cmd {
	struct ixxat_pci_dal_req req;
	u8 mode; /* operation mode */
	u8 exmode; /* extended operation mode used for fd settings */
	struct ixxat_canbtp bt; /* arbitration bittiming */
	struct ixxat_canbtp btd; /* fast data bittiming */
	struct ixxat_pci_dal_res res;
} __packed;

struct ixxat_pci_start_cmd {
	struct ixxat_pci_dal_req req;
	struct ixxat_pci_dal_res res;
	__le32 time;
} __packed;

struct ixxat_pci_stop_cmd {
	struct ixxat_pci_dal_req req;
	__le32 action;
	struct ixxat_pci_dal_res res;
} __packed;

struct ixxat_pci_interface {
	/* virtual and real memory pointers */
	/* Interface PCI(e) registers 1 */
	phys_addr_t reg1add;
	void __iomem *reg1vadd;
	unsigned long reg1len;
	/* Interface PCI(e) registers 2 */
	phys_addr_t reg2add;
	void __iomem *reg2vadd;
	unsigned long reg2len;
	/* Interface PCI(e) memory */
	phys_addr_t memadd;
	void __iomem *memvadd;
	unsigned long memlen;
	/* coherent DMA memory */
	dma_addr_t dmaadd;
	void *dmavadd;
	unsigned long dmalen;
	/* additional special purpose memory */
	void *addmemvadd;
	unsigned long addmemlen;

	/* Device interrupt line*/
	unsigned int device_irq;

	/* device, firmware and bootmanager info */
	struct ixxat_intf_info dev_info;
	struct ixxat_intf_firmware_info2 fw_info;
	struct ixxat_intf_firmware_info2 bm_info;

	struct pci_dev *pdev;
	struct ixxat_pci_device *dev;

	u8 started_mask;
	u8 handle_data;

	struct ixxat_fifo	cmd_tx_fifo;
	struct ixxat_fifo	cmd_rx_fifo;
	struct ixxat_fifo	cmd_dbg_fifo;
	struct ixxat_fifo	tx_fifo[IXXAT_MAX_CANCTRL_COUNT];
	struct ixxat_fifo	rx_fifo[IXXAT_MAX_CANCTRL_COUNT];

	/* Ensure safe memory access while writing to the controller */
	struct mutex		cmd_lock;
};

/* ixxat pci adapter descriptor */
struct ixxat_pci_adapter {
	const u32 clock;
	const char *name;
	const struct can_bittiming_const *bt;
	const struct can_bittiming_const *btd;
	unsigned int ctrl_count;
	const u32 modes;

	int (*dev_start_xmit)(struct sk_buff *skb, struct net_device *netdev, u8 loopMode);
	int (*dev_init_ctrl)(struct ixxat_pci_device *dev);
	int (*handle_msg)(struct ixxat_pci_device *dev, void *ifi_base);

	int sizeof_dev_private;
};

struct ixxat_pci_device {
	struct can_priv can;
	struct ixxat_pci_adapter *adapter;
	struct pci_dev *pdev;
	struct net_device *netdev;
	struct ixxat_pci_interface *intf;

	u32 state;
	u16 ctrl_idx;

	struct ixxat_fifo *tx_fifo;
	struct ixxat_fifo *rx_fifo;

	u8 can_mode;
	u8 can_exmode;
	bool loopback;

	struct ixxat_pci_device *prev_dev;
	struct ixxat_pci_device *next_dev;

	struct ixxat_time_ref time_ref;

	struct napi_struct napi;
	u32 frn_read;
	u32 frn_write;

	/* Ensures mutual exclusion while reading/writing crit. system memory */
	spinlock_t rcv_lock;

	struct can_berr_counter bec;
};

extern struct ixxat_pci_adapter can_adapter;
extern struct ixxat_pci_adapter can_fd_adapter;

void ixxat_pci_setup_cmd(struct ixxat_pci_dal_req *req, u32 req_size,
			 struct ixxat_pci_dal_res *res, u32 res_size,
			 u32 req_code);

void ixxat_pci_write_altera_mailbox(struct ixxat_pci_interface *intf, u16 off, u32 val);
u32 ixxat_pci_read_pc_mailbox(struct ixxat_pci_interface *intf, u16 off);

int ixxat_pci_exec_cmd  (struct ixxat_pci_interface *intf,
			 struct ixxat_pci_dal_req *req,
			 struct ixxat_pci_dal_res *res);

int ixxat_pci_handle_frn(struct ixxat_pci_device *dev, u8 frn, u32 timestamp);

void ixxat_pci_get_ts_tv(struct ixxat_pci_device *dev, u32 ts, ktime_t *k_time);

#endif /* IXXAT_PCI_CORE_H_ */
