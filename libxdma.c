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

#define pr_fmt(fmt)     KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include "libxdma.h"
#include "libxdma_api.h"
#include "xdma_mod.h"
#include "switch/ax_ptp.h"
#include "switch/ax_switch.h"

/* SECTION: Module licensing */

#ifdef __LIBXDMA_MOD__
#include "version.h"
#define DRV_MODULE_NAME		"AXM57104"
#define DRV_MODULE_DESC		"ASIX PCIe NIC Driver"
#define DRV_MODULE_RELDATE	"2020/07"

static char version[] =
        DRV_MODULE_DESC " " DRV_MODULE_NAME " v" DRV_MODULE_VERSION "\n";

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
#endif

/* Module Parameters */
static unsigned int enable_credit_mp = 0;

/*
 * xdma device management
 * maintains a list of the xdma devices
 */
static LIST_HEAD(xdev_list);
static DEFINE_MUTEX(xdev_mutex);

static LIST_HEAD(xdev_rcu_list);
static DEFINE_SPINLOCK(xdev_rcu_lock);

#ifndef list_last_entry
#define list_last_entry(ptr, type, member) \
		list_entry((ptr)->prev, type, member)
#endif

static inline void xdev_list_add(struct xdma_dev *xdev)
{
	mutex_lock(&xdev_mutex);
	if (list_empty(&xdev_list)) {
		xdev->idx = 0;
	}
	else {
		struct xdma_dev *last;

		last = list_last_entry(&xdev_list, struct xdma_dev, list_head);
		xdev->idx = last->idx + 1;
	}
	list_add_tail(&xdev->list_head, &xdev_list);
	mutex_unlock(&xdev_mutex);

	dbg_init("dev %s, xdev 0x%p, xdma idx %d.\n",
		dev_name(&xdev->pdev->dev), xdev, xdev->idx);

	spin_lock(&xdev_rcu_lock);
	list_add_tail_rcu(&xdev->rcu_node, &xdev_rcu_list);
	spin_unlock(&xdev_rcu_lock);
}

#undef list_last_entry

static inline void xdev_list_remove(struct xdma_dev *xdev)
{
	mutex_lock(&xdev_mutex);
	list_del(&xdev->list_head);
	mutex_unlock(&xdev_mutex);

	spin_lock(&xdev_rcu_lock);
	list_del_rcu(&xdev->rcu_node);
	spin_unlock(&xdev_rcu_lock);
	synchronize_rcu();
}

struct xdma_dev *xdev_find_by_pdev(struct pci_dev *pdev)
{
        struct xdma_dev *xdev, *tmp;

        mutex_lock(&xdev_mutex);
        list_for_each_entry_safe(xdev, tmp, &xdev_list, list_head) {
                if (xdev->pdev == pdev) {
                        mutex_unlock(&xdev_mutex);
                        return xdev;
                }
        }
        mutex_unlock(&xdev_mutex);
        return NULL;
}
EXPORT_SYMBOL_GPL(xdev_find_by_pdev);

//
static inline int debug_check_dev_hndl(const char *fname, struct pci_dev *pdev,
				 void *hndl)
{
	struct xdma_dev *xdev;

	if (!pdev)
		return -EINVAL;

	xdev = xdev_find_by_pdev(pdev);
	if (!xdev) {
		pr_info("%s pdev 0x%p, hndl 0x%p, NO match found!\n",
			fname, pdev, hndl);
		return -EINVAL;
	}
	if (xdev != hndl) {
		pr_err("%s pdev 0x%p, hndl 0x%p != 0x%p!\n",
			fname, pdev, hndl, xdev);
		return -EINVAL;
	}

	return 0;
}

#ifdef __LIBXDMA_DEBUG__
/* SECTION: Function definitions */
inline void 
__write_register(const char *fn, u32 value, void *iomem, unsigned long off)
{
	iowrite32(value, iomem);
}
#define write_register(v,mem,off) __write_register(__func__, v, mem, off)
#else
#define write_register(v,mem,off) iowrite32(v, mem)
#endif

inline u32 read_register(void *iomem)
{
	return ioread32(iomem);
}

static inline u32 build_u32(u32 hi, u32 lo)
{
	return ((hi & 0xFFFFUL) << 16) | (lo & 0xFFFFUL);
}

static inline u64 build_u64(u64 hi, u64 lo)
{
	return ((hi & 0xFFFFFFFULL) << 32) | (lo & 0xFFFFFFFFULL);
}

//
static void check_nonzero_interrupt_status(struct xdma_dev *xdev)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);
	u32 w;

	w = read_register(&reg->user_int_enable);
	if (w) {
		pr_info("%s xdma%d user_int_enable = 0x%08x\n",
			dev_name(&xdev->pdev->dev), xdev->idx, w);
	}

	w = read_register(&reg->channel_int_enable);
	if (w) {
		pr_info("%s xdma%d channel_int_enable = 0x%08x\n",
			dev_name(&xdev->pdev->dev), xdev->idx, w);
	}

	w = read_register(&reg->user_int_request);
	if (w) {
		pr_info("%s xdma%d user_int_request = 0x%08x\n",
			dev_name(&xdev->pdev->dev), xdev->idx, w);
	}
	w = read_register(&reg->channel_int_request);
	if (w) {
		pr_info("%s xdma%d channel_int_request = 0x%08x\n",
			dev_name(&xdev->pdev->dev), xdev->idx, w);
	}

	w = read_register(&reg->user_int_pending);
	if (w) {
		pr_info("%s xdma%d user_int_pending = 0x%08x\n",
			dev_name(&xdev->pdev->dev), xdev->idx, w);
	}
	w = read_register(&reg->channel_int_pending);
	if (w) {
		pr_info("%s xdma%d channel_int_pending = 0x%08x\n",
			dev_name(&xdev->pdev->dev), xdev->idx, w);
	}
}

/* channel_interrupts_enable -- Enable interrupts we are interested in */
static void channel_interrupts_enable(struct xdma_dev *xdev, u32 mask)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);

	write_register(mask, &reg->channel_int_enable_w1s, XDMA_OFS_INT_CTRL);
}

/* channel_interrupts_disable -- Disable interrupts we not interested in */
static void channel_interrupts_disable(struct xdma_dev *xdev, u32 mask)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);

	write_register(mask, &reg->channel_int_enable_w1c, XDMA_OFS_INT_CTRL);
}

/* user_interrupts_enable -- Enable interrupts we are interested in */
static void user_interrupts_enable(struct xdma_dev *xdev, u32 mask)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);

	write_register(mask, &reg->user_int_enable_w1s, XDMA_OFS_INT_CTRL);
}

/* user_interrupts_disable -- Disable interrupts we not interested in */
static void user_interrupts_disable(struct xdma_dev *xdev, u32 mask)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);

	write_register(mask, &reg->user_int_enable_w1c, XDMA_OFS_INT_CTRL);
}

/* read_interrupts -- Print the interrupt controller status */
static u32 read_interrupts(struct xdma_dev *xdev)
{
	struct interrupt_regs *reg = (struct interrupt_regs *)
		(xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);
	u32 lo;
	u32 hi;

	/* extra debugging; inspect complete engine set of registers */
	hi = read_register(&reg->user_int_request);
	dbg_io("ioread32(0x%p) returned 0x%08x (user_int_request).\n",
		&reg->user_int_request, hi);
	lo = read_register(&reg->channel_int_request);
	dbg_io("ioread32(0x%p) returned 0x%08x (channel_int_request)\n",
		&reg->channel_int_request, lo);

	/* 
	return interrupts: user in upper 16-bits, channel in lower 16-bits 
	*/
	return build_u32(hi, lo);
}

static void engine_reg_dump(struct xdma_engine *engine)
{
	u32 w;

	BUG_ON(!engine);

	w = read_register(&engine->regs->identifier);
	pr_info("%s: ioread32(0x%p) = 0x%08x (id).\n",
		engine->name, &engine->regs->identifier, w);
	w &= BLOCK_ID_MASK;
	if (w != BLOCK_ID_HEAD) {
		pr_info("%s: engine id missing, 0x%08x exp.& 0x%x = 0x%x\n",
			 engine->name, w, BLOCK_ID_MASK, BLOCK_ID_HEAD);
		return;
	}
	/* extra debugging; inspect complete engine set of registers */
	w = read_register(&engine->regs->status);
	pr_info("%s: ioread32(0x%p) = 0x%08x (status).\n",
		engine->name, &engine->regs->status, w);
	w = read_register(&engine->regs->control);
	pr_info("%s: ioread32(0x%p) = 0x%08x (control)\n",
		engine->name, &engine->regs->control, w);
	w = read_register(&engine->sgdma_regs->first_desc_lo);
	pr_info("%s: ioread32(0x%p) = 0x%08x (first_desc_lo)\n",
		engine->name, &engine->sgdma_regs->first_desc_lo, w);
	w = read_register(&engine->sgdma_regs->first_desc_hi);
	pr_info("%s: ioread32(0x%p) = 0x%08x (first_desc_hi)\n",
		engine->name, &engine->sgdma_regs->first_desc_hi, w);
	w = read_register(&engine->sgdma_regs->first_desc_adjacent);
	pr_info("%s: ioread32(0x%p) = 0x%08x (first_desc_adjacent).\n",
		engine->name, &engine->sgdma_regs->first_desc_adjacent, w);
	w = read_register(&engine->regs->completed_desc_count);
	pr_info("%s: ioread32(0x%p) = 0x%08x (completed_desc_count).\n",
		engine->name, &engine->regs->completed_desc_count, w);
	w = read_register(&engine->regs->interrupt_enable_mask);
	pr_info("%s: ioread32(0x%p) = 0x%08x (interrupt_enable_mask)\n",
		engine->name, &engine->regs->interrupt_enable_mask, w);
}

/**
 * engine_status_read() - read status of SG DMA engine (optionally reset)
 *
 * Stores status in engine->status.
 *
 * @return -1 on failure, status register otherwise
 */
