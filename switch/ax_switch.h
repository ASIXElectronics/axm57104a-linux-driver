/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic
 *     Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
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
/* Number of domains */
#define NUM_OF_DOMAIN           2 
/* Max number of timestamps in queue */
#define TIMESTAMP_QUEUE_SIZE	5
/* Max number of Capture IP cores in a system */
#define MAX_NUM_TSN		8
#define NUM_OF_CAP_REGS		6
#define PTP_HDR_SIZE		34
#define PTP_SEQ_ID_OFFSET	30
#define PTP_MSG_TYPE_OFFSET	0
#define PTP_CLOCK_ID_OFFSET	20
#define TX_TIMESTAMP_RETRIES    21
#define PTP_SUBDOMAIN_OFFSET  4
#define PTP_DOMAIN_NUM          2 
#define RSTP_FORWARDING		0x3

#define MAX_NUM_DOMAINS		2 
#define MAGIC_NUMBER           'g'

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
  AS_T_INT_MASK		= REG_AS_OFFSET(0x0000),
  AS_T_SELECTOR		= REG_AS_OFFSET(0x0004),
  AS_T_ADDEND		= REG_AS_OFFSET(0x0008),
  AS_T_COUNT_PERIOD	= REG_AS_OFFSET(0x000C),
  AS_T_CLOCK_PERIOD	= REG_AS_OFFSET(0x0010),
  AS_T_VAL_I_LO		= REG_AS_OFFSET(0x0014),
  AS_T_VAL_I_HI		= REG_AS_OFFSET(0x0018),
  AS_T_VAL_O_LO		= REG_AS_OFFSET(0x001C),
  AS_T_VAL_O_HI		= REG_AS_OFFSET(0x0020),

  MDIO_REG_OFFSET	= 0x0200,
#define REG_MDIO_OFFSET(value)		(MDIO_REG_OFFSET + value)
  MDIO_CTRL		= REG_MDIO_OFFSET(0x0000),
  MDIO_WRITE_DATA       = REG_MDIO_OFFSET(0x0004),
  MDIO_READ_DATA        = REG_MDIO_OFFSET(0x0008),

  I2C_REG_OFFSET	= 0x0300,
#define REG_I2C_OFFSET(value)		(I2C_REG_OFFSET + value)
  I2C_MASTER_CTRL	= REG_I2C_OFFSET(0x0000),
  I2C_WRITE_DATA_LO     = REG_I2C_OFFSET(0x0004),
  I2C_WRITE_DATA_HI     = REG_I2C_OFFSET(0x0008),
  I2C_READ_DATA_LO      = REG_I2C_OFFSET(0x000C),
  I2C_READ_DATA_HI      = REG_I2C_OFFSET(0x0010),

  IP_REG_OFFSET		= 0x0400,
#define REG_IP_OFFSET(value)		(IP_REG_OFFSET + value)
  IP_INT_VECTOR         = REG_IP_OFFSET(0x0000),
  IP_PSTATUS_MASK       = REG_IP_OFFSET(0x0008),
  IP_PSTATUS_SELECTION  = REG_IP_OFFSET(0x0010),
  IP_PS_VECTOR          = REG_IP_OFFSET(0x0014),
 
/* Filtering Database */
  FDB_REG_OFFSET	= 0x0600,
#define REG_FDB_OFFSET(value)		(FDB_REG_OFFSET + value)  
  FDB_BASE_ADDR = REG_FDB_OFFSET(0x0000),
  FDB_CTRL = REG_FDB_OFFSET(0x000C),

  STP_REG_OFFSET = 0x0900,
#define REG_STP_OFFSET(value)		(STP_REG_OFFSET + value)
  STP_CTRL            = REG_STP_OFFSET(0x0004),
  STP_MSTID_SELECTOR  = REG_STP_OFFSET(0x0008),
  STP_PORT_STATE_LO   = REG_STP_OFFSET(0x000C),
  STP_PORT_STATE_HI   = REG_STP_OFFSET(0x0010),

  DSA_OFFSET		= 0x0C00,
  QBV_REG_OFFSET	= 0x2000,
