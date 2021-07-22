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

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/can/dev.h>
#include <linux/kthread.h>
#include <linux/firmware.h>

#include "ixxat_pci_core.h"

MODULE_AUTHOR("HMS Technology Center Ravensburg Gmbh <socketcan@hms-networks.de>");
MODULE_DESCRIPTION("SocketCAN driver for HMS Ixxat IB2xx, IB4xx, IB6xx, IB810 boards");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("2.0.377-REL");

#define IX_STATISTICS_EXACT 0

#if defined(CONFIG_TRACING) && defined(DEBUG)
	#define ix_trace_printk(...) trace_printk(__VA_ARGS__)
#else
	#define ix_trace_printk(...)
#endif

static struct pci_device_id ixxat_pci_table[] = {
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB200_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB210_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB400_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB410_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB230_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB600_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB610_PRODUCT_ID) },
//	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB640_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB800_PRODUCT_ID) },
	{ PCI_DEVICE(IXXAT_PCI_VENDOR_ID, CAN_IB810_PRODUCT_ID) },
	{ },
};

MODULE_DEVICE_TABLE(pci, ixxat_pci_table);


#ifdef DEBUG
	static int Showdump ( u8 * pbdata, int length) 
	{
		char caDump[0x100] = "Dump: ";
		char szBuf[10]="";
		int i =0;
		int j =0;
		u16 len = 0; 

		while ( length > 0 ) {

			len = (length > 16)? 16 : length;
			
			sprintf (caDump, "%i:", j);
			for ( i =0; i< len; i++)
			{
				sprintf (szBuf, "%02x ", pbdata[i+j]);
				strcat (caDump, szBuf);
			}
			strcat (caDump, "\n");
			ix_trace_printk ( caDump );

			length -= len;
			j+=len;
		}

		return 0;
	}
  
#endif

static int ixxat_pci_send_cmd(void __iomem *tx_fifo,
			      struct ixxat_pci_dal_req *req,
			      struct ixxat_pci_dal_res *res)
{
	void __iomem *fifo;
	u32 write_index = ioread32(tx_fifo + IXXAT_PCI_RES_WRITE_IDX);
	u32 obj_size = ioread32(tx_fifo + IXXAT_PCI_RES_OBJ_SIZE);
	u32 data_off = write_index * obj_size;
	u32 size = le32_to_cpu(req->size) + sizeof(struct ixxat_pci_dal_res);

	if (ioread32(tx_fifo + IXXAT_PCI_RES_WRITE_IDX) ==
	    ioread32(tx_fifo + IXXAT_PCI_RES_READ_IDX))
		return -ENOBUFS;

	if (ioread32(tx_fifo + IXXAT_PCI_RES_DIR) != IXXAT_PCI_RESDIR_HTOD)
		return -EBADSLT;

	fifo = tx_fifo + IXXAT_PCI_RES_DATA + data_off;
	memcpy_toio(fifo, &size, sizeof(u32));

	fifo = tx_fifo + IXXAT_PCI_RES_DATA + data_off + sizeof(u32);
	memcpy_toio(fifo, req, le32_to_cpu(req->size));

	fifo = fifo + le32_to_cpu(req->size);
	memcpy_toio(fifo, res, sizeof(struct ixxat_pci_dal_res));

	iowrite32((write_index + 1) % ioread32(tx_fifo + IXXAT_PCI_RES_NUM_OBJ),
		  tx_fifo + IXXAT_PCI_RES_WRITE_IDX);

	return 0;
}

static int ixxat_pci_rcv_cmd(struct ixxat_pci_interface *intf,
			     void __iomem *rx_fifo,
			     struct ixxat_pci_dal_req *req,
			     struct ixxat_pci_dal_res *res)
{
	void __iomem *data;
	volatile u32 read_index  = ioread32(rx_fifo + IXXAT_PCI_RES_READ_IDX);
	volatile u32 write_index = ioread32(rx_fifo + IXXAT_PCI_RES_WRITE_IDX);
	ktime_t start, end;
	size_t req_size = sizeof(struct ixxat_pci_dal_req);
	u32 res_size;
	u32 data_off;
	u32 req_code;
	u16 req_port;

	if (ioread32(rx_fifo + IXXAT_PCI_RES_DIR) != IXXAT_PCI_RESDIR_DTOH)
		return -EBADSLT;

	start = ktime_get_real();
	end = start;

	while ((ktime_to_ns(end) - ktime_to_ns(start)) < IXXAT_PCI_CMD_TIMEOUT_NS) {
		if (++read_index == ioread32(rx_fifo + IXXAT_PCI_RES_NUM_OBJ))
			read_index = 0;

		if (write_index == ioread32(rx_fifo + IXXAT_PCI_RES_NUM_OBJ))
			write_index = 0;

		// sleep for 10 µsec and wait for data from the interface card
		if (read_index == write_index) {
			read_index = ioread32(rx_fifo + IXXAT_PCI_RES_READ_IDX);
			write_index = ioread32(rx_fifo + IXXAT_PCI_RES_WRITE_IDX);
			usleep_range(9, 10);
			goto cmd_continue;
		}

		data_off = read_index * ioread32(rx_fifo + IXXAT_PCI_RES_OBJ_SIZE);
		data = rx_fifo + IXXAT_PCI_RES_DATA + data_off;
		iowrite32(read_index, rx_fifo + IXXAT_PCI_RES_READ_IDX);

		res_size = ioread32(data);

		if (res_size != (le32_to_cpu(res->res_size) + req_size)) {
			dev_err(&intf->pdev->dev, "Error: Invalid cmd size!");
			goto cmd_continue;
		}

		req_port = ioread16(data + sizeof(u32) + sizeof(req->size));
		req_code = ioread32(data + sizeof(u32) + sizeof(req->size)
				    + sizeof(req->port) + sizeof(req->socket));

		if (req_code != le32_to_cpu(req->code)) {
			dev_err(&intf->pdev->dev, "Error: Invalid cmd code!");
			goto cmd_continue;
		}

		if (req_port != le16_to_cpu(req->port)) {
			dev_err(&intf->pdev->dev,
				"Error: Invalid cmd port index!");
			goto cmd_continue;
		}

		memcpy_fromio(res, data + sizeof(u32) + req_size,
			      le32_to_cpu(res->res_size));

		ix_trace_printk ("Req:%x ResSize %i, RetSize %i, Retcode %i \n", 
			req->code,
			res->res_size, res->ret_size, res->ret_code);
		if (res->ret_code) 
			dev_err(&intf->pdev->dev,
				"Error %x: Receiving command failure",
				res->ret_code);

		return le32_to_cpu(res->ret_code);

cmd_continue:
		end = ktime_get_real();
	}

	return -ENODATA;
}

void ixxat_pci_setup_altera_mailbox(struct ixxat_pci_interface *intf, u16 off, u32 val )
{
	void __iomem *mbx = intf->reg1vadd + IXXAT_PCI_ALTERA_MBX_OFF;
	iowrite32( val, mbx + off * sizeof(u32));
}

void ixxat_pci_setup_cmd(struct ixxat_pci_dal_req *req, u32 req_size,
			 struct ixxat_pci_dal_res *res, u32 res_size,
			 u32 req_code)
{
	req->size = cpu_to_le32(req_size);
	req->port = cpu_to_le16(0xffff);
	req->socket = cpu_to_le16(0xffff);
	req->code = cpu_to_le32(req_code);

	res->res_size = cpu_to_le32(res_size);
	res->ret_size = cpu_to_le32(0);
	res->ret_code = cpu_to_le32(0xffffffff);
}

int ixxat_pci_handle_cmd(struct ixxat_pci_interface *intf,
			 struct ixxat_pci_dal_req *req,
			 struct ixxat_pci_dal_res *res)
{
	int err;

	err = ixxat_pci_send_cmd(intf->cmd_tx_fifo, req, res);
	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Send cmd failed", err);
		goto fail;
	}

	ixxat_pci_setup_altera_mailbox(intf, 0, 0xFFFFFFFF); // write to mailbox 0

	err = ixxat_pci_rcv_cmd(intf, intf->cmd_rx_fifo, req, res);
	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Receive cmd failed", err);
		goto fail;
	}

fail:
	return err;
}

static void ixxat_pci_update_ts_now(struct ixxat_pci_device *dev, u32 ts_now)
{
	u32 *ts_dev = &dev->time_ref.ts_dev_0;
	ktime_t *kt_host = &dev->time_ref.kt_host_0;
	u64 timebase = (u64)0x00000000FFFFFFFF - (u64)(*ts_dev) + (u64)ts_now;

	*kt_host = ktime_add_us(*kt_host, timebase);
	*ts_dev = ts_now;
}

