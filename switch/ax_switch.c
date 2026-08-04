/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic
 *     Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
 *****************************************************************************/

/* INCLUDE FILE DECLARATIONS */
#include "ax_switch.h"
#include "../libxdma.h"
#include "linux/time.h"


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


uint64_t config_change_time_s, config_change_time_ns;
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


void _tsn_tsq_insert_item(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d)
{
	unsigned long flags;
	spin_lock_irqsave(&(queue->lock), flags);

	queue->ts_items[queue->write_ptr] = *tstamp_d;

	queue->write_ptr++;
	if (queue->write_ptr == queue->max_num_items) {
		queue->write_ptr = 0;
	}
	if (queue->num_items < queue->max_num_items) {
		queue->num_items++;
	}
	spin_unlock_irqrestore(&(queue->lock), flags);
}


static int
_tsn_tsq_init(TS_QUEUE *queue, int number_of_ports, int number_of_domains)
{
	queue->max_num_items = (number_of_ports * number_of_domains) + 8;
	queue->ts_items = 
		(CAPTURE_DATA *)kmalloc
		(queue->max_num_items * sizeof(CAPTURE_DATA), GFP_KERNEL);
	if (!queue->ts_items) {
		pr_err("Could not allocate memory for queue->ts_itmes");
		return -1;
	}
	spin_lock_init(&(queue->lock));
	_tsn_tsq_flush(queue);
	return 0;
} /* End of _tsn_tsq_init() */

void _tsn_tsq_remove(TS_QUEUE *queue)
{
	kfree(queue->ts_items);
	queue->max_num_items = 0;
}

int ax_tsn_init_switch(struct ax_switch *pSwitch, spinlock_t *pLock)
{
	if (pSwitch != NULL) {
		pSwitch->number_of_ports = switch_config.number_of_ports;
		pSwitch->number_of_domains = NUM_OF_DOMAIN;

		if ( _tsn_tsq_init(&pSwitch->rx_queue
				, pSwitch->number_of_ports
				, pSwitch->number_of_domains) < 0) {
			_tsn_tsq_remove(&pSwitch->rx_queue);
			return -1;
		}
		if ( _tsn_tsq_init(&pSwitch->tx_queue
				, pSwitch->number_of_ports
				, pSwitch->number_of_domains) < 0) {
			_tsn_tsq_remove(&pSwitch->tx_queue);
			return -1;
		}
		pSwitch->ptxrx_timestamp_lock = pLock;
		return 0;
	}
	return -1;
} /* End of ax_tsn_init_switch() */
// End of Initialize APIs

//
// Interrupt APIs
//
#if 0
static uint32_t _get_port_id (uint32_t config_change_reg)
{
	if (config_change_reg > 0 && config_change_reg <= 0x8000)
		return (__ilog2_u32(config_change_reg));
	else
		return -1;
}
#endif

uint64_t get_cycle_start_time(uint64_t current_time, uint64_t oper_base_time, uint32_t oper_cycle_time)
{
        uint64_t cycle_start_time, actualstep, nextstep;
        if (oper_base_time >= current_time)
        {
                cycle_start_time = oper_base_time;
        }
        else
        {
                actualstep = div_u64((current_time - oper_base_time), oper_cycle_time);
                nextstep = actualstep + 2;
                cycle_start_time = oper_base_time + (nextstep * oper_cycle_time);
        }
        return cycle_start_time;
}

