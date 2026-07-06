/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *****************************************************************************/

/* INCLUDE FILE DECLARATIONS */
#include "ax_ptp.h"

/* NAMING CONSTANT DECLARATIONS */

/* GLOBAL VARIABLES DECLARATIONS */

/* LOCAL VARIABLES DECLARATIONS */

/* LOCAL SUBPROGRAM DECLARATIONS */
static int ax_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb);
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

	pSwitch->phc_id = 0;	
	snprintf(pSwitch->ptp_caps.name, 16, "%pm", netdev->dev_addr);
	pSwitch->ptp_caps.owner = THIS_MODULE;
	pSwitch->ptp_caps.n_alarm = 0;
	pSwitch->ptp_caps.n_ext_ts = 0;
	pSwitch->ptp_caps.n_per_out = 0;
	pSwitch->ptp_caps.n_pins = 0;	
	pSwitch->ptp_caps.pps = 0;
	pSwitch->ptp_caps.adjfine = 0;
	pSwitch->ptp_caps.max_adj = 100000000;
	pSwitch->ptp_caps.adjfreq = ax_ptp_adjfreq;
	pSwitch->ptp_caps.adjtime = ax_ptp_adjtime;
	pSwitch->ptp_caps.gettime64 = ax_ptp_gettime64;
	pSwitch->ptp_caps.settime64 = ax_ptp_settime64;
	pSwitch->ptp_caps.enable = ax_ptp_enable;

	pSwitch->base_addend_val = ax_tsn_read_reg(pSwitch, AS_T_ADDEND);

	pSwitch->ptp_clock = ptp_clock_register(&pSwitch->ptp_caps,
						&ax_local->pdev->dev);
	if (IS_ERR(pSwitch->ptp_clock)) {
		pSwitch->ptp_clock = NULL;
		dev_err(&ax_local->pdev->dev, "ptp_clock_register failed\n");
	}

	pSwitch->phc_index[pSwitch->phc_id] = 
				ptp_clock_index(pSwitch->ptp_clock);
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

	if (pSwitch->ptp_clock) {
		ptp_clock_unregister(pSwitch->ptp_clock);
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
	struct ax_switch *pSwitch = 
				container_of(ptp, struct ax_switch, ptp_caps);	
	u32 remainder;
	u64 high_timer;
	u64 ns;

	/* Read current time and add delta value */
	ns = ax_tsn_read_reg(pSwitch, AS_T_VAL_O_HI);
	ns *= NS_PER_SEC;
	ns += ax_tsn_read_reg(pSwitch, AS_T_VAL_O_LO);
	ns += delta;

	/* Write new time value to hardware clock */
	high_timer = div_u64_rem(ns, NS_PER_SEC, &remainder);
	ax_tsn_write_reg(pSwitch, AS_T_VAL_I_HI, (u32)high_timer);
	ax_tsn_write_reg(pSwitch, AS_T_VAL_I_LO, remainder);

	return 0;
} /* End of ax_ptp_adjtime() */

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_ptp_adjfreq()
 * Purpose: Adjust the clock frequency
 * Params: @ptp: PTP clock info structure
 * 	   @ppb: Frequency in parts per billion
 * Returns:Always returns zero 
 * Note:
 * ----------------------------------------------------------------------------
 */
static int
ax_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct ax_switch *pSwitch = 
				container_of(ptp, struct ax_switch, ptp_caps);	
	u32 new_addend_val;
	u64 adjust_val;
	int neg_adj = 0;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	adjust_val = pSwitch->base_addend_val;
	adjust_val *= ppb;
	adjust_val = div_u64(adjust_val, NS_PER_SEC);

	if (neg_adj) {
		new_addend_val = (u32)(pSwitch->base_addend_val - adjust_val);
	} else {
		new_addend_val = (u32)(pSwitch->base_addend_val + adjust_val);
	}
	
	/* Write seconds and nanoseconds to hardware clock */
	ax_tsn_write_reg(pSwitch, AS_T_ADDEND, new_addend_val);

	return 0;
} /* End of ax_ptp_adjfreq() */

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
	struct ax_switch *pSwitch = 
				container_of(ptp, struct ax_switch, ptp_caps);	
	
	/* Read seconds and nanoseconds from hardware clock */
	ts->tv_sec = ax_tsn_read_reg(pSwitch, AS_T_VAL_O_HI);
	ts->tv_nsec = ax_tsn_read_reg(pSwitch, AS_T_VAL_O_LO);
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
	struct ax_switch *pSwitch = 
				container_of(ptp, struct ax_switch, ptp_caps);	
	
	/* Write seconds and nanoseconds to hardware clock */
	ax_tsn_write_reg(pSwitch, AS_T_VAL_I_HI, (u32)ts->tv_sec);
	ax_tsn_write_reg(pSwitch, AS_T_VAL_I_LO, (u32)ts->tv_nsec);
	
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
