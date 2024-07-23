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

#define IFIFD_RXFIFO_DLC		0x0000000F
#define IFIFD_RXFIFO_RTR		BIT(4)
#define IFIFD_RXFIFO_EDL		BIT(5)
#define IFIFD_RXFIFO_BRS		BIT(6)
#define IFIFD_RXFIFO_ESI		BIT(7)
#define IFIFD_RXFIFO_FRN		0xFF000000

#define IFIFD_RXFIFO_IDSTD		0x000007FF
#define IFIFD_RXFIFO_IDEXT_18_28	0x000007FF
#define IFIFD_RXFIFO_IDEXT_00_17	0x1FFFF800
#define IFIFD_RXFIFO_IDE		BIT(29)

#define IFIFD_RXFIFO_DLC_SHIFT		0
#define IFIFD_RXFIFO_FRN_SHIFT		24
#define IFIFD_RXFIFO_IDSTD_SHIFT	0
#define IFIFD_TXFIFO_FRN_SHIFT		24

#define IFIFD_TXFIFO_DLC		0x0000000F
#define IFIFD_TXFIFO_RTR		BIT(4)
#define IFIFD_TXFIFO_EDL		BIT(5)
#define IFIFD_TXFIFO_BRS		BIT(6)
#define IFIFD_TXFIFO_FRN		0xFF000000

#define IFIFD_TXFIFO_IDSTD		0x000007FF
#define IFIFD_TXFIFO_IDE		BIT(29)

#define IFIFD_R2_WR_ADD_MSG		BIT(0)

#define IXXAT_IFIFD_FIFOCMD		0x00
#define IXXAT_IFIFD_TXSUSPEND		0x04
#define IXXAT_IFIFD_TXREPCOUNT		0x08
#define IXXAT_IFIFD_DLC			0x0c
#define IXXAT_IFIFD_ID			0x10
#define IXXAT_IFIFD_DATA		0x14

#define IXXAT_IFIFD_RXTIMESTAMP		0x08

#define IXXAT_PCI_FD_MODES (CAN_CTRLMODE_3_SAMPLES | \
			    CAN_CTRLMODE_ONE_SHOT | \
			    CAN_CTRLMODE_LISTENONLY | \
			    CAN_CTRLMODE_LOOPBACK | \
			    CAN_CTRLMODE_BERR_REPORTING | \
			    CAN_CTRLMODE_FD | \
			    CAN_CTRLMODE_FD_NON_ISO)

#define IXXAT_PCI2CAN_TSEG1_MIN		1
#define IXXAT_PCI2CAN_TSEG1_MAX		256
#define IXXAT_PCI2CAN_TSEG2_MIN		1
#define IXXAT_PCI2CAN_TSEG2_MAX		256
#define IXXAT_PCI2CAN_SJW_MAX		128
#define IXXAT_PCI2CAN_BRP_MIN		2
#define IXXAT_PCI2CAN_BRP_MAX		513
#define IXXAT_PCI2CAN_BRP_INC		1

#define IXXAT_PCI2CAN_TSEG1_MIN_DATA	1
#define IXXAT_PCI2CAN_TSEG1_MAX_DATA	256
#define IXXAT_PCI2CAN_TSEG2_MIN_DATA	1
#define IXXAT_PCI2CAN_TSEG2_MAX_DATA	256
#define IXXAT_PCI2CAN_SJW_MAX_DATA	128
#define IXXAT_PCI2CAN_BRP_MIN_DATA	2
#define IXXAT_PCI2CAN_BRP_MAX_DATA	513
#define IXXAT_PCI2CAN_BRP_INC_DATA	1


