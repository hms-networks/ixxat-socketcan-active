/* SPDX-License-Identifier: GPL-2.0 */

/* CAN driver base for IXXAT PCI-to-CAN
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

#ifndef IXXAT_FIFO_H_
#define IXXAT_FIFO_H_

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/can/dev.h>

struct ixxat_pci_dal_res;
struct ixxat_pci_dal_req;
struct ixxat_pci_interface;

// FIFO resource identifier ("FIFO")
#define IXXAT_FIFO_RES_TAG   0x4F464946

// FIFO resource identifier ("FIF2")
#define IXXAT_FIFO2_RES_TAG  0x32464946

struct ixxat_fifo {
	void* __iomem	base;
	u32 		hdrsize;	// size of resource header
	u32		restag;		// resource tag
	u32		ressize;	// resource size
	u32		resdir;		// direction
	u32		numobjs;	// number of elements in the FIFO
	u32		objsize;	// size in bytes of a single element

	char		id[64];	// fifo id for debug output

	u32  (*get_writeidx)(struct ixxat_fifo* fifo);
	void (*set_writeidx)(struct ixxat_fifo* fifo, u32 idx);

	u32  (*get_readidx)(struct ixxat_fifo* fifo);
	void (*set_readidx)(struct ixxat_fifo* fifo, u32 idx);
};

#define IX_FIFO_BASE(fifo) 	((fifo)->base)
#define IX_FIFO_RESTAG(fifo)	((fifo)->restag)
#define IX_FIFO_RESSIZE(fifo)	((fifo)->ressize)
#define IX_FIFO_RESDIR(fifo)	((fifo)->resdir)
#define IX_FIFO_NUMOBJS(fifo)	((fifo)->numobjs)
#define IX_FIFO_OBJSIZE(fifo)	((fifo)->objsize)

#define IX_FIFO_GET_WRITEIDX(fifo)	(((fifo)->get_writeidx((fifo))))
#define IX_FIFO_SET_WRITEIDX(fifo, idx)	(((fifo)->set_writeidx((fifo), (idx))))
#define IX_FIFO_GET_READIDX(fifo)	(((fifo)->get_readidx((fifo))))
#define IX_FIFO_SET_READIDX(fifo, idx)	(((fifo)->set_readidx((fifo), (idx))))

#define IX_FIFO_GET_PTR(fifo, offset)	(IX_FIFO_BASE(fifo) + (offset))
#define IX_FIFO_GET_DATAPTR(fifo)	(IX_FIFO_GET_PTR(fifo, ((fifo)->hdrsize) ))



void ixxat_fifo_init(struct ixxat_fifo* fifo, void* __iomem base, const char* prefix, int index);

int ixxat_fifo_write(struct ixxat_fifo* fifo,	void* data, u32 size);
int ixxat_fifo_read(struct ixxat_fifo* fifo, void* data, u32 size);


int ixxat_fifo_write_cmd(struct ixxat_pci_interface *intf,
			 struct ixxat_fifo* fifo,
			 struct ixxat_pci_dal_req *req,
			 struct ixxat_pci_dal_res *res);

int ixxat_fifo_read_cmd(struct ixxat_pci_interface *intf,
		        struct ixxat_fifo* fifo,
		        struct ixxat_pci_dal_req *req,
			struct ixxat_pci_dal_res *res);

#endif