static void engine_status_dump(struct xdma_engine *engine)
{
	u32 v = engine->status;
	char buffer[256];
	char *buf = buffer;
	int len = 0;

	len = sprintf(buf, "SG engine %s status: 0x%08x: ", engine->name, v);
	
	if ((v & XDMA_STAT_BUSY)) {
		len += sprintf(buf + len, "BUSY,");
	}
	if ((v & XDMA_STAT_DESC_STOPPED)) {
		len += sprintf(buf + len, "DESC_STOPPED,");
	}
	if ((v & XDMA_STAT_DESC_COMPLETED)) {
		len += sprintf(buf + len, "DESC_COMPL,");
	}

	/* common H2C & C2H */	
 	if ((v & XDMA_STAT_COMMON_ERR_MASK)) {
		if ((v & XDMA_STAT_ALIGN_MISMATCH)) {
			len += sprintf(buf + len, "ALIGN_MISMATCH ");
		}
		if ((v & XDMA_STAT_MAGIC_STOPPED)) {
			len += sprintf(buf + len, "MAGIC_STOPPED ");
		}
		if ((v & XDMA_STAT_INVALID_LEN)) {
			len += sprintf(buf + len, "INVLIAD_LEN ");
		}
		if ((v & XDMA_STAT_IDLE_STOPPED)) {
			len += sprintf(buf + len, "IDLE_STOPPED ");
		}
		buf[len - 1] = ',';
	}

	if ((engine->dir == DMA_TO_DEVICE)) {
		/* H2C only */
		if ((v & XDMA_STAT_H2C_R_ERR_MASK)) {
			len += sprintf(buf + len, "R:");
			if ((v & XDMA_STAT_H2C_R_UNSUPP_REQ)) {
				len += sprintf(buf + len, "UNSUPP_REQ ");
			}
			if ((v & XDMA_STAT_H2C_R_COMPL_ABORT)) {
				len += sprintf(buf + len, "COMPL_ABORT ");
			}
			if ((v & XDMA_STAT_H2C_R_PARITY_ERR)) {
				len += sprintf(buf + len, "PARITY ");
			}
			if ((v & XDMA_STAT_H2C_R_HEADER_EP)) {
				len += sprintf(buf + len, "HEADER_EP ");
			}
			if ((v & XDMA_STAT_H2C_R_UNEXP_COMPL)) {
				len += sprintf(buf + len, "UNEXP_COMPL ");
			}
			buf[len - 1] = ',';
		}

		if ((v & XDMA_STAT_H2C_W_ERR_MASK)) {
			len += sprintf(buf + len, "W:");
			if ((v & XDMA_STAT_H2C_W_DECODE_ERR)) {
				len += sprintf(buf + len, "DECODE_ERR ");
			}
			if ((v & XDMA_STAT_H2C_W_SLAVE_ERR)) {
				len += sprintf(buf + len, "SLAVE_ERR ");
			}
			buf[len - 1] = ',';
		}
		
	} else {
		/* C2H only */
		if ((v & XDMA_STAT_C2H_R_ERR_MASK)) {
			len += sprintf(buf + len, "R:");
			if ((v & XDMA_STAT_C2H_R_DECODE_ERR)) {
				len += sprintf(buf + len, "DECODE_ERR ");
			}
			if ((v & XDMA_STAT_C2H_R_SLAVE_ERR)) {
				len += sprintf(buf + len, "SLAVE_ERR ");
			}
			buf[len - 1] = ',';
		}
	}

	/* common H2C & C2H */	
 	if ((v & XDMA_STAT_DESC_ERR_MASK)) {
		len += sprintf(buf + len, "DESC_ERR:");
		if ((v & XDMA_STAT_DESC_UNSUPP_REQ)) {
			len += sprintf(buf + len, "UNSUPP_REQ ");
		}
		if ((v & XDMA_STAT_DESC_COMPL_ABORT)) {
			len += sprintf(buf + len, "COMPL_ABORT ");
		}
		if ((v & XDMA_STAT_DESC_PARITY_ERR)) {
			len += sprintf(buf + len, "PARITY ");
		}
		if ((v & XDMA_STAT_DESC_HEADER_EP)) {
			len += sprintf(buf + len, "HEADER_EP ");
		}
		if ((v & XDMA_STAT_DESC_UNEXP_COMPL)) {
			len += sprintf(buf + len, "UNEXP_COMPL ");
		}
		buf[len - 1] = ',';
	}

	buf[len - 1] = '\0';
	pr_info("%s\n", buffer);
}

u32 engine_status_read(struct xdma_engine *engine, bool clear, bool dump)
{
	u32 value;

	BUG_ON(!engine);

	if (dump) {
		engine_reg_dump(engine);
	}

	/* read status register */
	if (clear) {
		value = engine->status =
			read_register(&engine->regs->status);
		write_register(value, &engine->regs->status, 0);
	} else
		value = engine->status = read_register(&engine->regs->status);

	if (dump) {
		engine_status_dump(engine);
	}

	return value;
}

/**
 * xdma_engine_stop() - stop an SG DMA engine
 *
 */
void xdma_engine_stop(struct xdma_engine *engine)
{
	u32 w;

	BUG_ON(!engine);

	ASIX_DEBUG("xdma_engine_stop(engine=%p)\n", engine);

	w = 0;
	w |= (u32)XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
	w |= (u32)XDMA_CTRL_IE_MAGIC_STOPPED;
	w |= (u32)XDMA_CTRL_IE_READ_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ERROR;


	w |= (u32)XDMA_CTRL_IE_DESC_STOPPED;
	w |= (u32)XDMA_CTRL_IE_DESC_COMPLETED;

	dbg_tfr("Stopping SG DMA %s engine; writing 0x%08x to 0x%p.\n",
			engine->name, w, (u32 *)&engine->regs->control);
	write_register(w, &engine->regs->control,
			(unsigned long)(&engine->regs->control) -
			(unsigned long)(&engine->regs));
	/* dummy read of status register to flush all previous writes */
	dbg_tfr("xdma_engine_stop(%s) done\n", engine->name);
	engine->running = 0;
}

static void engine_start_mode_config(struct xdma_engine *engine)
{
	u32 w;

	BUG_ON(!engine);

	
	w = XDMA_CTRL_IE_DESC_STOPPED;
	w |= XDMA_CTRL_IE_DESC_COMPLETED;
	w |= XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
	w |= XDMA_CTRL_IE_READ_ERROR;
	w |= XDMA_CTRL_IE_DESC_ERROR;

	write_register(w, &engine->regs->interrupt_enable_mask,
			(unsigned long)(&engine->regs->interrupt_enable_mask) -
			(unsigned long)(&engine->regs));

	/* write control register of SG DMA engine */
	w = (u32)XDMA_CTRL_RUN_STOP;
	w |= (u32)XDMA_CTRL_IE_READ_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ERROR;
	w |= (u32)XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;

	w |= (u32)XDMA_CTRL_IE_DESC_STOPPED;
	w |= (u32)XDMA_CTRL_IE_DESC_COMPLETED;

	dbg_tfr("iowrite32(0x%08x to 0x%p) (control)\n", w,
			(void *)&engine->regs->control);
	/* start the engine */
	write_register(w, &engine->regs->control,
			(unsigned long)(&engine->regs->control) -
			(unsigned long)(&engine->regs));

	/* dummy read of status register to flush all previous writes */
	w = read_register(&engine->regs->status);
	dbg_tfr("ioread32(0x%p) = 0x%08x (dummy read flushes writes).\n",
			&engine->regs->status, w);
}

/**
 * engine_start() - start an idle engine with its first transfer on queue
 *
 * The engine will run and process all transfers that are queued using
 * transfer_queue() and thus have their descriptor lists chained.
 *
 * During the run, new transfers will be processed if transfer_queue() has
 * chained the descriptors before the hardware fetches the last descriptor.
 * A transfer that was chained too late will invoke a new run of the engine
 * initiated from the engine_service() routine.
 *
 * The engine must be idle and at least one transfer must be queued.
 * This function does not take locks; the engine spinlock must already be
 * taken.
 *
 */
struct xdma_transfer *engine_start(struct xdma_engine *engine)
{
	struct xdma_transfer *transfer = engine->transfer;
	dma_addr_t desc_bus = transfer->desc_bus;
	u32 w;
	int extra_adj = 0;

	/* engine must be idle */
	BUG_ON(engine->running);

	BUG_ON(!transfer);

	/* engine is no longer shutdown */
	engine->shutdown = ENGINE_SHUTDOWN_NONE;

	dbg_tfr("engine_start(%s): transfer=0x%p.\n"
		, engine->name, transfer);

	/* initialize number of descriptors of dequeued transfers */
	engine->desc_dequeued = 0;

	desc_bus = transfer->list_desc[transfer->current_list];
	
	/* write lower 32-bit of bus address of transfer first descriptor */
	w = cpu_to_le32(PCI_DMA_L(desc_bus));
	dbg_tfr("iowrite32(0x%08x to 0x%p) (first_desc_lo)\n", w,
			(void *)&engine->sgdma_regs->first_desc_lo);
	write_register(w, &engine->sgdma_regs->first_desc_lo,
			(unsigned long)(&engine->sgdma_regs->first_desc_lo) -
			(unsigned long)(&engine->sgdma_regs));
	/* write upper 32-bit of bus address of transfer first descriptor */
	w = cpu_to_le32(PCI_DMA_H(desc_bus));
	dbg_tfr("iowrite32(0x%08x to 0x%p) (first_desc_hi)\n", w,
			(void *)&engine->sgdma_regs->first_desc_hi);
	write_register(w, &engine->sgdma_regs->first_desc_hi,
			(unsigned long)(&engine->sgdma_regs->first_desc_hi) -
			(unsigned long)(&engine->sgdma_regs));

	dbg_tfr("iowrite32(0x%08x to 0x%p) (first_desc_adjacent)\n",
		extra_adj, (void *)&engine->sgdma_regs->first_desc_adjacent);
	write_register(extra_adj, &engine->sgdma_regs->first_desc_adjacent,
		(unsigned long)(&engine->sgdma_regs->first_desc_adjacent) -
		(unsigned long)(&engine->sgdma_regs));

	dbg_tfr("ioread32(0x%p) (dummy read flushes writes).\n",
		&engine->regs->status);	
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,1,0)
	mmiowb();
#endif
	engine_start_mode_config(engine);

	dbg_tfr("%s engine 0x%p now running\n", engine->name, engine);
	/* remember the engine is running */
	engine->running = 1;
	return transfer;
}


static irqreturn_t user_irq_service(int irq, struct xdma_user_irq *user_irq,
				    struct ax_private *ax_local)
{
	BUG_ON(!user_irq);

	if (user_irq->handler) {
		return user_irq->handler(user_irq->user_idx, (void *)ax_local);
	}

	return IRQ_HANDLED;
}

static int
ax_net_desc_clear(struct net_device *dev, u32 current_list)
{
	struct ax_private	*ax_local = netdev_priv(dev);
	struct xdma_dev		*xdev = ax_local->xdev;
	struct xdma_engine 	*engine = &xdev->engine_c2h[0];
	struct xdma_result 	*cyclic_result = engine->cyclic_result;
	int			size_count, offset;

	size_count = ((RX_DESC_NUM / DESC_LIST_NUM) * 
			sizeof(struct xdma_result));
	offset = (current_list * (RX_DESC_NUM / DESC_LIST_NUM));
	memset(&cyclic_result[offset], 0, size_count);
	return 0;
}

