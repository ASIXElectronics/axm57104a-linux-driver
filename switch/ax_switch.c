/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *****************************************************************************/

/* INCLUDE FILE DECLARATIONS */
#include "ax_switch.h"
#include "../libxdma.h"


/* LOCAL VARIABLES DECLARATIONS */
static struct {
	uint8_t		number_of_ports;
	bool		port_has_capture[NUM_SWITCH_PORTS];
} switch_config = {
	.number_of_ports	= NUM_SWITCH_PORTS,
	.port_has_capture[0]	= true,
	.port_has_capture[1]	= true,
	.port_has_capture[2]	= true,
	.port_has_capture[3]	= true,
	.port_has_capture[4]	= true,		// Manager Port
};
/* LOCAL SUBPROGRAM DECLARATIONS */

//
// Access APIs
//
unsigned long 
ax_tsn_read_reg(struct ax_switch *pSwitch, unsigned short RegAddr)
{
	u32 value = ioread32(pSwitch->pMemBase + RegAddr);	

	return value;
} /* End of ax_tsn_read_reg() */


void
ax_tsn_write_reg(struct ax_switch *pSwitch, unsigned short RegAddr,
		 unsigned long Value)
{
	iowrite32(Value, pSwitch->pMemBase + RegAddr);
} /* End of ax_tsn_write_reg() */
// End of Access APIs


//
// Initialize APIs
//
void _tsn_tsq_flush(TS_QUEUE *queue)
{
	unsigned long flags;
	spin_lock_irqsave(&(queue->lock), flags);

	queue->write_ptr = 0;
	queue->num_items = 0;

	spin_unlock_irqrestore(&(queue->lock), flags);	
} /* End of _tsn_tsq_flush() */


void _tsn_tsq_init(TS_QUEUE *queue)
{
	spin_lock_init(&(queue->lock));
	_tsn_tsq_flush(queue);
} /* End of _tsn_tsq_init() */

void ax_tsn_init_switch(struct ax_switch *pSwitch)
{
	if (pSwitch != NULL) {
		int i;

		pSwitch->number_of_ports = switch_config.number_of_ports;
		for (i = 0; i < pSwitch->number_of_ports; i++) {
			pSwitch->port_has_capture[i] =
				switch_config.port_has_capture[i];
			if (pSwitch->port_has_capture[i]) {
				pSwitch->tsn_eth_tag[i] = i;
				_tsn_tsq_init(&(pSwitch->rx_queue[i]));
				_tsn_tsq_init(&(pSwitch->tx_queue[i]));
			} else {
				pSwitch->tsn_eth_tag[i] = 0xFF;
			}
			pSwitch->qbv_current_port = 0;
		}
	}
} /* End of ax_tsn_init_switch() */
// End of Initialize APIs

//
// Interrupt APIs
//
static uint32_t _get_port_id (uint32_t config_change_reg)
{
	if (config_change_reg > 0 && config_change_reg <= 0x8000)
		return (__ilog2_u32(config_change_reg));
	else
		return -1;
}

void ax_irq_config_change(struct ax_switch *pSwitch)
{
	uint64_t current_time, admin_base_time, config_change_time;
	uint64_t config_change_time_s, config_change_time_ns;
	uint64_t actualstep, nextstep;
	uint32_t admin_cycle_time, reg32;
	int port_id;

	reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, QBV_CONFIG_CHG_REQ);
	port_id = _get_port_id(reg32);
	if (port_id != -1) {
		ax_tsn_write_reg(pSwitch, QBV_PORT_ID, port_id);
		reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, QBV_GATE_ENABLED);
		if (reg32 & GATE_ENABLE) {
			ax_tsn_write_reg(pSwitch, QBV_GATE_ENABLED, 0);
			admin_cycle_time =
				(uint32_t)ax_tsn_read_reg(pSwitch,
							QBV_ADMIN_CYCLE_TIME);
			admin_base_time =
				(uint32_t)ax_tsn_read_reg(pSwitch,
							QBV_ADMIN_BASE_T_S);
			reg32 = (uint32_t)ax_tsn_read_reg(pSwitch,
							QBV_ADMIN_BASE_T_NS);
			admin_base_time = (admin_base_time * 1000000000) 
							+ reg32;
			current_time = (uint32_t)ax_tsn_read_reg(pSwitch, 
								AS_T_VAL_O_HI);
			reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, 
								AS_T_VAL_O_LO);
			current_time = (current_time * 1000000000) + reg32;

			if (admin_base_time >= current_time) {
				config_change_time = admin_base_time;
			} else {
				actualstep = div_u64(
					(current_time - admin_base_time),
					admin_cycle_time);
				nextstep = actualstep + 2;
				config_change_time = admin_base_time +
							(nextstep * 
							admin_cycle_time);
			}
			config_change_time_s = 
					div64_u64_rem(
						config_change_time,
						1000000000,
						&config_change_time_ns);
			ax_tsn_write_reg(pSwitch, QBV_CONFIG_CHG_T_S,
					 (uint32_t)config_change_time_s);
			ax_tsn_write_reg(pSwitch, QBV_CONFIG_CHG_T_NS,
					 (uint32_t)config_change_time_ns);
			ax_tsn_write_reg(pSwitch, 
					QBV_GATE_ENABLED, 
					GATE_ENABLE);
			ax_tsn_write_reg(pSwitch, 
					QBV_CONFIG_CHG, 
					CHANGE_ENABLE);
		}
	}
	ax_tsn_write_reg(pSwitch, QBV_CONFIG_CHG_REQ, 0);
} /* End of ax_irq_config_change() */

