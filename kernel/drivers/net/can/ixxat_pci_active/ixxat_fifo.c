// SPDX-License-Identifier: GPL-2.0

/* CAN driver for IXXAT PCI-to-CAN
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

#include <linux/module.h>
#include <linux/pci.h>

#include "ixxat_fifo.h"
#include "ixxat_pci_core.h"

#if defined(CONFIG_TRACING) && defined(DEBUG)
	#define ix_trace_printk(...) trace_printk(__VA_ARGS__)
#else
	#define ix_trace_printk(...)
#endif

/*
FIFO memory layout

typedef struct ixxat_fifo_header {
{
	u32		restag;		// resource tag (FIFO1_RES_TAG)
	u32		ressize;	// size in bytes of this memory block
	u16		resdir;		// communication direction
	u16		reserved;	//
	u32		numobjs;	// number of elements in the FIFO
	u32		objsize;	// size in bytes of a single element
	volatile u32	write;		// current write position
	volatile u32	read;		// current read position
} __packed;

typedef struct ixxat_fifo2_header {
{
	u32		restag;		// resource tag (FIFO1_RES_TAG)
	u32		ressize;	// size in bytes of this memory block
	u16		resdir;		// communication direction
	u16		errcnt;		// communication error counter
	u32		numobjs;	// number of elements in the FIFO
	u32		objsize;	// size in bytes of a single element
	volatile u32	write1;		// current write position
	volatile u32	read1;		// current read position
	volatile u32	write2;		// current write position
	volatile u32	read2;		// current read position
} __packed;
*/

// offsets
#define IXXAT_PCI_RES_TAG		0x00
#define IXXAT_PCI_RES_SIZE		0x04
#define IXXAT_PCI_RES_DIR		0x08
#define IXXAT_PCI_FIFO_NUM_OBJ		0x0c
#define IXXAT_PCI_FIFO_OBJ_SIZE		0x10
#define IXXAT_PCI_FIFO_WRITE_IDX	0x14
#define IXXAT_PCI_FIFO_READ_IDX		0x18
#define IXXAT_PCI_FIFO_HDRSIZE		0x1c
#define IXXAT_PCI_FIFO2_WRITE2_IDX	0x1c
#define IXXAT_PCI_FIFO2_READ2_IDX	0x20
#define IXXAT_PCI_FIFO2_HDRSIZE		0x24

/*
  The get functions currently uses double reads because there had been sporadic
  reads which delivered non plausible values (WaSt).
  According to the Cyclone IV manual a read in a write-read situation
  should deliver either the old or the new value and not an undefined value.
  To monitor the current behaviour we do a double read until both values
  are equal and check for non plausible values.
*/

static u32 fifo_get_writeidx(struct ixxat_fifo* fifo)
{
	int warn_once = 1;
	u32 idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
	u32 idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
	while (idx != idx2)
	{
		if ((idx > IX_FIFO_NUMOBJS(fifo)) || (idx2 > IX_FIFO_NUMOBJS(fifo)))
		{
			printk(KERN_WARNING "Warning: IXXAT fifo %s invalid write index value: widx1 %u widx2 %u", fifo->id, idx, idx2);
		}
		else {
			if (warn_once) {
				warn_once = 0;
				ix_trace_printk(KERN_INFO "Info: IXXAT fifo %s write index differs: widx1 %u widx2 %u", fifo->id, idx, idx2);
			}
		}
		idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
		idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
	}
	return idx;
}

static u32 fifo_get_readidx(struct ixxat_fifo* fifo)
{
	int warn_once = 1;
	u32 idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
	u32 idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
	while (idx != idx2)
	{
		if ((idx > IX_FIFO_NUMOBJS(fifo)) || (idx2 > IX_FIFO_NUMOBJS(fifo)))
		{
			printk(KERN_WARNING "Warning: IXXAT fifo %s invalid read index value: ridx1 %u ridx2 %u", fifo->id, idx, idx2);
		}
		else {
			if (warn_once) {
				warn_once = 0;
				ix_trace_printk(KERN_INFO "Info: IXXAT fifo %s read index differs: ridx1 %u ridx2 %u", fifo->id, idx, idx2);
			}
		}
		idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
		idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
	}
	return idx;
}