static void 
ax_rx_check_timestamp(struct sk_buff *skb, struct ax_switch *pSwitch)
{
	const struct ethhdr *eth;
	SKB_TSTAMP_MSG msg;
	u16 rx_ethertype, rx_ethertype_sec;
	u16 tmp;
	u8 vlan_size = 0;
	bool isdsa =0;
	u16 vlan_id = 0;

	msg.skb = skb;
	msg.pSwitch = pSwitch;
	rx_ethertype = ntohs(skb->protocol);
	if (rx_ethertype == 0xF8) {
		isdsa = 1;
		eth = eth_hdr(skb);
		rx_ethertype = ntohs(eth->h_proto);
	}
	skb_copy_from_linear_data_offset(skb, 2, &tmp, 2);
	rx_ethertype_sec = ntohs(tmp);

	if ((((rx_ethertype & 0xFF00) == 0x0000) ||
	    ((rx_ethertype & 0xFF00) == 0x2000)) &&
	    (rx_ethertype_sec == 0x88F7) && (isdsa == 1)) {
		skb_copy_from_linear_data_offset(skb, 1, &tmp, 1);
		vlan_id = (rx_ethertype & 0x0FF) >> 3;
		vlan_size = 4;
		skb_copy_from_linear_data_offset(skb, 2, &tmp, 2);
		rx_ethertype = ntohs(tmp);
	}
	if (rx_ethertype == ETH_P_8021Q) {
		skb_copy_from_linear_data_offset(skb, 0, &tmp, 2);
		vlan_id = ntohs(tmp) & 0xFFF;
		vlan_size = 4;
		skb_copy_from_linear_data_offset(skb, 2, &tmp, 2);
		rx_ethertype = ntohs(tmp);
		if (rx_ethertype == ETH_P_8021Q) {
			vlan_size = 8;
			skb_copy_from_linear_data_offset(skb, 6, &tmp, 2);
			rx_ethertype = ntohs(tmp);
		}
	}
	if (((rx_ethertype & 0xFF00) == 0x0000) ||
	    ((rx_ethertype & 0xFF00) == 0x2000)) {
		skb_copy_from_linear_data_offset(skb, -2, &tmp, 2);
		vlan_id = (rx_ethertype & 0x0FF) >> 3;
		vlan_size = 4;
		skb_copy_from_linear_data_offset(skb, 2, &tmp, 2);
		rx_ethertype = ntohs(tmp);
	}
	if (rx_ethertype == ETH_P_IP) {
		u8 transport_poto;
		u16 dst_port;
			skb_copy_from_linear_data_offset(skb, 
			AX_IP_PROTO_OFFSET + vlan_size,
			&transport_poto, 1);
		if (transport_poto == IPPROTO_UDP) {
			skb_copy_from_linear_data_offset(skb,
				AX_UDP_PORT_OFFSET + vlan_size,
				&dst_port, 2);
			if (ntohs(dst_port) == AX_PTP_EVENT_PORT_NUM) {				
				msg.ptp_msg_offset = AX_RX_PTPHDR_OFFSET_L3 +
						     vlan_size;
				msg.ptp_vlan_id = 0;
				msg.port_tag = vlan_id;
				ax_tsn_rx_hwtstamp(&msg);
			}
		}
	} else if (rx_ethertype == ETH_P_1588) {
		u8 msg_type;
		skb_copy_from_linear_data_offset(
			skb,
			AX_RX_PTPHDR_OFFSET_L2 + vlan_size,
			&msg_type, 1);
		if ((msg_type & 0xF) <= 7) {
			msg.ptp_msg_offset = 
					AX_RX_PTPHDR_OFFSET_L2 + vlan_size;
			msg.ptp_vlan_id = 0;
			msg.port_tag = vlan_id;
			ax_tsn_rx_hwtstamp(&msg);
		}
	}
}


static void 
ax_tx_check_timestamp(struct sk_buff *skb, struct ax_switch *pSwitch)
{
	SKB_TSTAMP_MSG msg;	

	msg.skb = skb;
	msg.pSwitch = pSwitch;
	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		u16 tmp, tx_ethertype, vlan_id = 0;
		u8 vlan_size = 0;

		skb_copy_from_linear_data_offset(skb, 
					AX_ETHTYPE_OFFSET, 
					&tmp, 2);
		tx_ethertype = ntohs(tmp);

		if (tx_ethertype == ETH_P_8021Q) {
			skb_copy_from_linear_data_offset(skb,
				AX_ETHTYPE_OFFSET + 2,
				&tmp, 2);
			vlan_id = ntohs(tmp) & 0xFFF;
			vlan_size = 4;
			skb_copy_from_linear_data_offset(skb,
				AX_ETHTYPE_OFFSET + 4,
				&tmp, 2);
			tx_ethertype = ntohs(tmp);
			if (tx_ethertype == ETH_P_8021Q) {
				vlan_size = 8;
				skb_copy_from_linear_data_offset(skb,
					AX_ETHTYPE_OFFSET + 8,
					&tmp, 2);
				tx_ethertype = ntohs(tmp);
			}
		}
		if (((tx_ethertype & 0xFF00) == 0x4000) ||
		    ((tx_ethertype & 0xFF00) == 0x6000)) {
			skb_copy_from_linear_data_offset(skb,
				AX_ETHTYPE_OFFSET + 1,
				&tmp, 1);
			vlan_id = ((ntohs(tmp) >> 3) & 0xFF);
			vlan_size = 4;
			skb_copy_from_linear_data_offset(skb,
				AX_ETHTYPE_OFFSET + 4,
				&tmp, 2);
			tx_ethertype = ntohs(tmp);
		}
		msg.ptp_vlan_id = 0;
		msg.port_tag = vlan_id;
		if (tx_ethertype == ETH_P_1588) {
			msg.ptp_msg_offset = AX_TX_PTPHDR_OFFSET_L2 +
					     vlan_size;							
		} else {
			msg.ptp_msg_offset = AX_TX_PTPHDR_OFFSET_L3 +
					     vlan_size;
		}
		ax_tsn_tx_hwtstamp(&msg);
	}
}

/**
 * eth_type_trans - determine the packet's protocol ID.
 * @skb: received socket data
 * @dev: receiving network device
 *
 * The rule here is that we
 * assume 802.3 if the type field is short enough to be a length.
 * This is normal practice and works for any 'now in use' protocol.
 */
static __be16 ax_eth_type_trans(struct sk_buff *skb, 
				struct ax_private *ax_local)
{
	struct net_device *dev = ax_local->dev;
	unsigned short _service_access_point;
	const unsigned short *sap;
	const struct ethhdr *eth;
	u32 i;

	skb->dev = dev;
	skb_reset_mac_header(skb);

	eth = (struct ethhdr *)skb->data;
	skb_pull_inline(skb, ETH_HLEN);

	if (unlikely(!ether_addr_equal_64bits(eth->h_dest,
					      dev->dev_addr))) {
		if (unlikely(is_multicast_ether_addr_64bits(eth->h_dest))) {
			if (ether_addr_equal_64bits(eth->h_dest, 
			dev->broadcast)) {
				skb->pkt_type = PACKET_BROADCAST;
			} else {
				skb->pkt_type = PACKET_MULTICAST;
			}
		} else {
			skb->pkt_type = PACKET_OTHERHOST;
		}
	}

	/*
	 * Some variants of DSA tagging don't have an ethertype field
	 * at all, so we check here whether one of those tagging
	 * variants has been configured on the receiving interface,
	 * and if so, set skb->protocol without looking at the packet.
	 */
	if (unlikely(netdev_uses_dsa(dev))) {		
		for (i = 0; i < 4; i++) {
			unsigned char *dsa_mac = ax_local->ax_dsa.dsa_mac[i];
			if (ether_addr_equal_64bits(dsa_mac, eth->h_dest)) {				
				return htons(ETH_P_XDSA);
			}
		}
	}

	if (likely(eth_proto_is_802_3(eth->h_proto))) {
		return eth->h_proto;
	}

	/*
	 *This is a magic hack to spot IPX packets. Older Novell breaks
	 *the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *won't work for fault tolerant netware but does for the rest.
	 */
	sap = skb_header_pointer(skb, 0, sizeof(*sap), &_service_access_point);
	if (sap && *sap == 0xFFFF) {
		return htons(ETH_P_802_3);
	}

	/*
	 *      Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}

static int ax_net_rx(struct ax_private *ax_local, int budget)
{
	struct xdma_dev 	*xdev = ax_local->xdev;
	struct xdma_engine 	*engine = &xdev->engine_c2h[0];
	struct xdma_result 	*cyclic_result = engine->cyclic_result;
	struct sk_buff 		*skb;
	struct napi_struct 	*napi = &ax_local->napi;
	struct packet_buff 	*rx_buff = engine->rx_buff;
	u32 entry;
	u32 length;
	u16 count = 0;

	entry = ax_local->cur_rx;
	while (((cyclic_result[entry].status & 0xFFFF0000) == C2H_WB) && budget) 
	{
		length = cyclic_result[entry].length;			

		skb = napi_alloc_skb(napi, length + 2);
		if (skb == NULL) {
			
			goto next_pkt;
		}
		skb_reserve(skb, 2);
		skb->dev = ax_local->dev;
		memcpy(skb->data, rx_buff[entry].data, length);

		skb_put(skb, length);	
		skb->protocol = ax_eth_type_trans (skb, ax_local);

		ax_rx_check_timestamp(skb, &ax_local->axswitch);

#ifdef CONFIG_NAPA_NAPI
		napi_gro_receive(napi, skb);
		count++;
#else
		netif_rx(skb);	
#endif /* End of CONFIG_NAPA_NAPI */		

		ax_local->net_stats.rx_bytes += length;
		ax_local->net_stats.rx_packets++;
		ax_local->cur_rx_pkt_count++;
next_pkt:
		budget--;
		entry = (entry + 1) % RX_DESC_NUM;
		if ((entry == 0) || (entry == (RX_DESC_NUM / DESC_LIST_NUM))) {
			ax_net_desc_clear(ax_local->dev, ax_local->rx_flags);
			ax_local->rx_flags = (ax_local->rx_flags + 1) % 
					     DESC_LIST_NUM;
			break;
		}
	}

	ax_local->cur_rx = entry;

	return count;
}

int ax_net_poll(struct napi_struct *napi, int budget)
{
	struct ax_private *ax_local =
				container_of(napi, struct ax_private, napi);
	struct xdma_dev *xdev = ax_local->xdev;
	struct xdma_engine *engine = &xdev->engine_c2h[0];
	int work_done;
	work_done = ax_net_rx(ax_local, budget);
	napi_complete_done(napi, budget);
	channel_interrupts_enable(engine->xdev, engine->irq_bitmask);

	return work_done;
}