void ixxat_pci_get_ts_tv(struct ixxat_pci_device *dev, u32 ts, ktime_t *k_time)
{
	ktime_t tmp_time = dev->time_ref.kt_host_0;

	if (ts < dev->time_ref.ts_dev_last)
		ixxat_pci_update_ts_now(dev, ts);

	dev->time_ref.ts_dev_last = ts;
	tmp_time = ktime_add_us(tmp_time, ts - dev->time_ref.ts_dev_0);

	if (k_time)
		*k_time = tmp_time;
}

static void ixxat_pci_set_ts_now(struct ixxat_pci_device *dev, u32 ts_now)
{
	dev->time_ref.ts_dev_0 = ts_now;
	dev->time_ref.kt_host_0 = ktime_get_real();
	dev->time_ref.ts_dev_last = ts_now;
}

static int ixxat_pci_int_ena_req(struct ixxat_pci_interface *intf,
				 u8 mode, u32 IntlCtrlMask)
{
	volatile u32 regval = ioread32(intf->reg1vadd + PCIE_ALTERALCR_A2P_INTENA);
	volatile u32 intSR  = ioread32(intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);
	volatile u32 dest   = 0;

	ix_trace_printk (">> ixxat_pci_int_ena_req mask %x, mode %i \n", IntlCtrlMask, mode);	

	switch (mode) {
		case 0: // disable mask
			dest = regval & ~IntlCtrlMask;
			break;
		case 1: // enable mask
			dest = regval | IntlCtrlMask;
			break;
		default:
		case 0xFF: 
			dest = IntlCtrlMask;
			break;
	}

	iowrite32(dest, intf->reg1vadd + PCIE_ALTERALCR_A2P_INTENA);

	ix_trace_printk ("<< ixxat_pci_int_ena_req ena %x (%x) , sr:%x\n", 
			regval, dest, intSR);			  

	return (0 != (regval & IntlCtrlMask));
}

static void ixxat_pci_int_clr_req(struct ixxat_pci_interface *intf,
                  u32 regval)
{
//	u32 regval = ioread32(intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);
	iowrite32(regval, intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);
}

static int ixxat_pci_int_get_stat(struct ixxat_pci_interface *intf)
{
	u32 regval = ioread32(intf->reg1vadd + PCIE_ALTERA_LCR_INTCSR);

	return (regval & IXXAT_PCI_STAT_MASK);
}

static irqreturn_t ixxat_pci_irq_handler(int irq, void *dev_id)
{
	u8 enable;
	u32 intCtrlMask;			

	struct ixxat_pci_device *dev;
	struct ixxat_pci_interface *intf = (struct ixxat_pci_interface *)dev_id;

	// is a Interrupt from act ctrl available 
	volatile u32 intSR   = ixxat_pci_int_get_stat(intf);
	if (0 == (intSR & IXXAT_PCI_ENABLE_INT))
		return IRQ_NONE;

	ix_trace_printk (">> ixxat_pci_irq_handler stat %x \n", intSR);			
	
	// disable all interrupts! 
	enable = ixxat_pci_int_ena_req(intf, 0, IXXAT_PCI_ENABLE_INT);
	ixxat_pci_int_clr_req(intf, intSR);
/*
	if (!enable) {
		enable = ixxat_pci_int_ena_req(intf, 1, intCtrlMask);
		return IRQ_HANDLED;
	}
*/	

	for (dev = intf->dev; dev; dev = dev->next_dev) {
		if (!(dev->state & IXXAT_PCI_STATE_RUNNING))
			continue;

		intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));			

		if ( intSR & intCtrlMask) {
			dev->intf->handle_data |= (1 << dev->ctrl_idx);
			napi_schedule(&dev->napi);
		}
		else {
			ixxat_pci_int_ena_req(intf, 1, intCtrlMask);	
		}
	}

//	if ((intf->handle_data & intf->started_mask) != intf->started_mask)
//		ixxat_pci_int_ena_req(intf, 1, intCtrlMask);
	ix_trace_printk ("<< ixxat_pci_irq_handler \n");			

	return IRQ_HANDLED;
}

static int ixxat_pci_register_interrupts(struct pci_dev *pdev,
					 struct ixxat_pci_interface *intf)
{
	int ret = 0;
	ix_trace_printk (">> ixxat_pci_register_interrupts \n");

	ixxat_pci_int_ena_req(intf, 0xFF, 0);

	ret = pci_alloc_irq_vectors(pdev, 1, 1,
					PCI_IRQ_MSI | PCI_IRQ_LEGACY);
	if (ret < 0)
		dev_err(&pdev->dev,
			 "Error %d: Failed to allocate IRQ-vector\n", ret);

	ret = devm_request_irq(&pdev->dev, pdev->irq, ixxat_pci_irq_handler,
			       IRQF_SHARED, dev_name(&pdev->dev), intf);

	if (ret < 0) {
		dev_err(&pdev->dev,
			"Error %d: Failed to request IRQ\n", ret);
		return ret;
	}

	intf->device_irq = pdev->irq;

	ix_trace_printk ("<< ixxat_pci_register_interrupts \n");
	return 0;
}

static int ixxat_pci_register_dev(struct pci_dev *pdev,
				  struct ixxat_pci_interface *intf)
{
	int err = -ENOMEM;

	ix_trace_printk (">> ixxat_pci_register_dev \n");

	pci_set_master(pdev);

	intf->dev = NULL;

	intf->reg1add = pci_resource_start(pdev, 0);
	intf->reg1len = pci_resource_len(pdev, 0);

	intf->memadd = pci_resource_start(pdev, 2);
	intf->memlen = pci_resource_len(pdev, 2);

	request_mem_region(intf->reg1add, intf->reg1len, "IXXAT PCI Registers");
	intf->reg1vadd = ioremap(intf->reg1add, intf->reg1len);
	if (!intf->reg1vadd) {
		dev_err(&pdev->dev, "Error: reg1vadd ioremap_nocache failed\n");
		goto release_reg1;
	}

	request_mem_region(intf->memadd, intf->memlen, "IXXAT PCI Memory");
	intf->memvadd = ioremap(intf->memadd, intf->memlen);
	if (!intf->memvadd) {
		dev_err(&pdev->dev, "Error: memvadd ioremap_nocache failed\n");
		goto release_memreg;
	}

	intf->addmemlen = IXXAT_PCI_ADDMEM_LEN;
	intf->dmalen = IXXAT_PCI_DMA_LEN;
	
	intf->dmavadd = pci_alloc_consistent(pdev, intf->dmalen, &intf->dmaadd);
	
	if (!intf->dmavadd) {
		dev_err(&pdev->dev, "Error: Allocating dmavadd failed\n");
		goto release_dma;
	} else {
		memset(intf->dmavadd, 0, intf->dmalen);
	}

	intf->addmemvadd = kzalloc(intf->addmemlen, GFP_KERNEL);
	if (!intf->addmemvadd)
		goto release_addmem;

	err = ixxat_pci_register_interrupts(pdev, intf);
	if (err) {
		dev_err(&pdev->dev, "Error %x: Init interrupts failed\n", err);
		goto release_irq;
	}

	ix_trace_printk ("<< ixxat_pci_register_dev \n");
	return 0;

release_irq:
	kfree(intf->addmemvadd);
	pci_free_irq_vectors(intf->pdev);
	intf->addmemvadd = NULL;
release_addmem:
	pci_free_consistent(pdev, intf->dmalen, intf->dmavadd, intf->dmaadd);
release_dma:
	iounmap(intf->memvadd);
	release_mem_region(intf->memadd, intf->memlen);
release_memreg:
	iounmap(intf->reg1vadd);
	release_mem_region(intf->reg1add, intf->reg1len);
release_reg1:

	ix_trace_printk ("<< ixxat_pci_register_dev \n");
	return err;
}

