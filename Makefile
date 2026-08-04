SHELL = /bin/bash
SCRIPT_FILE:=/usr/local/bin/axm57104A_driver_install.sh
SERVICE_FILE:=/lib/systemd/system/AXM57104A_Module.service
ifneq ($(xvc_bar_num),)
	XVC_FLAGS += -D__XVC_BAR_NUM__=$(xvc_bar_num)
endif

ifneq ($(xvc_bar_offset),)
	XVC_FLAGS += -D__XVC_BAR_OFFSET__=$(xvc_bar_offset)
endif

$(warning XVC_FLAGS: $(XVC_FLAGS).)

topdir := $(shell cd $(src)/.. && pwd)

DSA_FILES = DSA/devlink.c \
	DSA/dsa2.c \
	DSA/conduit.c \
	DSA/netlink.c \
	DSA/port.c \
	DSA/user.c \
	DSA/switch.c \
	DSA/tag_8021q.c \
	DSA/trace.c \
	DSA/tag.c \

TAG_FILES =DSA/tag_asix.c

CFILES = DSA/dsa_loop.c \
	libxdma.c \
	xdma_mod.c \
	switch/ax_switch.c \
	switch/ax_ptp.c


TARGET_MODULE:=axm57104A
DSA_MODULE:=ax_dsa_core
DSA_TAG_MODULE:=asix_tag
BUILDSYSTEM:=/lib/modules/$(shell uname -r)
INSTALL_PATH:=kernel/drivers/net/ethernet
EXTRA_CFLAGS := -I$(topdir)/include $(XVC_FLAGS)
#EXTRA_CFLAGS := -I$(topdir)/include $(XVC_FLAGS) \
	-Wno-array-bounds \
	-fno-aggressive-loop-optimizations \
	-fno-strict-overflow \
	-fwrapv \
	-fno-delete-null-pointer-checks \
	-fno-tree-slp-vectorize

#KBUILD_EXTRA_SYMBOLS+=./Module.symvers
#export KBUILD_EXTRA_SYMBOLS
#EXTRA_CFLAGS += -DAXM57104_DEBUG
#EXTRA_CFLAGS += -D__LIBXDMA_DEBUG__
#EXTRA_CFLAGS += -DINTERNAL_TESTING

ifneq ($(KERNELRELEASE),)
	$(TARGET_MODULE)-objs := $(CFILES:.c=.o)
	$(DSA_MODULE)-objs := $(DSA_FILES:.c=.o)
	$(DSA_TAG_MODULE)-objs := $(TAG_FILES:.c=.o)
	obj-m := $(TARGET_MODULE).o $(DSA_MODULE).o $(DSA_TAG_MODULE).o
else
	BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	TARGET_PATH:= kernel/drivers/net/ethernet
	AXM57104A_PATH:= /lib/modules/$(shell uname -r)/$(TARGET_PATH)/axm57104A.ko
	DSA_PATH:= /lib/modules/$(shell uname -r)/$(TARGET_PATH)/ax_dsa_core.ko
	ASIX_TAG:= /lib/modules/$(shell uname -r)/$(TARGET_PATH)/asix_tag.ko
	PWD:=$(shell pwd)
endif

.PHONY: all
all : 
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean
	rm -rf *.o.* .cache.* *.mod 

#.PHONY: uninstall
#uninstall: clean
#	LSMOD = $(findstring $(TARGET_MODULE), $(shell lsmod))
#	if [ $(LSMOD) ]; then \
#		rmmod axm57104A ;\
#		rmmod asix_tag ;\
#		rmmod ax_dsa_core ;\
#		rm -f $(BUILDSYSTEM)/$(INSTALL_PATH)/axm57104A.ko;\
#		rm -f $(BUILDSYSTEM)/$(INSTALL_PATH)/asix_tag.ko;\
#		rm -f $(BUILDSYSTEM)/$(INSTALL_PATH)/ax_dsa_core.ko;\
#	fi

.PHONY: uninstall
uninstall: clean
	@echo "Unloading modules...."
	@if lsmod | grep -q axm57104A; then sudo rmmod axm57104A; fi
	@if lsmod | grep -q asix_tag; then sudo rmmod asix_tag; fi
	@if lsmod | grep -q ax_dsa_core; then sudo rmmod ax_dsa_core; fi
	
	@echo  "Removing .ko files..."
	sudo rm -f $(BUILDSYSTEM)/$(INSTALL_PATH)/axm57104A.ko
	sudo rm -f $(BUILDSYSTEM)/$(INSTALL_PATH)/asix_tag.ko
	sudo rm -f $(BUILDSYSTEM)/$(INSTALL_PATH)/ax_dsa_core.ko

ifneq ($(wildcard $(SCRIPT_FILE)), )
	rm -f $(SCRIPT_FILE)
	@echo "Remove install script file"
endif

ifneq ($(wildcard $(SERVICE_FILE)), )
	systemctl stop AXM57104A_Module
	systemctl disable AXM57104A_Module
	rm -f $(SERVICE_FILE)
	systemctl daemon-reload
	@echo "Remove and disable service module"
endif

ifneq ($(wildcard $(AXM57104A_PATH)), )
	rm -rf $(AXM57104A_PATH)
endif
ifneq ($(wildcard $(DSA_PATH)), )
	rm -rf $(DSA_PATH)
endif
ifneq ($(wildcard $(ASIX_TAG)), )
	rm -rf $(ASIX_TAG)
endif
	sudo /usr/sbin/depmod -a

.PHONY: install
install: clean all 
	cp *.ko $(BUILDSYSTEM)/$(INSTALL_PATH)
	cp ./axm57104A_driver_install.sh /usr/local/bin
	install -o root -g root -m 644 ./AXM57104A_Module.service /lib/systemd/system
	systemctl daemon-reload
	systemctl enable AXM57104A_Module