void ax_irq_timer_change(struct ax_switch *pSwitch)
{
	uint64_t current_time, oper_base_time;
	uint64_t config_apply_time, config_apply_time_s, config_apply_time_ns;
	uint64_t actualstep, nextstep;
	uint32_t oper_cycle_time, reg32;
	int port_id;

	for (port_id = 0; port_id < pSwitch->number_of_ports; port_id++) {
		ax_tsn_write_reg(pSwitch, QBV_PORT_ID, port_id);
		reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, QBV_GATE_ENABLED);
		if (reg32 & GATE_ENABLE) {
			oper_cycle_time =
				(uint32_t)ax_tsn_read_reg(pSwitch,
							  QBV_OPER_CYCLE_TIME);
			oper_base_time =
				(uint32_t)ax_tsn_read_reg(pSwitch,
							  QBV_OPER_BASE_T_S);
			reg32 = (uint32_t)ax_tsn_read_reg(pSwitch,
							  QBV_OPER_BASE_T_NS);
			oper_base_time = (oper_base_time * 1000000000) + reg32;
			current_time = (uint32_t)ax_tsn_read_reg(pSwitch,
								AS_T_VAL_O_HI);
			reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, 
								AS_T_VAL_O_LO);
			current_time = (current_time * 1000000000) + reg32;

			if (oper_base_time >= current_time) {
				config_apply_time = oper_base_time;
			} else {
				actualstep = div_u64(
						(current_time-oper_base_time),
					   	oper_cycle_time);
				nextstep = actualstep + 2;
				config_apply_time = oper_base_time +
						    (nextstep*oper_cycle_time);
			}
			config_apply_time_s = div64_u64_rem(config_apply_time,
							1000000000,
							&config_apply_time_ns);
							    
			ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY_TIME_S,
					 (uint32_t)config_apply_time_s);
			ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY_TIME_NS,
					 (uint32_t)config_apply_time_ns);
			ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY, 0x01);
		}
	}
} /* End of ax_irq_timer_change() */

void ax_irq_config_apply(struct ax_switch *pSwitch)
{
	uint64_t current_time, oper_base_time;
	uint64_t config_apply_time, config_apply_time_s, config_apply_time_ns;
	uint64_t actualstep, nextstep;
	uint32_t oper_cycle_time, reg32;
	int port_id;

	reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, QBV_CONFIG_APPLY_REQ);
	port_id = _get_port_id(reg32);
	if (port_id != -1) {
		ax_tsn_write_reg(pSwitch, QBV_PORT_ID, port_id);
		oper_cycle_time =
			(uint32_t)ax_tsn_read_reg(pSwitch,
						  QBV_OPER_CYCLE_TIME);
		oper_base_time =
			(uint32_t)ax_tsn_read_reg(pSwitch,
						  QBV_OPER_BASE_T_S);
		reg32 = (uint32_t)ax_tsn_read_reg(pSwitch,
						  QBV_OPER_BASE_T_NS);
		oper_base_time = (oper_base_time * 1000000000) + reg32;
		current_time = (uint32_t)ax_tsn_read_reg(pSwitch,
					        	 AS_T_VAL_O_HI);
		reg32 = (uint32_t)ax_tsn_read_reg(pSwitch, AS_T_VAL_O_LO);
		current_time = (current_time * 1000000000) + reg32;

		if (oper_base_time >= current_time) {
			config_apply_time = oper_base_time;
		} else {
			actualstep = div_u64((current_time - oper_base_time),
				     	     oper_cycle_time);
			nextstep = actualstep + 2;
			config_apply_time = oper_base_time +
				     	     (nextstep * oper_cycle_time);
		}
		config_apply_time_s = div64_u64_rem(config_apply_time,
						    1000000000,
						    &config_apply_time_ns);
						    
		ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY_TIME_S,
				 (uint32_t)config_apply_time_s);
		ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY_TIME_NS,
				 (uint32_t)config_apply_time_ns);
		ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY, 0x01);
	}
	ax_tsn_write_reg(pSwitch, QBV_CONFIG_APPLY_REQ, 0);
} /* End of ax_irq_config_apply() */