/*
 * xdma_isr() - Interrupt handler
 *
 * @dev_id pointer to xdma_dev
 */
static irqreturn_t xdma_isr(int irq, void *dev_id)
{
	struct interrupt_regs *irq_regs;
	struct xdma_dev *xdev;
	u32 ch_irq;
	u32 user_irq;
	u32 mask;
	
	xdev = (struct xdma_dev *)dev_id;
	if (!xdev) {
		WARN_ON(!xdev);
		dbg_irq("xdma_isr(irq=%d) xdev=%p ??\n", irq, xdev);
		return IRQ_NONE;
	}

	irq_regs = (struct interrupt_regs *)
		   (xdev->bar[xdev->config_bar_idx] + XDMA_OFS_INT_CTRL);

	/* read channel interrupt requests */
	ch_irq = read_register(&irq_regs->channel_int_request);

	/*
	 * disable all interrupts that fired; these are re-enabled individually
	 * after the causing module has been fully serviced.
	 */
	if (ch_irq) {
		channel_interrupts_disable(xdev, ch_irq);
	}
	
	/* read user interrupts - this read also flushes the above write */
	user_irq = read_register(xdev->bar[0] + SWITCH_ISR);	

	if (user_irq) {

		struct pci_dev *pdev = xdev->pdev;
		struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);
		struct ax_private *ax_local = xpdev->ax_netdev_priv;
		int user = 0;		

		user_interrupts_disable(xdev, xdev->mask_irq_user);
		ASIX_DEBUG("%s, %d, user_irq = %x", 
				__FUNCTION__,
				__LINE__,
				 user_irq);
		for (user = 0 ; user < 8 ; user++) {
			mask = (1 << (16 + user));			
			if (user_irq & mask) {				
				write_register((user_irq & mask), 
						xdev->bar[0] + SWITCH_ISR,
						SWITCH_ISR);
				user_irq &= ~(mask);
				user_irq_service(irq, &xdev->user_irq[user],
						 ax_local);
			}
		}
		ASIX_DEBUG("%s, %d, user_irq = %x", 
				__FUNCTION__,
				__LINE__,
				 user_irq);
		user_interrupts_enable(xdev, xdev->mask_irq_user);

	}
	
	mask = ch_irq & xdev->mask_irq_h2c;
	if (mask) {
		int channel = 0;
		unsigned long flags;
		int max = xdev->h2c_channel_max;
		struct pci_dev *pdev = xdev->pdev;
		struct xdma_pci_dev *xpdev = dev_get_drvdata(&pdev->dev);
		struct ax_private *ax_local = xpdev->ax_netdev_priv;

		spin_lock_irqsave (&ax_local->lock, flags);

		/* iterate over H2C (PCIe read) */
		for (channel = 0; channel < max && mask; channel++) {
			struct xdma_engine *engine = 
					&xdev->engine_h2c[channel];

			/* engine present and its interrupt fired? */
			if((engine->irq_bitmask & mask) &&
			   (engine->magic == MAGIC_ENGINE)) {
				struct xdma_transfer *transfer = 
							engine->transfer;
				struct xdma_desc *desc_virt = 
							transfer->desc_virt;
				struct ax_tx_list *list = 
						&ax_local->tx_list[channel];
				u32 isr = 0, complete;				

				mask &= ~engine->irq_bitmask;				

				isr = engine_status_read(engine, 1, 0);
				xdma_engine_stop(engine);				
				
				
				complete = read_register(
					&engine->regs->completed_desc_count);

				if (list->cur_pkt_count != 0) {
					u32 stop_desc;
			
					if (list->cur == 0) {
						stop_desc = RX_DESC_NUM - 1;
					} else {
						stop_desc = list->cur - 1;
					}
					desc_virt[stop_desc].control =
						cpu_to_le32(DESC_MAGIC |
							    XDMA_DESC_EOP |
							    XDMA_DESC_STOPPED);

					engine_start(engine);
					transfer->current_list =
						((transfer->current_list + 1) %
						DESC_LIST_NUM);
					list->cur =
						(transfer->current_list *
						(TX_DESC_NUM / DESC_LIST_NUM));

					list->cur_pkt_count = 0;

					list->idle = 0;					
				} else {
					list->idle = 1;
				}
					
				channel_interrupts_enable(engine->xdev,
							  engine->irq_bitmask);
				if (ax_local->stop_queue_channel == channel) {
					if (netif_queue_stopped(
							ax_local->dev)) {
						netif_wake_queue(
							ax_local->dev);
					}
					ax_local->stop_queue_channel = 0xFF;
				}	
			}			
		}
		spin_unlock_irqrestore(&ax_local->lock, flags);		
	}

	mask = ch_irq & xdev->mask_irq_c2h;
	if (mask) {
		int channel = 0;
		int max = 1;//xdev->c2h_channel_max;
		for (channel = 0; channel < max && mask; channel++) {
			struct xdma_engine *engine = 
						&xdev->engine_c2h[channel];

			if((engine->irq_bitmask & mask) &&
			   (engine->magic == MAGIC_ENGINE)) {
				struct pci_dev *pdev = xdev->pdev;
				struct xdma_pci_dev *xpdev = 
						dev_get_drvdata(&pdev->dev);
				struct ax_private *ax_local = 
						xpdev->ax_netdev_priv;
				struct xdma_transfer *transfer = 
						engine->transfer;
				u32 isr = 0;

				mask &= ~engine->irq_bitmask;

				isr = engine_status_read(engine, 1, 0);
				if (isr & XDMA_STAT_DESC_STOPPED) {
					xdma_engine_stop(engine);					
					transfer->current_list =
					(transfer->current_list + 1) % 
								DESC_LIST_NUM;					
					engine_start(engine);							
				}
#ifdef CONFIG_NAPA_NAPI
				napi_schedule(&ax_local->napi);
#else
				ax_net_rx(ax_local, 128);
				channel_interrupts_enable(engine->xdev,
							  engine->irq_bitmask);
#endif /* End of CONFIG_NAPA_NAPI */

			}
		}
	}

	xdev->irq_count++;
	return IRQ_HANDLED;
}


/*
 * Unmap the BAR regions that had been mapped earlier using map_bars()
 */
static void unmap_bars(struct xdma_dev *xdev, struct pci_dev *dev)
{
	int i;

	for (i = 0; i < XDMA_BAR_NUM; i++) {
		/* is this BAR mapped? */
		if (xdev->bar[i]) {
			/* unmap BAR */
			pci_iounmap(dev, xdev->bar[i]);
			/* mark as unmapped */
			xdev->bar[i] = NULL;
		}
	}
}

//
static int map_single_bar(struct xdma_dev *xdev, struct pci_dev *dev, int idx)
{
	resource_size_t bar_start;
	resource_size_t bar_len;
	resource_size_t map_len;

	bar_start = pci_resource_start(dev, idx);
	bar_len = pci_resource_len(dev, idx);
	map_len = bar_len;

	xdev->bar[idx] = NULL;

	/* do not map BARs with length 0. Note that start MAY be 0! */
	if (!bar_len) {
		return 0;
	}

	/* BAR size exceeds maximum desired mapping? */
	if (bar_len > INT_MAX) {
		pr_info("Limit BAR %d mapping from %llu to %d bytes\n", idx,
			(u64)bar_len, INT_MAX);
		map_len = (resource_size_t)INT_MAX;
	}
	/*
	 * map the full device memory or IO region into kernel virtual
	 * address space
	 */
	dbg_init("BAR%d: %llu bytes to be mapped.\n", idx, (u64)map_len);
	xdev->phy_bar[idx] = bar_start;
	xdev->bar[idx] = pci_iomap(dev, idx, map_len);

	if (!xdev->bar[idx]) {
		pr_info("Could not map BAR %d.\n", idx);
		return -1;
	}

	pr_info("BAR%d at 0x%llx mapped at 0x%p, length=%llu(/%llu)\n", idx,
		(u64)bar_start, xdev->bar[idx], (u64)map_len, (u64)bar_len);

	return (int)map_len;
}

//
static int is_config_bar(struct xdma_dev *xdev, int idx)
{
	u32 irq_id = 0;
	u32 cfg_id = 0;
	int flag = 0;
	u32 mask = 0xffff0000; /* Compare only XDMA ID's not Version number */
	struct interrupt_regs *irq_regs =
		(struct interrupt_regs *) (xdev->bar[idx] + XDMA_OFS_INT_CTRL);
	struct config_regs *cfg_regs =
		(struct config_regs *)(xdev->bar[idx] + XDMA_OFS_CONFIG);

	irq_id = read_register(&irq_regs->identifier);
	cfg_id = read_register(&cfg_regs->identifier);

	if (((irq_id & mask)== IRQ_BLOCK_ID) &&
	    ((cfg_id & mask)== CONFIG_BLOCK_ID)) {
		dbg_init("BAR %d is the XDMA config BAR\n", idx);
		flag = 1;
	} else {
		dbg_init("BAR %d is NOT the XDMA config BAR: 0x%x, 0x%x.\n",
			idx, irq_id, cfg_id);
		flag = 0;
	}

	return flag;
}

//
static void identify_bars(struct xdma_dev *xdev, int *bar_id_list, int num_bars,
			int config_bar_pos)
{
	/*
	 * The following logic identifies which BARs contain what functionality
	 * based on the position of the XDMA config BAR and the number of BARs
	 * detected. The rules are that the user logic and bypass logic BARs
	 * are optional. When both are present, the XDMA config BAR will be the
	 * 2nd BAR detected (config_bar_pos = 1), with the user logic being
	 * detected first and the bypass being detected last. When one is
	 * omitted, the type of BAR present can be identified by whether the
	 * XDMA config BAR is detected first or last.  When both are omitted,
	 * only the XDMA config BAR is present.  This somewhat convoluted
	 * approach is used instead of relying on BAR numbers in order to work
	 * correctly with both 32-bit and 64-bit BARs.
	 */

	BUG_ON(!xdev);
	BUG_ON(!bar_id_list);

	dbg_init("xdev 0x%p, bars %d, config at %d.\n",
		xdev, num_bars, config_bar_pos);

	switch (num_bars) {
	case 1:
		/* Only one BAR present - no extra work necessary */
		break;

	case 2:
		if (config_bar_pos == 0) {
			xdev->bypass_bar_idx = bar_id_list[1];
		} else if (config_bar_pos == 1) {
			xdev->user_bar_idx = bar_id_list[0];
		} else {
			pr_info("2, XDMA config BAR unexpected %d.\n",
				config_bar_pos);
		}
		break;

	case 3:
	case 4:
		if ((config_bar_pos == 1) || (config_bar_pos == 2)) {
			/* user bar at bar #0 */
			xdev->user_bar_idx = bar_id_list[0];
			/* bypass bar at the last bar */
			xdev->bypass_bar_idx = bar_id_list[num_bars - 1];
		} else {
			pr_info("3/4, XDMA config BAR unexpected %d.\n",
				config_bar_pos);
		}
		break;

	default:
		/* Should not occur - warn user but safe to continue */
		pr_info("Unexpected # BARs (%d), XDMA config BAR only.\n",
			num_bars);
		break;

	}
	pr_info("%d BARs: config %d, user %d, bypass %d.\n",
		num_bars, config_bar_pos, xdev->user_bar_idx,
		xdev->bypass_bar_idx);
}