#define REG_QBV_OFFSET(value)		(QBV_REG_OFFSET + value)
  QBV_PORT_SELECTOR		= REG_QBV_OFFSET(0x0000),
  QBV_QUEUE_SELECTOR	= REG_QBV_OFFSET(0x0004),
  QBV_GATE_ENABLED    = REG_QBV_OFFSET(0x0008),
#define GATE_ENABLE			(1 << 0)
  QBV_ADMIN_CONTROL_LIST_LENGTH	    = REG_QBV_OFFSET(0x0014),
  QBV_ADMIN_CONTROL_LIST_POINTER    = REG_QBV_OFFSET(0x0018),
  QBV_ADMIN_GCL_GATE_STATES         = REG_QBV_OFFSET(0x001C),
  QBV_ADMIN_CTRL_LIST_TIME_INTERVAL = REG_QBV_OFFSET(0x0020),
  QBV_ADMIN_CYCLE_TIME              = REG_QBV_OFFSET(0x0024),
  QBV_ADMIN_CYCLE_TIME_EXTENDED     = REG_QBV_OFFSET(0x0028),
  QBV_ADMIN_BASE_TIME_NS_HI         = REG_QBV_OFFSET(0x0030),
  QBV_ADMIN_BASE_TIME_NS_LO         = REG_QBV_OFFSET(0x0034),
  QBV_OPER_CONTROL_LIST_LENGTH      = REG_QBV_OFFSET(0x003C),
  QBV_OPER_CONTROL_LIST_POINTER     = REG_QBV_OFFSET(0x0040),
  QBV_OPER_GCL_GATE_STATES          = REG_QBV_OFFSET(0x0044),
  QBV_OPER_CTRL_LIST_TIME_INTERVAL  = REG_QBV_OFFSET(0x0048),
  QBV_OPER_CYCLE_TIME               = REG_QBV_OFFSET(0x004C),
  QBV_OPER_CYCLE_TIME_EXTENDED      = REG_QBV_OFFSET(0x0050),
  QBV_OPER_BASE_TIME_NS_HI          = REG_QBV_OFFSET(0x0058),
  QBV_OPER_BASE_TIME_NS_LO          = REG_QBV_OFFSET(0x005C),
  QBV_CONFIG_CHANGE_REQ             = REG_QBV_OFFSET(0x0060),
  QBV_CONFIG_CHANGE_CTRL            = REG_QBV_OFFSET(0x0064),
  QBV_CONFIG_CHANGE_STATE           = REG_QBV_OFFSET(0x0068),
  QBV_CONFIG_CHANGE_TIME_S_H        = REG_QBV_OFFSET(0x006C),
  QBV_CONFIG_CHANGE_TIME_NS_HI      = REG_QBV_OFFSET(0x0070),
  QBV_CONFIG_CHANGE_TIME_NS_LO      = REG_QBV_OFFSET(0x0074),
  QBV_CYCLE_START_TIME_CTRL         = REG_QBV_OFFSET(0x0078),
  QBV_CYCLE_START_TIME_S_H          = REG_QBV_OFFSET(0x007C),
  QBV_CYCLE_START_TIME_NS_HI        = REG_QBV_OFFSET(0x0080),
  QBV_CYCLE_START_TIME_NS_LO        = REG_QBV_OFFSET(0x0084),
  QBV_CONFIG_CHANGE_ERROR_CNT       = REG_QBV_OFFSET(0x0088),
  QBV_TIMER_DOMAIN_INDEX            = REG_QBV_OFFSET(0x0098),
  QBV_TIMER_CHANGE_REQUEST          = REG_QBV_OFFSET(0x009C),

#define CHANGE_ENABLE			(1 << 0)

  PTP_REG_OFFSET        = 0x3100,