//
// For PTP APIs
//
void _ax_time_to_ull(uint64_t *time64, uint32_t seconds, uint32_t nanoseconds)
{
	*time64 = seconds;
	*time64 = NS_PER_SEC;
	*time64 += nanoseconds;
} /* End of _ax_time_to_ull() */


void _ax_get_cap_data(struct ax_switch *pSwitch, int offset, PCAPTURE_DATA ts)
{
	int port, off;
	uint32_t reg_val[NUM_OF_CAP_REGS];
	
	for (port = 0, off = offset; port < NUM_OF_CAP_REGS; port++, off+=4) {
		reg_val[port] = (uint32_t)ax_tsn_read_reg(pSwitch, off);
	}
	ts->timestamp_l 	= (uint32_t)reg_val[0];
	ts->timestamp_h 	= (uint32_t)reg_val[1];
	ts->msg_type 		= (uint8_t) (reg_val[2] & 0xF);
	ts->sequence_id 	= (uint16_t)(reg_val[2] >> 16);
	ts->ptp_port_id 	= (uint16_t)reg_val[3];
	ts->ptp_vlan_id 	= (uint16_t)(reg_val[3] >> 16);
	ts->ptp_clock_id_l 	= (uint32_t)reg_val[4];
	ts->ptp_clock_id_h 	= (uint32_t)reg_val[5];
	ASIX_DEBUG("timestamp_l: 0x%x timestamp_h: 0x%x ptp_clock_id_l: 0x%x \
			ptp_clock_id_h: 0x%x ",
			ts->timestamp_l, 
			ts->timestamp_h, 
			ts->ptp_clock_id_l, 
			ts->ptp_clock_id_h);
	ASIX_DEBUG("msg_type: 0x%x sequence_id: 0x%x ptp_port_id: 0x%x \
			ptp_vlan_id: 0x%x ",
			ts->msg_type, 
			ts->sequence_id, 
			ts->ptp_port_id, 
			ts->ptp_vlan_id);
} /* End of _ax_get_cap_data() */

void _ax_insert_items(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d)
{
	unsigned long flags;
	
	spin_lock_irqsave(&(queue->lock), flags);

	queue->ts_items[queue->write_ptr] = *tstamp_d;
	queue->write_ptr++;
	if (queue->write_ptr == TIMESTAMP_QUEUE_SIZE) {
		queue->write_ptr = 0;
	}
	if (queue->num_items < TIMESTAMP_QUEUE_SIZE) {
		queue->num_items++;
	}
	ASIX_DEBUG("_ax_insert_items queue->num_items %d", queue->num_items);
	spin_unlock_irqrestore(&(queue->lock), flags);	
} /* End of _ax_insert_items() */