static void fifo_set_writeidx(struct ixxat_fifo* fifo, u32 idx)
{
	iowrite32(idx, IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
}

static void fifo_set_readidx(struct ixxat_fifo* fifo, u32 idx)
{
	iowrite32(idx, IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
}

static u32 fifo2_get_writeidx(struct ixxat_fifo* fifo)
{
	int warn_once = 1;
	u32 idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
	u32 idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO2_WRITE2_IDX);
	while (idx != idx2)
	{
		if ((idx > IX_FIFO_NUMOBJS(fifo)) || (idx2 > IX_FIFO_NUMOBJS(fifo)))
		{
			printk(KERN_WARNING "Warning: IXXAT fifo %s invalid write index value: widx1 %u widx2 %u", fifo->id, idx, idx2);
		}
		else {
			if (warn_once) {
				warn_once = 0;
				ix_trace_printk(KERN_INFO "Info: IXXAT fifo %s write index differs: widx1 %u widx2 %u", fifo->id, idx, idx2);
			}
		}
		idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
		idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO2_WRITE2_IDX);
	}
	return idx;
}
static u32 fifo2_get_readidx(struct ixxat_fifo* fifo)
{
	int warn_once = 1;
	u32 idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
	u32 idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO2_READ2_IDX);
	while (idx != idx2)
	{
		if ((idx > IX_FIFO_NUMOBJS(fifo)) || (idx2 > IX_FIFO_NUMOBJS(fifo)))
		{
			printk(KERN_WARNING "Warning: IXXAT fifo %s invalid read index value: ridx1 %u ridx2 %u", fifo->id, idx, idx2);
		}
		else {
			if (warn_once) {
				warn_once = 0;
				ix_trace_printk(KERN_INFO "Info: IXXAT fifo %s read index differs: ridx1 %u ridx2 %u", fifo->id, idx, idx2);
			}
		}
		idx = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
		idx2 = ioread32(IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO2_READ2_IDX);
	}
	return idx;
}
static void fifo2_set_writeidx(struct ixxat_fifo* fifo, u32 idx)
{
	iowrite32(idx, IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO2_WRITE2_IDX);
	iowrite32(idx, IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_WRITE_IDX);
}
static void fifo2_set_readidx(struct ixxat_fifo* fifo, u32 idx)
{
	iowrite32(idx, IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO2_READ2_IDX);
	iowrite32(idx, IX_FIFO_BASE(fifo) + IXXAT_PCI_FIFO_READ_IDX);
}

void ixxat_fifo_init(struct ixxat_fifo* fifo, void* __iomem base, const char* prefix, int index)
{
	if (index < 0) {
		strncpy(fifo->id, prefix, 20);
	} else {
		snprintf(fifo->id, sizeof(fifo->id), "%.20s[%i]", prefix, index);
	}

	fifo->base = base;
	fifo->restag = ioread32(base + IXXAT_PCI_RES_TAG);
	fifo->resdir = ioread32(base + IXXAT_PCI_RES_DIR);
	fifo->ressize = ioread32(base + IXXAT_PCI_RES_SIZE);
	fifo->numobjs = ioread32(base + IXXAT_PCI_FIFO_NUM_OBJ);
	fifo->objsize = ioread32(base + IXXAT_PCI_FIFO_OBJ_SIZE);

	if (IXXAT_FIFO2_RES_TAG ==fifo->restag) {
		fifo->hdrsize = IXXAT_PCI_FIFO2_HDRSIZE;
		fifo->get_writeidx = fifo2_get_writeidx;
		fifo->set_writeidx = fifo2_set_writeidx;
		fifo->get_readidx = fifo2_get_readidx;
		fifo->set_readidx = fifo2_set_readidx;
	} else {
		fifo->hdrsize = IXXAT_PCI_FIFO_HDRSIZE;
		fifo->get_writeidx = fifo_get_writeidx;
		fifo->set_writeidx = fifo_set_writeidx;
		fifo->get_readidx = fifo_get_readidx;
		fifo->set_readidx = fifo_set_readidx;
	}
}

static int copy_todev(void* pDest, void* pSrc, int length)
{
	int idx;
	int numdw = length / sizeof(u32);
	int restbytes = length - (numdw * sizeof(u32));

	for (idx = 0; idx < numdw; idx++)
	 	iowrite32(ioread32(pSrc + idx * sizeof(u32)), pDest + (idx * sizeof(u32)));
	pDest += numdw * sizeof(u32);
	pSrc += numdw * sizeof(u32);
	for (idx = 0; idx < restbytes; idx++)
		iowrite8(ioread8(pSrc + idx), pDest + idx);

	return length;
}

static int copy_fromdev(void* pDest, void* pSrc, int length)
{
	int idx;
	int numdw = length / sizeof(u32);
	int restbytes = length - (numdw * sizeof(u32));

	for (idx = 0; idx < numdw; idx++)
	 	iowrite32(ioread32(pSrc + (idx * sizeof(u32))), pDest + (idx * sizeof(u32)));
	pDest += numdw * sizeof(u32);
	pSrc += numdw * sizeof(u32);
	for (idx = 0; idx < restbytes; idx++)
		*((u8*)pDest + idx) = ioread8(pSrc + idx);

	return length;
}

int ixxat_fifo_write_cmd(struct ixxat_pci_interface *intf, 
			 struct ixxat_fifo* fifo,
			 struct ixxat_pci_dal_req *req,
			 struct ixxat_pci_dal_res *res)
{
	void* __iomem dest;
	u32 read_index;
	u32 write_index;
	u32 obj_size;
	u32 num_obj;
	u32 reqsize;
	u32 size;

	// check fifo type
	if (IX_FIFO_RESDIR(fifo) != IXXAT_PCI_RESDIR_HTOD) {
		dev_err(&intf->pdev->dev, "Error: %s wrong fifo direction: %u exp %u", fifo->id, IX_FIFO_RESDIR(fifo), IXXAT_PCI_RESDIR_HTOD);
		return -EBADSLT;
	}

