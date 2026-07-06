/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
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

#ifndef XDMA_LIB_H
#define XDMA_LIB_H

#include <linux/version.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/timer.h>
#include <net/dsa.h>

#include "./switch/ax_switch.h"

#define ASIX_MSG(fmt, args...) \
printk(fmt "\n", ##args)

#ifdef AXM57104_DEBUG
#define ASIX_DEBUG(fmt, args...) \
printk(KERN_DEBUG "%s: "fmt "\n", __FUNCTION__, ##args)
#else
#define ASIX_DEBUG(fmt, args...)
#endif

/* maximum number of desc per transfer request */
#define XDMA_TRANSFER_MAX_DESC	(2048)

/* Network deivce */
#define PKT_BUF_LEN			2048
#define RX_DESC_NUM			1024 //XDMA_TRANSFER_MAX_DESC
#define TX_DESC_NUM			RX_DESC_NUM
#define RX_SKB_COPY
#define TX_SKB_COPY
#define DMA_PACKET_SIZE		2048

#define DESC_LIST_NUM		2

#define CONFIG_NAPA_NAPI

#define PHY_POLLING_TIMER	500	/* in milliseconds */

/* Phy device */
#define RTL8211FS_PHY_AD_SHIFT	0x01


/* Switch debug printing on/off */
#define XDMA_DEBUG 0

/* SECTION: Preprocessor macros/constants */
#define XDMA_BAR_NUM (3)	// Default: 6 ----  bar0 NIC bar1 DMA bar2 TSN

/* maximum amount of register space to map */
#define XDMA_BAR_SIZE (0x8000UL)

/* Use this definition to poll several times between calls to schedule */
#define NUM_POLLS_PER_SCHED 100

#define XDMA_CHANNEL_NUM_MAX (2)	// Default: 4
/*
 * interrupts per engine, rad2_vul.sv:237
 * .REG_IRQ_OUT	(reg_irq_from_ch[(channel*2) +: 2]),
 */
#define XDMA_ENG_IRQ_NUM (1)
#define MAX_EXTRA_ADJ (15)
#define RX_STATUS_EOP (1)

/* Target internal components on XDMA control BAR */
#define XDMA_OFS_INT_CTRL	(0x2000UL)
#define XDMA_OFS_CONFIG		(0x3000UL)

/* maximum size of a single DMA transfer descriptor */
#define XDMA_DESC_BLEN_BITS 	28
#define XDMA_DESC_BLEN_MAX	((1 << (XDMA_DESC_BLEN_BITS)) - 1)

/* bits of the SG DMA control register */
#define XDMA_CTRL_RUN_STOP			(1UL << 0)
#define XDMA_CTRL_IE_DESC_STOPPED		(1UL << 1)
#define XDMA_CTRL_IE_DESC_COMPLETED		(1UL << 2)
#define XDMA_CTRL_IE_DESC_ALIGN_MISMATCH	(1UL << 3)
#define XDMA_CTRL_IE_MAGIC_STOPPED		(1UL << 4)
#define XDMA_CTRL_IE_IDLE_STOPPED		(1UL << 6)
#define XDMA_CTRL_IE_READ_ERROR			(0x1FUL << 9)
#define XDMA_CTRL_IE_DESC_ERROR			(0x1FUL << 19)
#define XDMA_CTRL_NON_INCR_ADDR			(1UL << 25)
#define XDMA_CTRL_POLL_MODE_WB			(1UL << 26)

/* bits of the SG DMA status register */
#define XDMA_STAT_BUSY			(1UL << 0)
#define XDMA_STAT_DESC_STOPPED		(1UL << 1)
#define XDMA_STAT_DESC_COMPLETED	(1UL << 2)
#define XDMA_STAT_ALIGN_MISMATCH	(1UL << 3)
#define XDMA_STAT_MAGIC_STOPPED		(1UL << 4)
#define XDMA_STAT_INVALID_LEN		(1UL << 5)
#define XDMA_STAT_IDLE_STOPPED		(1UL << 6)

#define XDMA_STAT_COMMON_ERR_MASK \
	(XDMA_STAT_ALIGN_MISMATCH | XDMA_STAT_MAGIC_STOPPED | \
	 XDMA_STAT_INVALID_LEN)

/* desc_error, C2H & H2C */
#define XDMA_STAT_DESC_UNSUPP_REQ	(1UL << 19)
#define XDMA_STAT_DESC_COMPL_ABORT	(1UL << 20)
#define XDMA_STAT_DESC_PARITY_ERR	(1UL << 21)
#define XDMA_STAT_DESC_HEADER_EP	(1UL << 22)
#define XDMA_STAT_DESC_UNEXP_COMPL	(1UL << 23)

#define XDMA_STAT_DESC_ERR_MASK	\
	(XDMA_STAT_DESC_UNSUPP_REQ | XDMA_STAT_DESC_COMPL_ABORT | \
	 XDMA_STAT_DESC_PARITY_ERR | XDMA_STAT_DESC_HEADER_EP | \
	 XDMA_STAT_DESC_UNEXP_COMPL)

/* read error: H2C */
#define XDMA_STAT_H2C_R_UNSUPP_REQ	(1UL << 9)
#define XDMA_STAT_H2C_R_COMPL_ABORT	(1UL << 10)
#define XDMA_STAT_H2C_R_PARITY_ERR	(1UL << 11)
#define XDMA_STAT_H2C_R_HEADER_EP	(1UL << 12)
#define XDMA_STAT_H2C_R_UNEXP_COMPL	(1UL << 13)

#define XDMA_STAT_H2C_R_ERR_MASK	\
	(XDMA_STAT_H2C_R_UNSUPP_REQ | XDMA_STAT_H2C_R_COMPL_ABORT | \
	 XDMA_STAT_H2C_R_PARITY_ERR | XDMA_STAT_H2C_R_HEADER_EP | \
	 XDMA_STAT_H2C_R_UNEXP_COMPL)

/* write error, H2C only */
#define XDMA_STAT_H2C_W_DECODE_ERR	(1UL << 14)
#define XDMA_STAT_H2C_W_SLAVE_ERR	(1UL << 15)

#define XDMA_STAT_H2C_W_ERR_MASK	\
	(XDMA_STAT_H2C_W_DECODE_ERR | XDMA_STAT_H2C_W_SLAVE_ERR)

/* read error: C2H */
#define XDMA_STAT_C2H_R_DECODE_ERR	(1UL << 9)
#define XDMA_STAT_C2H_R_SLAVE_ERR	(1UL << 10)

#define XDMA_STAT_C2H_R_ERR_MASK	\
	(XDMA_STAT_C2H_R_DECODE_ERR | XDMA_STAT_C2H_R_SLAVE_ERR)

/* all combined */
#define XDMA_STAT_H2C_ERR_MASK	\
	(XDMA_STAT_COMMON_ERR_MASK | XDMA_STAT_DESC_ERR_MASK | \
	 XDMA_STAT_H2C_R_ERR_MASK | XDMA_STAT_H2C_W_ERR_MASK) 

#define XDMA_STAT_C2H_ERR_MASK	\
	(XDMA_STAT_COMMON_ERR_MASK | XDMA_STAT_DESC_ERR_MASK | \
	 XDMA_STAT_C2H_R_ERR_MASK)

/* bits of the SGDMA descriptor control field */
#define XDMA_DESC_STOPPED	(1UL << 0)
#define XDMA_DESC_COMPLETED	(1UL << 1)
#define XDMA_DESC_EOP		(1UL << 4)

#define XDMA_PERF_RUN	(1UL << 0)
#define XDMA_PERF_CLEAR	(1UL << 1)
#define XDMA_PERF_AUTO	(1UL << 2)

#define MAGIC_ENGINE	0xEEEEEEEEUL
#define MAGIC_DEVICE	0xDDDDDDDDUL

/* upper 16-bits of engine identifier register */
/* DMA Subsystem for PCIe identifier + Channel target */
#define XDMA_ID_H2C 0x1fc0U	
#define XDMA_ID_C2H 0x1fc1U

/* for C2H AXI-ST mode */
#define CYCLIC_RX_PAGES_MAX	RX_DESC_NUM	

#define LS_BYTE_MASK 0x000000FFUL

#define BLOCK_ID_MASK 0xFFF00000
#define BLOCK_ID_HEAD 0x1FC00000

#define IRQ_BLOCK_ID 0x1fc20000UL
#define CONFIG_BLOCK_ID 0x1fc30000UL

#define WB_COUNT_MASK 0x00ffffffUL
#define WB_ERR_MASK (1UL << 31)
#define POLL_TIMEOUT_SECONDS 10

#define MAX_USER_IRQ 8			// Default:16

#define MAX_DESC_BUS_ADDR (0xffffffffULL)

#define DESC_MAGIC 0xAD4B0000UL

#define C2H_WB 0x52B40000UL

#define MAX_NUM_ENGINES (XDMA_CHANNEL_NUM_MAX * 2)
#define H2C_CHANNEL_OFFSET 0x1000
#define SGDMA_OFFSET_FROM_CHANNEL 0x4000
#define CHANNEL_SPACING 0x100
#define TARGET_SPACING 0x1000

#define BYPASS_MODE_SPACING 0x0100

#define INTR_RAGE   32
#define INTR_PTPPS  0x10000
#define INTR_PTPAM  0x20000
#define INTR_EPS    0x40000
#define INTR_SYNCI  0x80000
#define INTR_QBCCR  0x100000
#define INTR_QBCAR  0x200000
#define INTR_TSCHG  0x400000

/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define PCI_DMA_H(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define PCI_DMA_L(addr) (addr & 0xffffffffUL)

#ifndef VM_RESERVED
	#define VMEM_FLAGS (VM_IO | VM_DONTEXPAND | VM_DONTDUMP)
#else
	#define VMEM_FLAGS (VM_IO | VM_RESERVED)
#endif

//#define __LIBXDMA_DEBUG__

#ifdef __LIBXDMA_DEBUG__
#define dbg_io		pr_err
#define dbg_fops	pr_err
#define dbg_perf	pr_err
#define dbg_sg		pr_err
//#define dbg_tfr		pr_err
#define dbg_tfr(...)
//#define dbg_irq		pr_err
#define dbg_irq(...)
#define dbg_init	pr_err
#define dbg_desc	pr_err
#else
/* disable debugging */
#define dbg_io(...)
#define dbg_fops(...)
#define dbg_perf(...)
#define dbg_sg(...)
#define dbg_tfr(...)
#define dbg_irq(...)
#define dbg_init(...)
#define dbg_desc(...)
#endif

//-----------------------------------------------------------------------------
// ASIX NIC Control/Status Registers
//-----------------------------------------------------------------------------
enum {
  MAC_MODULE_VERSION	= 0x0000,	/* MAC Module Version */  
  RX_CTRL		= 0x0004,	/* RX Control */
	#define AX_RX_CTL_PRO			(1 << 0)
	#define AX_RX_CTL_AB			(1 << 1)
	#define AX_RX_CTL_AM			(1 << 2)
	#define AX_RX_CTL_AMALL			(1 << 3)
  NODE_ID_0		= 0x0008,	/* Node ID reg. 0 */
  NODE_ID_1		= 0x000C,	/* Node ID reg. 1 */
  MULTI_FILTER_ARR_0	= 0x0010,	/* Multicast Filter Array Reg. 0 */
  MULTI_FILTER_ARR_1	= 0x0014,	/* Multicast Filter Array Reg. 1 */
  GLOBAL_MAC_CONFIG	= 0x0018,	/* Global MAC Configuration */
	#define AX_PLINK			(1 << 0)
  SWITCH_ISR		= 0x0040,	/* Switch Interrupt Status Reg. */
};
#define AX_MCAST_FILTER_SIZE		8
#define AX_MAX_MCAST			64
// End of ASIX NIC Register ---------------------------------------------------

/* SECTION: Enum definitions */
enum transfer_state {
	TRANSFER_STATE_NEW = 0,
	TRANSFER_STATE_SUBMITTED,
	TRANSFER_STATE_COMPLETED,
	TRANSFER_STATE_FAILED,
	TRANSFER_STATE_ABORTED
};

enum shutdown_state {
	ENGINE_SHUTDOWN_NONE = 0,	/* No shutdown in progress */
	ENGINE_SHUTDOWN_REQUEST = 1,	/* engine requested to shutdown */
	ENGINE_SHUTDOWN_IDLE = 2	/* engine has shutdown and is idle */
};

enum dev_capabilities {
	CAP_64BIT_DMA = 2,
	CAP_64BIT_DESC = 4,
	CAP_ENGINE_WRITE = 8,
	CAP_ENGINE_READ = 16
};

/* SECTION: Structure definitions */

struct config_regs {
	u32 identifier;
	u32 reserved_1[4];
	u32 msi_enable;
};

/**
 * SG DMA Controller status and control registers
 *
 * These registers make the control interface for DMA transfers.
 *
 * It sits in End Point (FPGA) memory BAR[0] for 32-bit or BAR[0:1] for 64-bit.
 * It references the first descriptor which exists in Root Complex (PC) memory.
 *
 * @note The registers must be accessed using 32-bit (PCI DWORD) read/writes,
 * and their values are in little-endian byte ordering.
 */
struct engine_regs {
	u32 identifier;
	u32 control;
	u32 control_w1s;
	u32 control_w1c;
	u32 reserved_1[12];	/* padding */

	u32 status;
	u32 status_rc;
	u32 completed_desc_count;
	u32 alignments;
	u32 reserved_2[14];	/* padding */

	u32 poll_mode_wb_lo;
	u32 poll_mode_wb_hi;
	u32 interrupt_enable_mask;
	u32 interrupt_enable_mask_w1s;
	u32 interrupt_enable_mask_w1c;
	u32 reserved_3[9];	/* padding */

	u32 perf_ctrl;
	u32 perf_cyc_lo;
	u32 perf_cyc_hi;
	u32 perf_dat_lo;
	u32 perf_dat_hi;
	u32 perf_pnd_lo;
	u32 perf_pnd_hi;
} __packed;

struct engine_sgdma_regs {
	u32 identifier;
	u32 reserved_1[31];	/* padding */

	/* bus address to first descriptor in Root Complex Memory */
	u32 first_desc_lo;
	u32 first_desc_hi;
	/* number of adjacent descriptors at first_desc */
	u32 first_desc_adjacent;
	u32 credits;
} __packed;

struct msix_vec_table_entry {
	u32 msi_vec_addr_lo;
	u32 msi_vec_addr_hi;
	u32 msi_vec_data_lo;
	u32 msi_vec_data_hi;
} __packed;

struct msix_vec_table {
	struct msix_vec_table_entry entry_list[32];
} __packed;

struct interrupt_regs {
	u32 identifier;
	u32 user_int_enable;
	u32 user_int_enable_w1s;
	u32 user_int_enable_w1c;
	u32 channel_int_enable;
	u32 channel_int_enable_w1s;
	u32 channel_int_enable_w1c;
	u32 reserved_1[9];	/* padding */

	u32 user_int_request;
	u32 channel_int_request;
	u32 user_int_pending;
	u32 channel_int_pending;
	u32 reserved_2[12];	/* padding */

	u32 user_msi_vector[8];
	u32 channel_msi_vector[8];
} __packed;

struct sgdma_common_regs {
	u32 padding[8];
	u32 credit_mode_enable;
	u32 credit_mode_enable_w1s;
	u32 credit_mode_enable_w1c;
} __packed;

/**
 * Descriptor for a single contiguous memory block transfer.
 *
 * Multiple descriptors are linked by means of the next pointer. An additional
 * extra adjacent number gives the amount of extra contiguous descriptors.
 *
 * The descriptors are in root complex memory, and the bytes in the 32-bit
 * words must be in little-endian byte ordering.
 */
struct xdma_desc {
	u32 control;
	u32 bytes;		/* transfer length in bytes */
	u32 src_addr_lo;	/* source address (low 32-bit) */
	u32 src_addr_hi;	/* source address (high 32-bit) */
	u32 dst_addr_lo;	/* destination address (low 32-bit) */
	u32 dst_addr_hi;	/* destination address (high 32-bit) */
	/*
	 * next descriptor in the single-linked list of descriptors;
	 * this is the PCIe (bus) address of the next descriptor in the
	 * root complex memory
	 */
	u32 next_lo;		/* next desc address (low 32-bit) */
	u32 next_hi;		/* next desc address (high 32-bit) */
} __packed;

/* 32 bytes (four 32-bit words) or 64 bytes (eight 32-bit words) */
struct xdma_result {
	u32 status;
	u32 length;
	u32 reserved_1[6];	/* padding */
} __packed;

struct sw_desc {
	dma_addr_t addr;
	unsigned int len;
};

#define ALIGN_8_BYTES(x)	((x + 0x7) & (~0x7))	

struct nic_tx_header {
	u16	length;
	u8	reserved[4];
	u8	version;
	u8	priority;
}__packed;

/* Rx and Tx buffer descriptors. */
struct ring_info {
	struct sk_buff	*skb;
	dma_addr_t	mapping;
	long		len;
};

struct ax_dsa_data {
	unsigned char dsa_mac[4][8];
};

struct packet_buff {
	char data[DMA_PACKET_SIZE];
} __packed;

struct ax_tx_list {
	u32 idle;
	u16 cur;
	u32 cur_pkt_count;
};

struct ax_ethphy_status {
	int speed;
	int duplex;
	int pause;
	int asym_pause;
	int link;
};

struct ax_private {
	struct net_device      *dev;
	struct pci_dev         *pdev;
	struct xdma_dev        *xdev;
#define MDIO_BRIDGE	0x204
	struct mii_bus	       *mdio;
	struct mdio_device     *mdiodev;

	spinlock_t		mdio_lock;
	spinlock_t		lock;
	spinlock_t		rx_lock;

	struct net_device_stats net_stats;
	
	struct ax_tx_list	tx_list[XDMA_CHANNEL_NUM_MAX];
	struct ring_info	tx_buffers[TX_DESC_NUM];
	struct ring_info	rx_buffers[RX_DESC_NUM];
	struct sk_buff_head 	tx_timestamp;
	__u32			rx_flags;
	__u16			cur_rx;
	__u16			dirty_rx;
	u32			cur_rx_pkt_count;
	u8			stop_queue_channel;

	struct mii_if_info      dsa_mii[4];

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,24)
	struct napi_struct	napi;
#endif

	struct ax_switch 	axswitch;

	struct ax_dsa_data ax_dsa;
	char ax_dsa_port_names[DSA_MAX_PORTS][24];
	/* Proc file */
#define ASIX_PROC_DIR	"ASIX"
	struct proc_dir_entry	*proc_dir;
#define ASIX_PROC_IFNAME_OLD	"ifname_old"
	struct proc_dir_entry 	*pron_ifname_old;
#define ASIX_PROC_IFNAME_NEW	"ifname_new"
	struct proc_dir_entry 	*pron_ifname_new;
	char ax_netdev_oldname[24];
	
	struct timer_list 	ax_phy_timer;
	struct ax_ethphy_status phy_status[4];
};


/* Describes a (SG DMA) single transfer for the engine */
struct xdma_transfer {
	struct list_head entry;        /* queue of non-completed transfers */
	struct xdma_desc *desc_virt;   /* virt addr of the 1st descriptor */
	dma_addr_t desc_bus;           /* bus addr of the first descriptor */
	int desc_adjacent;             /* adjacent descriptors at desc_bus */
	int desc_num;                  /* number of descriptors in transfer */
	enum dma_data_direction dir;
	wait_queue_head_t wq;          /* wait queue for transfer completion */

	enum transfer_state state;	/* state of the transfer */
	unsigned int flags;
#define XFER_FLAG_NEED_UNMAP	0x1
	int cyclic;			/* flag if transfer is cyclic */
	int last_in_request;		/* flag if last within request */
	unsigned int len;

	
	dma_addr_t list_desc[DESC_LIST_NUM];
	/* For TX */
	u8 current_list;
};

struct xdma_request_cb {
	struct sg_table *sgt;
	unsigned int total_len;
	u64 ep_addr;

	struct xdma_transfer xfer;

	unsigned int sw_desc_idx;
	unsigned int sw_desc_cnt;
	struct sw_desc sdesc[0];
};

struct xdma_engine {
	unsigned long magic;	/* structure ID for sanity checks */
	struct xdma_dev *xdev;	/* parent device */
	char name[5];		/* name of this engine */
	int version;		/* version of this engine */

	/* HW register address offsets */
	struct engine_regs *regs;		/* Control reg BAR offset */
	struct engine_sgdma_regs *sgdma_regs;	/* SGDAM reg BAR offset */
	u32 bypass_offset;			/* Bypass mode BAR offset */

	/* Engine state, configuration and flags */
	enum shutdown_state shutdown;	/* engine shutdown mode */
	enum dma_data_direction dir;
	int device_open;	/* flag if engine node open, ST mode only */
	int running;		/* flag if the driver started engine */
	int non_incr_addr;	/* flag if non-incremental addressing used */
	int streaming;
	int addr_align;		/* source/dest alignment in bytes */
	int len_granularity;	/* transfer length multiple */
	int addr_bits;		/* HW datapath address width */
	int channel;		/* engine indices */
	int max_extra_adj;	/* descriptor prefetch capability */
	int desc_dequeued;	/* num descriptors of completed transfers */
	u32 status;		/* last known status of device */
	/* only used for MSIX mode to store per-engine interrupt mask value */
	u32 interrupt_enable_mask_value;
	
	struct xdma_transfer *transfer;

	/* Members applicable to AXI-ST C2H (cyclic) transfers */
	struct xdma_result *cyclic_result;
	dma_addr_t cyclic_result_bus;	/* bus addr for transfer */
	/* RX packet buffer */
	struct packet_buff *rx_buff;
	dma_addr_t rx_buff_bus;	/* bus addr for transfer */

	/* TX packet buffer */
	struct packet_buff *tx_buff;
	dma_addr_t tx_buff_bus;	/* bus addr for transfer */

	int rx_tail;	/* follows the HW */
	int rx_head;	/* where the SW reads from */
	int rx_overrun;	/* flag if overrun occured */

	/* Members associated with interrupt mode support */
	wait_queue_head_t shutdown_wq;	/* wait queue for shutdown sync */
	spinlock_t lock;		/* protects concurrent access */
	int prev_cpu;			/* remember CPU# of (last) locker */
	int msix_irq_line;		/* MSI-X vector for this engine */
	u32 irq_bitmask;		/* IRQ bit mask for this engine */
	struct work_struct work;	/* Work queue for interrupt handling */

	spinlock_t desc_lock;		/* protects concurrent access */
	dma_addr_t desc_bus;
	struct xdma_desc *desc;
};

struct xdma_user_irq {
	struct xdma_dev *xdev;        /* parent device */
	u8 user_idx;                  /* 0 ~ 15 */
	u8 events_irq;                /* accumulated IRQs */
	spinlock_t events_lock;       /* lock to safely update events_irq */
	wait_queue_head_t events_wq;  /* wait queue to sync waiting threads */
	irq_handler_t handler;

	void *dev;	
};

/* XDMA PCIe device specific book-keeping */
#define XDEV_FLAG_OFFLINE	0x1
struct xdma_dev {
	struct list_head list_head;
        struct list_head rcu_node;

	unsigned long magic;		/* structure ID for sanity checks */
	struct pci_dev *pdev;	/* pci device struct from probe() */
	int idx;		/* dev index */

	const char *mod_name;		/* name of module owning the dev */

	spinlock_t lock;		/* protects concurrent access */
	unsigned int flags;

	/* PCIe BAR management */
	void *__iomem bar[XDMA_BAR_NUM];	/* addresses for mapped BARs */
	resource_size_t phy_bar[XDMA_BAR_NUM];
	int user_bar_idx;	/* BAR index of user logic */
	int config_bar_idx;	/* BAR index of XDMA config logic */
	int bypass_bar_idx;	/* BAR index of XDMA bypass logic */
	int regions_in_use;	/* flag if dev was in use during probe() */
	int got_regions;	/* flag if probe() obtained the regions */

	int user_max;
	int c2h_channel_max;
	int h2c_channel_max;

	/* Interrupt management */
	int irq_count;		/* interrupt counter */
	int irq_line;		/* flag if irq allocated successfully */
	int msi_enabled;	/* flag if msi was enabled for the device */
	int msix_enabled;	/* flag if msi-x was enabled for the device */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	struct msix_entry entry[32];	/* msi-x vector/entry table */
#endif
	/* user IRQ management */
	struct xdma_user_irq user_irq[MAX_USER_IRQ];
	unsigned int mask_irq_user;

	/* XDMA engine management */
	int engines_num;	/* Total engine count */
	u32 mask_irq_h2c;
	u32 mask_irq_c2h;
	struct xdma_engine engine_h2c[XDMA_CHANNEL_NUM_MAX];
	struct xdma_engine engine_c2h[XDMA_CHANNEL_NUM_MAX];

	/* SD_Accel specific */
	enum dev_capabilities capabilities;
	u64 feature_id;
};

static inline int xdma_device_flag_check(struct xdma_dev *xdev, unsigned int f)
{
	unsigned long flags;

	spin_lock_irqsave(&xdev->lock, flags);
	if (xdev->flags & f) {
		spin_unlock_irqrestore(&xdev->lock, flags);
		return 1;
	}
	spin_unlock_irqrestore(&xdev->lock, flags);
	return 0;
}

static inline int xdma_device_flag_test_n_set(struct xdma_dev *xdev,
					 unsigned int f)
{
	unsigned long flags;
	int rv = 0;

	spin_lock_irqsave(&xdev->lock, flags);
	if (xdev->flags & f) {
		spin_unlock_irqrestore(&xdev->lock, flags);
		rv = 1;
	} else
		xdev->flags |= f;
	spin_unlock_irqrestore(&xdev->lock, flags);
	return rv;
}

static inline void xdma_device_flag_set(struct xdma_dev *xdev, unsigned int f)
{
	unsigned long flags;

	spin_lock_irqsave(&xdev->lock, flags);
	xdev->flags |= f;
	spin_unlock_irqrestore(&xdev->lock, flags);
}

static inline void 
xdma_device_flag_clear(struct xdma_dev *xdev, unsigned int f)
{
	unsigned long flags;

	spin_lock_irqsave(&xdev->lock, flags);
	xdev->flags &= ~f;
	spin_unlock_irqrestore(&xdev->lock, flags);
}

struct xdma_dev *xdev_find_by_pdev(struct pci_dev *pdev);

void xdma_device_offline(struct pci_dev *pdev, void *dev_handle);
void xdma_device_online(struct pci_dev *pdev, void *dev_handle);

int xdma_desc_setup(struct xdma_dev *xdev, struct xdma_engine *engine);

struct xdma_transfer *engine_cyclic_stop(struct xdma_engine *engine);

int engine_addrmode_set(struct xdma_engine *engine, unsigned long arg);

struct xdma_transfer *engine_start(struct xdma_engine *engine);

u32 engine_status_read(struct xdma_engine *engine, bool clear, bool dump);

void xdma_engine_stop(struct xdma_engine *engine);

int ax_net_poll(struct napi_struct *napi, int budget);

int xdma_user_isr_register(struct xdma_dev *xdev);
int xdma_user_isr_enable(struct xdma_dev *xdev, unsigned int mask);
int xdma_user_isr_disable(struct xdma_dev *xdev, unsigned int mask);
#endif /* XDMA_LIB_H */