//
/* map_bars() -- map device regions into kernel virtual address space
 *
 * Map the device memory regions into kernel virtual address space after
 * verifying their sizes respect the minimum sizes needed
 */
static int map_bars(struct xdma_dev *xdev, struct pci_dev *dev)
{
	int rv;
	int i;
	int bar_id_list[XDMA_BAR_NUM];
	int bar_id_idx = 0;
	int config_bar_pos = 0;

	/* iterate through all the BARs */
	for (i = 0; i < XDMA_BAR_NUM; i++) {
		int bar_len;

		//
		bar_len = map_single_bar(xdev, dev, i);
		if (bar_len == 0) {
			continue;
		} else if (bar_len < 0) {
			rv = -EINVAL;
			goto fail;
		}

		/* Try to identify BAR as XDMA control BAR */
		if ((bar_len >= XDMA_BAR_SIZE) && (xdev->config_bar_idx < 0)) {
			if (is_config_bar(xdev, i)) {
				xdev->config_bar_idx = i;
				config_bar_pos = bar_id_idx;
				pr_info("config bar %d, pos %d.\n",
					xdev->config_bar_idx, config_bar_pos);
			}
		}

		bar_id_list[bar_id_idx] = i;
		bar_id_idx++;
	}

	/* The XDMA config BAR must always be present */
	if (xdev->config_bar_idx < 0) {
		pr_info("Failed to detect XDMA config BAR\n");
		rv = -EINVAL;
		goto fail;
	}

#ifdef __LIBXDMA_CONFIG_BAR_ONLY__
	/* unmapped all other bars, except XDMA config. bar */
	for (i = 0; i < XDMA_BAR_NUM; i++) {
		if (i == xdev->config_bar_idx) {
			continue;
		}

		/* is this BAR mapped? */
		if (xdev->bar[i]) {
			/* unmap BAR */
			pci_iounmap(dev, xdev->bar[i]);
			/* mark as unmapped */
			xdev->bar[i] = NULL;
			pr_info("unmap non-config bar %d.\n", i);
		}
	}
#else
	identify_bars(xdev, bar_id_list, bar_id_idx, config_bar_pos);
#endif

	/* successfully mapped all required BAR regions */
	return 0;

fail:
	/* unwind; unmap any BARs that we did map */
	unmap_bars(xdev, dev);
	return rv;
}

//
static void pci_check_intr_pend(struct pci_dev *pdev)
{
	u16 v;

	pci_read_config_word(pdev, PCI_STATUS, &v);
	if (v & PCI_STATUS_INTERRUPT) {
		pr_info("%s PCI STATUS Interrupt pending 0x%x.\n",
                        dev_name(&pdev->dev), v);
		pci_write_config_word(pdev, PCI_STATUS, PCI_STATUS_INTERRUPT);
	}
}

static void pci_keep_intx_enabled(struct pci_dev *pdev)
{
	/* workaround to a h/w bug:
	 * when msix/msi become unavaile, default to legacy.
	 * However the legacy enable was not checked.
	 * If the legacy was disabled, no ack then everything stuck
	 */
	u16 pcmd, pcmd_new;

	pci_read_config_word(pdev, PCI_COMMAND, &pcmd);
	pcmd_new = pcmd & ~PCI_COMMAND_INTX_DISABLE;
	if (pcmd_new != pcmd) {
		pr_info("%s: clear INTX_DISABLE, 0x%x -> 0x%x.\n",
			dev_name(&pdev->dev), pcmd, pcmd_new);
		pci_write_config_word(pdev, PCI_COMMAND, pcmd_new);
	}
}


//
static int irq_legacy_setup(struct xdma_dev *xdev, struct pci_dev *pdev)
{
	u32 w;
	u8 val;
	void *reg;
	int rv;

	pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &val);
	dbg_init("Legacy Interrupt register value = %d\n", val);
	if (val > 1) {
		val--;
		w = (val<<24) | (val<<16) | (val<<8)| val;
		/* Program IRQ Block Channel vactor and IRQ Block User vector
		 * with Legacy interrupt value */
		reg = xdev->bar[xdev->config_bar_idx] + 0x2080;   // IRQ user
		write_register(w, reg, 0x2080);
		write_register(w, reg+0x4, 0x2084);
		write_register(w, reg+0x8, 0x2088);
		write_register(w, reg+0xC, 0x208C);
		reg = xdev->bar[xdev->config_bar_idx] + 0x20A0;   // IRQ Block
		write_register(w, reg, 0x20A0);
		write_register(w, reg+0x4, 0x20A4);
	}

	xdev->irq_line = (int)pdev->irq;
	rv = request_irq(pdev->irq, xdma_isr, IRQF_SHARED, xdev->mod_name,
			xdev);
	if (rv) {
		dbg_init("Couldn't use IRQ#%d, %d\n", pdev->irq, rv);
	} else {
		dbg_init("Using IRQ#%d with 0x%p\n", pdev->irq, xdev);
	}

	return rv;
}

static void irq_teardown(struct xdma_dev *xdev)
{
	dbg_init("Releasing IRQ#%d\n", xdev->irq_line);
	free_irq(xdev->irq_line, xdev);
}

static int irq_setup(struct xdma_dev *xdev, struct pci_dev *pdev)
{
	pci_keep_intx_enabled(pdev);
	return irq_legacy_setup(xdev, pdev);
}

static void transfer_desc_init(struct xdma_transfer *transfer, int count)
{
	struct xdma_desc *desc_virt = transfer->desc_virt;
	dma_addr_t desc_bus = transfer->desc_bus;
	int i, temp_count = count / DESC_LIST_NUM;
	u32 list_count = 0;
	u32 magic = 
		cpu_to_le32(DESC_MAGIC | XDMA_DESC_COMPLETED | XDMA_DESC_EOP);

	BUG_ON(count > XDMA_TRANSFER_MAX_DESC);	

	transfer->list_desc[list_count++] = desc_bus;
	for (i = 0; i < count; i++) {	
		desc_bus += sizeof(struct xdma_desc);
		
		if ((i % temp_count) == (temp_count - 1)) {
			/* the last */
			desc_virt[i].next_lo = cpu_to_le32(0);
			desc_virt[i].next_hi = cpu_to_le32(0);			
			desc_virt[i].bytes   = cpu_to_le32(0);
			desc_virt[i].control =
					cpu_to_le32(magic | XDMA_DESC_STOPPED);			
			transfer->list_desc[list_count++] = desc_bus;
		} else {
			desc_virt[i].next_lo =
					cpu_to_le32(PCI_DMA_L(desc_bus));
			desc_virt[i].next_hi =
					cpu_to_le32(PCI_DMA_H(desc_bus));
			desc_virt[i].bytes   = cpu_to_le32(0);
			desc_virt[i].control = cpu_to_le32(magic);
		}
	}	
}

/* xdma_desc_done - recycle cache-coherent linked list of descriptors.
 *
 * @dev Pointer to pci_dev
 * @number Number of descriptors to be allocated
 * @desc_virt Pointer to (i.e. virtual address of) first descriptor in list
 * @desc_bus Bus address of first descriptor in list
 */
static inline void xdma_desc_done(struct xdma_desc *desc_virt)
{
	memset(desc_virt, 0, 
			XDMA_TRANSFER_MAX_DESC * sizeof(struct xdma_desc));
}

/* transfer_queue() - Queue a DMA transfer on the engine
 *
 * @engine DMA engine doing the transfer
 * @transfer DMA transfer submitted to the engine
 *
 * Takes and releases the engine spinlock
 */
static int transfer_queue(struct xdma_engine *engine,
		struct xdma_transfer *transfer)
{
	int rv = 0;	
	struct xdma_dev *xdev;
	unsigned long flags;

	BUG_ON(!engine);
	BUG_ON(!engine->xdev);
	BUG_ON(!transfer);
	BUG_ON(transfer->desc_num == 0);
	dbg_tfr("transfer_queue(transfer=0x%p).\n", transfer);

	xdev = engine->xdev;
	if (xdma_device_flag_check(xdev, XDEV_FLAG_OFFLINE)) {
		pr_info("dev 0x%p offline, transfer 0x%p not queued.\n",
			xdev, transfer);
		return -EBUSY;
	}

	/* lock the engine state */
	spin_lock_irqsave(&engine->lock, flags);

	engine->prev_cpu = get_cpu();
	put_cpu();

	/* engine is being shutdown; do not accept new transfers */
	if (engine->shutdown & ENGINE_SHUTDOWN_REQUEST) {
		pr_info("engine %s offline, transfer 0x%p not queued.\n",
			engine->name, transfer);
		rv = -EBUSY;
		goto shutdown;
	}

	/* mark the transfer as submitted */
	transfer->state = TRANSFER_STATE_SUBMITTED;
	/* add transfer to the tail of the engine transfer queue */
	engine->transfer = transfer;
shutdown:
	/* unlock the engine state */
	dbg_tfr("engine->running = %d\n", engine->running);
	spin_unlock_irqrestore(&engine->lock, flags);
	return rv;
}

//
static void engine_alignments(struct xdma_engine *engine)
{
	u32 w;
	u32 align_bytes;
	u32 granularity_bytes;
	u32 address_bits;

	w = read_register(&engine->regs->alignments);
	dbg_init("engine %p name %s alignments=0x%08x\n", engine,
		engine->name, (int)w);

	/* RTO  - add some macros to extract these fields */
	align_bytes = (w & 0x00ff0000U) >> 16;
	granularity_bytes = (w & 0x0000ff00U) >> 8;
	address_bits = (w & 0x000000ffU);

	dbg_init("align_bytes = %d\n", align_bytes);
	dbg_init("granularity_bytes = %d\n", granularity_bytes);
	dbg_init("address_bits = %d\n", address_bits);

	if (w) {
		engine->addr_align = align_bytes;
		engine->len_granularity = granularity_bytes;
		engine->addr_bits = address_bits;
	} else {
		/* Some default values if alignments are unspecified */
		engine->addr_align = 1;
		engine->len_granularity = 1;
		engine->addr_bits = 64;
	}
}

