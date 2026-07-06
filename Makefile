SHELL = /bin/bash
ifneq ($(xvc_bar_num),)
	XVC_FLAGS += -D__XVC_BAR_NUM__=$(xvc_bar_num)
endif

ifneq ($(xvc_bar_offset),)
	XVC_FLAGS += -D__XVC_BAR_OFFSET__=$(xvc_bar_offset)
endif

$(warning XVC_FLAGS: $(XVC_FLAGS).)

topdir := $(shell cd $(src)/.. && pwd)

CFILES = libxdma.c \
	xdma_mod.c \
	ax_dsa.c \
	switch/ax_switch.c \
	switch/ax_ptp.c

TARGET_MODULE:=axm57104

EXTRA_CFLAGS := -I$(topdir)/include $(XVC_FLAGS)
#EXTRA_CFLAGS += -DAXM57104_DEBUG
#EXTRA_CFLAGS += -D__LIBXDMA_DEBUG__
#EXTRA_CFLAGS += -DINTERNAL_TESTING

ifneq ($(KERNELRELEASE),)
	$(TARGET_MODULE)-objs := $(CFILES:.c=.o)
	obj-m := $(TARGET_MODULE).o
else
	BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	TARGET_PATH:= kernel/drivers/net/ethernet
	RM_PATH:= /lib/modules/$(shell uname -r)/$(TARGET_PATH)/axm57104.ko
	PWD:=$(shell pwd)

.PHONY: all
all : 
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean
	rm -rf *.o.* .cache.*

.PHONY: uninstall
uninstall: clean
ifneq ($(shell lsmod | grep axm57104),)
	modprobe -r axm57104
endif
	rm -rf $(RM_PATH)
	sudo /sbin/depmod -a

.PHONY: instatll
install: clean all uninstall
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) INSTALL_MOD_DIR=$(TARGET_PATH) modules_install
	sudo /sbin/depmod -a
	modprobe axm57104
endif
