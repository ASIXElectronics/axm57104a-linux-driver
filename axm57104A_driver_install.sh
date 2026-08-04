#!/bin/bash
SYSTEM_NAME=$(uname -r)
MODULE_PATH=/lib/modules/$SYSTEM_NAME/kernel/drivers/net/ethernet

cd $MODULE_PATH
modprobe ptp
modprobe phylink
modprobe bridge
modprobe hsr
insmod ax_dsa_core.ko
insmod asix_tag.ko
insmod axm57104A.ko

sleep 3