static int ixxat_pci_test_cmd(struct ixxat_pci_interface *intf)
{
	int i, err;
	struct ixxat_pci_loopback_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 res_size = sizeof(cmd->res) + sizeof(cmd->res_data);
	const u32 req_size = cmd_size - res_size;
	const u32 req_code = IXXAT_PCI_CMD_LOOPBACK;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);

	for (i = 0; i < IXXAT_PCI_CMD_LB_SIZE; i++)
		cmd->req_data[i] = i + 1;

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err) {
		} else if (le32_to_cpu(cmd->res.ret_size) == req_size) {
			for (i = 0; i < IXXAT_PCI_CMD_LB_SIZE; i++)
				if (cmd->req_data[i] != cmd->res_data[i])
					err = -EINVAL;
		} else {
			err = -EINVAL;
		}

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_test_dma(struct ixxat_pci_interface *intf)
{
	int err;
	struct ixxat_pci_read_req *req;
	struct ixxat_pci_read_res *res;
	const u32 res_size = sizeof(res->dal_res)
		+ sizeof(IXXAT_PCI_DMA_TEST_CONTENT);
	const u32 req_size = sizeof(*req);
	const u32 req_code = IXXAT_PCI_CMD_READ_BLOCK;

	req = kmalloc(req_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	sprintf((char *)intf->dmavadd, IXXAT_PCI_DMA_TEST_CONTENT);

	res = kmalloc(res_size, GFP_KERNEL);
	if (!res) {
		kfree(req);
		return -ENOMEM;
	}

	ixxat_pci_setup_cmd(&req->dal_req, req_size, &res->dal_res,
			    res_size, req_code);
	req->addr = cpu_to_le32(IXXAT_PCI_DMA_TEST_ADDR);

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(req);
		kfree(res);
	} else {
		err = ixxat_pci_handle_cmd(intf, &req->dal_req, &res->dal_res);
		if (err) {
		} else {
			err = memcmp(intf->dmavadd, res->data,
					 sizeof(IXXAT_PCI_DMA_TEST_CONTENT));
		}

		kfree(req);
		kfree(res);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_upload_fw(struct pci_dev *pdev,
			       struct ixxat_pci_interface *intf)
{
	const struct firmware *fw;
	int i, err;
	struct ixxat_pci_write_res *res;
	struct ixxat_pci_write_req *req = NULL;
	struct ixxat_pci_fwHdr * hdr = NULL;
	const u32 res_size = sizeof(*res);
	u32 req_size;
	const u32 req_code = IXXAT_PCI_CMD_WRITE_BLOCK;
	void * pFw;
	u32 loopCnt = 0;

	res = kmalloc(res_size, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	err = request_firmware(&fw, IXXAT_FIRMWARE, &pdev->dev);
	if (err) {
		dev_err(&intf->pdev->dev, "Error %d: Request fw failed", err);
	} else {
		pFw = (void *) fw->data;
		hdr = (struct ixxat_pci_fwHdr *) (fw->data);
		ix_trace_printk ("%s, typ:%i, maxlen %i\n", hdr->caIdent, hdr->type, hdr->maxlen );
		pFw += sizeof (struct ixxat_pci_fwHdr);
			
		for (i = 0; i < fw->size;) {
			struct ixxat_pci_fw *fwb = (struct ixxat_pci_fw *) (pFw + i);
			u32 add = fwb->add; //be32_to_cpu(fwb->add);
			u16 len = fwb->len;//be16_to_cpu(fwb->len);
			u8 *data = fwb->data;
			
			if ((add==0)&&(len==0))
				break;

			ix_trace_printk ("adr %08x, len:%04x, data %02x %02x \n", add, len, data[0], data[1] );

			req_size = len + sizeof(req->dal_req)
					+ sizeof(req->addr);

			req = kmalloc(req_size, GFP_KERNEL);
			if (!req) {
				err = -ENOMEM;
				break;
			}

			ixxat_pci_setup_cmd(&req->dal_req, req_size,
						&res->dal_res, res_size, req_code);
			req->addr = cpu_to_le32(add);

			if (req_size > IXXAT_PCI_CMD_MAX_SIZE) {
				err = -ENOBUFS;
				kfree(req);
				break;
			}

			err = mutex_lock_interruptible(&intf->cmd_lock);

			if (err) {
				dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
				kfree(req);
				break;
			}

			memcpy(req->data, data, len);

			err = ixxat_pci_handle_cmd(intf, &req->dal_req, &res->dal_res);
			if (err) {
				dev_err(&intf->pdev->dev,
					"Error %d: Upload fw failed %d\n", err, i);

				kfree(req);
				req = NULL;
				mutex_unlock(&intf->cmd_lock);
				break;
			}

			kfree(req);
			req = NULL;
			mutex_unlock(&intf->cmd_lock);

//			i = i + sizeof(add) + sizeof(len) + len + sizeof(reserved);
//			i = i + sizeof(add) + sizeof(len) + len;
			i = i + sizeof(add) + sizeof(len) + hdr->maxlen;

			if ( ++ loopCnt > 10000 ) {
				ix_trace_printk ("leave Fw download Pos:%i, size:%li \n", i, fw->size);
			}
		}

		release_firmware(fw);
	}

	kfree(res);

	return err;
}

static int ixxat_pci_start_fw(struct ixxat_pci_interface *intf)
{
	int err;
	struct ixxat_pci_start_fw_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 res_size = sizeof(cmd->res);
	const u32 req_size = cmd_size - res_size;
	const u32 req_code = IXXAT_PCI_CMD_STARTFIRMWARE;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);
	cmd->reserved = 0;

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err)
			dev_err(&intf->pdev->dev, "Error %d: Start fw failed\n", err);


		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_AdrTableSize(struct ixxat_pci_interface *intf, u32 * mem_len)
{
	int err;
	struct ixxat_pci_adrtable_size_cmd *cmd;

	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 res_size = cmd_size - req_size;
	const u32 req_code = IXXAT_PCI_CMD_ADRTABLE_SIZE;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err)
			dev_err(&intf->pdev->dev, "Error %d: query adr table size failed\n", err);
		else {
			if ( mem_len)
				*mem_len = cmd->mem_size;
		}

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_AdrTable_Establish (struct ixxat_pci_interface *intf, u32 mem_offset, u32 mem_len)
{
	int err;
	struct ixxat_pci_adrtable_establish_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 res_size = sizeof(cmd->res);
	const u32 req_size = cmd_size - res_size;
	const u32 req_code = IXXAT_PCI_CMD_ADRTABLE_ESTABLISH;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);
//	cmd->reserved = 0;

	err = mutex_lock_interruptible(&intf->cmd_lock);

	cmd->mem_offset = mem_offset;
	cmd->mem_len = mem_len;
	cmd->msi_offset = 0;
	cmd->msi_len = 0;
	cmd->msi_num = 0;

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err)
			dev_err(&intf->pdev->dev, "Error %d: query adr table size failed\n", err);


		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static void ixxat_pci_dma_get_info(struct ixxat_pci_interface *intf,
				   u32 *page_sz, u16 *page_cnt)
{
	u16 entry;
	u16 entry_size = sizeof(u32) * 2;
	u32 adr_start = IXXAT_PCI_ALTERA_ADRTRANSSTART_OFF;
	u32 adr_end = IXXAT_PCI_ALTERA_ADRTRANSEND_OFF - entry_size;
	u16 max_entry_num = (u16)((adr_end - adr_start) / entry_size);
	u32 val;
	void __iomem *adr_trans;
	void __iomem *adr_trans_first;

	adr_trans_first = intf->reg1vadd + adr_start;

	iowrite32(IXXAT_PCI_DMA_ADD_LOW, adr_trans_first);
	iowrite32(IXXAT_PCI_DMA_ADD_HIGH, adr_trans_first + sizeof(u32));

	val = ioread32(adr_trans_first);
	*page_sz = ~val + 1;

	*page_cnt = max_entry_num;
	for (entry = 1; entry < max_entry_num; entry++) {
		adr_trans = intf->reg1vadd + adr_start + entry * entry_size;

		if (ioread32(adr_trans + sizeof(u32)) != 0) {
			*page_cnt = entry;
			break;
		}

		iowrite32(0, adr_trans);
		iowrite32(0, adr_trans + sizeof(u32));
	}

	iowrite32(0, adr_trans_first);
	iowrite32(0, adr_trans_first + sizeof(u32));
}

static int ixxat_pci_mc_reset(struct ixxat_pci_interface *intf)
{
	void __iomem *reset = intf->reg1vadd + IXXAT_PCI_ALTERA_RESET_CARD_OFF;
	u32 period = IXXAT_PCI_RESET_PERIOD / 100;
	u8 reset_done = 0;

	// Reset Device
	iowrite32(ioread32(reset) | IXXAT_PCI_RESET, reset);

	// Hold reset up to reset hold time
	do {
		usleep_range(99, 100);
		reset_done = ioread32(reset) & IXXAT_PCI_RESET;
	} while (reset_done && period--);

	if (reset_done)
		iowrite32(ioread32(reset) & ~IXXAT_PCI_RESET, reset);

	// Delay after reset in us
	msleep(IXXAT_PCI_RESET_DELAY / 1000);

	return 0;
}

static int ixxat_pci_convey_dma(struct ixxat_pci_interface *intf, u8 enable)
{
	int i;
	void __iomem *lcr_adr_trans;
	u32 page_sz;
	u16 page_cnt;
	u32 dma_high_addr;
	u32 dma_low_addr;
	dma_addr_t dma_phys_addr = intf->dmaadd;

	lcr_adr_trans = intf->reg1vadd + IXXAT_PCI_ALTERA_ADRTRANSSTART_OFF;

	// ixxat_pci_mc_reset(intf);

	ixxat_pci_dma_get_info(intf, &page_sz, &page_cnt);

	/* min verwendet typeof() welches dynamisch zur Laufzeit einen Datentyp
	 * setzt => statisches Analysetool erkennt void und meldet Fehler
	 */
	if (enable)
		page_cnt = min(page_cnt, (u16)(IXXAT_PCI_DMA_SIZE / page_sz));
	else
		dma_phys_addr = 0;

	for (i = 0; i < page_cnt; i++) {
		dma_high_addr = dma_phys_addr >> IXXAT_PCI_DMA_OFFSET_HIGH;
		dma_low_addr = (dma_phys_addr & IXXAT_PCI_DMA_ADD_LOW);

		iowrite32(dma_low_addr, lcr_adr_trans);
		lcr_adr_trans += sizeof(u32);
		iowrite32(dma_high_addr, lcr_adr_trans);
		lcr_adr_trans += sizeof(u32);

		if (enable)
			dma_phys_addr += page_sz;
	}

	return 0;
}

static int ixxat_pci_handle_error(struct ixxat_pci_device *dev,
				  struct ixxat_can_msg *rx)
{
	u8 raw_error;
	u32 time = le32_to_cpu(rx->base.time);
	struct net_device *netdev = dev->netdev;
	struct can_frame *cf;
	struct sk_buff *skb;

	if (dev->can.state == CAN_STATE_BUS_OFF)
		return 0;

	if (dev->adapter == &can_adapter) {
		raw_error = rx->cl1.data[0];
		dev->bec.rxerr = rx->cl1.data[IXXAT_PCI_CAN_ERROR_COUNTER_RX];
		dev->bec.txerr = rx->cl1.data[IXXAT_PCI_CAN_ERROR_COUNTER_TX];
	} else {
		raw_error = rx->cl2.data[0];
		dev->bec.rxerr = rx->cl2.data[IXXAT_PCI_CAN_ERROR_COUNTER_RX];
		dev->bec.txerr = rx->cl2.data[IXXAT_PCI_CAN_ERROR_COUNTER_TX];
	}

	if (raw_error == IXXAT_PCI_CAN_ERROR_ACK)
		netdev->stats.tx_errors++;
	else
		netdev->stats.rx_errors++;

	skb = alloc_can_err_skb(netdev, &cf);
	if (!skb)
		return -ENOMEM;

	switch (raw_error) {
	case IXXAT_PCI_CAN_ERROR_ACK:
		cf->can_id |= CAN_ERR_ACK;
		break;
	case IXXAT_PCI_CAN_ERROR_BIT:
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_BIT;
		break;
	case IXXAT_PCI_CAN_ERROR_CRC:
		cf->can_id |= CAN_ERR_PROT;
		cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
		break;
	case IXXAT_PCI_CAN_ERROR_FORM:
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_FORM;
		break;
	case IXXAT_PCI_CAN_ERROR_STUFF:
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_STUFF;
		break;
	default:
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_UNSPEC;
		break;
	}

	ixxat_pci_get_ts_tv(dev, time, &skb->tstamp);
	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);

	return 1;
}