int ax_retrieve_hw_timestamps(struct ax_switch *pSwitch)
{
	CAPTURE_DATA ts;	
	int port;
	int tx_count = 0;

	for (port = 0; port < (pSwitch->number_of_ports - 1); port++) {
		
		if (pSwitch->port_has_capture[port]) {
			uint32_t fifo_ctrl = ax_tsn_read_reg(pSwitch,
					REG_PORTS_OFFSET(port, PORT_CAP_CTRL));
			ASIX_DEBUG("%s %d %x %x start\n",
				 __FUNCTION__, 
				port, 
				REG_PORTS_OFFSET(port, PORT_CAP_CTRL), 
				fifo_ctrl);
			/* RX */
			while ((fifo_ctrl & PCAPC_RX_FIFO_STATUS) != 0) {
				ASIX_DEBUG("%s RX %d %x start\n", 
					__FUNCTION__, 
					port, 
					fifo_ctrl);
				_ax_get_cap_data(pSwitch,
						 REG_PORTS_OFFSET(port, 
							PORT_RX_TIMEA_LO),
						 &ts);
				_ax_insert_items(&pSwitch->rx_queue[port], 
						 &ts);
				fifo_ctrl = ax_tsn_read_reg(pSwitch,
					REG_PORTS_OFFSET(port, PORT_CAP_CTRL));
				ASIX_DEBUG("%s RX %d %x out\n", 
					__FUNCTION__, 
					port, 
					fifo_ctrl);
			};
			/* TX */
			while ((fifo_ctrl & PCAPC_TX_FIFO_STATUS) != 0) {
				ASIX_DEBUG("%s RX %d %x start\n", 
					__FUNCTION__, 
					port, 
					fifo_ctrl);
				_ax_get_cap_data(pSwitch,
						 REG_PORTS_OFFSET(port, 
							PORT_TX_TIMEA_LO),
						 &ts);
				_ax_insert_items(&pSwitch->tx_queue[port], 
						 &ts);
				fifo_ctrl = ax_tsn_read_reg(pSwitch,
					REG_PORTS_OFFSET(port, PORT_CAP_CTRL));
				tx_count++;
				ASIX_DEBUG("%s RX %d %x out\n", 
					__FUNCTION__, 
					port, 
					fifo_ctrl);
			};
		}
	}
	return tx_count;
} /* End of ax_retrieve_hw_timestamps() */


bool _ax_tsq_check_item_1(CAPTURE_DATA *queue_d, CAPTURE_DATA *tstamp_d)
{
	ASIX_DEBUG("msg_type: 0x%x 0x%x\n", 
				tstamp_d->msg_type, 
				queue_d->msg_type);
	ASIX_DEBUG("sequence_id: 0x%x 0x%x\n", 
				tstamp_d->sequence_id, 
				queue_d->sequence_id);
	ASIX_DEBUG("ptp_clock_id_h: 0x%x 0x%x\n", 
				tstamp_d->ptp_clock_id_h, 
				queue_d->ptp_clock_id_h);
	ASIX_DEBUG("ptp_clock_id_l: 0x%x 0x%x\n", 
				tstamp_d->ptp_clock_id_l, 
				queue_d->ptp_clock_id_l);
	ASIX_DEBUG("ptp_port_id: 0x%x 0x%x\n", 
				tstamp_d->ptp_port_id, 
				queue_d->ptp_port_id);
	ASIX_DEBUG("ptp_vlan_id: 0x%x 0x%x\n", 
				tstamp_d->ptp_vlan_id, 
				queue_d->ptp_vlan_id);
	if (queue_d->msg_type == tstamp_d->msg_type) {
		if ((queue_d->sequence_id == tstamp_d->sequence_id) &&
		    (queue_d->ptp_clock_id_h == tstamp_d->ptp_clock_id_h) &&
		    (queue_d->ptp_clock_id_l == tstamp_d->ptp_clock_id_l) &&
		    (queue_d->ptp_port_id == tstamp_d->ptp_port_id) &&
		    (queue_d->ptp_vlan_id == tstamp_d->ptp_vlan_id)) {
			return true;
		}
	}
	return false;
} /* End of _ax_tsq_check_item_1() */


int ax_tsq_find_item_1(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d)
{
	unsigned long flags;
	int i, read_ptr, items_in_queue, ret = AX_STATUS_SUCCESS;

	spin_lock_irqsave(&(queue->lock), flags);

	items_in_queue = queue->num_items;
	ASIX_DEBUG("%s queue->num_items %d\n", __FUNCTION__, queue->num_items);
	if (items_in_queue) {
		read_ptr = queue->write_ptr - 1;
		if (read_ptr < 0) {
			read_ptr = TIMESTAMP_QUEUE_SIZE - 1;
		}

		for (i = 0; i < TIMESTAMP_QUEUE_SIZE/*items_in_queue*/; i++) {
			if (_ax_tsq_check_item_1(&(queue->ts_items[read_ptr]),
						   tstamp_d)) {
				break;
			}
			read_ptr--;
			if (read_ptr < 0) {
				read_ptr = TIMESTAMP_QUEUE_SIZE - 1;
			}
		}

		if (i == TIMESTAMP_QUEUE_SIZE/*items_in_queue*/) {
			/* Item not found */
			ret = AX_STATUS_QITEM_NOT_FOUND;			
		} else {
			*tstamp_d = queue->ts_items[read_ptr];
			queue->num_items--;
		}
	} else {
		ret = AX_STATUS_Q_NO_ITEM;

	}
	spin_unlock_irqrestore(&(queue->lock), flags);

	return ret;
} /* End of ax_tsq_find_item_1() */