void ax_irq_timer_value_change(struct ax_switch *pSwitch)
{

	volatile uint32_t *p_tmr_interrupt_mask 	= pSwitch->pMemBase + AS_T_INT_MASK;
	volatile uint32_t *p_tmr_selector 		= pSwitch->pMemBase + AS_T_SELECTOR;
	volatile uint32_t *p_timer_L 			= pSwitch->pMemBase + AS_T_VAL_O_LO;
	volatile uint32_t *p_timer_H 			= pSwitch->pMemBase + AS_T_VAL_O_HI;
	volatile uint32_t *p_port_id 			= pSwitch->pMemBase + QBV_PORT_SELECTOR;
	volatile uint32_t *p_gate_enabled 		= pSwitch->pMemBase + QBV_GATE_ENABLED;
	volatile uint32_t *p_oper_cycle_time 		= pSwitch->pMemBase + QBV_OPER_CYCLE_TIME;
	volatile uint32_t *p_oper_base_time_ns_hi 	= pSwitch->pMemBase + QBV_OPER_BASE_TIME_NS_HI;
	volatile uint32_t *p_oper_base_time_ns_lo 	= pSwitch->pMemBase + QBV_OPER_BASE_TIME_NS_LO;	
	volatile uint32_t *p_cycle_start_time_ctrl  	= pSwitch->pMemBase + QBV_CYCLE_START_TIME_CTRL;
	volatile uint32_t *p_cycle_start_time_ns_hi 	= pSwitch->pMemBase + QBV_CYCLE_START_TIME_NS_HI;
	volatile uint32_t *p_cycle_start_time_ns_lo 	= pSwitch->pMemBase + QBV_CYCLE_START_TIME_NS_LO;
	volatile uint32_t *p_timer_domain_index 	= pSwitch->pMemBase + QBV_TIMER_DOMAIN_INDEX;
	volatile uint32_t *p_config_change_ctrl 	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_CTRL; 
	volatile uint32_t *p_config_change_state 	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_STATE;
	volatile uint32_t *p_config_change_time_ns_hi 	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_TIME_NS_HI;
	volatile uint32_t *p_config_change_time_ns_lo 	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_TIME_NS_LO;

	uint64_t current_time, oper_base_time, cycle_start_time, config_change_time;
	uint32_t oper_cycle_time, timer_changed_index, aux;
	int i;

	/* Max number of timer equals number of domains + 1 */
	timer_changed_index = get_index_by_mask(*p_tmr_interrupt_mask, pSwitch->number_of_domains + 1);

	for (i = 0; i < pSwitch->number_of_ports; i++)	
	{
		*p_port_id = (uint32_t)i;
		/* Check if Qbv is enabled */
		if (*p_gate_enabled == 1)
		{
			/* Check if the timer changed is in use by this port */
			if (timer_changed_index == (*p_timer_domain_index + 1))
			{
				/** 
				* Prior to sending the new cycle_start_time the sw needs
				* to put the Qbv module in a safe_state where administrative_gate_states
				* will be used as the operative_gate_states. This is done to avoid undesirable
				* behaviours in the gate_control_list.
				*/
				*p_cycle_start_time_ctrl = 2;

				/* Calculate next cycle_start_time using oper_base_time and oper_cycle_time and current_time */
				oper_cycle_time = *p_oper_cycle_time;
				oper_base_time = ((uint64_t)*p_oper_base_time_ns_hi << 32) + *p_oper_base_time_ns_lo;				

				/* Obtain current time from the timer in use by the port 
				 * Qbv timer selector index 0 is for domain 0 (FRT cannot be selected for Qbv)
				 * 802.1AS timer selector for index 0 is for FRT, for domain 0 is index 1
				 * */
				*p_tmr_selector = *p_timer_domain_index + 1;
				current_time = *p_timer_H;
				current_time = (current_time * 1000000000) + *p_timer_L;
				/* Chek if there is a configuration pending */
				if(*p_config_change_state == 1)
				{
					config_change_time = ((uint64_t)*p_config_change_time_ns_hi << 32) + *p_config_change_time_ns_lo;

					/* Check if config change time is happening too soon (before (6+n)*oper_cycle_time)
					 * Being n the number of future slots to check for gate
		 			 * close events. This value is configured by generic and the
		 			 * maximum value is 4 and we cover the worst case. In this case
					 * do send config_change_again and skip the cycle_start_time
					 */
					if(config_change_time <= (current_time + 10*oper_cycle_time))
					{
						*p_config_change_ctrl = 1;
					}
					else
					{
						/* Calculate next cycle_start_time using oper_base_time and oper_cycle_time and current_time */
						cycle_start_time = get_cycle_start_time(current_time, oper_base_time, oper_cycle_time);

						/* Apply cycle_start_time commanded by sw */
						aux = (uint32_t)((cycle_start_time & 0xFFFFFFFF00000000) >> 32);
						*p_cycle_start_time_ns_hi = aux;
						aux = (uint32_t)(cycle_start_time & 0x00000000FFFFFFFF);
						*p_cycle_start_time_ns_lo = aux;
						*p_cycle_start_time_ctrl = 1;

						/* Command also a configuration change */
						*p_config_change_ctrl = 1;
					}
				}
				else
				{
					/* Calculate next cycle_start_time using oper_base_time and oper_cycle_time and current_time */
					cycle_start_time = get_cycle_start_time(current_time, oper_base_time, oper_cycle_time);

					/* Apply cycle_start_time commanded by sw */
					aux = (uint32_t)((cycle_start_time & 0xFFFFFFFF00000000) >> 32);
					*p_cycle_start_time_ns_hi = aux;
					aux = (uint32_t)(cycle_start_time & 0x00000000FFFFFFFF);
					*p_cycle_start_time_ns_lo = aux;
					*p_cycle_start_time_ctrl = 1;
				}
			}
		}
	}
	/* Clear interrupt mask */
	*p_tmr_interrupt_mask = 0;
}