static int ixxat_pci_handle_status(struct ixxat_pci_device *dev,
				   struct ixxat_can_msg *rx)
{
	u8 raw_status;
	u32 tmp_read;
	unsigned long spin_flags;
	struct can_frame *cf;
	struct net_device *netdev = dev->netdev;
	struct sk_buff *skb;
	enum can_state new_state = CAN_STATE_ERROR_ACTIVE;

	if (dev->adapter == &can_adapter)
		raw_status = rx->cl1.data[0];
	else
		raw_status = rx->cl2.data[0];

	if (raw_status != IXXAT_PCI_CAN_STATUS_OK) {
		if (raw_status & IXXAT_PCI_CAN_STATUS_BUSOFF) {
			dev->can.can_stats.bus_off++;
			new_state = CAN_STATE_BUS_OFF;
			can_bus_off(netdev);
		} else {
			if (raw_status & IXXAT_PCI_CAN_STATUS_ERRLIM) {
				dev->can.can_stats.error_warning++;
				new_state = CAN_STATE_ERROR_WARNING;
			}

			if (raw_status & IXXAT_PCI_CAN_STATUS_ERR_PAS) {
				dev->can.can_stats.error_passive++;
				new_state = CAN_STATE_ERROR_PASSIVE;
			}

			if (raw_status & IXXAT_PCI_CAN_STATUS_OVRRUN)
				new_state = CAN_STATE_MAX;
		}
	}

	if (new_state == CAN_STATE_ERROR_ACTIVE) {
		dev->bec.txerr = 0;
		dev->bec.rxerr = 0;
	}

	if (new_state != CAN_STATE_MAX)
		dev->can.state = new_state;

	skb = alloc_can_err_skb(netdev, &cf);
	if (!skb)
		return -ENOMEM;

	switch (new_state) {
	case CAN_STATE_ERROR_ACTIVE:
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] |= CAN_ERR_CRTL_ACTIVE;
		break;
	case CAN_STATE_ERROR_WARNING:
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] |= CAN_ERR_CRTL_TX_WARNING;
		cf->data[1] |= CAN_ERR_CRTL_RX_WARNING;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
		break;
	case CAN_STATE_BUS_OFF:
		cf->can_id |= CAN_ERR_BUSOFF;
		break;
	case CAN_STATE_MAX:
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] |= CAN_ERR_PROT_OVERLOAD;
		netdev->stats.rx_over_errors++;

		spin_lock_irqsave(&dev->rcv_lock, spin_flags);
		tmp_read = dev->frn_read + 1;
		if (tmp_read > IXXAT_PCI_MAX_TX_TRANS)
			tmp_read = 1;

		if (tmp_read != dev->frn_write) {
			can_free_echo_skb(dev->netdev,
					  dev->frn_read - 1);
			dev->frn_read = tmp_read;
		}
		spin_unlock_irqrestore(&dev->rcv_lock, spin_flags);
		break;
	default:
		netdev_err(netdev, "Error: Unhandled can status %d\n",
			   new_state);
		break;
	}

	ixxat_pci_get_ts_tv(dev, le32_to_cpu(rx->base.time), &skb->tstamp);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += cf->can_dlc;
	netif_receive_skb(skb);

	return 1;
}

static int ixxat_pci_handle_vci_msg(struct ixxat_pci_device *dev, void *data)
{
	struct ixxat_can_msg *can_msg = (struct ixxat_can_msg *)data;
	u32 msg_type = (le32_to_cpu(can_msg->base.flags)
			& IXXAT_PCI_MSG_FLAGS_TYPE);
	u32 ret = 0;

	ix_trace_printk (">> ixxat_pci_handle_vci_msg \n");

	switch (msg_type) {
	case IXXAT_PCI_CAN_STATUS:
		ret = ixxat_pci_handle_status(dev, can_msg);
		ix_trace_printk ("-- ixxat_pci_handle_status %i\n", ret);		
		break;

	case IXXAT_PCI_CAN_ERROR:
		ret = ixxat_pci_handle_error(dev, can_msg);
		ix_trace_printk ("-- ixxat_pci_handle_error %i\n", ret);		
		break;

	case IXXAT_PCI_CAN_INFO:
	case IXXAT_PCI_CAN_WAKEUP:
	case IXXAT_PCI_CAN_TIMERST:
		ix_trace_printk ("-- other %i\n", ret);		
		ret = 1;
		break;
	default:
		netdev_err(dev->netdev,
			   "Unhandled rec type 0x%02x (%d): ignored\n",
			   msg_type, msg_type);
		break;
	}

	if (ret < 0)
		netdev_err(dev->netdev, "Error %d: VCI-handling failed\n", ret);

	ix_trace_printk ("<< ixxat_pci_handle_vci_msg %i\n", ret);		

	return ret;
}

