/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DSA_LOOP_H
#define __DSA_LOOP_H

#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include "dsa.h"


struct dsa_chip_data;

struct dsa_loop_pdata {
	/* Must be first, such that dsa_register_switch() can access this
	 * without gory pointer manipulations
	 */
	struct dsa_chip_data cd;
	const char *name;
	unsigned int enabled_ports;
	const char *netdev;
};

#define DSA_LOOP_CPU_PORT   4 // the cpu port index
#define DSA_LOOP_NUM_PORTS  4 //how many outside ports

#endif /* __DSA_LOOP_H */