/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *****************************************************************************/

#ifndef __AX_SWITCH_H__
#define __AX_SWITCH_H__

/* INCLUDE FILE DECLARATIONS */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/mii.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/in.h>
#include <linux/log2.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 3, 0)
        #include <asm/switch_to.h>
#else
        #include <asm/system.h>
#endif
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
//#include "ax_ptp.h"

/* NAMING CONSTANT AND TYPE DECLARATIONS */
#define NS_PER_SEC	1000000000ULL	/* Nanosecond per second */

//Return Value definition
typedef int				NAPA_STATUS;
#define AX_STATUS_NO_DATA		1
#define AX_STATUS_SUCCESS		0
#define AX_STATUS_FAILURE		-1
#define AX_STATUS_NOT_SUPPORT		-2
#define AX_STATUS_QITEM_NOT_FOUND	-3
#define AX_STATUS_Q_NO_ITEM		-4

typedef unsigned char			NAPA_CONFIG;
#define ENABLE				1
#define DISABLE				0

//
//Ethernet related definition
//
#define	MAX_ETHERNET_FRAME_SIZE     1518
#define	MIN_ETHERNET_FRAME_SIZE     60
#define	ETHERNET_HEADER_SIZE        14
#define	ETH_ADDR_LEN                6
#define	MAX_MULTICAST_LIST          8

/* Number of ports */
#define NUM_SWITCH_PORTS	5
/* Max number of timestamps in queue */
#define TIMESTAMP_QUEUE_SIZE	5
/* Max number of Capture IP cores in a system */
#define MAX_NUM_TSN		8
#define NUM_OF_CAP_REGS		6
#define PTP_HDR_SIZE		34
#define PTP_SEQ_ID_OFFSET	30
#define PTP_MSG_TYPE_OFFSET	0
#define PTP_CLOCK_ID_OFFSET	20

//-----------------------------------------------------------------------------
// TSN Switch Control/Status Registers (CSR)
//-----------------------------------------------------------------------------
enum {
  DRIVER_VERSION	= 0x0000,
  CORE_VERSION		= 0x0000,
  MAC_ADDR_LO		= 0x0004,
  MAC_ADDR_HI		= 0x0008,
  IP_ADDRESS		= 0x000C,
  TSN_LICENSE		= 0x0010,
  GUI_ENABLE		= 0x0014,

  AS_REG_OFFSET		= 0x0100,	
#define REG_AS_OFFSET(value)		(AS_REG_OFFSET + value)
  AS_MOD_VERN		= REG_AS_OFFSET(0x0000),
  AS_T_ADDEND		= REG_AS_OFFSET(0x0004),
  AS_T_PERIOD		= REG_AS_OFFSET(0x0008),
  AS_T_VAL_I_LO		= REG_AS_OFFSET(0x000C),
  AS_T_VAL_I_HI		= REG_AS_OFFSET(0x0010),
  AS_T_VAL_O_LO		= REG_AS_OFFSET(0x0014),
  AS_T_VAL_O_HI		= REG_AS_OFFSET(0x0018),
  AS_VALID_ALARM	= REG_AS_OFFSET(0x001C),
  AS_A_VAL_LO		= REG_AS_OFFSET(0x0020),
  AS_A_VAL_HI		= REG_AS_OFFSET(0x0024),

  MDIO_REG_OFFSET	= 0x0200,
#define REG_MDIO_OFFSET(value)		(MDIO_REG_OFFSET + value)
  MDIO_MOD_VERN		= REG_MDIO_OFFSET(0x0000),
  MDIO_CTRL		= REG_MDIO_OFFSET(0x0004),

  I2C_REG_OFFSET	= 0x0300,
#define REG_I2C_OFFSET(value)		(I2C_REG_OFFSET + value)
  I2C_MOD_VERN		= REG_I2C_OFFSET(0x0000),
  I2C_MASTER_CTRL	= REG_I2C_OFFSET(0x0004),
  I2C_RW_DATA_LO	= REG_I2C_OFFSET(0x000C),
  I2C_RW_DATA_HI	= REG_I2C_OFFSET(0x0010),