static int ixxat_pci_handle_devmsg(struct ixxat_pci_device *dev)
{
	void *fifo = dev->rx_fifo;
	void *ifi_base;
	volatile u32 read_index = (*(u32 *)(fifo + IXXAT_PCI_RES_READ_IDX)) + 1;
	volatile u32 write_index = *(u32 *)(fifo + IXXAT_PCI_RES_WRITE_IDX);
	u32 obj_size = *(u32 *)(fifo + IXXAT_PCI_RES_OBJ_SIZE);
	u32 ret = 0;
	u32 msg_type;

	ix_trace_printk (">> %i) ixxat_pci_handle_devmsg \n", dev->ctrl_idx);

	if (read_index == *(u32 *)(fifo + IXXAT_PCI_RES_NUM_OBJ))
		read_index = 0;

	if (read_index != write_index) {
		ifi_base = fifo + IXXAT_PCI_RES_DATA + read_index * obj_size;
		msg_type = *(u32 *)(ifi_base);
		ifi_base += sizeof(u32);

		ix_trace_printk ("-- message read %i,write %i, type %i \n", read_index, write_index, msg_type);	

		switch (msg_type) {
		case IXXAT_PCI_MSG_TYPE_IFI:
			ret = dev->adapter->handle_msg(dev, ifi_base);
			
			break;
		case IXXAT_PCI_MSG_TYPE_VCI:
			ret = ixxat_pci_handle_vci_msg(dev, ifi_base);
			break;
		default:
			netdev_warn(dev->netdev, "Unknown message received");
			ix_trace_printk ("-- Unknown message received \n");
			break;
		}

		if (*(u32 *)(fifo + IXXAT_PCI_RES_READ_IDX) + 1
			== *(u32 *)(fifo + IXXAT_PCI_RES_NUM_OBJ))
			*(u32 *)(fifo + IXXAT_PCI_RES_READ_IDX) = 0;
		else
			*(u32 *)(fifo + IXXAT_PCI_RES_READ_IDX) += 1;
	}
	else {
		ix_trace_printk ("-- no message read %i, write %i \n", read_index, write_index);	
	}

	ix_trace_printk ("<< ixxat_pci_handle_devmsg %i \n", ret);
	return ret;
}

static int ixxat_pci_napi_rx_poll(struct napi_struct *napi, int quota)
{
	volatile u32 read_index;
	volatile u32 write_index;
	void *rx_fifo;
	void __iomem *tx_fifo;
	unsigned long spin_flags;
	int nxpackets = 0;
	struct ixxat_pci_device *dev = netdev_priv(napi->dev);
	struct ixxat_pci_interface *intf = dev->intf;
	u32 intCtrlMask = 0;
	u32 loopExit = 0;

	ix_trace_printk (">> %i) ixxat_pci_napi_rx_poll \n", dev->ctrl_idx);	

	while ((nxpackets < quota)&&(0 == loopExit)) {
		
		rx_fifo = dev->rx_fifo;
		read_index = (*(u32 *)(rx_fifo + IXXAT_PCI_RES_READ_IDX)) + 1;
		if (read_index == *(u32 *)(rx_fifo + IXXAT_PCI_RES_NUM_OBJ))
			read_index = 0;

		write_index = *(u32 *)(rx_fifo + IXXAT_PCI_RES_WRITE_IDX);

		if (read_index == write_index ||
		    !(dev->intf->started_mask & (0x01 << dev->ctrl_idx))) 
				loopExit = 1;

		if (ixxat_pci_handle_devmsg(dev) > 0)
			nxpackets++;
		else
			loopExit = 2;

		ix_trace_printk ("  loop %i / %i (Exit %i) \n", nxpackets, quota, loopExit);				
	}


	spin_lock_irqsave(&dev->rcv_lock, spin_flags);
	if (dev->tx_fifo) {
		tx_fifo = dev->tx_fifo;

		if (ioread32(tx_fifo + IXXAT_PCI_RES_READ_IDX) !=
		    ioread32(tx_fifo + IXXAT_PCI_RES_WRITE_IDX) &&
		    dev->frn_write != dev->frn_read &&
		    netif_queue_stopped(dev->netdev)) {
			netif_wake_queue(dev->netdev);
		}
	}
	spin_unlock_irqrestore(&dev->rcv_lock, spin_flags);

	if (nxpackets < quota) {
		dev->intf->handle_data &= ~(0x01 << dev->ctrl_idx);
		napi_complete(&dev->napi);

		ix_trace_printk ("%i) ixxat_pci_napi_rx_poll \n", dev->ctrl_idx);
		intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
		ixxat_pci_int_ena_req(intf, 1, intCtrlMask);
	}

	ix_trace_printk ("<< ixxat_pci_napi_rx_poll packets %i \n", nxpackets);	
	return nxpackets;
}

static int ixxat_pci_get_intf_caps(struct ixxat_pci_interface *intf,
				   struct ixxat_intf_caps *intf_caps)
{
	int err, i;
	struct ixxat_pci_intf_caps_cmd *cmd;
	u16 num_ctrl;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 res_size = cmd_size - req_size;
	const u32 req_code = IXXAT_PCI_CMD_GET_DEVCAPS;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err) {
			dev_err(&intf->pdev->dev, "Error %d: Get caps failed\n", err);
		} else {
			memcpy(intf_caps, &cmd->intf_caps, sizeof(cmd->intf_caps));
			intf_caps->bus_ctrl_count = cmd->intf_caps.bus_ctrl_count;
			num_ctrl = intf_caps->bus_ctrl_count;
			if (num_ctrl > ARRAY_SIZE(intf_caps->bus_ctrl_types)) {
				err = -EINVAL;
			} else {
				for (i = 0; i < intf_caps->bus_ctrl_count; ++i)
					intf_caps->bus_ctrl_types[i] = cmd->intf_caps.bus_ctrl_types[i];
			}
		}

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_get_intf_info(struct ixxat_pci_interface *intf,
				   struct ixxat_intf_info *dev_info)
{
	int err;
	struct ixxat_pci_intf_info_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 res_size = cmd_size - req_size;
	const u32 req_code = IXXAT_PCI_CMD_GET_DEVINFO;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err) {
			dev_err(&intf->pdev->dev, "Error %d: Get info failed\n", err);
		} else {
			err = le32_to_cpu(cmd->res.ret_code);
			if (dev_info)
				memcpy(dev_info, &cmd->info, sizeof(cmd->info));
		}

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_start_ctrl(struct ixxat_pci_device *dev, u32 *time_ref)
{
	int err;
	struct ixxat_pci_interface *intf = dev->intf;
	struct ixxat_pci_start_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 req_size = sizeof(cmd->req);
	const u32 res_size = cmd_size - req_size;
	const u32 req_code = IXXAT_PCI_CMD_START;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);
	cmd->req.port = cpu_to_le16(dev->ctrl_idx);
	cmd->time = 0;

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err) {
			dev_err(&intf->pdev->dev, "Error %d: Start ctrl failed\n", err);
		} else {
			if (time_ref)
				*time_ref = le32_to_cpu(cmd->time);
		}

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_stop_ctrl(struct ixxat_pci_device *dev)
{
	int err;
	struct ixxat_pci_interface *intf = dev->intf;
	struct ixxat_pci_stop_cmd *cmd;
	const u32 cmd_size = sizeof(*cmd);
	const u32 res_size = sizeof(cmd->res);
	const u32 req_size = cmd_size - res_size;
	const u32 req_code = IXXAT_PCI_CMD_STOP;

	cmd = kmalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	ixxat_pci_setup_cmd(&cmd->req, req_size, &cmd->res, res_size, req_code);
	cmd->req.port = cpu_to_le16(dev->ctrl_idx);
	cmd->action = cpu_to_le32(IXXAT_PCI_STOP_ACTION_CLEARALL);

	err = mutex_lock_interruptible(&intf->cmd_lock);

	if (err) {
		dev_err(&intf->pdev->dev, "Error %x: Mutex lock interrupted", err);
		kfree(cmd);
	} else {
		err = ixxat_pci_handle_cmd(intf, &cmd->req, &cmd->res);
		if (err)
			dev_err(&intf->pdev->dev, "Error %d: Stop ctrl failed\n", err);
		else
			dev->can.state = CAN_STATE_STOPPED;

		kfree(cmd);
		mutex_unlock(&intf->cmd_lock);
	}

	return err;
}

