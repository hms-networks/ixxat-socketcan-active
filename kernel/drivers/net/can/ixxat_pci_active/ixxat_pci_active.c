// SPDX-License-Identifier: GPL-2.0

/* CAN driver for IXXAT PCI-to-CAN
 *
 * Copyright (C) 2018 HMS Industrial Networks <socketcan@hms-networks.de>
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

#include <linux/pci.h>
#include <linux/can/dev.h>
#include <linux/version.h>

#include "ixxat_pci_core.h"

#include "ixxat_kernel_adapt.h"


#if defined(CONFIG_TRACING) && defined(DEBUG)
	#define ix_trace_printk(...) trace_printk(__VA_ARGS__)
#else
	#define ix_trace_printk(...)
#endif

#define IFIREG_DLC 0
#define IFIREG_IDENTIFER 4
#define IFIREG_DATA14 8
#define IFIREG_TIMESTAMP 40

#define IFI_R0_RD_DLC 0x0000000F
#define IFI_R0_RD_RTR BIT(4)
#define IFI_R0_RD_FRN 0xFF000000

#define IFI_R0_DLC_S 0
#define IFI_R0_FRN_S 24
#define IFI_R1_IDSTD_S 0

#define IFI_R0_WR_DLC 0x0000000F
#define IFI_R0_WR_RTR BIT(4)
#define IFI_R0_WR_SSM BIT(23)

#define IFI_R1_IDSTD 0x000007FF
#define IFI_R1_IDEXT_18_28 0x000007FF
#define IFI_R1_IDEXT_00_17 0x1FFFF800
#define IFI_R1_IDE BIT(29)

#define IXXAT_PCI_MODES (CAN_CTRLMODE_3_SAMPLES | \
			 CAN_CTRLMODE_ONE_SHOT | \
			 CAN_CTRLMODE_LISTENONLY | \
			 CAN_CTRLMODE_LOOPBACK | \
			 CAN_CTRLMODE_BERR_REPORTING)

#define IXXAT_PCI2CAN_TSEG1_MIN 1
#define IXXAT_PCI2CAN_TSEG1_MAX 32
#define IXXAT_PCI2CAN_TSEG2_MIN 2
#define IXXAT_PCI2CAN_TSEG2_MAX 33
#define IXXAT_PCI2CAN_SJW_MAX 4
#define IXXAT_PCI2CAN_BRP_MIN 2
#define IXXAT_PCI2CAN_BRP_MAX 257
#define IXXAT_PCI2CAN_BRP_INC 1


static const struct can_bittiming_const pci2can_bt = {
	.name = KBUILD_MODNAME,
	.tseg1_min = IXXAT_PCI2CAN_TSEG1_MIN,
	.tseg1_max = IXXAT_PCI2CAN_TSEG1_MAX,
	.tseg2_min = IXXAT_PCI2CAN_TSEG2_MIN,
	.tseg2_max = IXXAT_PCI2CAN_TSEG2_MAX,
	.sjw_max = IXXAT_PCI2CAN_SJW_MAX,
	.brp_min = IXXAT_PCI2CAN_BRP_MIN,
	.brp_max = IXXAT_PCI2CAN_BRP_MAX,
	.brp_inc = IXXAT_PCI2CAN_BRP_INC,
};

static int ixxat_pci_init_ctrl(struct ixxat_pci_device *dev)
{
	const struct can_bittiming *bt = &dev->can.bittiming;
	struct ixxat_pci_interface *intf = dev->intf;

	int err;
	u8 opmode = IXXAT_PCI_OPMODE_EXTENDED | IXXAT_PCI_OPMODE_STANDARD;
	u8 exmode = 0;

	struct ixxat_pci_init_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 res_size = sizeof(cmd->res);
	const u32 req_size = cmd_size - res_size;
	const u32 req_code = IXXAT_PCI_CMD_INIT_V2;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);
	cmd->req.port = cpu_to_le16(dev->ctrl_idx);

	if (dev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		opmode |= IXXAT_PCI_OPMODE_ERRFRAME;
	if (dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		opmode |= IXXAT_PCI_OPMODE_LISTONLY;

	cmd->mode = opmode;
	cmd->exmode = exmode;
	cmd->bt.mode = cpu_to_le32(IXXAT_PCI_BTMODE_NAT);
	cmd->bt.bps = cpu_to_le32(bt->brp);
	cmd->bt.ts1 = cpu_to_le16(bt->prop_seg + bt->phase_seg1);
	cmd->bt.ts2 = cpu_to_le16(bt->phase_seg2);
	cmd->bt.sjw = cpu_to_le16(bt->sjw);

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err)
			dev_err(&intf->pdev->dev, "Error %d: Init ctrl failed\n", err);
		else
			err = le32_to_cpu(cmd->res.ret_code);

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_handle_sr_canmsg(struct ixxat_pci_device *dev, void *base)
{
	u32 reg_dlc = *(u32 *)(base + IFIREG_DLC);
	u32 tstamp = *(u32 *)(base + IFIREG_TIMESTAMP);
	u8 frn = (reg_dlc & IFI_R0_RD_FRN) >> IFI_R0_FRN_S;

	dev->netdev->stats.tx_packets++;
	dev->netdev->stats.tx_bytes += can_cc_dlc2len(reg_dlc & IFI_R0_RD_DLC);

	return ixxat_pci_handle_frn(dev, frn, tstamp);
}

static int ixxat_pci_handle_canmsg(struct ixxat_pci_device *dev, void *base)
{
	struct can_frame *cf;
	struct sk_buff *skb = alloc_can_skb(dev->netdev, &cf);
	u32 raw_id = *(u32 *)(base + IFIREG_IDENTIFER);
	u32 raw_dlc = *(u32 *)(base + IFIREG_DLC);
	u32 raw_tstamp = *(u32 *)(base + IFIREG_TIMESTAMP);
	u32 *data = (u32 *)cf->data;
	int i, j;

	if (!skb)
		return -ENOMEM;

	cf->can_dlc = (raw_dlc & IFI_R0_RD_DLC) >> IFI_R0_DLC_S;

	if (raw_id & IFI_R1_IDE) {
		cf->can_id |= CAN_EFF_FLAG;
		cf->can_id |= ((raw_id & IFI_R1_IDEXT_18_28) << 18)
			+ ((raw_id & IFI_R1_IDEXT_00_17) >> 11);
	} else {
		cf->can_id |= (raw_id & IFI_R1_IDSTD) >> IFI_R1_IDSTD_S;
	}

	if (raw_dlc & IFI_R0_RD_RTR)
		cf->can_id |= CAN_RTR_FLAG;
	else
		for (i = 0, j = 0; i < cf->can_dlc; ++j, i += sizeof(u32))
			data[j] = *(u32 *)(base + IFIREG_DATA14 + i);

	ixxat_pci_get_ts_tv(dev, raw_tstamp, &skb->tstamp);

	dev->netdev->stats.rx_packets++;
	dev->netdev->stats.rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);

	return 1;
}

static int ixxat_pci_handle_ifi_msg(struct ixxat_pci_device *dev, void *base)
{
	int ret;
	u32 raw_dlc = *(u32 *)(base + IFIREG_DLC);

	if (raw_dlc & IFI_R0_RD_FRN)
		ret = ixxat_pci_handle_sr_canmsg(dev, base);
	else
		ret = ixxat_pci_handle_canmsg(dev, base);

	if (ret < 0)
		netdev_err(dev->netdev, "Error %d: IFI-handling failed\n", ret);

	return ret;
}

static int ixxat_pci_start_xmit(struct sk_buff *skb, struct net_device *netdev, u8	loopMode)
{
	struct ixxat_pci_device *dev = netdev_priv(netdev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	void __iomem *fifo = dev->tx_fifo;
	void __iomem *fifo_data;
	void __iomem *data;
	int i;
	bool selfReception = false;
	bool echoSkb       = false;
	u32 can_id;
	u32 can_dlc = 0;
	volatile u32 write_index = ioread32(fifo + IXXAT_PCI_RES_WRITE_IDX);
	volatile u32 read_index = ioread32(fifo + IXXAT_PCI_RES_READ_IDX);
	volatile u32 obj_size = ioread32(fifo + IXXAT_PCI_RES_OBJ_SIZE);
	volatile u32 obj_num = ioread32(fifo + IXXAT_PCI_RES_NUM_OBJ);
	unsigned long spin_flags;
	u32 intCtrlMask;
	bool fSend = true;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	if (!(dev->state & IXXAT_PCI_STATE_RUNNING)) {
		netif_stop_queue(netdev);
		return NETDEV_TX_OK;
	}

	if (dev->frn_write == dev->frn_read || write_index == read_index)
		/* should not occur except during restart */
		return NETDEV_TX_BUSY;

	fifo_data = fifo + IXXAT_PCI_RES_DATA + write_index * obj_size;
	iowrite32(IXXAT_PCI_MSG_TYPE_IFI, fifo_data);
	fifo_data += sizeof(u32);

	data = fifo_data + IFIREG_DATA14;

	if (cf->can_id & CAN_EFF_FLAG)
		can_id = (((cf->can_id & IXXAT_PCI_SFF_ID) <<
			   IXXAT_PCI_SFF_SHIFT) +
			  ((cf->can_id & IXXAT_PCI_EFF_ID) >>
			   IXXAT_PCI_EFF_SHIFT)) | IFI_R1_IDE;
	else {
		if ( cf->can_id == 0x800) {

#if defined(CONFIG_TRACING) && defined(DEBUG)
			volatile u32 regval = ioread32(dev->intf->reg1vadd + PCIE_ALTERALCR_A2P_INTENA);
			volatile u32 intSR  = ioread32(dev->intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);
			ix_trace_printk (" ena:%x sr:%x\n",regval, intSR);
#endif
			fSend = false;
		}
		else if ( cf->can_id == 0x801) {
			volatile u32 intSR  = ioread32(dev->intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);
			iowrite32(intSR, dev->intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);
			ix_trace_printk (" reset Status sr:%x\n", intSR);
			fSend = false;
		}
		else if ( cf->can_id == 0x802) {
			iowrite32(0xFFFFFFFF, dev->intf->reg1vadd + PCIE_ALTERALCR_A2P_INTENA);
			ix_trace_printk (" set all ints enabled\n");
			fSend = false;
		}


		can_id = cf->can_id & IFI_R1_IDSTD;
	}

	if ( fSend ){

		can_dlc = cf->can_dlc & IFI_R0_WR_DLC;

		if (cf->can_id & CAN_RTR_FLAG) {
			can_dlc |= IFI_R0_WR_RTR;  //Fix - RTR
		}

		if (dev->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
			can_dlc |= IFI_R0_WR_SSM;

		if (!(cf->can_id & CAN_RTR_FLAG)) {
			for (i = 0; i < cf->can_dlc; i += sizeof(u32))
				iowrite32(le32_to_cpup((__le32 *)(cf->data + i)),
					data + i);
		}

		if ((loopMode & IX_LOOP_SELF_RX) == IX_LOOP_SELF_RX) {

			selfReception = true; // set self reception

			if ((loopMode & IX_LOOPBACK) == IX_LOOPBACK) {
				echoSkb = true;
			}
		} else {
			netdev->stats.tx_bytes += cf->can_dlc;
			netdev->stats.tx_packets += 1;
		}

		if ( selfReception ) {
			can_dlc |= (dev->frn_write << IFI_R0_FRN_S) & IFI_R0_RD_FRN;
		}

		if ( echoSkb) {
			spin_lock_irqsave(&dev->rcv_lock, spin_flags);
			if (dev->can.echo_skb[dev->frn_write - 1])
				can_free_echo_skb(dev->netdev, dev->frn_write - 1, NULL);

			can_put_echo_skb(skb, dev->netdev, dev->frn_write - 1, 0);

			dev->frn_write++;
			if (dev->frn_write > IXXAT_PCI_MAX_TX_TRANS)
				dev->frn_write = 1;

			spin_unlock_irqrestore(&dev->rcv_lock, spin_flags);
		}
		else {
			kfree_skb(skb);
		}

		iowrite32(can_id , fifo_data + IFIREG_IDENTIFER);
		iowrite32(can_dlc, fifo_data + IFIREG_DLC);

		spin_lock_irqsave(&dev->rcv_lock, spin_flags);
		iowrite32 (((write_index + 1) % obj_num), fifo + IXXAT_PCI_RES_WRITE_IDX);

		if (dev->frn_write == dev->frn_read ||
			ioread32(fifo + IXXAT_PCI_RES_WRITE_IDX) ==
				ioread32(fifo + IXXAT_PCI_RES_READ_IDX))
			netif_stop_queue(netdev);

		spin_unlock_irqrestore(&dev->rcv_lock, spin_flags);

		intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
		// 0x00020000 -> ctrl 0
		ixxat_pci_setup_altera_mailbox(dev->intf, (dev->ctrl_idx + 1), intCtrlMask);
	}


	return NETDEV_TX_OK;
}

struct ixxat_pci_adapter can_adapter = {
	.name = KBUILD_MODNAME, //"CAN2PCI",
	.clock = IXXAT_PCI_CLOCK,
	.bt = &pci2can_bt,
	.modes = IXXAT_PCI_MODES,
	.sizeof_dev_private = sizeof(struct ixxat_pci_device),

	.dev_init_ctrl = ixxat_pci_init_ctrl,
	.dev_start_xmit = ixxat_pci_start_xmit,
	.handle_msg = ixxat_pci_handle_ifi_msg,
};