	read_index = IX_FIFO_GET_READIDX(fifo);
	write_index = IX_FIFO_GET_WRITEIDX(fifo);

	if (write_index == read_index) {
		dev_err(&intf->pdev->dev, "Error: %s Send cmd no buffer: widx %u ridx %u", fifo->id, write_index, read_index);
		return -ENOBUFS;
	}

	obj_size = IX_FIFO_OBJSIZE(fifo);
	num_obj = IX_FIFO_NUMOBJS(fifo);

	dest = IX_FIFO_GET_DATAPTR(fifo) + write_index * obj_size;
	reqsize = roundup(le32_to_cpu(req->size), 4);
	size = reqsize + sizeof(struct ixxat_pci_dal_res);
	iowrite32(size, dest);

	dest += sizeof(u32);
	copy_todev(dest, req, le32_to_cpu(req->size));

	dest += reqsize;
	copy_todev(dest, res, sizeof(struct ixxat_pci_dal_res));

	write_index = (write_index + 1) % num_obj;
	IX_FIFO_SET_WRITEIDX(fifo, write_index);

	return 0;
}

int ixxat_fifo_read_cmd(struct ixxat_pci_interface *intf,
			struct ixxat_fifo *fifo,
			struct ixxat_pci_dal_req *req,
			struct ixxat_pci_dal_res *res)
{
	void __iomem *src;
	u32 read_index;
	u32 write_index;
	u32 obj_size;
	u32 num_obj;

	ktime_t start, end;
	size_t req_size;
	u32 res_size;
	u32 req_code;
	u16 req_port;
	
	int err_once = 1;
	int result = 0;

	// check fifo type
	if (IX_FIFO_RESDIR(fifo) != IXXAT_PCI_RESDIR_DTOH) {
		dev_err(&intf->pdev->dev, "Error: %s wrong fifo direction: %u exp %u", fifo->id, IX_FIFO_RESDIR(fifo), IXXAT_PCI_RESDIR_DTOH);
		return -EBADSLT;
	}

	obj_size = IX_FIFO_OBJSIZE(fifo);
	num_obj = IX_FIFO_NUMOBJS(fifo);

	read_index = IX_FIFO_GET_READIDX(fifo) + 1;
	write_index = IX_FIFO_GET_WRITEIDX(fifo);

	if (read_index == num_obj)
		read_index = 0;

	if (write_index == num_obj)
		write_index = 0;

	// calc request size
	req_size = sizeof(struct ixxat_pci_dal_req);

	start = ktime_get_real();
	end = start;

	// wait for answer
	int answer_present = 0;
 	while ((ktime_to_ns(end) - ktime_to_ns(start)) < IXXAT_PCI_CMD_TIMEOUT_NS) {

		// sleep for 10 µsec and wait for data from the interface card
		if (read_index == write_index) {
			usleep_range(9, 10);

			read_index = IX_FIFO_GET_READIDX(fifo) + 1;
			write_index = IX_FIFO_GET_WRITEIDX(fifo);

			if (read_index == num_obj)
				read_index = 0;

			if (write_index == num_obj)
				write_index = 0;

		} else {
			answer_present= 1;
			break;
		}
		end = ktime_get_real();
	}

	if (!answer_present) {
		dev_err(&intf->pdev->dev, "Error: %s: No answer from device", fifo->id);
		return -ENODATA;
	}

	src = IX_FIFO_GET_DATAPTR(fifo) + read_index * obj_size;
	res_size = ioread32(src);
	src += sizeof(u32);
	
	if (res_size != (le32_to_cpu(res->res_size) + req_size)) {
		if (err_once) {
			err_once = 0;
			// incorrect answer
			dev_err(&intf->pdev->dev, "Error: %s: Invalid cmd size %d %d %d", fifo->id, res_size, (u32)(res->res_size + req_size), (u32)(le32_to_cpu(res->res_size) + req_size));
		}

		result = -ENODATA;
	}

	if (!result)
	{
		req_port = ioread16(src + sizeof(req->size));
		req_code = ioread32(src + sizeof(req->size)+ sizeof(req->port) + sizeof(req->socket));

		if (req_code != le32_to_cpu(req->code)) {
			dev_err(&intf->pdev->dev, "Error: %s: Invalid cmd code %d expected: %d", fifo->id, req_code, le32_to_cpu(req->code));
			result = -ENODATA;
		}

		if (req_port != le16_to_cpu(req->port)) {
			dev_err(&intf->pdev->dev, "Error: Invalid cmd port index!");
			result = -ENODATA;
		}
	}

	if (!result)
	{
		copy_fromdev(res, src + req_size, le32_to_cpu(res->res_size));

		ix_trace_printk("Req:%x ResSize %i, RetSize %i, Retcode %i \n",
			req->code,
			res->res_size, res->ret_size, res->ret_code);
		if (res->ret_code)
			dev_err(&intf->pdev->dev, "Error %x: Receiving command failure", res->ret_code);

		result = le32_to_cpu(res->ret_code);
	}

	// increment read index
	IX_FIFO_SET_READIDX(fifo, read_index);

	return result;
}