static int ixxat_pci_handle_correct_frn(struct ixxat_pci_device *dev, u8 frn,
					struct sk_buff *skb, u32 ts, u32 idx)
{
	int iSkbRet;
	skb = dev->can.echo_skb[frn - 1];
	if (skb)
		ixxat_pci_get_ts_tv(dev, ts, &skb->tstamp);

	iSkbRet = can_get_echo_skb(dev->netdev, frn - 1);

	// is no loopback is active ?
	if (iSkbRet) {
		
	} else { 
		// it's possible that no loopback is active !
		// if the tx packets are counted through self-reception frames !
	}	

	dev->frn_read = idx;
	return 1;
}

int ixxat_pci_handle_frn(struct ixxat_pci_device *dev, u8 frn, u32 ts)
{
	int ret = 0;
	u32 expfrn;
	struct sk_buff *skb = NULL;

	ix_trace_printk (">> ixxat_pci_handle_frn\n");

	spin_lock(&dev->rcv_lock);
	expfrn = dev->frn_read + 1;

	if (expfrn > IXXAT_PCI_MAX_TX_TRANS)
		expfrn = 1;

	if (expfrn < dev->frn_write) {
		if (frn == expfrn) {
			ret = ixxat_pci_handle_correct_frn(dev, frn, skb, ts,
							   dev->frn_read++);
		} else if (frn < expfrn) {
			while (expfrn != frn)
				frn++;
		} else if (frn > expfrn) {
			// Overflow => lost messages
			while (expfrn != frn)
				expfrn++;
			ret = ixxat_pci_handle_correct_frn(dev, frn, skb, ts,
							   expfrn);
		} else if (frn > expfrn && frn >= dev->frn_write) {
			while (expfrn != frn) {
				frn++;
				if (frn > IXXAT_PCI_MAX_TX_TRANS)
					frn = 1;
			}
		}
	} else if (expfrn > dev->frn_write) {
		if (frn == expfrn) {
			ret = ixxat_pci_handle_correct_frn(dev, frn, skb, ts,
							   dev->frn_read++);
		} else if (frn > expfrn) {
			while (expfrn != frn)
				expfrn++;
			ret = ixxat_pci_handle_correct_frn(dev, frn, skb, ts,
							   expfrn);
		} else if (frn < expfrn && frn >= dev->frn_write) {
			while (dev->frn_read != frn)
				frn++;
		} else if (frn < expfrn && frn < dev->frn_write) {
			// Overflow => lost messages
			while (expfrn != frn) {
				expfrn++;
				if (expfrn > IXXAT_PCI_MAX_TX_TRANS)
					expfrn = 1;
			}
			ret = ixxat_pci_handle_correct_frn(dev, frn, skb, ts,
							   expfrn);
		}
	}
	if (dev->frn_read > IXXAT_PCI_MAX_TX_TRANS)
		dev->frn_read = 1;

	spin_unlock(&dev->rcv_lock);

	ix_trace_printk ("<< ixxat_pci_handle_frn %i \n", ret);
	return ret;
}

static u8 determineLoopMode(bool loopback, bool global_loopback)
{
	// decision if this message should be loopbacked !!
	u8 loopMode = IX_LOOP_DIS;

	// exact statistics means that all messages are sent with active
	// self reception ( overhead ) so that the statistic counter are incremented
	// after the message was really on the can bus, otherwise the counter is
	// incremented after the WriteURB returns
	const bool statistics_exact = IX_STATISTICS_EXACT;

	trace_printk ("global loopback %x \n", global_loopback);
	// is loopback set with ip link .. loopback on
	if (global_loopback == true) {
		
		// is loopback set with setsockopt
		// can be changed between message transmission
		if (loopback) 
			loopMode = (IX_LOOP_SELF_RX | IX_LOOPBACK);
	} 

	if ((loopMode & IX_LOOP_SELF_RX) != IX_LOOP_SELF_RX) {
		if (statistics_exact)
			loopMode = IX_LOOP_SELF_RX;
	}

	trace_printk ("Loopback %x \n", loopMode);

	return loopMode;
}

static netdev_tx_t ixxat_pci_start_xmit(struct sk_buff *skb,
					struct net_device *netdev)
{
	int ret = 0;
	u8	loopMode = 0;
	struct ixxat_pci_device *dev = netdev_priv(netdev);

    ix_trace_printk (">> %i) ixxat_pci_start_xmit \n", dev->ctrl_idx);
	// check loopback 
	loopMode = determineLoopMode((skb->pkt_type == PACKET_LOOPBACK),
									dev->loopback);	
	ret = dev->adapter->dev_start_xmit(skb, netdev, loopMode);
	ix_trace_printk ("<< ixxat_pci_start_xmit %i \n", ret);

	return ret;
}

static int ixxat_pci_start(struct ixxat_pci_device *dev)
{
	int err = 0;
	u32 time_ref = 0;
	u32 intCtrlMask;

	ix_trace_printk (">> %i) ixxat_pci_start \n", dev->ctrl_idx);

	if (!(dev->state & IXXAT_PCI_STATE_RUNNING)) {
		/* opening first device: */
		err = dev->adapter->dev_init_ctrl(dev);
		if (err)
			return err;

		napi_enable(&dev->napi);
		err = ixxat_pci_start_ctrl(dev, &time_ref);
		if (err)
			return err;

		if (dev->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) { // Loopback mode
			dev->loopback = true;
		} else {
			dev->loopback = false;
		}			

		ixxat_pci_set_ts_now(dev, time_ref);

		dev->frn_read = 0x1;
		dev->frn_write = 0x2;

		dev->intf->started_mask |= (0x01 << dev->ctrl_idx);

        intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
		ixxat_pci_int_ena_req(dev->intf, 1, intCtrlMask);
	}

	dev->bec.txerr = 0;
	dev->bec.rxerr = 0;

	dev->state |= IXXAT_PCI_STATE_RUNNING;
	dev->can.state = CAN_STATE_ERROR_ACTIVE;

	ix_trace_printk ("<< ixxat_pci_start \n");

	return err;
}

static void ixxat_pci_free(struct ixxat_pci_device *dev)
{
	u32 intCtrlMask;

	if (!dev)
		return;

	ix_trace_printk (">> %i)  ixxat_pci_free (devState %x) \n", dev->ctrl_idx, dev->state);

	if (dev->state & IXXAT_PCI_STATE_RUNNING) {
		dev->can.state = CAN_STATE_STOPPED;
		dev->state &= ~IXXAT_PCI_STATE_RUNNING;
		dev->intf->started_mask &= ~(0x01 << dev->ctrl_idx);

		ixxat_pci_stop_ctrl(dev);

		napi_disable(&dev->napi);
		intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
		ixxat_pci_int_ena_req(dev->intf, 0, intCtrlMask);

		dev->frn_read = 0x1;
		dev->frn_write = 0x2;
	}

	// is it the last open dev
	if (!dev->prev_dev && !dev->next_dev) {
		// todo check
//		intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
//		ixxat_pci_int_ena_req(dev->intf, 0, intCtrlMask);

		ixxat_pci_stop_ctrl(dev);

		if (dev->intf->device_irq) {
			devm_free_irq(&dev->intf->pdev->dev,
				      pci_irq_vector(dev->intf->pdev, 0),
				      dev->intf);
			pci_free_irq_vectors(dev->intf->pdev);
		}

		// kfree(NULL) is safe so no check is required
		kfree(dev->intf->addmemvadd);
		dev->intf->addmemvadd = NULL;

		if (dev->intf->dmavadd)
			pci_free_consistent(dev->pdev, dev->intf->dmalen,
					    dev->intf->dmavadd,
					    dev->intf->dmaadd);

		iounmap(dev->intf->reg1vadd);
		release_mem_region(dev->intf->reg1add, dev->intf->reg1len);

		iounmap(dev->intf->memvadd);
		release_mem_region(dev->intf->memadd, dev->intf->memlen);
	}

	ix_trace_printk ("<< ixxat_pci_free \n");
}

static void ixxat_pci_disconnect(struct pci_dev *pdev)
{
	struct ixxat_pci_device *dev;
	struct ixxat_pci_device *prev_dev;

	ix_trace_printk (">> ixxat_pci_disconnect \n");
	
	dev = pci_get_drvdata(pdev);
	
	/* unregister the given device and all previous devices */
	while ( dev )
	{
		prev_dev = dev->prev_dev;
		dev->next_dev = NULL;
		
		ixxat_pci_free(dev);
		
		unregister_netdev(dev->netdev);
		free_candev(dev->netdev);
		
		dev = prev_dev;
	}

	pci_disable_device(pdev);

	ix_trace_printk ("<< ixxat_pci_disconnect \n");
}