#define REG_PTP_OFFSET(value)           (PTP_REG_OFFSET + value)
  PTP_TIMESTAMP_CTRL    = REG_PTP_OFFSET(0x0010),
  PTP_RX_PORT           = REG_PTP_OFFSET(0x0014),
  PTP_FR_RX_TIMESTAMP_LO = REG_PTP_OFFSET(0x0018),
  PTP_FR_RX_TIMESTAMP_HI = REG_PTP_OFFSET(0x001C),
  PTP_RX_D0_TIMESTAMP_LO = REG_PTP_OFFSET(0x0020),
  PTP_RX_D0_TIMESTAMP_HI = REG_PTP_OFFSET(0x0024),
  PTP_RX_D1_TIMESTAMP_LO = REG_PTP_OFFSET(0x0028),
  PTP_RX_D1_TIMESTAMP_HI = REG_PTP_OFFSET(0x002C),
  PTP_RX_D2_TIMESTAMP_LO = REG_PTP_OFFSET(0x0030),
  PTP_RX_D2_TIMESTAMP_HI = REG_PTP_OFFSET(0x0034),
  PTP_RX_D3_TIMESTAMP_LO = REG_PTP_OFFSET(0x0038),
  PTP_RX_D3_TIMESTAMP_HI = REG_PTP_OFFSET(0x003C),
  PTP_RX_MESSAGE_INFO    = REG_PTP_OFFSET(0x0040),
  PTP_TX_PORT            = REG_PTP_OFFSET(0x0044),
  PTP_FR_TX_TIMESTAMP_LO = REG_PTP_OFFSET(0x0048),
  PTP_FR_TX_TIMESTAMP_HI = REG_PTP_OFFSET(0x004C),
  PTP_TX_D0_TIMESTAMP_LO = REG_PTP_OFFSET(0x0050),
  PTP_TX_D0_TIMESTAMP_HI = REG_PTP_OFFSET(0x0054),
  PTP_TX_D1_TIMESTAMP_LO = REG_PTP_OFFSET(0x0058),
  PTP_TX_D1_TIMESTAMP_HI = REG_PTP_OFFSET(0x005C),
  PTP_TX_D2_TIMESTAMP_LO = REG_PTP_OFFSET(0x0060),
  PTP_TX_D2_TIMESTAMP_HI = REG_PTP_OFFSET(0x0064),
  PTP_TX_D3_TIMESTAMP_LO = REG_PTP_OFFSET(0x0068),
  PTP_TX_D3_TIMESTAMP_HI = REG_PTP_OFFSET(0x006C),
  PTP_TX_MESSAGE_INFO   = REG_PTP_OFFSET(0x0070),


  PORT_OFFSET		 = 0x3000,
  PORT_MOD_VERN		 = 0x0000,
  PORT_LTCN_TXRX_10	 = 0x0008,
  PORT_LTCN_TXRX_100	 = 0x000C,
  PORT_LTCN_TXRX_1000	 = 0x0010,
  PORT_M_PHY_SPEED	 = 0x001C,
  PORT_RX_FRAME		 = 0x0020,
  PORT_TX_FRAME		 = 0x0024,
  PORT_FRAME_ERR	 = 0x0028,

  PORT_CAP_CTRL		 = 0x008C,
#define PCAPC_RX_FIFO_STATUS		0x00000001
#define PCAPC_TX_FIFO_STATUS		0x00000002
  PORT_RX_TIMEA_LO	 = 0x0090,
  PORT_TX_TIMEA_LO	 = 0x00A8,
  PORT_RX_CLOCK_ID_HI    = 0x00A4,
};
#define REG_PORTS_OFFSET(port, offset)   (PORT_OFFSET + (0x100 * port) + offset)
// End of TSN Switch Register -------------------------------------------------

struct domain_number_table_data {
	int domain_number_map[MAX_NUM_DOMAINS+1];
};

struct ioctl_timestamp_data {
	uint8_t port_id;
	uint8_t msg_type;
	uint16_t seq_id;
	uint8_t domain_number;
	uint64_t timestamp;
};