static void engine_free_resource(struct xdma_engine *engine)
{
	struct xdma_dev *xdev = engine->xdev;

	if (engine->desc) {
		dbg_init("device %s, engine %s pre-alloc desc 0x%p,0x%llx\n",
			dev_name(&xdev->pdev->dev), engine->name,
			engine->desc, engine->desc_bus);
		dma_free_coherent(&xdev->pdev->dev,
			XDMA_TRANSFER_MAX_DESC * sizeof(struct xdma_desc),
			engine->desc, engine->desc_bus);
		engine->desc = NULL;
	}

	if (engine->cyclic_result) {
		dma_free_coherent(&xdev->pdev->dev,
			CYCLIC_RX_PAGES_MAX * sizeof(struct xdma_result),
			engine->cyclic_result, engine->cyclic_result_bus);
		engine->cyclic_result = NULL;
	}

#ifdef RX_SKB_COPY
	if (engine->rx_buff) {
		dma_free_coherent(&xdev->pdev->dev,
			XDMA_TRANSFER_MAX_DESC * sizeof(struct packet_buff),
			engine->rx_buff, engine->rx_buff_bus);
		engine->rx_buff = NULL;
	}
#endif
#ifdef TX_SKB_COPY
	if (engine->tx_buff) {
		dma_free_coherent(&xdev->pdev->dev,
			XDMA_TRANSFER_MAX_DESC * sizeof(struct packet_buff),
			engine->tx_buff, engine->tx_buff_bus);
		engine->tx_buff = NULL;
	}
#endif
}

static void engine_destroy(struct xdma_dev *xdev, struct xdma_engine *engine)
{
	BUG_ON(!xdev);
	BUG_ON(!engine);

	dbg_sg("Shutting down engine %s%d", engine->name, engine->channel);

	/* Disable interrupts to stop processing new events during shutdown */
	write_register(0x0, &engine->regs->interrupt_enable_mask,
			(unsigned long)(&engine->regs->interrupt_enable_mask) -
			(unsigned long)(&engine->regs));

	if (enable_credit_mp && engine->streaming &&
		engine->dir == DMA_FROM_DEVICE) {
		u32 reg_value = (0x1 << engine->channel) << 16;
		struct sgdma_common_regs *reg = (struct sgdma_common_regs *)
				(xdev->bar[xdev->config_bar_idx] +
				 (0x6*TARGET_SPACING));	
		write_register(reg_value, &reg->credit_mode_enable_w1c, 0);
	}

	/* Release memory use for descriptor writebacks */
	engine_free_resource(engine);

	memset(engine, 0, sizeof(struct xdma_engine));
	/* Decrement the number of engines available */
	xdev->engines_num--;
}


//
/* engine_create() - Create an SG DMA engine bookkeeping data structure
 *
 * An SG DMA engine consists of the resources for a single-direction transfer
 * queue; the SG DMA hardware, the software queue and interrupt handling.
 *
 * @dev Pointer to pci_dev
 * @offset byte address offset in BAR[xdev->config_bar_idx] resource for the
 * SG DMA * controller registers.
 * @dir: DMA_TO/FROM_DEVICE
 * @streaming Whether the engine is attached to AXI ST (rather than MM)
 */
static int engine_init_regs(struct xdma_engine *engine)
{
	u32 reg_value;
	int rv = 0;

	write_register(XDMA_CTRL_NON_INCR_ADDR, &engine->regs->control_w1c,
			(unsigned long)(&engine->regs->control_w1c) -
			(unsigned long)(&engine->regs));

	engine_alignments(engine);

	/* Configure error interrupts by default */
	reg_value = XDMA_CTRL_IE_DESC_ALIGN_MISMATCH;
	reg_value |= XDMA_CTRL_IE_MAGIC_STOPPED;
	reg_value |= XDMA_CTRL_IE_READ_ERROR;
	reg_value |= XDMA_CTRL_IE_DESC_ERROR;

	/* enable the relevant completion interrupts */
	reg_value |= XDMA_CTRL_IE_DESC_STOPPED;
	reg_value |= XDMA_CTRL_IE_DESC_COMPLETED;

	if (engine->streaming && engine->dir == DMA_FROM_DEVICE)
		reg_value |= XDMA_CTRL_IE_IDLE_STOPPED;

	/* Apply engine configurations */
	write_register(reg_value, &engine->regs->interrupt_enable_mask,
			(unsigned long)(&engine->regs->interrupt_enable_mask) -
			(unsigned long)(&engine->regs));

	engine->interrupt_enable_mask_value = reg_value;

	/* only enable credit mode for AXI-ST C2H */
	if (enable_credit_mp && engine->streaming &&
		engine->dir == DMA_FROM_DEVICE) {

		struct xdma_dev *xdev = engine->xdev;
		u32 reg_value = (0x1 << engine->channel) << 16;
		struct sgdma_common_regs *reg = (struct sgdma_common_regs *)
				(xdev->bar[xdev->config_bar_idx] +
				 (0x6*TARGET_SPACING));	

		write_register(reg_value, &reg->credit_mode_enable_w1s, 0);
	}

	return rv;
}

//
static int engine_alloc_resource(struct xdma_engine *engine)
{
	struct xdma_dev *xdev = engine->xdev;

	engine->desc = dma_alloc_coherent(&xdev->pdev->dev,
			XDMA_TRANSFER_MAX_DESC * sizeof(struct xdma_desc),
			&engine->desc_bus, GFP_KERNEL);
	if (!engine->desc) {
		pr_warn("dev %s, %s pre-alloc desc OOM.\n",
			dev_name(&xdev->pdev->dev), engine->name);
		goto err_out;
	}

	if (engine->streaming && engine->dir == DMA_FROM_DEVICE) {
		engine->cyclic_result = dma_alloc_coherent(&xdev->pdev->dev,
			CYCLIC_RX_PAGES_MAX * sizeof(struct xdma_result),
			&engine->cyclic_result_bus, GFP_KERNEL);

		if (!engine->cyclic_result) {
                        pr_warn("%s, %s pre-alloc result OOM.\n",
				dev_name(&xdev->pdev->dev), engine->name);
			goto err_out;
		}
	}
#ifdef RX_SKB_COPY
	engine->rx_buff = dma_alloc_coherent(&xdev->pdev->dev,
			XDMA_TRANSFER_MAX_DESC * sizeof(struct packet_buff),
			&engine->rx_buff_bus, GFP_KERNEL);

	if (!engine->rx_buff) {
		pr_warn("%s, %s pre-alloc rx_buff result OOM.\n",
				dev_name(&xdev->pdev->dev), engine->name);
		goto err_out;
	}
#endif

#ifdef TX_SKB_COPY
	engine->tx_buff = dma_alloc_coherent(&xdev->pdev->dev,
			XDMA_TRANSFER_MAX_DESC * sizeof(struct packet_buff),
			&engine->tx_buff_bus, GFP_KERNEL);

	if (!engine->tx_buff) {
		pr_warn("%s, %s pre-alloc tx_buff result OOM.\n",
				dev_name(&xdev->pdev->dev), engine->name);
		goto err_out;
	}
#endif

	return 0;

err_out:
	engine_free_resource(engine);
	return -ENOMEM;
}

static int engine_init(struct xdma_engine *engine, struct xdma_dev *xdev,
			int offset, enum dma_data_direction dir, int channel)
{
	int rv;
	u32 val;

	dbg_init("channel %d, offset 0x%x, dir %d.\n", channel, offset, dir);

	/* set magic */
	engine->magic = MAGIC_ENGINE;
	engine->channel = channel;

	/* engine interrupt request bit */
	engine->irq_bitmask = (1 << XDMA_ENG_IRQ_NUM) - 1;
	engine->irq_bitmask <<= (xdev->engines_num * XDMA_ENG_IRQ_NUM);
	engine->bypass_offset = xdev->engines_num * BYPASS_MODE_SPACING;

	/* parent */
	engine->xdev = xdev;
	
	/* register address */
	engine->regs = (xdev->bar[xdev->config_bar_idx] + offset);
	engine->sgdma_regs = xdev->bar[xdev->config_bar_idx] + offset +
				SGDMA_OFFSET_FROM_CHANNEL;
	val = read_register(&engine->regs->identifier);
        if (val & 0x8000U) {
		engine->streaming = 1;
	}

	/* remember SG DMA direction */
	engine->dir = dir;
	sprintf(engine->name, "%d-%s%d-%s", xdev->idx,
		(dir == DMA_TO_DEVICE) ? "H2C" : "C2H", channel,
		engine->streaming ? "ST" : "MM");

	dbg_init("engine %p name %s irq_bitmask=0x%08x\n"
			, engine, engine->name,
			(int)engine->irq_bitmask);

	if (dir == DMA_TO_DEVICE) {
		xdev->mask_irq_h2c |= engine->irq_bitmask;
	} else {
		xdev->mask_irq_c2h |= engine->irq_bitmask;
	}
	xdev->engines_num++;

	//
	rv = engine_alloc_resource(engine);
	if (rv) {
		return rv;
	}

	//
	rv = engine_init_regs(engine);
	if (rv) {
		return rv;
	}

	return 0;
}


int xdma_desc_setup(struct xdma_dev *xdev, struct xdma_engine *engine)
{
	struct xdma_transfer *transfer;
	enum dma_data_direction dir;
	int num_desc_in_a_loop;

	/* allocate transfer data structure */
	transfer = kzalloc(sizeof(struct xdma_transfer), GFP_KERNEL);
	BUG_ON(!transfer);

	/* 0 = write engine (to_dev=0) , 1 = read engine (to_dev=1) */
	transfer->dir = engine->dir;
	dir = transfer->dir;
	/* set number of descriptors */
	if (dir == DMA_TO_DEVICE) {
		num_desc_in_a_loop = TX_DESC_NUM;
	} else {
		num_desc_in_a_loop = RX_DESC_NUM;
	}
	transfer->desc_num = num_desc_in_a_loop;

	/* allocate descriptor list */
	if (!engine->desc) {
		engine->desc = dma_alloc_coherent(&xdev->pdev->dev,
			num_desc_in_a_loop * sizeof(struct xdma_desc),
			&engine->desc_bus, GFP_KERNEL);
		BUG_ON(!engine->desc);
		dbg_init("device %s, engine %s pre-alloc desc 0x%p,0x%llx\n",
			dev_name(&xdev->pdev->dev), engine->name,
			engine->desc, engine->desc_bus);
	}
	transfer->desc_virt = engine->desc;
	transfer->desc_bus = engine->desc_bus;
	transfer->current_list = 0;
	
	transfer_desc_init(transfer, transfer->desc_num);	

	dbg_sg("transfer->desc_bus = 0x%llx.\n", (u64)transfer->desc_bus);


	transfer->cyclic = 1;

	/* initialize wait queue */
	init_waitqueue_head(&transfer->wq);

	engine->xdev = xdev;

	dbg_perf("Queue XDMA I/O %s request for performance measurement.\n",
		engine->dir ? "write (to dev)" : "read (from dev)");
	transfer_queue(engine, transfer);

	return 0;

}