static int ixxat_pci_open(struct net_device *netdev)
{
	int err = 0;
	struct ixxat_pci_device *dev = netdev_priv(netdev);

	ix_trace_printk (">> %i) ixxat_pci_open \n", dev->ctrl_idx);

	err = open_candev(netdev);

	if (err)
		return err;

	err = ixxat_pci_start(dev);
	if (err) {
		netdev_err(netdev, "Error %d: Couldn't start device.\n", err);
		close_candev(netdev);
		return err;
	}
	netif_start_queue(netdev);

	ix_trace_printk ("<< ixxat_pci_open \n");

	return 0;
}

static int ixxat_pci_stop(struct net_device *netdev)
{
	int err;
	struct ixxat_pci_device *dev = netdev_priv(netdev);
	u32 intCtrlMask;

	ix_trace_printk (">> %i) ixxat_pci_stop \n", dev->ctrl_idx);

	netif_stop_queue(netdev);

	if (dev->state & IXXAT_PCI_STATE_RUNNING) {
		dev->intf->started_mask &= ~(0x01 << dev->ctrl_idx);

		err = ixxat_pci_stop_ctrl(dev);
		if (err)
			netdev_warn(netdev, "Error %d: Cannot stop device\n",
				    err);

		napi_disable(&dev->napi);
		
		intCtrlMask =(1 << (dev->ctrl_idx +1 + 16));
		ixxat_pci_int_ena_req(dev->intf, 0, intCtrlMask);

		dev->frn_read = 0x1;
		dev->frn_write = 0x2;
	}
	dev->state &= ~IXXAT_PCI_STATE_RUNNING;
	close_candev(netdev);
	dev->can.state = CAN_STATE_STOPPED;

	ix_trace_printk ("<< ixxat_pci_stop \n");

	return 0;
}

static int ixxat_pci_restart(struct ixxat_pci_device *dev)
{
	int err;
	u32 time_ref;
	struct net_device *netdev = dev->netdev;

	ix_trace_printk (">> ixxat_pci_restart \n");

	err = ixxat_pci_stop_ctrl(dev);
	if (err)
		goto fail;

	err = ixxat_pci_start_ctrl(dev, &time_ref);
	if (err)
		goto fail;

	dev->can.state = CAN_STATE_ERROR_ACTIVE;
	netif_wake_queue(netdev);
fail:

	ix_trace_printk ("<< ixxat_pci_restart \n");
	return err;
}

static int ixxat_pci_set_mode(struct net_device *netdev, enum can_mode mode)
{
	struct ixxat_pci_device *dev = netdev_priv(netdev);

	switch (mode) {
	case CAN_MODE_START:
		return ixxat_pci_restart(dev);
	default:
		return -EOPNOTSUPP;
	}
}

static int ixxat_pci_get_berr_counter(const struct net_device *netdev,
				      struct can_berr_counter *bec)
{
	struct ixxat_pci_device *dev = netdev_priv(netdev);

	*bec = dev->bec;
	return 0;
}