  IP_REG_OFFSET		= 0x0400,
#define REG_IP_OFFSET(value)		(IP_REG_OFFSET + value)
  IP_MOD_VERN		= REG_IP_OFFSET(0x0000),
  IP_INT_VECTOR		= REG_IP_OFFSET(0x0004),
  IP_GSTATUS_MASK	= REG_IP_OFFSET(0x0008),
  IP_PSTATUS_MASK	= REG_IP_OFFSET(0x000C),
  IP_GSTATUS_VECTOR	= REG_IP_OFFSET(0x0010),
  IP_P0_STATUS		= REG_IP_OFFSET(0x0014),
  IP_P1_STATUS		= REG_IP_OFFSET(0x0018),
  IP_P2_STATUS		= REG_IP_OFFSET(0x001C),
  IP_P3_STATUS		= REG_IP_OFFSET(0x0020),

  ESE_REG_OFFSET	= 0x0500,
#define REG_ESE_OFFSET(value)		(ESE_REG_OFFSET + value)
  ESE_MOD_VERN		= REG_ESE_OFFSET(0x0000),
  ESE_FORWARD_CONF	= REG_ESE_OFFSET(0x0004),
  ESE_NATIVE_VLAN	= REG_ESE_OFFSET(0x0008),
  ESE_VID_MASK_CONF	= REG_ESE_OFFSET(0x000C),
  ESE_VID_MASK		= REG_ESE_OFFSET(0x0010),
  ESE_SWITCH_MASK	= REG_ESE_OFFSET(0x0014),
  ESE_MAC_TAB_CLR	= REG_ESE_OFFSET(0x0018),
  ESE_AGEING_TIME	= REG_ESE_OFFSET(0x001C),
  ESE_PRIORITY_CONF	= REG_ESE_OFFSET(0x0020),
  ESE_PORT_FR_LIMIT	= REG_ESE_OFFSET(0x002C),
  ESE_MAC_ADDR_QUERY	= REG_ESE_OFFSET(0x0030),
  ESE_TSN_CTRL		= REG_ESE_OFFSET(0x004C),
  ESE_RSTP_PORT_STATUS	= REG_ESE_OFFSET(0x005C),
#define RSTP_FORWARDING			0x3
  ESE_WIN_DURATION	= REG_ESE_OFFSET(0x006C),

  DSA_OFFSET		= 0x0900,

  QBV_REG_OFFSET	= 0x0A00,
#define REG_QBV_OFFSET(value)		(QBV_REG_OFFSET + value)
  QBV_PORT_ID		= REG_QBV_OFFSET(0x0000),
  QBV_GATE_ENABLED	= REG_QBV_OFFSET(0x0004),
#define GATE_ENABLE			(1 << 0)
  QBV_ADMIN_CTRL_LENG	= REG_QBV_OFFSET(0x0010),
  QBV_OPER_CTRL_LENG	= REG_QBV_OFFSET(0x0014),
  QBV_ADMIN_CTRL_POINT	= REG_QBV_OFFSET(0x0018),
  QBV_ADMIN_GCLE_GATE_S	= REG_QBV_OFFSET(0x001C),
  QBV_ADMIN_GCLE_TIME_I	= REG_QBV_OFFSET(0x0020),
  QBV_OPER_CTRL_POINT	= REG_QBV_OFFSET(0x0024),
  QBV_OPER_GCLE_GATE_S	= REG_QBV_OFFSET(0x0028),
  QBV_OPER_GCLE_TIME_I	= REG_QBV_OFFSET(0x002C),
  QBV_ADMIN_CYCLE_TIME	= REG_QBV_OFFSET(0x0030),
  QBV_OPER_CYCLE_TIME	= REG_QBV_OFFSET(0x0034),
  QBV_ADMIN_BASE_T_S	= REG_QBV_OFFSET(0x0044),
  QBV_ADMIN_BASE_T_NS	= REG_QBV_OFFSET(0x0048),
  QBV_OPER_BASE_T_S	= REG_QBV_OFFSET(0x0050),
  QBV_OPER_BASE_T_NS	= REG_QBV_OFFSET(0x0054),
  QBV_CONFIG_CHG_REQ	= REG_QBV_OFFSET(0x0058),
  QBV_CONFIG_CHG	= REG_QBV_OFFSET(0x005C),
#define CHANGE_ENABLE			(1 << 0)