void ax_irq_config_change(struct ax_switch *pSwitch)
{
	volatile uint32_t *p_tmr_selector 		= pSwitch->pMemBase + AS_T_SELECTOR;
	volatile uint32_t *p_timer_L			= pSwitch->pMemBase + AS_T_VAL_O_LO;
	volatile uint32_t *p_timer_H			= pSwitch->pMemBase + AS_T_VAL_O_HI;
	volatile uint32_t *p_port_id			= pSwitch->pMemBase + QBV_PORT_SELECTOR;
	volatile uint32_t *p_admin_cycle_time		= pSwitch->pMemBase + QBV_ADMIN_CYCLE_TIME;
	volatile uint32_t *p_admin_base_time_ns_hi	= pSwitch->pMemBase + QBV_ADMIN_BASE_TIME_NS_HI;
	volatile uint32_t *p_admin_base_time_ns_lo	= pSwitch->pMemBase + QBV_ADMIN_BASE_TIME_NS_LO;
	
	volatile uint32_t *p_config_change_req		= pSwitch->pMemBase + QBV_CONFIG_CHANGE_REQ;
	volatile uint32_t *p_config_change_ctrl		= pSwitch->pMemBase + QBV_CONFIG_CHANGE_CTRL;
	volatile uint32_t *p_config_change_time_ns_hi	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_TIME_NS_HI;
	volatile uint32_t *p_config_change_time_ns_lo	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_TIME_NS_LO;
	
	volatile uint32_t *p_timer_domain_index		= pSwitch->pMemBase + QBV_TIMER_DOMAIN_INDEX;

	uint64_t current_time, admin_base_time, config_change_time, actualstep, nextstep;
	uint32_t admin_cycle_time, aux;

	int port_id;
	/* Get port_id selected for configuration_change */
	port_id = get_index_by_mask(*p_config_change_req, pSwitch->number_of_ports);
	if (port_id != -1)
	{
		/* Select port */
		*p_port_id = (uint32_t)port_id;
		
		/* Calculate config_change_time using admin_cycle_time, admin_base_time and current_time */
		admin_cycle_time = *p_admin_cycle_time;
		admin_base_time = ((uint64_t)*p_admin_base_time_ns_hi << 32) + *p_admin_base_time_ns_lo;
		
		/* Obtain current time from the timer in use by the port 
		 * Qbv timer selector index 0 is for domain 0 (FRT cannot be selected for Qbv)
		 * 802.1AS timer selector for index 0 is for FRT, for domain 0 is index 1
		 * */
		*p_tmr_selector = *p_timer_domain_index + 1;
		current_time = *p_timer_H;
		current_time = (current_time * 1000000000) + *p_timer_L;

		/* Config Change Time must be set al least 6+n cycles ahead
		 * Being n the number of future slots to check for gate
		 * close events. This value is configured by generic and the
		 * maximum value is 4 and we cover the worst case
		 */
		if (admin_base_time >= (current_time + 10*admin_cycle_time))
		{
			config_change_time = admin_base_time;
		}
		else
		{
			actualstep = div_u64((current_time - admin_base_time), admin_cycle_time);
			nextstep = actualstep + 10;
			config_change_time = admin_base_time + (nextstep * admin_cycle_time);
		}

		/* Send config_change_time and config_change_valid */
		aux = (uint32_t)((config_change_time & 0xFFFFFFFF00000000) >> 32);
		*p_config_change_time_ns_hi = aux;
		aux = (uint32_t)(config_change_time & 0x00000000FFFFFFFF);
		*p_config_change_time_ns_lo = aux;
		*p_config_change_ctrl = 1;
	}
	else
	{
		printk(KERN_ERR "%s: could not find port_id for configuration.\n",__func__);
	}

	/* Clear config_change_req */
	*p_config_change_req = 0;
} /* End of ax_irq_config_change() */


/**
 * When the 802.1AS timer is changed for a port a new cycle start time 
 * needs to be calculated using the new timer. 
 */