static const struct net_device_ops ixxat_pci_netdev_ops = {
	.ndo_open = ixxat_pci_open,
	.ndo_stop = ixxat_pci_stop,
	.ndo_start_xmit = ixxat_pci_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static struct ixxat_pci_adapter *ixxat_pci_get_adapter(const u16 id)
{
	switch (id) {
	case CAN_IB200_PRODUCT_ID:
	case CAN_IB210_PRODUCT_ID:
	case CAN_IB230_PRODUCT_ID:
	case CAN_IB400_PRODUCT_ID:
	case CAN_IB410_PRODUCT_ID:
		return &can_adapter;
	case CAN_IB600_PRODUCT_ID:
	case CAN_IB610_PRODUCT_ID:
//	case CAN_IB640_PRODUCT_ID:
	case CAN_IB800_PRODUCT_ID:
	case CAN_IB810_PRODUCT_ID:
		return &can_fd_adapter;
	default:
		return NULL;
	}
}

static int ixxat_init_cmdfifo ( struct ixxat_pci_interface *intf, u32 * pdwOffset )
{
	u32 obj_size = 0;
	u32 obj_num = 0;
	u32 data_off = 0;
	
	// command Tx
	intf->cmd_tx_fifo = intf->memvadd + IXXAT_PCI_RES_VER_OFF;
	
	obj_size = ioread32(intf->cmd_tx_fifo + IXXAT_PCI_RES_OBJ_SIZE);
	obj_num = ioread32(intf->cmd_tx_fifo + IXXAT_PCI_RES_NUM_OBJ);
	data_off = obj_size * obj_num;

	ix_trace_printk ("cmdFifo Tx %p, size:%i, num, %i\n", intf->cmd_tx_fifo, obj_size, obj_num);

	// command Rx
	intf->cmd_rx_fifo = intf->cmd_tx_fifo + IXXAT_PCI_RES_HEADER_SIZE + data_off;
	obj_size = ioread32(intf->cmd_rx_fifo + IXXAT_PCI_RES_OBJ_SIZE);
	obj_num = ioread32(intf->cmd_rx_fifo + IXXAT_PCI_RES_NUM_OBJ);	
	data_off = obj_size * obj_num;
	ix_trace_printk ("cmdFifo Rx %p, size:%i, num, %i\n", intf->cmd_rx_fifo, obj_size, obj_num);

	intf->cmd_dbg_fifo = intf->cmd_rx_fifo + IXXAT_PCI_RES_HEADER_SIZE + data_off;
	obj_size = ioread32(intf->cmd_dbg_fifo + IXXAT_PCI_RES_OBJ_SIZE);
	obj_num = ioread32(intf->cmd_dbg_fifo + IXXAT_PCI_RES_NUM_OBJ);	
	data_off = obj_size * obj_num;
	ix_trace_printk ("dbgFifo Rx %p, size:%i, num, %i\n", intf->cmd_dbg_fifo, obj_size, obj_num);	

	if ( pdwOffset )
		*pdwOffset = data_off;
	
	return 0;
}

static int ixxat_init_fwfifos ( struct ixxat_pci_interface *intf, u8 maxCtrl )
{
	u32 obj_size = 0;
	u32 obj_num = 0;
	u32 obj_tag = 0;
	u32 data_off = 0;
	int ctrlNo=0;
	u32 MemOffset;
	
	ixxat_init_cmdfifo ( intf, &data_off );
	
	ctrlNo=0;
	
	while (ctrlNo < maxCtrl) {
		if ( ctrlNo == 0) {
			intf->tx_fifo[ctrlNo] = intf->cmd_dbg_fifo + IXXAT_PCI_RES_HEADER_SIZE + data_off;
		}
		else {
			intf->tx_fifo[ctrlNo] = intf->tx_fifo[ctrlNo-1] + IXXAT_PCI_RES_HEADER_SIZE + data_off;
		}

		obj_tag = ioread32(intf->tx_fifo[ctrlNo] + IXXAT_PCI_RES_TAG); 
		obj_size = ioread32(intf->tx_fifo[ctrlNo] + IXXAT_PCI_RES_OBJ_SIZE);
		obj_num = ioread32(intf->tx_fifo[ctrlNo] + IXXAT_PCI_RES_NUM_OBJ);	
		data_off = obj_size * obj_num;	

		MemOffset = intf->tx_fifo[ctrlNo] - intf->cmd_dbg_fifo;
		ix_trace_printk ("%08x Fifo %i Tx %p (%i), size:%i, num, %i\n", obj_tag, ctrlNo, intf->tx_fifo[ctrlNo], MemOffset, obj_size, obj_num);
		++ctrlNo;
	}
	
	
	ctrlNo=0;
	data_off=0;

	while (ctrlNo < maxCtrl) {
		if ( ctrlNo == 0) {	
			intf->rx_fifo[ctrlNo] = intf->dmavadd;
		}
		else {
			intf->rx_fifo[ctrlNo] = intf->rx_fifo[ctrlNo-1] + IXXAT_PCI_RES_HEADER_SIZE + data_off;
		}

		obj_tag = *((u32 *) (intf->rx_fifo[ctrlNo] + IXXAT_PCI_RES_TAG)); 
		obj_size = *((u32 *) (intf->rx_fifo[ctrlNo] + IXXAT_PCI_RES_OBJ_SIZE));
		obj_num = *((u32 *) (intf->rx_fifo[ctrlNo] + IXXAT_PCI_RES_NUM_OBJ));	
		data_off = obj_size * obj_num;

		MemOffset = intf->rx_fifo[ctrlNo] - intf->dmavadd;
		ix_trace_printk ("%08x Fifo %i Rx %p (%i), size:%i, num, %i\n",  obj_tag,ctrlNo, intf->rx_fifo[ctrlNo], MemOffset, obj_size, obj_num);
		#ifdef DEBUG	
			Showdump (intf->rx_fifo[ctrlNo], 0x40);
		#endif			

		++ctrlNo;
	} ;

	return 0;
}

static int ixxat_pci_create_dev(struct ixxat_pci_interface *intf,
				struct ixxat_pci_adapter *adapter,
				struct pci_dev *pdev, int ctrl_idx)
{
	int err;
	struct ixxat_pci_device *dev;
	struct net_device *netdev = alloc_candev(sizeof(*dev),
						 IXXAT_PCI_MAX_TX_TRANS);

	if (!netdev) {
		dev_err(&intf->pdev->dev, "Error: Cannot allocate candev\n");
		return -ENOMEM;
	}

	dev = netdev_priv(netdev);
	dev->intf = intf;
	dev->pdev = pdev;
	dev->netdev = netdev;
	dev->adapter = adapter;
	dev->ctrl_idx = ctrl_idx;
	dev->state = IXXAT_PCI_STATE_CONNECTED;

	spin_lock_init(&dev->rcv_lock);
	netif_napi_add(netdev, &dev->napi, ixxat_pci_napi_rx_poll, 2);

	dev->can.clock.freq = adapter->clock;
	dev->can.bittiming_const = adapter->bt;
	dev->can.data_bittiming_const = adapter->btd;

	dev->can.do_set_mode = ixxat_pci_set_mode;
	dev->can.do_get_berr_counter = ixxat_pci_get_berr_counter;

	dev->can.ctrlmode_supported = adapter->modes;

	dev->can.restart_ms = IXXAT_PCI_DEFAULT_RESTART_MS;

	netdev->netdev_ops = &ixxat_pci_netdev_ops;
	netdev->flags |= IFF_ECHO;

	dev->prev_dev = pci_get_drvdata(pdev);
	pci_set_drvdata(pdev, dev);
	
	dev->tx_fifo = intf->tx_fifo[ctrl_idx];
	dev->rx_fifo = intf->rx_fifo[ctrl_idx];

	SET_NETDEV_DEV(netdev, &pdev->dev);

	err = register_candev(netdev);
	if (err) {
		dev_err(&intf->pdev->dev,
			"Error %d: Failed to register Can device\n", err);
		goto free_candev;
	}

	if (dev->prev_dev)
		(dev->prev_dev)->next_dev = dev;

	if (!intf->dev)
		intf->dev = dev;
	
	err = ixxat_pci_get_intf_info(intf, &intf->dev_info);
	if (err) {
		dev_err(&intf->pdev->dev,
			"Error %d: Failed to get device information\n", err);
		goto unreg_candev;
	}

	netdev_info(netdev, "%s: Connected Channel %u (device %s)\n",
		    intf->dev_info.intf_name, ctrl_idx,
		    intf->dev_info.intf_id);

	return 0;

unreg_candev:
	unregister_candev(netdev);
free_candev:
	pci_set_drvdata(intf->pdev, dev->prev_dev);
	free_candev(netdev);

	return err;
}

static int ixxat_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ixxat_pci_adapter *adapter;
	struct ixxat_pci_interface *intf;
	struct ixxat_intf_caps intf_caps;
	int i;
	u32 mem_len;	
	int err = pci_enable_device(pdev);

	if (err)
		return err;

	pci_set_master(pdev);

	adapter = ixxat_pci_get_adapter(id->device);
	if (!adapter) {
		pr_err("%s: Unknown device id %d\n",
		       KBUILD_MODNAME, id->device);
		return -ENODEV;
	}

	intf = devm_kzalloc(&pdev->dev, sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return -ENOMEM;

	intf->pdev = pdev;
	mutex_init(&intf->cmd_lock);

	/* call the probe function of corresponding adapter */
	err = ixxat_pci_register_dev(pdev, intf);
	if (err)
		goto lbl_set_intf;
	
	ix_trace_printk ( "ixxat_init_cmdfifo\n");
	ixxat_init_cmdfifo ( intf, NULL );

	/* reset the interface once */
	err = ixxat_pci_mc_reset(intf);
	if (err)
		goto lbl_free_complete;	

	/* convey dma hardware address to the interface */
	err = ixxat_pci_convey_dma(intf, 1);
	if (err)
		goto lbl_free_memory;

	/* enable interface's pci interrupts  */
	// is made on the start ctrl
	//err = ixxat_pci_int_ena_req(intf, 1);
	//if (err)
	//	goto lbl_free_complete;

	/* send a test command to the device's command channel */
	err = ixxat_pci_test_cmd(intf);
	if (err)
		goto lbl_free_complete;

	/* test if dma address was successfully delivered to the interface */
	err = ixxat_pci_test_dma(intf);
	if (err)
		goto lbl_free_complete;

	/* upload the device's firmware */
	err = ixxat_pci_upload_fw(pdev, intf);
	if (err)
		goto lbl_free_complete; //Changed

	/* start uploaded firmware */
	err = ixxat_pci_start_fw(intf);
	if (err)
		goto lbl_free_complete;

	/* give device some time to start */
	msleep(100);
	
	ix_trace_printk ( "ixxat_init_cmdfifo\n");
	ixxat_init_cmdfifo ( intf, NULL );	
	
	err = ixxat_pci_get_intf_caps(intf, &intf_caps);
	if (err)
		goto lbl_free_complete;

	err = ixxat_pci_AdrTableSize(intf, &mem_len);
	ix_trace_printk ( "ixxat_pci_AdrTableSize %i (-> mem_len %i)\n", err, mem_len);
	err = ixxat_pci_AdrTable_Establish (intf, 0, mem_len + 1);
	ix_trace_printk ( "ixxat_pci_AdrTable_Establish %i\n", err);

//	ixxat_mem_dump (int, mem_len);
	adapter->ctrl_count = 0;

	for (i = 0; i < intf_caps.bus_ctrl_count; i++) {
		if (IXXAT_PCI_BUS_TYPE(intf_caps.bus_ctrl_types[i])
			== IXXAT_PCI_BUS_CAN) {
			adapter->ctrl_count++;
		}
	}
	
	ix_trace_printk ( "ixxat_init_fwfifos\n");
	ixxat_init_fwfifos ( intf, adapter->ctrl_count );

	for (i = 0; i < intf_caps.bus_ctrl_count; i++) {
		if (IXXAT_PCI_BUS_TYPE(intf_caps.bus_ctrl_types[i])
			== IXXAT_PCI_BUS_CAN) {

			err = ixxat_pci_create_dev(intf, adapter, pdev, i);
			if (err) {
				dev_err(&intf->pdev->dev,
					"Error %d: Device failed %d\n", err, i);
				ixxat_pci_disconnect(pdev);
				goto lbl_free_complete;
			}
		}
	}

	return err;

lbl_free_complete:
	ixxat_pci_convey_dma(intf, 1);  //Firmware Fix (0)
	ixxat_pci_int_ena_req(intf, 0, 0x00FF0000);

lbl_free_memory:
	if (intf->device_irq) {
		devm_free_irq(&intf->pdev->dev,
			      pci_irq_vector(intf->pdev, 0),
			      intf);
		pci_free_irq_vectors(intf->pdev);
	}

	// kfree(NULL) is safe so no check is required
	kfree(intf->addmemvadd);
	intf->addmemvadd = NULL;

	if (intf->dmavadd)
		pci_free_consistent(pdev, intf->dmalen, intf->dmavadd,
				    intf->dmaadd);

	iounmap(intf->memvadd);
	release_mem_region(intf->memadd, intf->memlen);

	iounmap(intf->reg1vadd);
	release_mem_region(intf->reg1add, intf->reg1len);

lbl_set_intf:
	pci_disable_device(pdev);
	devm_kfree(&pdev->dev, intf);

	return err;
}

static struct pci_driver ixxat_pci_driver = {
	.name = KBUILD_MODNAME,
	.remove = ixxat_pci_disconnect,
	.probe = ixxat_pci_probe,
	.id_table = ixxat_pci_table,
};

module_pci_driver(ixxat_pci_driver);
