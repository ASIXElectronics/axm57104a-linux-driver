/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic
 *     Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
 *****************************************************************************/
/*
 * This file is part of the Xilinx DMA IP Core driver for Linux
 *
 * Copyright (c) 2016-present,  Xilinx, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef __XDMA_MODULE_H__
#define __XDMA_MODULE_H__

#include <linux/types.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/aio.h>
#include <linux/splice.h>
#include <linux/version.h>
#include <linux/uio.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>

#include "libxdma.h"
#include "switch/ax_switch.h"


#define VID_ASIX	0x125B

#define MAGIC_ENGINE	0xEEEEEEEEUL
#define MAGIC_DEVICE	0xDDDDDDDDUL
#define MAGIC_CHAR	0xCCCCCCCCUL
#define MAGIC_BITSTREAM 0xBBBBBBBBUL

#define VENDOR_OUI_0      0x00
#define VENDOR_OUI_1      0x0e
#define VENDOR_OUI_2      0xc6
#define CHECK_MAC_COUNT   2
#define MAX_SLOT_NUM      32
#define DOUBLE_SLOT_NUM   64
#define TIME_SLOT_RESERVE 30000 //50us
#define ETH_P_SDSA        0xDCDC

/* XDMA PCIe device specific book-keeping */
struct xdma_pci_dev {
	unsigned long magic;		/* structure ID for sanity checks */
	struct pci_dev *pdev;	/* pci device struct from probe() */
	struct xdma_dev *xdev;
	int user_max;
	int c2h_channel_max;
	int h2c_channel_max;

	unsigned int flags;

	void *data;

	struct ax_switch *pSwitch;

	/* ASIX Network device */
	struct ax_private *ax_netdev_priv;
};


#endif /* ifndef __XDMA_MODULE_H__ */