void ax_irq_timer_change(struct ax_switch *pSwitch)
{
	volatile uint32_t *p_tmr_selector		= pSwitch->pMemBase + AS_T_SELECTOR;
	volatile uint32_t *p_timer_L			= pSwitch->pMemBase + AS_T_VAL_O_LO;
	volatile uint32_t *p_timer_H			= pSwitch->pMemBase + AS_T_VAL_O_HI;
	volatile uint32_t *p_port_id			= pSwitch->pMemBase + QBV_PORT_SELECTOR;
	volatile uint32_t *p_gate_enabled 		= pSwitch->pMemBase + QBV_GATE_ENABLED;
	volatile uint32_t *p_oper_cycle_time		= pSwitch->pMemBase + QBV_OPER_CYCLE_TIME;
	volatile uint32_t *p_oper_base_time_ns_hi	= pSwitch->pMemBase + QBV_ADMIN_BASE_TIME_NS_HI;
	volatile uint32_t *p_oper_base_time_ns_lo	= pSwitch->pMemBase + QBV_ADMIN_BASE_TIME_NS_LO;
	volatile uint32_t *p_cycle_start_time_ctrl	= pSwitch->pMemBase + QBV_CYCLE_START_TIME_CTRL;
	volatile uint32_t *p_cycle_start_time_ns_hi 	= pSwitch->pMemBase + QBV_CYCLE_START_TIME_NS_HI;
	volatile uint32_t *p_cycle_start_time_ns_lo 	= pSwitch->pMemBase + QBV_CYCLE_START_TIME_NS_LO;
	volatile uint32_t *p_timer_domain_index		= pSwitch->pMemBase + QBV_TIMER_DOMAIN_INDEX;
	volatile uint32_t *p_timer_change_request	= pSwitch->pMemBase + QBV_TIMER_CHANGE_REQUEST;
	volatile uint32_t *p_config_change_ctrl		= pSwitch->pMemBase + QBV_CONFIG_CHANGE_CTRL;
	volatile uint32_t *p_config_change_state	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_STATE;
	volatile uint32_t *p_config_change_time_ns_hi	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_TIME_NS_HI;
	volatile uint32_t *p_config_change_time_ns_lo	= pSwitch->pMemBase + QBV_CONFIG_CHANGE_TIME_NS_LO;


	uint64_t current_time, oper_base_time, cycle_start_time, config_change_time;	
	uint32_t port_id, oper_cycle_time, aux;

	/* Get port_id selected for timer_change */
	port_id = get_index_by_mask(*p_timer_change_request, pSwitch->number_of_ports);
	if (port_id != -1)
	{
		/* Select port */
		*p_port_id = (uint32_t)port_id;

		/* Check if Qbv is enabled */
		if (*p_gate_enabled == 1)
		{
			/**
			* Prior to sending the new cycle_start_time the sw needs
			* to put the Qbv module in a safe_state where administrative_gate_states
			* will be used as the operative_gate_states. This is done to avoid undesirable
			* behaviours in the gate_control_list.
			*/
			*p_cycle_start_time_ctrl = 2;

			/* Calculate next cycle_start_time using oper_base_time and oper_cycle_time and current_time */
			oper_cycle_time = *p_oper_cycle_time;
			oper_base_time = ((uint64_t)*p_oper_base_time_ns_hi << 32) + *p_oper_base_time_ns_lo;
			

			/* Obtain current time from the timer in use by the port 
			 * Qbv timer selector index 0 is for domain 0 (FRT cannot be selected for Qbv)
			 * 802.1AS timer selector for index 0 is for FRT, for domain 0 is index 1
			 * */
			*p_tmr_selector = *p_timer_domain_index + 1;
			current_time = *p_timer_H;
			current_time = (current_time * 1000000000) + *p_timer_L;

			/* Chek if there is a configuration pending */
			if(*p_config_change_state == 1)
			{
				config_change_time = ((uint64_t)*p_config_change_time_ns_hi << 32) + *p_config_change_time_ns_lo;

				/* Check if config change time is happening too soon (before (6+n)*oper_cycle_time)
				* Being n the number of future slots to check for gate
				* close events. This value is configured by generic and the
				* maximum value is 4 and we cover the worst case. In this case
				* do send config_change_again and skip the cycle_start_time
				*/
				if(config_change_time <= (current_time + 10*oper_cycle_time))
				{
					*p_config_change_ctrl = 1;
				}
				else
				{
					/* Calculate next cycle_start_time using oper_base_time and oper_cycle_time and current_time */
					cycle_start_time = get_cycle_start_time(current_time, oper_base_time, oper_cycle_time);

					/* Apply cycle_start_time commanded by sw */
					aux = (uint32_t)((cycle_start_time & 0xFFFFFFFF00000000) >> 32);
					*p_cycle_start_time_ns_hi = aux;
					aux = (uint32_t)(cycle_start_time & 0x00000000FFFFFFFF);
					*p_cycle_start_time_ns_lo = aux;
					*p_cycle_start_time_ctrl = 1;

					/* Command also a configuration change */
					*p_config_change_ctrl = 1;
				}
			}
			else
			{
				/* Calculate next cycle_start_time using oper_base_time and oper_cycle_time and current_time */
				cycle_start_time = get_cycle_start_time(current_time, oper_base_time, oper_cycle_time);

				/* Apply cycle_start_time commanded by sw */
				aux = (uint32_t)((cycle_start_time & 0xFFFFFFFF00000000) >> 32);
				*p_cycle_start_time_ns_hi = aux;
				aux = (uint32_t)(cycle_start_time & 0x00000000FFFFFFFF);
				*p_cycle_start_time_ns_lo = aux;
				*p_cycle_start_time_ctrl = 1;
			}
		}
	}
	else
	{
		printk(KERN_ERR "%s: could not find port_id for configuration.\n",__func__ );
	}

	/* Clear timer_change_request */
	*p_timer_change_request = 0;
} /* End of ax_irq_timer_change() */

//
// For PTP APIs
//
void _ax_time_to_ull(uint64_t *time64, uint32_t seconds, uint32_t nanoseconds)
{
	*time64 = seconds;
	*time64 = NS_PER_SEC;
	*time64 += nanoseconds;
} /* End of _ax_time_to_ull() */


int get_index_by_mask(uint32_t mask, uint32_t max_index) 
{
	int i;
	for (i = 0; i < max_index; i++){
		if (mask & (1 << i))
			return i;
	}
	return -1;
}