  QBV_CONFIG_CHG_T_S	= REG_QBV_OFFSET(0x0064),
  QBV_CONFIG_CHG_T_NS	= REG_QBV_OFFSET(0x0068),
  QBV_CONFIG_APPLY_REQ	= REG_QBV_OFFSET(0x006C),
  QBV_CONFIG_APPLY	= REG_QBV_OFFSET(0x0070),
  QBV_CONFIG_APPLY_TIME_S	= REG_QBV_OFFSET(0x0078),
  QBV_CONFIG_APPLY_TIME_NS	= REG_QBV_OFFSET(0x007C),

  PORT_OFFSET		= 0x1000,
  PORT_MOD_VERN		= 0x0000,
  PORT_LTCN_TXRX_10	= 0x0008,
  PORT_LTCN_TXRX_100	= 0x000C,
  PORT_LTCN_TXRX_1000	= 0x0010,
  PORT_M_PHY_SPEED	= 0x001C,
  PORT_RX_FRAME		= 0x0020,
  PORT_TX_FRAME		= 0x0024,
  PORT_FRAME_ERR	= 0x0028,

  PORT_CAP_CTRL		= 0x008C,
#define PCAPC_RX_FIFO_STATUS		(1 << 7)
#define PCAPC_TX_FIFO_STATUS		(1 << 15)
  PORT_RX_TIMEA_LO	= 0x0090,
  PORT_TX_TIMEA_LO	= 0x00A8,
};
#define REG_PORTS_OFFSET(port,offset)	(PORT_OFFSET + (0x100 * port) + offset)
// End of TSN Switch Register -------------------------------------------------

typedef struct _capture_data {
	uint32_t	timestamp_l;
	uint32_t	timestamp_h;
	uint8_t		msg_type;
	uint16_t	sequence_id;
	uint32_t	ptp_clock_id_l;
	uint32_t	ptp_clock_id_h;
	uint16_t	ptp_port_id;
	uint16_t	ptp_vlan_id;
} CAPTURE_DATA, *PCAPTURE_DATA;

typedef struct _ts_queue {
	CAPTURE_DATA	ts_items[TIMESTAMP_QUEUE_SIZE];
	int		write_ptr;
	int		num_items;
	spinlock_t	lock;
} TS_QUEUE, *PTS_QUEUE;

struct ax_switch {
	struct ptp_clock_info 	ptp_caps;
	struct ptp_clock 	*ptp_clock;
	unsigned int 		phc_id;
	unsigned int		phc_index[MAX_NUM_TSN];
	u32 			base_addend_val;

	void __iomem		*pMemBase;
	void __iomem		*ptestMemBase;

	unsigned long		FTPage;
	unsigned char		FTEnd;
	
	unsigned long		INT_MASK;	

	uint32_t		qbv_current_port;
	int			number_of_ports;
	int			port_has_capture[NUM_SWITCH_PORTS];
	unsigned int		tsn_eth_tag[MAX_NUM_TSN];	
	TS_QUEUE		rx_queue[MAX_NUM_TSN];
	TS_QUEUE		tx_queue[MAX_NUM_TSN];
};

typedef struct _skb_tstamp_msg {
	struct sk_buff 	*skb;
	struct ax_switch *pSwitch;
	unsigned int 	ptp_msg_offset;
	unsigned short  ptp_vlan_id;
	unsigned short	port_tag;	
} SKB_TSTAMP_MSG, *PSKB_TSTAMP_MSG;


/* EXPORTED SUBPROGRAM SPECIFICATIONS */
//
// Access APIs
//
unsigned long 
ax_tsn_read_reg(struct ax_switch *pSwitch, unsigned short RegAddr);
void ax_tsn_write_reg(struct ax_switch *pSwitch, 
		      unsigned short RegAddr, unsigned long Value);
// End of Access APIs

//
// Initialize APIs
//
void ax_tsn_init_switch(struct ax_switch *pSwitch);
// End of Initialize APIs

//
// Interrupt APIs
//
void ax_irq_config_change(struct ax_switch *pSwitch);
void ax_irq_timer_change(struct ax_switch *pSwitch);
void ax_irq_config_apply(struct ax_switch *pSwitch);
// End of Interrupt APIs

//
// For PTP APIs
//
int ax_retrieve_hw_timestamps(struct ax_switch *pSwitch);
int ax_tsq_find_item_1(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d);
void ax_tsn_rx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp);
void ax_tsn_tx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp);
// End of For PTP APIs
#endif /* End of __AX_SWITCH_H__ */