void ax_tsn_rx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp)
{
	int (*gettime)(struct ptp_clock_info *ptp, struct timespec64 *ts);
	struct ax_switch *pSwitch = pSkbptp->pSwitch;	
	struct sk_buff *skb = pSkbptp->skb;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
	unsigned char ptp_header[PTP_HDR_SIZE];
	struct timespec64 ts;
	int ret, i;
	CAPTURE_DATA ts_d;
	gettime = pSwitch->ptp_caps.gettime64;
	/* Read hw time stamp for a received PTP frame */
	skb_copy_from_linear_data_offset(skb, pSkbptp->ptp_msg_offset,
					 ptp_header, PTP_HDR_SIZE);
	gettime(&(pSwitch->ptp_caps), &ts);	
	ts_d.timestamp_h = ts.tv_sec;
	ts_d.timestamp_l = ts.tv_nsec;
	ts_d.sequence_id = ntohs(*(u16 *)(ptp_header + PTP_SEQ_ID_OFFSET));
	ts_d.msg_type = *(ptp_header + PTP_MSG_TYPE_OFFSET) & 0x0F;
	ts_d.ptp_clock_id_h = 
			ntohl(*(u32 *)(ptp_header + PTP_CLOCK_ID_OFFSET));
	ts_d.ptp_clock_id_l = 
			ntohl(*(u32 *)(ptp_header + PTP_CLOCK_ID_OFFSET + 4));
	ts_d.ptp_port_id = 
			ntohs(*(u16 *)(ptp_header + PTP_CLOCK_ID_OFFSET + 8));
	ts_d.ptp_vlan_id = pSkbptp->ptp_vlan_id;


	/* If there is only one TSN the index of that TSN is always 0 */
	if (pSwitch->number_of_ports == 1) {
		ret = ax_tsq_find_item_1(&(pSwitch->rx_queue[0]), &ts_d);
	} else {
		for (i = 0; i < pSwitch->number_of_ports; i++) {
			if (pSwitch->tsn_eth_tag[i] == pSkbptp->port_tag) {
				break;
			}
		}
		if (i < pSwitch->number_of_ports) {
			ret = ax_tsq_find_item_1(&(pSwitch->rx_queue[i]),
						 &ts_d);
		} else {
			ret = -1;
		}
	}
	if (ret >= 0) {
		u32 sec, nsec;
		u64 time64;

		sec = (u32)ts_d.timestamp_h;
		nsec = (u32)ts_d.timestamp_l;
		time64 = (sec * NS_PER_SEC) + nsec;
		memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamps->hwtstamp = ns_to_ktime(time64);
	} else {
		for (i = 0; i < pSwitch->number_of_ports; i++) {
			if (pSwitch->tsn_eth_tag[i] == pSkbptp->port_tag)
				break;
		}
		ax_retrieve_hw_timestamps(pSwitch);
		ret = ax_tsq_find_item_1(&(pSwitch->rx_queue[i]), &ts_d);
		if (ret >= 0) {
			u32 sec, nsec;
			u64 time64;

			sec = (u32)ts_d.timestamp_h;
			nsec = (u32)ts_d.timestamp_l;
			time64 = (sec * NS_PER_SEC) + nsec;
			memset(shhwtstamps, 
				0, 
				sizeof(struct skb_shared_hwtstamps));
			shhwtstamps->hwtstamp = ns_to_ktime(time64);
		} else {
			printk("### (%s) - skb_tstamp_rx None t%x###", 
				__func__, 
				ts_d.msg_type);
			printk("### (%s) - skb_tstamp_rx None i%x###", 
				__func__, 
				ts_d.sequence_id);
			ax_tsn_write_reg(pSwitch, 
					AS_T_VAL_O_HI, 
					ts_d.sequence_id & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_T_VAL_O_HI, 
					ts_d.msg_type & 
					0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_T_VAL_O_HI, 
					ts_d.ptp_clock_id_l & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_T_VAL_O_HI, 
					ts_d.ptp_clock_id_h & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_T_VAL_O_HI, 
					ts_d.ptp_port_id & 0xFFFFFFFF);
		}			
	}

} /* End of ax_tsn_rx_hwtstamp() */


