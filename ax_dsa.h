/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *****************************************************************************/
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AX_DSA_H
#define __AX_DSA_H

struct dsa_chip_data;

struct dsa_loop_pdata {
	/* Must be first, such that dsa_register_switch() can access this
	 * without gory pointer manipulations
	 */
	struct dsa_chip_data cd;
	const char *name;
	unsigned int enabled_ports;
	struct net_device *netdev;
};

#define DSA_LOOP_NUM_PORTS	4
#define DSA_LOOP_CPU_PORT	4


#endif /* __AX_DSA_H */