//
static struct xdma_dev *alloc_dev_instance(struct pci_dev *pdev)
{
	int i;
	struct xdma_dev *xdev;
	struct xdma_engine *engine;

	BUG_ON(!pdev);

	/* allocate zeroed device book keeping structure */
	xdev = kzalloc(sizeof(struct xdma_dev), GFP_KERNEL);
	if (!xdev) {
		pr_info("OOM, xdma_dev.\n");
		return NULL;
	}
	spin_lock_init(&xdev->lock);

	xdev->magic = MAGIC_DEVICE;
	xdev->config_bar_idx = -1;
	xdev->user_bar_idx = -1;
	xdev->bypass_bar_idx = -1;
	xdev->irq_line = -1;

	/* create a driver to device reference */
	xdev->pdev = pdev;
	dbg_init("xdev = 0x%p\n", xdev);

	/* Set up data user IRQ data structures */
	for (i = 0; i < MAX_USER_IRQ; i++) {
		xdev->user_irq[i].xdev = xdev;
		spin_lock_init(&xdev->user_irq[i].events_lock);
		init_waitqueue_head(&xdev->user_irq[i].events_wq);
		xdev->user_irq[i].handler = NULL;
		xdev->user_irq[i].user_idx = i; /* 0 based */
	}

	engine = xdev->engine_h2c;
	for (i = 0; i < XDMA_CHANNEL_NUM_MAX; i++, engine++) {
		spin_lock_init(&engine->lock);
		spin_lock_init(&engine->desc_lock);		
		init_waitqueue_head(&engine->shutdown_wq);		
	}

	engine = xdev->engine_c2h;
	for (i = 0; i < XDMA_CHANNEL_NUM_MAX; i++, engine++) {
		spin_lock_init(&engine->lock);
		spin_lock_init(&engine->desc_lock);
		init_waitqueue_head(&engine->shutdown_wq);
	}

	return xdev;
}

//
static int request_regions(struct xdma_dev *xdev, struct pci_dev *pdev)
{
	int rv;

	BUG_ON(!xdev);
	BUG_ON(!pdev);

	dbg_init("pci_request_regions()\n");
	rv = pci_request_regions(pdev, xdev->mod_name);
	/* could not request all regions? */
	if (rv) {
		dbg_init("pci_request_regions() = %d, device in use?\n", rv);
		/* assume device is in use so do not disable it later */
		xdev->regions_in_use = 1;
	} else {
		xdev->got_regions = 1;
	}

	return rv;
}

//
static int set_dma_mask(struct pci_dev *pdev)
{
	BUG_ON(!pdev);

	dbg_init("sizeof(dma_addr_t) == %ld\n", sizeof(dma_addr_t));
	/* 64-bit addressing capability for XDMA? */
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		/* query for DMA transfer */
		/* @see Documentation/DMA-mapping.txt */
		dbg_init("pci_set_dma_mask()\n");
		/* use 64-bit DMA */
		dbg_init("Using a 64-bit DMA mask.\n");
		/* use 32-bit DMA for descriptors */
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		/* use 64-bit DMA, 32-bit for consistent */
	} else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		dbg_init("Could not set 64-bit DMA mask.\n");
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		/* use 32-bit DMA */
		dbg_init("Using a 32-bit DMA mask.\n");
	} else {
		dbg_init("No suitable DMA possible.\n");
		return -EINVAL;
	}

	return 0;
}

//
static u32 get_engine_channel_id(struct engine_regs *regs)
{
	u32 value;

	BUG_ON(!regs);

	value = read_register(&regs->identifier);

	return (value & 0x00000f00U) >> 8;
}

//
static u32 get_engine_id(struct engine_regs *regs)
{
	u32 value;

	BUG_ON(!regs);

	value = read_register(&regs->identifier);
	return (value & 0xffff0000U) >> 16;
}

static void remove_engines(struct xdma_dev *xdev)
{
	struct xdma_engine *engine;
	int i;

	BUG_ON(!xdev);

	/* iterate over channels */
	for (i = 0; i < xdev->h2c_channel_max; i++) {
		engine = &xdev->engine_h2c[i];
		if (engine->magic == MAGIC_ENGINE) {
			dbg_sg("Remove %s, %d", engine->name, i);
			engine_destroy(xdev, engine);
			dbg_sg("%s, %d removed", engine->name, i);
		}
	}

	for (i = 0; i < xdev->c2h_channel_max; i++) {
		engine = &xdev->engine_c2h[i];
		if (engine->magic == MAGIC_ENGINE) {
			dbg_sg("Remove %s, %d", engine->name, i);
			engine_destroy(xdev, engine);
			dbg_sg("%s, %d removed", engine->name, i);
		}
	}
}

//
static int probe_for_engine(struct xdma_dev *xdev, enum dma_data_direction dir,
			int channel)
{
	struct engine_regs *regs;
	int offset = channel * CHANNEL_SPACING;
	u32 engine_id;
	u32 engine_id_expected;
	u32 channel_id;
	struct xdma_engine *engine;
	int rv;

	/* register offset for the engine */
	/* read channels at 0x0000, write channels at 0x1000,
	 * channels at 0x100 interval */
	if (dir == DMA_TO_DEVICE) {
		engine_id_expected = XDMA_ID_H2C;
		engine = &xdev->engine_h2c[channel];
	} else {
		offset += H2C_CHANNEL_OFFSET;
		engine_id_expected = XDMA_ID_C2H;
		engine = &xdev->engine_c2h[channel];
	}

	regs = xdev->bar[xdev->config_bar_idx] + offset;
	engine_id = get_engine_id(regs);
	channel_id = get_engine_channel_id(regs);

	if ((engine_id != engine_id_expected) || (channel_id != channel)) {
		dbg_init("%s %d engine, reg off 0x%x, id mismatch 0x%x,0x%x,"
			"exp 0x%x,0x%x, SKIP.\n",
		 	dir == DMA_TO_DEVICE ? "H2C" : "C2H",
			 channel, offset, engine_id, channel_id,
			engine_id_expected, channel_id != channel);
		return -EINVAL;
	}

	dbg_init("found AXI %s %d engine, reg. off 0x%x, id 0x%x,0x%x.\n",
		 dir == DMA_TO_DEVICE ? "H2C" : "C2H", channel,
		 offset, engine_id, channel_id);

	//
	/* allocate and initialize engine */
	rv = engine_init(engine, xdev, offset, dir, channel);
	if (rv != 0) {
		pr_info("failed to create AXI %s %d engine.\n",
			dir == DMA_TO_DEVICE ? "H2C" : "C2H",
			channel);
		return rv;
	}

	return 0;
}

//
static int probe_engines(struct xdma_dev *xdev)
{
	int i;
	int rv = 0;

	BUG_ON(!xdev);

	/* iterate over channels */
	for (i = 0; i < xdev->h2c_channel_max; i++) {
		rv = probe_for_engine(xdev, DMA_TO_DEVICE, i);
		if (rv)
			break;
	}
	xdev->h2c_channel_max = i;

	for (i = 0; i < xdev->c2h_channel_max; i++) {
		rv = probe_for_engine(xdev, DMA_FROM_DEVICE, i);
		if (rv)
			break;
	}
	xdev->c2h_channel_max = i;

	return 0;
}

//
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
static void pci_enable_relaxed_ordering(struct pci_dev *pdev)
{
	pcie_capability_set_word(pdev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN);
}
#else
static void pci_enable_relaxed_ordering(struct pci_dev *pdev)
{
	u16 v;
	int pos;

	pos = pci_pcie_cap(pdev);
	if (pos > 0) {
		pci_read_config_word(pdev, pos + PCI_EXP_DEVCTL, &v);
		v |= PCI_EXP_DEVCTL_RELAX_EN;
		pci_write_config_word(pdev, pos + PCI_EXP_DEVCTL, v);
	}
}
#endif

//
static void pci_check_extended_tag(struct xdma_dev *xdev, struct pci_dev *pdev)
{
	u16 cap;
	u32 v;
	void *__iomem reg;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
	pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &cap);
#else
	int pos;

	pos = pci_pcie_cap(pdev);
	if (pos > 0)
		pci_read_config_word(pdev, pos + PCI_EXP_DEVCTL, &cap);
	else {
		pr_info("pdev 0x%p, unable to access pcie cap.\n", pdev);
		return;
	}