static const struct can_bittiming_const pci2canfd_bt = {
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

static const struct can_bittiming_const pci2canfd_btd = {
	.name = KBUILD_MODNAME,
	.tseg1_min = IXXAT_PCI2CAN_TSEG1_MIN_DATA,
	.tseg1_max = IXXAT_PCI2CAN_TSEG1_MAX_DATA,
	.tseg2_min = IXXAT_PCI2CAN_TSEG2_MIN_DATA,
	.tseg2_max = IXXAT_PCI2CAN_TSEG2_MAX_DATA,
	.sjw_max = IXXAT_PCI2CAN_SJW_MAX_DATA,
	.brp_min = IXXAT_PCI2CAN_BRP_MIN_DATA,
	.brp_max = IXXAT_PCI2CAN_BRP_MAX_DATA,
	.brp_inc = IXXAT_PCI2CAN_BRP_INC_DATA,
};

static int ixxat_pci_init_ctrl(struct ixxat_pci_device *dev)
{
	const struct can_bittiming *bt = &dev->can.bittiming;
	const struct can_bittiming *btd = &dev->can.data_bittiming;
	struct ixxat_pci_interface *intf = dev->intf;

	int err;
	u32 btmode = IXXAT_PCI_BTMODE_NAT;
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

	if (dev->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		btmode = IXXAT_PCI_BTMODE_TSM;
	if (dev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		opmode |= IXXAT_PCI_OPMODE_ERRFRAME;
	if (dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		opmode |= IXXAT_PCI_OPMODE_LISTONLY;

	if (dev->can.ctrlmode & (CAN_CTRLMODE_FD | CAN_CTRLMODE_FD_NON_ISO)) {
		exmode |= IXXAT_PCI_EXMODE_EXTDATA | IXXAT_PCI_EXMODE_FASTDATA;

		if (!(dev->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO))
			exmode |= IXXAT_PCI_EXMODE_ISOFD;
	}

	cmd->mode = opmode;
	cmd->exmode = exmode;
	cmd->bt.mode = cpu_to_le32(btmode);
	cmd->bt.bps = cpu_to_le32(bt->brp);
	cmd->bt.ts1 = cpu_to_le16(bt->prop_seg + bt->phase_seg1);
	cmd->bt.ts2 = cpu_to_le16(bt->phase_seg2);
	cmd->bt.sjw = cpu_to_le16(bt->sjw);
	cmd->btd.tdo = 0;

	if (exmode) {
		cmd->btd.mode = cpu_to_le32(btmode);
		cmd->btd.bps = cpu_to_le32(btd->brp);
		cmd->btd.ts1 = cpu_to_le16(btd->prop_seg + btd->phase_seg1);
		cmd->btd.ts2 = cpu_to_le16(btd->phase_seg2);
		cmd->btd.sjw = cpu_to_le16(btd->sjw);
		cmd->btd.tdo = cpu_to_le16(btd->brp * (btd->phase_seg1 + 1 +
							btd->prop_seg));
	}

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if ( err ) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree (cmd);
	}
	else
	{
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err) {
			dev_err(&intf->pdev->dev, "Error %d: Init ctrl failed\n", err);
		}
		else {
			err = le32_to_cpu(cmd->res.ret_code);
		}

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_handle_sr_canfdmsg(struct ixxat_pci_device *dev,
					void *base)
{
	u32 raw_dlc = *(u32 *)(base + IXXAT_IFIFD_DLC);
	u32 tstamp = *(u32 *)(base + IXXAT_IFIFD_RXTIMESTAMP);
	u8 frn = (raw_dlc & IFIFD_RXFIFO_FRN) >> IFIFD_RXFIFO_FRN_SHIFT;

	dev->netdev->stats.tx_packets++;
	dev->netdev->stats.tx_bytes += can_fd_dlc2len((raw_dlc & IFIFD_RXFIFO_DLC)
						  >> IFIFD_RXFIFO_DLC_SHIFT);

	return ixxat_pci_handle_frn(dev, frn, tstamp);
}

static int ixxat_pci_handle_canfdmsg(struct ixxat_pci_device *dev, void *base)
{
	struct canfd_frame *cf;
	struct sk_buff *skb = NULL;
	u32 raw_id = *(u32 *)(base + IXXAT_IFIFD_ID);
	u32 raw_dlc = *(u32 *)(base + IXXAT_IFIFD_DLC);
	u32 raw_tstamp = *(u32 *)(base + IXXAT_IFIFD_RXTIMESTAMP);
	u32 *data = NULL;
	int i, j;

	if ((raw_dlc & IFIFD_RXFIFO_EDL) || (raw_dlc & IFIFD_RXFIFO_BRS)) {
		skb = alloc_canfd_skb(dev->netdev, &cf);
	} else  {
		skb = alloc_can_skb(dev->netdev, (struct can_frame **) &cf);
	}
	if (!skb)
		return -ENOMEM;

	cf->len = can_fd_dlc2len((raw_dlc & IFIFD_RXFIFO_DLC) >> IFIFD_RXFIFO_DLC_SHIFT);

	/* Identifier Extension Flag */
	if (raw_id & IFIFD_RXFIFO_IDE) {
		cf->can_id |= CAN_EFF_FLAG;
		cf->can_id |= ((raw_id & IFIFD_RXFIFO_IDEXT_18_28) << 18)
			+ ((raw_id & IFIFD_RXFIFO_IDEXT_00_17) >> 11);
	} else {
		cf->can_id |= (raw_id & IFIFD_RXFIFO_IDSTD) >> IFIFD_RXFIFO_IDSTD_SHIFT;
	}

	/* Remote Transmission Request */
	if (raw_dlc & IFIFD_RXFIFO_RTR)
		cf->can_id |= CAN_RTR_FLAG;
	else {
		data = (u32 *)cf->data;
		for (i = 0, j = 0; i < cf->len; ++j, i += sizeof(u32))
			data[j] = *(u32 *)(base + IXXAT_IFIFD_DATA + i);
	}

	/* Bit Rate Switch */
	if (raw_dlc & IFIFD_RXFIFO_BRS)
		cf->flags |= CANFD_BRS;

	/* Error State Indicator */
	if (raw_dlc & IFIFD_RXFIFO_ESI)
		cf->flags |= CANFD_ESI;

	ixxat_pci_get_ts_tv(dev, raw_tstamp, &skb->tstamp);

	dev->netdev->stats.rx_packets++;
	dev->netdev->stats.rx_bytes += cf->len;
	netif_receive_skb(skb);

	return 1;
}

static int ixxat_pci_handle_ififd_msg(struct ixxat_pci_device *dev, void *base)
{
	int ret;
	u32 raw_dlc = *(u32 *)(base + IXXAT_IFIFD_DLC);

	if (raw_dlc & IFIFD_RXFIFO_FRN)
		ret = ixxat_pci_handle_sr_canfdmsg(dev, base);
	else
		ret = ixxat_pci_handle_canfdmsg(dev, base);

	if (ret < 0)
		netdev_err(dev->netdev, "Error %d: IFI-handling failed\n", ret);

	return ret;
}

static int ixxat_pci_start_xmit_fd(struct sk_buff *skb,
				   struct net_device *netdev,
				   u8 loopMode)
{
	struct ixxat_pci_device *dev = netdev_priv(netdev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct ixxat_fifo *fifo = dev->tx_fifo;
	void __iomem *dest;
	void __iomem *destdata;

	int i;
	bool selfReception = false;
	bool isloopback    = false;
	u32 can_id;
	u32 can_dlc = 0;

	u32 write_index = IX_FIFO_GET_WRITEIDX(fifo);
	u32 read_index = IX_FIFO_GET_READIDX(fifo);
	u32 obj_size = IX_FIFO_OBJSIZE(fifo);
	u32 num_obj = IX_FIFO_NUMOBJS(fifo);

	unsigned long spin_flags;
	u32 intCtrlMask;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;
#else
	if (can_dev_dropped_skb(netdev, skb))
		return NETDEV_TX_OK;
#endif

	if (!(dev->state & IXXAT_PCI_STATE_RUNNING)) {
		netif_stop_queue(netdev);
		return NETDEV_TX_OK;
	}

	if (dev->frn_write == dev->frn_read || write_index == read_index)
		return NETDEV_TX_BUSY;

	dest = IX_FIFO_GET_DATAPTR(fifo) + write_index * obj_size;
	iowrite32(IXXAT_PCI_MSG_TYPE_IFI, dest);
	dest += sizeof(u32);

	destdata = dest + IXXAT_IFIFD_DATA;

	if (cf->can_id & CAN_EFF_FLAG) {
		can_id = (((cf->can_id & IXXAT_PCI_SFF_ID) <<
			   IXXAT_PCI_SFF_SHIFT) +
			  ((cf->can_id & IXXAT_PCI_EFF_ID) >>
			   IXXAT_PCI_EFF_SHIFT)) | IFIFD_TXFIFO_IDE;
	} else {
		can_id = cf->can_id & IFIFD_TXFIFO_IDSTD;
	}

	if (cf->can_id & CAN_RTR_FLAG) {
		can_dlc |= IFIFD_TXFIFO_RTR;
	} else {
		can_dlc |= can_fd_len2dlc(cf->len) & IFIFD_TXFIFO_DLC;
		if (cf->len > 8)
			can_dlc |= IFIFD_TXFIFO_EDL;
	}

	if (dev->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		iowrite32(1, dest + IXXAT_IFIFD_TXREPCOUNT);
	else
		iowrite32(0, dest + IXXAT_IFIFD_TXREPCOUNT);

	if (!(cf->can_id & CAN_RTR_FLAG)) {
		if (cf->flags & CANFD_BRS)
			can_dlc |= IFIFD_TXFIFO_EDL | IFIFD_TXFIFO_BRS;

		for (i = 0; i < cf->len; i += sizeof(u32))
			iowrite32(le32_to_cpup((__le32 *)(cf->data + i)), destdata + i);
	}

	selfReception = ((loopMode & IX_LOOP_SELF_RX) == IX_LOOP_SELF_RX);
	if (selfReception) {

		can_dlc |= (dev->frn_write << IFIFD_TXFIFO_FRN_SHIFT) & IFIFD_TXFIFO_FRN;

		isloopback = ((loopMode & IX_LOOPBACK) == IX_LOOPBACK);
		if (isloopback) {
			spin_lock_irqsave(&dev->rcv_lock, spin_flags);

			// if there is already a echo skb registered -> free it
			if (dev->can.echo_skb[dev->frn_write - 1])
				can_free_echo_skb(dev->netdev, dev->frn_write - 1, NULL);

			can_put_echo_skb(skb, dev->netdev, dev->frn_write - 1, 0);

			dev->frn_write++;
			if (dev->frn_write > IXXAT_PCI_MAX_TX_TRANS)
				dev->frn_write = 1;

			spin_unlock_irqrestore(&dev->rcv_lock, spin_flags);
		}
		else {
			dev_kfree_skb(skb);
		}

	} else {
		netdev->stats.tx_bytes += cf->len;
		netdev->stats.tx_packets += 1;
	}

	iowrite32(can_id, dest + IXXAT_IFIFD_ID);
	iowrite32(can_dlc, dest + IXXAT_IFIFD_DLC);
	iowrite32(ioread32(dest + IXXAT_IFIFD_FIFOCMD) | IFIFD_R2_WR_ADD_MSG, dest + IXXAT_IFIFD_FIFOCMD);

	spin_lock_irqsave(&dev->rcv_lock, spin_flags);

	write_index = ((write_index + 1) % num_obj);
	IX_FIFO_SET_WRITEIDX(fifo, write_index);

	if (dev->frn_write == dev->frn_read ||
		IX_FIFO_GET_WRITEIDX(fifo) == IX_FIFO_GET_READIDX(fifo)) {
		netif_stop_queue(netdev);
	}

	spin_unlock_irqrestore(&dev->rcv_lock, spin_flags);

	intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
	ixxat_pci_write_altera_mailbox(dev->intf, (dev->ctrl_idx + 1), intCtrlMask);

	return NETDEV_TX_OK;
}

struct ixxat_pci_adapter can_fd_adapter = {
	.clock = IXXAT_PCI_CLOCK,
	.bt = &pci2canfd_bt,
	.btd = &pci2canfd_btd,
	.modes = IXXAT_PCI_FD_MODES,
	.sizeof_dev_private = sizeof(struct ixxat_pci_device),

	.dev_init_ctrl = ixxat_pci_init_ctrl,
	.dev_start_xmit = ixxat_pci_start_xmit_fd,
	.handle_msg = ixxat_pci_handle_ififd_msg,
};
