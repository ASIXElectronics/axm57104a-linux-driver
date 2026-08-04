/******************************************************************************
 *     Copyright (c) 2020 ASIX Electronic Corporation All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic
 *     Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
 *****************************************************************************/
#ifndef __AX_IOCTL_H
#define __AX_IOCTL_H

#define IOCTL_SEND_CMD				(SIOCDEVPRIVATE + 0)
#define IOCTL_SET_DOMAIN_NUMBER_TABLE   (SIOCDEVPRIVATE + 1)
#define IOCTL_GET_SYNC_TX_TIMESTAMP     (SIOCDEVPRIVATE + 2)
#define IOCTL_GET_SYNC_RX_TIMESTAMP     (SIOCDEVPRIVATE + 3)
#define IOCTL_GPTP_SET_PTP_MODE         (SIOCDEVPRIVATE + 4)
const unsigned char	ASIX_GID[8] = {'A', 'S', 'I', 'X', 'X', 'I', 'S', 'A'};

//Definition of return Value
#define AX_STATUS_NO_DATA                     1
#define AX_STATUS_SUCCESS                     0
#define AX_STATUS_FAILURE                    -1
#define AX_STATUS_NOT_SUPPORT                -2

#define AX_IOCTL_SIGNATURE			0
#define AX_IOCTL_READ_REG			1
#define AX_IOCTL_WRITE_REG			2
#define AX_IOCTL_GET_BAR_BASE			3
#define AX_IOCTL_TIME_SLOT                      4
#define AX_IOCTL_CLEAN_FIFO                     5
#define AX_IOCTL_LAST				AX_IOCTL_CLEAN_FIFO

typedef enum _MMAP_BAR {
	BAR0 = 0,	// PCIe NIC
	BAR1 = 1,	// PCIe DMA
	BAR2 = 2	// TSN Switch
} MMAP_BAR;

#pragma pack(push)
#pragma pack(1)
typedef struct {
	unsigned char			Sig[8];
	unsigned char			MacAddr[6];
} AX_SIGNATURE, *PAX_SIGNATURE;

typedef struct {
	unsigned long	offset;
	unsigned long	value;
	MMAP_BAR	bar;
} AX_MEM_REG, *PAX_MEM_REG;

typedef struct {
	uint64_t bar_base_addr;
	MMAP_BAR bar;
} AX_BAR_BASE, *PAX_BAR_BASE;

typedef union {
	AX_SIGNATURE		Signature;
	AX_MEM_REG		Mem_register;
	AX_BAR_BASE		Bar_base_addr;
} AX_CMD_DATA, *PAX_CMD_DATA;


typedef struct {
	unsigned char		Gid[8];
	unsigned long		Opcode;
	int			Status;
	AX_CMD_DATA		CmdData;
} IOCTL;
#pragma pack(pop)

#endif /* End of __AX_IOCTL_H */