#endif

	if ((cap & PCI_EXP_DEVCTL_EXT_TAG)) {
		return;
	}

	/* extended tag not enabled */
	pr_info("0x%p EXT_TAG disabled.\n", pdev);

	if (xdev->config_bar_idx < 0) {
		pr_info("pdev 0x%p, xdev 0x%p, config bar UNKNOWN.\n",
			pdev, xdev);
                return;
	}

	reg = xdev->bar[xdev->config_bar_idx] + XDMA_OFS_CONFIG + 0x4C;
	v =  read_register(reg);
	v = (v & 0xFF) | (((u32)32) << 8);
	write_register(v, reg, XDMA_OFS_CONFIG + 0x4C);
}
//
void *xdma_device_open(const char *mname, struct pci_dev *pdev, int *user_max,
			int *h2c_channel_max, int *c2h_channel_max)
{
	struct xdma_dev *xdev = NULL;
	int rv = 0;

	pr_info("%s device %s, 0x%p.\n", mname, dev_name(&pdev->dev), pdev);

	//
	/* allocate zeroed device book keeping structure */
	xdev = alloc_dev_instance(pdev);
	if (!xdev) {
		return NULL;
	}
	xdev->mod_name = mname;
	xdev->user_max = *user_max;
	xdev->h2c_channel_max = *h2c_channel_max;
	xdev->c2h_channel_max = *c2h_channel_max;

	//
	xdma_device_flag_set(xdev, XDEV_FLAG_OFFLINE);
	xdev_list_add(xdev);

	if (xdev->user_max == 0 || xdev->user_max > MAX_USER_IRQ) {
		xdev->user_max = MAX_USER_IRQ;
	}
	if (xdev->h2c_channel_max == 0 ||
	    xdev->h2c_channel_max > XDMA_CHANNEL_NUM_MAX) {
		xdev->h2c_channel_max = XDMA_CHANNEL_NUM_MAX;
	}
	if (xdev->c2h_channel_max == 0 ||
	    xdev->c2h_channel_max > XDMA_CHANNEL_NUM_MAX) {
		xdev->c2h_channel_max = XDMA_CHANNEL_NUM_MAX;
	}
	//
	rv = pci_enable_device(pdev);
	if (rv) {
		dbg_init("pci_enable_device() failed, %d.\n", rv);
		goto err_enable;
	}

	/* keep INTx enabled */
	pci_check_intr_pend(pdev);

	/* enable relaxed ordering */
	pci_enable_relaxed_ordering(pdev);

	pci_check_extended_tag(xdev, pdev);

	/* force MRRS to be 512 */
	rv = pcie_set_readrq(pdev, 128);
	if (rv) {
		pr_info("device %s, error set PCI_EXP_DEVCTL_READRQ: %d.\n",
			dev_name(&pdev->dev), rv);
	}

	/* enable bus master capability */
	pci_set_master(pdev);

	//
	rv = request_regions(xdev, pdev);
	if (rv) {
		goto err_regions;
	}
	//
	rv = map_bars(xdev, pdev);
	if (rv) {
		goto err_map;
	}

	//
	rv = set_dma_mask(pdev);
	if (rv) {
		goto err_mask;
	}

	//
	check_nonzero_interrupt_status(xdev);
	/* explicitely zero all interrupt enable masks */
	channel_interrupts_disable(xdev, ~0);
	user_interrupts_disable(xdev, ~0);
	read_interrupts(xdev);

	//
	rv = probe_engines(xdev);
	if (rv) {
		goto err_engines;
	}

	rv = irq_setup(xdev, pdev);
	if (rv < 0) {
		goto err_interrupts;
	}

	channel_interrupts_enable(xdev, ~0);

	/* Flush writes */
	read_interrupts(xdev);

	*user_max = xdev->user_max;
	*h2c_channel_max = xdev->h2c_channel_max;
	*c2h_channel_max = xdev->c2h_channel_max;

	xdma_device_flag_clear(xdev, XDEV_FLAG_OFFLINE);
	return (void *)xdev;

err_interrupts:
	irq_teardown(xdev);
#if 0
err_enable_msix:
	disable_msi_msix(xdev, pdev);
#endif
err_engines:
	remove_engines(xdev);
err_mask:
	unmap_bars(xdev, pdev);
err_map:
	if (xdev->got_regions) {
		pci_release_regions(pdev);
	}
err_regions:
	if (!xdev->regions_in_use) {
		pci_disable_device(pdev);
	}
err_enable:
	xdev_list_remove(xdev);
	kfree(xdev);
	return NULL;
}
EXPORT_SYMBOL_GPL(xdma_device_open);

void xdma_device_close(struct pci_dev *pdev, void *dev_hndl)
{
	struct xdma_dev *xdev = (struct xdma_dev *)dev_hndl;

	dbg_init("pdev 0x%p, xdev 0x%p.\n", pdev, dev_hndl);

	if (!dev_hndl) {
		return;
	}

	if (debug_check_dev_hndl(__func__, pdev, dev_hndl) < 0) {
		return;
	}

	dbg_sg("remove(dev = 0x%p) where pdev->dev.driver_data = 0x%p\n",
		   pdev, xdev);
	if (xdev->pdev != pdev) {
		dbg_sg("pci_dev(0x%lx) != pdev(0x%lx)\n",
			(unsigned long)xdev->pdev, (unsigned long)pdev);
	}

	channel_interrupts_disable(xdev, ~0);
	user_interrupts_disable(xdev, ~0);
	read_interrupts(xdev);

	irq_teardown(xdev);
//	disable_msi_msix(xdev, pdev);

	remove_engines(xdev);
	unmap_bars(xdev, pdev);

	if (xdev->got_regions) {
		dbg_init("pci_release_regions 0x%p.\n", pdev);
		pci_release_regions(pdev);
	}

	if (!xdev->regions_in_use) {
		dbg_init("pci_disable_device 0x%p.\n", pdev);
		pci_disable_device(pdev);
	}

	xdev_list_remove(xdev);

	kfree(xdev);
}
EXPORT_SYMBOL_GPL(xdma_device_close);

void xdma_device_offline(struct pci_dev *pdev, void *dev_hndl)
{
	struct xdma_dev *xdev = (struct xdma_dev *)dev_hndl;
	struct xdma_engine *engine;
	int i;

	if (!dev_hndl)
		return;

	if (debug_check_dev_hndl(__func__, pdev, dev_hndl) < 0)
		return;

	pr_info("pdev 0x%p, xdev 0x%p.\n", pdev, xdev);
	xdma_device_flag_set(xdev, XDEV_FLAG_OFFLINE);

	/* wait for all engines to be idle */
	for (i  = 0; i < xdev->h2c_channel_max; i++) {
		unsigned long flags;

		engine = &xdev->engine_h2c[i];
		
		if (engine->magic == MAGIC_ENGINE) {
			spin_lock_irqsave(&engine->lock, flags);
			engine->shutdown |= ENGINE_SHUTDOWN_REQUEST;

			xdma_engine_stop(engine);
			engine->running = 0;
			spin_unlock_irqrestore(&engine->lock, flags);
		}
	}

	for (i  = 0; i < xdev->c2h_channel_max; i++) {
		unsigned long flags;

		engine = &xdev->engine_c2h[i];
		if (engine->magic == MAGIC_ENGINE) {
			spin_lock_irqsave(&engine->lock, flags);
			engine->shutdown |= ENGINE_SHUTDOWN_REQUEST;

			xdma_engine_stop(engine);
			engine->running = 0;
			spin_unlock_irqrestore(&engine->lock, flags);
		}
	}

	/* turn off interrupts */
	channel_interrupts_disable(xdev, ~0);
	user_interrupts_disable(xdev, ~0);
	read_interrupts(xdev);
	irq_teardown(xdev);

	pr_info("xdev 0x%p, done.\n", xdev);
}
EXPORT_SYMBOL_GPL(xdma_device_offline);

void xdma_device_online(struct pci_dev *pdev, void *dev_hndl)
{
	struct xdma_dev *xdev = (struct xdma_dev *)dev_hndl;
	struct xdma_engine *engine;
	unsigned long flags;
	int i;

	if (!dev_hndl) {
		return;
	}

	if (debug_check_dev_hndl(__func__, pdev, dev_hndl) < 0) {
		return;
	}

	pr_info("pdev 0x%p, xdev 0x%p.\n", pdev, xdev);

	for (i  = 0; i < xdev->h2c_channel_max; i++) {
		engine = &xdev->engine_h2c[i];
		if (engine->magic == MAGIC_ENGINE) {
			engine_init_regs(engine);
			spin_lock_irqsave(&engine->lock, flags);
			engine->shutdown &= ~ENGINE_SHUTDOWN_REQUEST;
			spin_unlock_irqrestore(&engine->lock, flags);
		}
	}

	for (i  = 0; i < xdev->c2h_channel_max; i++) {
		engine = &xdev->engine_c2h[i];
		if (engine->magic == MAGIC_ENGINE) {
			engine_init_regs(engine);
			spin_lock_irqsave(&engine->lock, flags);
			engine->shutdown &= ~ENGINE_SHUTDOWN_REQUEST;
			spin_unlock_irqrestore(&engine->lock, flags);
		}
	}

	irq_setup(xdev, pdev);

	channel_interrupts_enable(xdev, ~0);
	user_interrupts_enable(xdev, xdev->mask_irq_user);
	read_interrupts(xdev);
	
	xdma_device_flag_clear(xdev, XDEV_FLAG_OFFLINE);
	pr_info("xdev 0x%p, done.\n", xdev);
}
EXPORT_SYMBOL_GPL(xdma_device_online);

int xdma_device_restart(struct pci_dev *pdev, void *dev_hndl)
{
	struct xdma_dev *xdev = (struct xdma_dev *)dev_hndl;

	if (!dev_hndl) {
		return -EINVAL;
	}

	if (debug_check_dev_hndl(__func__, pdev, dev_hndl) < 0) {
		return -EINVAL;
	}
	pr_info("NOT implemented, 0x%p.\n", xdev);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(xdma_device_restart);

static irqreturn_t ax_tsn_irq_config_apply(int irq, void *lp)
{
	struct ax_private *ax_local = (struct ax_private *)lp;

	ax_irq_config_apply(&ax_local->axswitch);
	return IRQ_HANDLED;
}

static irqreturn_t ax_tsn_irq_timer_change(int irq, void *lp)
{
	struct ax_private *ax_local = (struct ax_private *)lp;

	ax_irq_timer_change(&ax_local->axswitch);
	return IRQ_HANDLED;
}

static irqreturn_t ax_tsn_irq_config_change(int irq, void *lp)
{
	struct ax_private *ax_local = (struct ax_private *)lp;

	ax_irq_config_change(&ax_local->axswitch);
	return IRQ_HANDLED;
}

static irqreturn_t ax_tsn_irq_ptp(int irq, void *lp)
{
	struct ax_private *ax_local = (struct ax_private *)lp;
	int i;

	if (ax_retrieve_hw_timestamps(&ax_local->axswitch) == 0) {
		return IRQ_HANDLED;
	}
	for (i = 0; i < 3; i++) {
		if (skb_queue_len(&ax_local->tx_timestamp) == 0) {
			mdelay(200);
		} else {
			goto tx;
		}
	}
	if (i == 3) {
		return IRQ_HANDLED;
	}
tx:
	while (skb_queue_len(&ax_local->tx_timestamp) != 0) {
		struct sk_buff *skb;

		skb = __skb_dequeue(&ax_local->tx_timestamp);
		if (!skb) {
			break;
		}
		ax_tx_check_timestamp(skb, &ax_local->axswitch);
		dev_kfree_skb_irq(skb);
	}
	return IRQ_HANDLED;
}


int xdma_user_isr_register(struct xdma_dev *xdev)
{
	xdev->user_irq[0].handler = ax_tsn_irq_ptp;
	xdev->user_irq[1].handler = NULL;
	xdev->user_irq[2].handler = NULL;
	xdev->user_irq[3].handler = NULL;
	xdev->user_irq[4].handler = ax_tsn_irq_config_change;
	xdev->user_irq[5].handler = ax_tsn_irq_config_apply;
	xdev->user_irq[6].handler = ax_tsn_irq_timer_change;

	return 0;
}

int xdma_user_isr_enable(struct xdma_dev *xdev, unsigned int mask)
{
	xdev->mask_irq_user |= mask;
	/* enable user interrupts */
	user_interrupts_enable(xdev, mask);
	read_interrupts(xdev);

	return 0;
}

int xdma_user_isr_disable(struct xdma_dev *xdev, unsigned int mask)
{
	xdev->mask_irq_user &= ~mask;
	user_interrupts_disable(xdev, mask);
	read_interrupts(xdev);

	return 0;
}
