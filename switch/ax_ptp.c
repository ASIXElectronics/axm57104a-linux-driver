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
#include "ax_ptp.h"
#include <asm/div64.h>
/* NAMING CONSTANT DECLARATIONS */

/* GLOBAL VARIABLES DECLARATIONS */

/* LOCAL VARIABLES DECLARATIONS */

/* LOCAL SUBPROGRAM DECLARATIONS */
static int ax_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm);
static int ax_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta);
static int ax_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts);
static int ax_ptp_settime64(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts);
static int ax_ptp_enable(struct ptp_clock_info *ptp,
			 struct ptp_clock_request *rq, int on);
/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_init()
 * Purpose: Intial the ptp structure
 * Params: @dev: net device
 * Returns: void
 * Note:
 * ----------------------------------------------------------------------------
 */
void
ax_ptp_init(struct net_device *netdev)
{
	struct ax_private *ax_local = netdev_priv(netdev);
	struct ax_switch *pSwitch = &ax_local->axswitch;
	volatile uint32_t *p_addend = pSwitch->pMemBase + AS_T_ADDEND;
	volatile uint32_t *p_tmr_selector = pSwitch->pMemBase + AS_T_SELECTOR;
	int i = 0;

	pSwitch->phc_id = 0;
	pSwitch->ptp_mode = 0;
	for (i = 0; i < PTP_DOMAIN_NUM; i++) {
		
		pSwitch->ax_ptp_clocks[i].ptp_caps.owner = THIS_MODULE;
		snprintf(pSwitch->ax_ptp_clocks[i].ptp_caps.name,16,"%pm",netdev->dev_addr);
		pSwitch->ax_ptp_clocks[i].ptp_caps.n_alarm = 0;
		pSwitch->ax_ptp_clocks[i].ptp_caps.n_ext_ts = 0;
		pSwitch->ax_ptp_clocks[i].ptp_caps.n_per_out = 0;
		pSwitch->ax_ptp_clocks[i].ptp_caps.n_pins = 0;
		pSwitch->ax_ptp_clocks[i].ptp_caps.pps = 0;
		pSwitch->ax_ptp_clocks[i].ptp_caps.adjfine = ax_ptp_adjfine;
		pSwitch->ax_ptp_clocks[i].ptp_caps.adjtime = ax_ptp_adjtime;
		pSwitch->ax_ptp_clocks[i].ptp_caps.gettime64 = ax_ptp_gettime64;
		pSwitch->ax_ptp_clocks[i].ptp_caps.settime64 = ax_ptp_settime64;
		pSwitch->ax_ptp_clocks[i].ptp_caps.enable = ax_ptp_enable;
		pSwitch->ax_ptp_clocks[i].domain_index = i;
		/* Timer selector is always domain_index+1,
		domain_index=0 is for FRT */
		*p_tmr_selector = pSwitch->ax_ptp_clocks[i].domain_index + 1;
		pSwitch->ax_ptp_clocks[i].base_addend_val = *p_addend;
		pSwitch->ax_ptp_clocks[i].ptp_caps.max_adj = 100000000;
		pSwitch->ax_ptp_clocks[i].pMemBase = pSwitch->pMemBase;
		pSwitch->ax_ptp_clocks[i].ptp_clock = 
		ptp_clock_register(&pSwitch->ax_ptp_clocks[i].ptp_caps,
					&ax_local->pdev->dev);
		
		if (IS_ERR(pSwitch->ax_ptp_clocks[i].ptp_clock)) {
			pSwitch->ax_ptp_clocks[i].ptp_clock = NULL;
			dev_err(&ax_local->pdev->dev,
					"ptp_clock_register failed\n");
		}
		pSwitch->ax_ptp_clocks[i].ptp_clock_index =
			ptp_clock_index(pSwitch->ax_ptp_clocks[i].ptp_clock);

		pSwitch->domain_number_table.domain_number_map[i] = 0;		
	}	
} /* End of ax_ptp_init() */
EXPORT_SYMBOL_GPL(ax_ptp_init);

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_remove()
 * Purpose:
 * Params: @dev: net device
 * Returns: void
 * Note:
 * ----------------------------------------------------------------------------
 */
void
ax_ptp_remove(struct net_device *netdev)
{
	struct ax_private *ax_local = netdev_priv(netdev);
	struct ax_switch *pSwitch = &ax_local->axswitch;
	int i = 0;

	for (i = 0; i < PTP_DOMAIN_NUM; i++) {
		if (pSwitch->ax_ptp_clocks[i].ptp_clock) {
			ptp_clock_unregister(
					pSwitch->ax_ptp_clocks[i].ptp_clock);
		}
	}
} /* End of ax_ptp_remove() */
EXPORT_SYMBOL_GPL(ax_ptp_remove);

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_adjtime()
 * Purpose: Adjust the timer counter value with delta
 * Params: @ptp: PTP clock info structure
 * 	   @delta: Delta value in nano seconds
 * Returns:Always returns zero
 * Note:
 * ----------------------------------------------------------------------------
 */