#if 0
void _ax_get_cap_data(struct ax_switch *pSwitch, int offset, PCAPTURE_DATA ts)
{
	int port, off;
	uint32_t reg_val[NUM_OF_CAP_REGS];

	for (port = 0, off = offset; port < NUM_OF_CAP_REGS; port++, off += 4) {
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
#endif


void _ax_insert_items(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d)
{
	unsigned long flags;

	spin_lock_irqsave(&(queue->lock), flags);
	
	queue->ts_items[queue->write_ptr] = *tstamp_d;
	
	queue->write_ptr++;
	if (queue->write_ptr == queue->max_num_items) {
                queue->write_ptr = 0;
        }
        if (queue->num_items < queue->max_num_items) {
                queue->num_items++;
        }

	spin_unlock_irqrestore(&(queue->lock), flags);


} /* End of _ax_insert_items() */

void ax_retrieve_rx_hw_timestamps(struct ax_switch *pSwitch)
{
	CAPTURE_DATA ts;
	int i;
	volatile uint32_t *p_timestamping_ctrl = pSwitch->pMemBase + PTP_TIMESTAMP_CTRL;
	volatile uint32_t *p_rx_port_id = pSwitch->pMemBase + PTP_RX_PORT;
	volatile uint32_t *p_rx_frt_timestamp_lo = pSwitch->pMemBase +PTP_FR_RX_TIMESTAMP_LO;
	volatile uint32_t *p_rx_frt_timestamp_hi = pSwitch->pMemBase +PTP_FR_RX_TIMESTAMP_HI;
	volatile uint32_t *p_rx_message_info = pSwitch->pMemBase + PTP_RX_MESSAGE_INFO;
	uint32_t msg_info;
#ifdef ASIX_GPTP_DEBUG
	uint64_t nanoSec = 0;
	uint32_t tmp_msg_info;
#endif
	/* Get RX timestamps until RX FIFO is empty */

	while ((*p_timestamping_ctrl & 0x1) != 0) {
		ts.port_id = (uint8_t)*p_rx_port_id;
		for (i = 0; i < (pSwitch->number_of_domains + 1); i++){
			ts.timestamp_l[i] = *(p_rx_frt_timestamp_lo + (i * 2));
                        ts.timestamp_h[i] = *(p_rx_frt_timestamp_hi + (i * 2));
#ifdef ASIX_GPTP_DEBUG
			printk("%s, Rx, port_id:%d, Address_ts_lo:%x, Address_ts_Hi:%x, ts_lo:0x%x, ts_Hi:0x%x",\
                                        __func__,ts.port_id,\
                                        &(*(p_rx_frt_timestamp_lo+(i*2))),\
                                        &(*(p_rx_frt_timestamp_hi+(i*2))),\
                                        ts.timestamp_l[i],ts.timestamp_h[i]);
#endif
#if 0
			printk("[%s:%d] Domain:%d, Addr_ts_lo:%x:, Addr_ts_hi:%x, timeStamp:%llu",
					__func__,__LINE__,i,
					(&(*(p_rx_frt_timestamp_lo+(i*2)))) & 0xff, 
					(&(*(p_rx_frt_timestamp_hi+(i*2)))) & 0xff,
					nanoSec);
#endif
                }
		
		/* MsgInfo must be read only once and needs to be the last 
		   register to read */
		msg_info = *p_rx_message_info;
#ifdef ASIX_GPTP_DEBUG
		tmp_msg_info = msg_info;
#endif
		ts.msg_type = (uint8_t)(msg_info & 0xF);
		ts.sequence_id = (uint16_t)((msg_info >> 12)& 0xFFFF);
		ts.domain_number = (uint8_t)((msg_info >> 4) & 0xFF);
#ifdef ASIX_GPTP_DEBUG
		printk("%s, Rx, msg_info:%x",__func__,tmp_msg_info);
#endif

		_tsn_tsq_insert_item(&pSwitch->rx_queue, &ts);
	}
} /* End of ax_retrieve_rx_hw_timestamps() */


int ax_retrieve_hw_timestamps(struct ax_switch *pSwitch) 
{
	volatile uint32_t *p_timestamping_ctrl 		= pSwitch->pMemBase + PTP_TIMESTAMP_CTRL;
	volatile uint32_t *p_rx_port_id			= pSwitch->pMemBase + PTP_RX_PORT;
	volatile uint32_t *p_rx_frt_timestamp_lo	= pSwitch->pMemBase + PTP_FR_RX_TIMESTAMP_LO;
	volatile uint32_t *p_rx_frt_timestamp_hi	= pSwitch->pMemBase + PTP_FR_RX_TIMESTAMP_HI;
	volatile uint32_t *p_rx_message_info		= pSwitch->pMemBase + PTP_RX_MESSAGE_INFO;
	volatile uint32_t *p_tx_port_id 		= pSwitch->pMemBase + PTP_TX_PORT;
	volatile uint32_t *p_tx_frt_timestamp_lo	= pSwitch->pMemBase + PTP_FR_TX_TIMESTAMP_LO;
	volatile uint32_t *p_tx_frt_timestamp_hi	= pSwitch->pMemBase + PTP_FR_TX_TIMESTAMP_HI;
	volatile uint32_t *p_tx_message_info		= pSwitch->pMemBase + PTP_TX_MESSAGE_INFO;
	CAPTURE_DATA ts;
	uint32_t msg_info;
	int i,tx_count = 0;
#ifdef ASIX_GPTP_DEBUG
        uint64_t nanoSec = 0;
        uint32_t tmp_msg_info;
#endif
	/* Get RX timestamps until RX FIFO is empty */
	while ((*p_timestamping_ctrl & 0x01) != 0) {
		ts.port_id = (uint8_t)*p_rx_port_id;
		for (i = 0; i < (pSwitch->number_of_domains + 1); i++){
			ts.timestamp_l[i] = *(p_rx_frt_timestamp_lo + (i * 2));
			ts.timestamp_h[i] = *(p_rx_frt_timestamp_hi + (i * 2));
#ifdef ASIX_GPTP_DEBUG
                        //nanoSec = ((uint64_t)ts.timestamp_h[i])*1000000000+ts.timestamp_l[i];
                        printk("%s, Rx, port_id:%d, Address_ts_lo:%x, Address_ts_Hi:%x, ts_lo:0x%x, ts_Hi:0x%x",\
					__func__,ts.port_id,\
					&(*(p_rx_frt_timestamp_lo+(i*2))),\
					&(*(p_rx_frt_timestamp_hi+(i*2))),\
					ts.timestamp_l[i],ts.timestamp_h[i]);
#endif
		}
		/* MsgInfo must be read only once and needs to be the last register to read */
		msg_info = *p_rx_message_info;
#ifdef ASIX_GPTP_DEBUG		
		tmp_msg_info = msg_info;
#endif
		ts.msg_type = (uint8_t)(msg_info & 0xF);
		ts.sequence_id = (uint16_t)((msg_info >> 12) & 0xFFFF);
		ts.domain_number = (uint8_t)((msg_info >> 4) & 0xFF);
#ifdef ASIX_GPTP_DEBUG
		printk("%s, Rx, msg_info:%x",__func__,tmp_msg_info);
#endif
		_ax_insert_items(&pSwitch->rx_queue, &ts);
	}

	/* Get TX timestamps until TX FIFO is empty */
	while ((*p_timestamping_ctrl & 0x02) != 0){
		ts.port_id = (uint8_t)*p_tx_port_id;
		for (i = 0; i < (pSwitch->number_of_domains + 1); i++){
			ts.timestamp_l[i] = *(p_tx_frt_timestamp_lo + (i * 2));
			ts.timestamp_h[i] = *(p_tx_frt_timestamp_hi + (i * 2));
#ifdef ASIX_GPTP_DEBUG
                        //nanoSec = ((uint64_t)ts.timestamp_h[i])*1000000000+ts.timestamp_l[i];
			printk("%s, Tx, port_id:%d, Address_ts_lo:%x, Address_ts_Hi:%x, ts_lo:0x%x, ts_Hi:0x%x",\
                                        __func__,ts.port_id,\
                                        &(*(p_tx_frt_timestamp_lo+(i*2))),\
                                        &(*(p_tx_frt_timestamp_hi+(i*2))),\
                                        ts.timestamp_l[i],ts.timestamp_h[i]);
#endif

		}
		/* MsgInfo must be read only once and needs to be the last register to read */
		msg_info = *p_tx_message_info;
#ifdef ASIX_GPTP_DEBUG
		tmp_msg_info = msg_info;
#endif
		ts.msg_type = (uint8_t)(msg_info & 0xF);
		ts.sequence_id = (uint16_t)((msg_info >> 12) & 0xFFFF);
		ts.domain_number = (uint8_t)((msg_info >> 4) & 0xFF);
#ifdef ASIX_GPTP_DEBUG
		printk("%s, Tx, msg_info:%x",__func__,tmp_msg_info);
#endif
		_ax_insert_items(&pSwitch->tx_queue, &ts);
		tx_count++;
	}
	return tx_count;
} /* End of ax_retrieve_hw_timestamps() */


bool _ax_tsq_check_item_1(CAPTURE_DATA *queue_d, CAPTURE_DATA *tstamp_d)
{
#ifdef ASIX_GPTP_DEBUG
	printk("port_id: c_0x%x, q_0x%x\n",
                                tstamp_d->port_id,
                                queue_d->port_id);
        printk("msg_type: c_0x%x, q_0x%x\n",
                                tstamp_d->msg_type,
                                queue_d->msg_type);
        printk("sequence_id: c_0x%x, q_0x%x\n",
                                tstamp_d->sequence_id,
                                queue_d->sequence_id);
        printk("domain_number: c_0x%x, q_0x%x\n",
                                tstamp_d->domain_number,
                                queue_d->domain_number);
	printk("\n\n");
#else
	ASIX_DEBUG("port_id: 0x%x, q_0x%x\n",
				tstamp_d->port_id,
				queue_d->port_id);
	ASIX_DEBUG("msg_type: 0x%x, q_0x%x\n",
				tstamp_d->msg_type,
				queue_d->msg_type);
	ASIX_DEBUG("sequence_id: 0x%x, q_0x%x\n",
				tstamp_d->sequence_id,
				queue_d->sequence_id);
	ASIX_DEBUG("domain_number: 0x%x, q_0x%x\n",
				tstamp_d->domain_number,
				queue_d->domain_number);
#endif
	
	if((queue_d->port_id == tstamp_d->port_id) &&
	   (queue_d->msg_type == tstamp_d->msg_type) &&
	   (queue_d->sequence_id == tstamp_d->sequence_id) &&
	   (queue_d->domain_number == tstamp_d->domain_number)){
		return true;
	}

	return false;
} /* End of _ax_tsq_check_item_1() */


int ax_tsq_find_item_1(TS_QUEUE *queue, CAPTURE_DATA *tstamp_d)
{
	unsigned long flags;
	int i, read_ptr, items_in_queue, ret = AX_STATUS_SUCCESS;

	spin_lock_irqsave(&(queue->lock), flags);

	items_in_queue = queue->num_items;

	if (items_in_queue){
		read_ptr = queue->write_ptr - 1;
		if (read_ptr < 0){
			read_ptr = queue->max_num_items - 1;
		}

		for (i = 0; i < items_in_queue; i++){ /* Search the queue */
			if (_ax_tsq_check_item_1(&(queue->ts_items[read_ptr]),
						   tstamp_d)) {
				break;
			}
			read_ptr--;
			if (read_ptr < 0)
				read_ptr = queue->max_num_items - 1;
		}
		if (i == items_in_queue){
			/* Item not found */
			ret= AX_STATUS_QITEM_NOT_FOUND;
		}
		else{
			*tstamp_d = queue->ts_items[read_ptr];
		}
	}
	else {
		ret = AX_STATUS_Q_NO_ITEM;
	}
	spin_unlock_irqrestore(&(queue->lock), flags);
	return ret;
} /* End of ax_tsq_find_item_1() */

void ax_tsn_rx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp)
{
	struct ax_switch *pSwitch = pSkbptp->pSwitch;
	struct sk_buff *skb = pSkbptp->skb;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
	unsigned char ptp_header[PTP_HDR_SIZE];
	int ptp_msg_offset = pSkbptp->ptp_msg_offset;
	u32 sec, nsec;
	u64 time64;
#ifdef ASIX_GPTP_DEBUG
	u64 sec_u64, nsec_u64, time64_u64;
#endif
	CAPTURE_DATA ts_d;
	skb_copy_from_linear_data_offset(skb, ptp_msg_offset,
					 ptp_header, PTP_HDR_SIZE);
	ts_d.msg_type = *(ptp_header + PTP_MSG_TYPE_OFFSET) & 0x0F;
	ts_d.sequence_id = ntohs(*(u16 *)(ptp_header + PTP_SEQ_ID_OFFSET));
	ts_d.domain_number = *(ptp_header + PTP_SUBDOMAIN_OFFSET) & 0xFF;
	ts_d.port_id = pSkbptp->port_tag;
	if (ax_tsq_find_item_1(&pSwitch->rx_queue, &ts_d) < 0) {
		pr_err("%s: could not retrieve timestamp from queue. \
			MsgType = %u, SeqID = %u Domain = %u",__func__,
			ts_d.msg_type,
			ts_d.sequence_id,
			ts_d.domain_number);
		return;
	}

	if(pSwitch->ptp_mode == 0){
		sec = (u32)ts_d.timestamp_h[0];
		nsec = (u32)ts_d.timestamp_l[0];
	}
	else if(pSwitch->ptp_mode == 1)	{
		sec = (u32)ts_d.timestamp_h[1];
		nsec = (u32)ts_d.timestamp_l[1];
	}
	else{
		pr_err("%s: PTP working mode %d not valid\n",__FUNCTION__,pSwitch->ptp_mode);
		return;
	}
#ifdef ASIX_GPTP_DEBUG
	sec_u64 = sec;
	nsec_u64 = nsec;
	time64_u64 = sec_u64 * NS_PER_SEC + nsec_u64;
	printk("%s,Rx, sec_u64:%lld, nsec_u64:%lld, time64_u64:%lld, ptp_mode:%d",__func__,sec_u64,nsec_u64,time64_u64,pSwitch->ptp_mode);
	time64 = time64_u64;
#else
	time64 = (sec * NS_PER_SEC) + nsec;
#endif
#ifdef ASIX_GPTP_DEBUG
        printk("%s, ==Rx, port_id:%d, msg_type: %d, seque_id:%d, ts_lo:0x%x, ts_Hi:0x%x, time: %lld",\
			__func__, \
			ts_d.port_id,\
			ts_d.msg_type,\
			ts_d.sequence_id,\
			nsec_u64,\
		       	sec_u64,\
		       	time64);
        printk("---------------------------------------------------------------");
#endif

	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = ns_to_ktime(time64);
	return;
} /* End of ax_tsn_rx_hwtstamp() */


void ax_tsn_tx_hwtstamp(PSKB_TSTAMP_MSG pSkbptp)
{
	unsigned long flags;
	struct ax_switch *pSwitch = pSkbptp->pSwitch;
	struct sk_buff *skb = pSkbptp->skb;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
	unsigned char ptp_header[PTP_HDR_SIZE];
	int ptp_msg_offset = pSkbptp->ptp_msg_offset;
	CAPTURE_DATA ts_d;
	u32 sec, nsec;
	u64 time64;
#ifdef ASIX_GPTP_DEBUG
        u64 sec_u64, nsec_u64, time64_u64;
#endif
	int i;
	/* Read hardware time stamp for a transmited PTP frame */
	skb_copy_from_linear_data_offset(skb, ptp_msg_offset, ptp_header, PTP_HDR_SIZE);
	ts_d.msg_type = *(ptp_header + PTP_MSG_TYPE_OFFSET) & 0x0F;
	ts_d.sequence_id = ntohs(*(u16 *)(ptp_header + PTP_SEQ_ID_OFFSET));
	ts_d.domain_number = *(ptp_header + PTP_SUBDOMAIN_OFFSET) & 0xFF;
	ts_d.port_id = pSkbptp->port_tag;
	for (i = 0; i < TX_TIMESTAMP_RETRIES; i++)
	{
		spin_lock_irqsave(pSwitch->ptxrx_timestamp_lock,flags);
		ax_retrieve_hw_timestamps(pSwitch);
		spin_unlock_irqrestore(pSwitch->ptxrx_timestamp_lock,flags);
		if (ax_tsq_find_item_1(&pSwitch->tx_queue, &ts_d) == 0) 
		{
			/* Timestamp found */
			if(pSwitch->ptp_mode == 0){  /* If ptp working mode is 802.1AS, return frt timestamp on the skb anxiliary data */
				/* FRT timestamp at index 0 */
				sec = (u32)ts_d.timestamp_h[0];
				nsec = (u32)ts_d.timestamp_l[0];
			
			}
			else if(pSwitch->ptp_mode == 1){ /* If ptp working mode is 1588, return domain timer timestamp on the skb anxiliary data */
				/* First domain timer at index 1 */
				sec = (u32)ts_d.timestamp_h[1];
				nsec = (u32)ts_d.timestamp_l[1];
			}
			else{
				pr_err("%s: PTP working mode %d not valid\n",__FUNCTION__,pSwitch->ptp_mode);
				return;
			}

#ifdef ASIX_GPTP_DEBUG
        		sec_u64 = sec;
		        nsec_u64 = nsec;
		        time64_u64 = sec_u64 * NS_PER_SEC + nsec_u64;
			printk("%s,Tx, sec_u64:%lld, nsec_u64:%lld, time64_u64:%lld, ptp_mode:%d",__func__,sec_u64,nsec_u64,time64_u64,pSwitch->ptp_mode);
		        time64 = time64_u64;
#else
		        time64 = (sec * NS_PER_SEC) + nsec;
#endif

#ifdef ASIX_GPTP_DEBUG
			printk("%s, ==Tx, port_id:%d, msg_type: %d, seque_id: %d, ts_lo:0x%x, ts_Hi:0x%x, time: %lld",\
					__func__,\
					ts_d.port_id,\
					ts_d.msg_type,\
					ts_d.sequence_id,\
				       	nsec_u64,\
					sec_u64,\
				       	time64);
			printk("-------------------------------------------------------------");
#endif
			memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
			shhwtstamps->hwtstamp = ns_to_ktime(time64);
			skb_tstamp_tx(skb, shhwtstamps);
			return;
		}
		/* Retry after 500us */
		udelay(500);
		pr_info("%s: try to retreive timestamp after 500us, Try=%d MsgType: %x \tSeqID: %x.\n",__func__, i, ts_d.msg_type, ts_d.sequence_id);
	}
	printk("%s: could not retrieve timestamp from queue. MsgType=%x \tSeqID=%x Domain=%d\n",__func__, ts_d.msg_type, ts_d.sequence_id, ts_d.domain_number);
	return;
} /* End of ax_tsn_tx_hwtstamp() */


void ax_fast_age(struct ax_switch *pSwitch)
{
	volatile uint32_t *p_fdb_ctrl = pSwitch->pMemBase + FDB_CTRL;
  	//wait until previous mstid clear operation finishes
  	while (((*p_fdb_ctrl) & 0x80) == 0x80);
  	*p_fdb_ctrl = 0x10;

}