void ax_tsn_tx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp)
{
	int (*gettime)(struct ptp_clock_info *ptp, struct timespec64 *ts);
	struct ax_switch *pSwitch = pSkbptp->pSwitch;	
	struct sk_buff *skb = pSkbptp->skb;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
	unsigned char ptp_header[PTP_HDR_SIZE];
	struct timespec64 ts;
	int ret, i;
	CAPTURE_DATA ts_d;
	gettime = pSwitch->ptp_caps.gettime64;	
	/* Read hw time stamp for a received PTP frame */
	skb_copy_from_linear_data_offset(skb, pSkbptp->ptp_msg_offset,
					 ptp_header, PTP_HDR_SIZE);
	gettime(&(pSwitch->ptp_caps), &ts);	
	ts_d.timestamp_h = ts.tv_sec;
	ts_d.timestamp_l = ts.tv_nsec;
	ts_d.sequence_id = ntohs(*(u16 *)(ptp_header + PTP_SEQ_ID_OFFSET));
	ts_d.msg_type = *(ptp_header + PTP_MSG_TYPE_OFFSET) & 0x0F;
	ts_d.ptp_clock_id_h = 
			ntohl(*(u32 *)(ptp_header + PTP_CLOCK_ID_OFFSET));
	ts_d.ptp_clock_id_l = 
			ntohl(*(u32 *)(ptp_header + PTP_CLOCK_ID_OFFSET + 4));
	ts_d.ptp_port_id = 
			ntohs(*(u16 *)(ptp_header + PTP_CLOCK_ID_OFFSET + 8));
	ts_d.ptp_vlan_id = pSkbptp->ptp_vlan_id;


	/* If there is only one TSN the index of that TSN is always 0 */
	if (pSwitch->number_of_ports == 1) {
		ax_retrieve_hw_timestamps(pSwitch);
		ret = ax_tsq_find_item_1(&(pSwitch->tx_queue[0]), &ts_d);
	} else {
		/* Find the TSN connected to the ethernet port which have
		 * received this message */
		for (i = 0; i < pSwitch->number_of_ports; i++) {
			if (pSwitch->tsn_eth_tag[i] == pSkbptp->port_tag) {
				break;
			}
		}
		if (i < pSwitch->number_of_ports) {
			ax_retrieve_hw_timestamps(pSwitch);
			ret = ax_tsq_find_item_1(&(pSwitch->tx_queue[i]),
						 &ts_d);
		} else {
			ret = -1;
		}
	}
	if (ret < 0) {
		/* Try again after 1000 usecs */
		udelay(1000);
		for (i = 0; i < pSwitch->number_of_ports; i++) {
			if (pSwitch->tsn_eth_tag[i] == pSkbptp->port_tag) {
				break;
			}
		}
		ax_retrieve_hw_timestamps(pSwitch);
		ret = ax_tsq_find_item_1(&(pSwitch->tx_queue[i]), &ts_d);
		if (ret == 0) {
			u32 sec, nsec;
			u64 time64;

			sec = (u32)ts_d.timestamp_h;
			nsec = (u32)ts_d.timestamp_l;
			time64 = (sec * NS_PER_SEC) + nsec;
			memset(shhwtstamps, 
				0, 
				sizeof(struct skb_shared_hwtstamps));
			shhwtstamps->hwtstamp = ns_to_ktime(time64);
			skb_tstamp_tx(skb, shhwtstamps);
		} else {
			ax_tsn_write_reg(pSwitch, 
					AS_MOD_VERN, 
					ts_d.sequence_id & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_MOD_VERN, 
					ts_d.msg_type & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_MOD_VERN, 
					ts_d.ptp_clock_id_l & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_MOD_VERN, 
					ts_d.ptp_clock_id_h & 0xFFFFFFFF);
			ax_tsn_write_reg(pSwitch, 
					AS_MOD_VERN, 
					ts_d.ptp_port_id & 0xFFFFFFFF);
		}
	} else {
		u32 sec, nsec;
		u64 time64;

		sec = (u32)ts_d.timestamp_h;
		nsec = (u32)ts_d.timestamp_l;
		time64 = (sec * NS_PER_SEC) + nsec;
		memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
		shhwtstamps->hwtstamp = ns_to_ktime(time64);
		skb_tstamp_tx(skb, shhwtstamps);
	} 
} /* End of ax_tsn_tx_hwtstamp() */