static int
ax_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ax_ptp_clock_data *ptp_clock_data =
				container_of(ptp,
					struct ax_ptp_clock_data,
					ptp_caps);
	volatile u32 *p_tmr_selector = ptp_clock_data->pMemBase + AS_T_SELECTOR;
	volatile u32 *p_second_out = ptp_clock_data->pMemBase + AS_T_VAL_O_HI;
	volatile u32 *p_nanosecond_out = ptp_clock_data->pMemBase + AS_T_VAL_O_LO;
	volatile u32 *p_second_in = ptp_clock_data->pMemBase + AS_T_VAL_I_HI;
	volatile u32 *p_nanosecond_in = ptp_clock_data->pMemBase + AS_T_VAL_I_LO;
	u32 remainder;
	u64 ns;
	/* Read current time and add delta value */
	/* domain_index = 0 is for FRT */
	*p_tmr_selector = ptp_clock_data->domain_index + 1;
	ns = *p_second_out;
	ns *= NS_PER_SEC;
	ns += *p_nanosecond_out;
	ns += delta;

	/* Write new time value to hardware clock */
	*p_second_in = div_u64_rem(ns, NS_PER_SEC, &remainder);
	*p_nanosecond_in = remainder;
	return 0;
} /* End of ax_ptp_adjtime() */


/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_adjfine()
 * Purpose: Adjust the clock frequency
 * Params: @ptp: PTP clock info structure
 * 	   @ppb: Frequency in parts per billion
 * Returns:Always returns zero
 * Note:
 * ----------------------------------------------------------------------------
 */
static int
ax_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ax_ptp_clock_data *ptp_clock_data =
				container_of(ptp,
					struct ax_ptp_clock_data,
					ptp_caps);
	volatile u32 *p_tmr_selector = ptp_clock_data->pMemBase + AS_T_SELECTOR;
	volatile u32 *p_addend = ptp_clock_data->pMemBase + AS_T_ADDEND;

	u32 new_addend_val;
	u64 adjust_val;
	int neg_adj = 0;
	long ppb = scaled_ppm_to_ppb(scaled_ppm);
		
	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	adjust_val = ptp_clock_data->base_addend_val;
	adjust_val *= ppb;
	adjust_val = div_u64(adjust_val, NS_PER_SEC);

	if (neg_adj) {
		new_addend_val =
			(u32)(ptp_clock_data->base_addend_val - adjust_val);
	} else {
		new_addend_val =
			(u32)(ptp_clock_data->base_addend_val + adjust_val);
	}

	/* Write seconds and nanoseconds to hardware clock */
	*p_tmr_selector = ptp_clock_data->domain_index + 1;
	*p_addend = new_addend_val;

	return 0;

} /* End of ax_ptp_adjfine() */

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_gettime64()
 * Purpose: Get the current time from the timer counter
 * Params: @ptp: PTP clock info structure
 * 	   @ts: Timespec structure to hold the current time value
 * Returns:Always returns zero
 * Note:
 * ----------------------------------------------------------------------------
 */
static int
ax_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ax_ptp_clock_data *ptp_clock_data =
				container_of(ptp,struct ax_ptp_clock_data,ptp_caps);
	volatile u32 *p_tmr_selector =  ptp_clock_data->pMemBase + AS_T_SELECTOR;
	volatile u32 *p_second = ptp_clock_data->pMemBase + AS_T_VAL_O_HI;
	volatile u32 *p_nanosecond = ptp_clock_data->pMemBase + AS_T_VAL_O_LO;	
	
	/* Read seconds and nanoseconds from hardware clock */
	*p_tmr_selector = ptp_clock_data->domain_index + 1;
	ts->tv_sec = *p_second;
	ts->tv_nsec = *p_nanosecond;

	return 0;
} /* End of ax_ptp_gettime64() */ 

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_settime64()
 * Purpose: Reset the timer counter to use new base value
 * Params: @ptp: PTP clock info structure
 * 	   @ts: Timespec structure to hold the current time value
 * Returns:Always returns zero
 * Note:
 * ----------------------------------------------------------------------------
 */
static int
ax_ptp_settime64(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct ax_ptp_clock_data *ptp_clock_data =
				container_of(ptp,
					struct ax_ptp_clock_data,
					ptp_caps);
	volatile u32 *p_tmr_selector = ptp_clock_data->pMemBase + AS_T_SELECTOR;
	volatile u32 *p_second = ptp_clock_data->pMemBase + AS_T_VAL_I_HI;
	volatile u32 *p_nanosecond = ptp_clock_data->pMemBase + AS_T_VAL_I_LO;

	*p_tmr_selector = ptp_clock_data->domain_index +1;
	*p_second = (u32)ts->tv_sec;
	*p_nanosecond = (u32)ts->tv_nsec;

	return 0;
} /* End of ax_ptp_settime64() */


/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_enable()
 * Purpose: Select the mode of operation
 * Params: @ptp: PTP clock info structure
 * 	   @rq: Requested feature to change
	   @on: Whether to enable or disable the feature
 * Returns:Always returns EOPNOTSUPP
 * Note:
 * ----------------------------------------------------------------------------
 */
static int
ax_ptp_enable(struct ptp_clock_info *ptp,
	      struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
} /* End of ax_ptp_enable() */


/* End of ax_ptp.c */