typedef struct _capture_data {
	uint8_t		port_id;
	/* Arrays used to store the captured timestamps, size depends on number 
	   of implemented domains, reserved on probe */
	uint32_t	timestamp_l[MAX_NUM_DOMAINS+1];
	uint32_t	timestamp_h[MAX_NUM_DOMAINS+1];
	uint8_t		msg_type;
	uint16_t	sequence_id;
	uint8_t		domain_number;
} CAPTURE_DATA, *PCAPTURE_DATA;

typedef struct _ts_queue {
	CAPTURE_DATA	*ts_items;
	int		write_ptr;
	int		num_items;
	int		max_num_items;
	spinlock_t	lock;
} TS_QUEUE, *PTS_QUEUE;

struct ax_ptp_clock_data {
	struct ptp_clock_info   ptp_caps;
	struct ptp_clock        *ptp_clock;
	unsigned int            ptp_clock_index;
	int                     domain_index;
	uint32_t                base_addend_val;
	void __iomem            *pMemBase;
};

struct ax_switch {
  unsigned long			mem_start;
  unsigned long			mem_end;
  void __iomem			*pMemBase;
  void __iomem			*ptestMemBase;
  spinlock_t		*ptxrx_timestamp_lock;
  struct device *dev;
  struct cdev mtsn_cdev;

  int irq_timer_change;
  int irq_timer_value_change;
  int irq_config_change;
  int irq_ptp;

  TS_QUEUE			rx_queue;
  TS_QUEUE			tx_queue;
  struct ax_ptp_clock_data	ax_ptp_clocks[PTP_DOMAIN_NUM];
  int				number_of_ports;
  int				number_of_domains;
  struct domain_number_table_data domain_number_table;
  unsigned int			phc_id;
  unsigned int			phc_index[MAX_NUM_TSN];
  unsigned int		tsn_eth_tag[MAX_NUM_TSN];
  /* PTP working mode, 0 for 802.1AS, 1 for 1588 */
  int ptp_mode;
};

typedef struct _skb_tstamp_msg {
	struct sk_buff 	*skb;
	struct ax_switch *pSwitch;
	unsigned int 	ptp_msg_offset;
	unsigned short  ptp_vlan_id;
	unsigned short	port_tag;
} SKB_TSTAMP_MSG, *PSKB_TSTAMP_MSG;

/*SWITCH API*/
void _tsn_tsq_flush(TS_QUEUE *queue);
void _tsn_tsq_insert_item(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d);
void _tsn_tsq_remove(TS_QUEUE *queue);
uint64_t get_cycle_start_time(uint64_t current_time, uint64_t oper_base_time, uint32_t oper_cycle_time);
void _ax_time_to_ull(uint64_t *time64, uint32_t seconds, uint32_t nanoseconds);
void _ax_insert_items(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d);
bool _ax_tsq_check_item_1(CAPTURE_DATA *queue_d, CAPTURE_DATA *tstamp_d);
//End of SWITCH APIs

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
int ax_tsn_init_switch(struct ax_switch *pSwitch, spinlock_t *pLock);
// End of Initialize APIs

//
// Interrupt APIs
//
void ax_irq_config_change(struct ax_switch *pSwitch);
void ax_irq_timer_change(struct ax_switch *pSwitch);
void ax_irq_timer_value_change(struct ax_switch *pSwitch);
//void ax_irq_config_apply(struct ax_switch *pSwitch);
// End of Interrupt APIs

//
// For PTP APIs
//
int ax_retrieve_hw_timestamps(struct ax_switch *pSwitch);
void ax_retrieve_rx_hw_timestamps(struct ax_switch *pSwitch);
int ax_tsq_find_item_1(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d);
void ax_tsn_rx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp);
void ax_tsn_tx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp);
int get_index_by_mask(uint32_t mask, uint32_t max_index);
// End of For PTP APIs
void ax_fast_age(struct ax_switch *pSwitch);
#endif /* End of __AX_SWITCH_H__ */
