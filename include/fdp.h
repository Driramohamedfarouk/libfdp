
// Almost alli nvme structs and constants and enum defitions are taken from
// libnvme. later just copy the src/nvme/types.h under include as is, with its
// copyright and remove redundant definitions here.

// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 *
 * Authors: Keith Busch <keith.busch@wdc.com>
 *	    Chaitanya Kulkarni <chaitanya.kulkarni@wdc.com>
 */
#include <liburing.h>

typedef uint64_t lba_t;
typedef int rc_t;
typedef uint16_t plid_t;

// TODO(mfd) : define this flag appropriately in the Makefile
#define CONFIG_NVME_URING_CMD

/**
 * NVME_SET() - set field into complex value
 * @value: The value to be set in its completed position
 * @name: The name of the sub-field within an nvme value
 *
 * Returns: The 'name' field from 'value'
 */
#define NVME_SET(value, name)                                                  \
	(((__u32)(value) & NVME_##name##_MASK) << NVME_##name##_SHIFT)

// Reference: https://github.com/axboe/fio/blob/master/engines/nvme.h
// If the uapi headers installed on the system lacks nvme uring command
// support, use the local version to prevent compilation issues.
#ifndef CONFIG_NVME_URING_CMD
#include <linux/types.h>
struct nvme_uring_cmd {
	__u8 opcode;
	__u8 flags;
	__u16 rsvd1;
	__u32 nsid;
	__u32 cdw2;
	__u32 cdw3;
	__u64 metadata;
	__u64 addr;
	__u32 metadata_len;
	__u32 data_len;
	__u32 cdw10;
	__u32 cdw11;
	__u32 cdw12;
	__u32 cdw13;
	__u32 cdw14;
	__u32 cdw15;
	__u32 timeout_ms;
	__u32 rsvd2;
};

/**
 * struct nvme_passthru_cmd - nvme passthrough command structure
 * @opcode:	Operation code, see &enum nvme_io_opcodes and &enum
 * nvme_admin_opcodes
 * @flags:	Not supported: intended for command flags (eg: SGL, FUSE)
 * @rsvd1:	Reserved for future use
 * @nsid:	Namespace Identifier, or Fabrics type
 * @cdw2:	Command Dword 2 (no spec defined use)
 * @cdw3:	Command Dword 3 (no spec defined use)
 * @metadata:	User space address to metadata buffer (NULL if not used)
 * @addr:	User space address to data buffer (NULL if not used)
 * @metadata_len: Metadata buffer transfer length
 * @data_len:	Data buffer transfer length
 * @cdw10:	Command Dword 10 (command specific)
 * @cdw11:	Command Dword 11 (command specific)
 * @cdw12:	Command Dword 12 (command specific)
 * @cdw13:	Command Dword 13 (command specific)
 * @cdw14:	Command Dword 14 (command specific)
 * @cdw15:	Command Dword 15 (command specific)
 * @timeout_ms:	If non-zero, overrides system default timeout in milliseconds
 * @result:	Set on completion to the command's CQE DWORD 0 controller response
 */
struct nvme_passthru_cmd {
	__u8 opcode;
	__u8 flags;
	__u16 rsvd1;
	__u32 nsid;
	__u32 cdw2;
	__u32 cdw3;
	__u64 metadata;
	__u64 addr;
	__u32 metadata_len;
	__u32 data_len;
	__u32 cdw10;
	__u32 cdw11;
	__u32 cdw12;
	__u32 cdw13;
	__u32 cdw14;
	__u32 cdw15;
	__u32 timeout_ms;
	__u32 result;
};

#define NVME_URING_CMD_IO _IOWR('N', 0x80, struct nvme_uring_cmd)
#define NVME_URING_CMD_IO_VEC _IOWR('N', 0x81, struct nvme_uring_cmd)
#define NVME_IOCTL_ADMIN_CMD _IOWR('N', 0x41, struct nvme_passthru_cmd)
#endif /* CONFIG_NVME_URING_CMD */

#define NVME_DEFAULT_IOCTL_TIMEOUT 0

enum nvme_io_mgmt_recv_mo {
	NVME_IO_MGMT_RECV_RUH_STATUS = 0x1,
};

struct nvme_fdp_ruh_status_desc {
	uint16_t pid;
	uint16_t ruhid;
	uint32_t earutr;
	uint64_t ruamw;
	uint8_t rsvd16[16];
};

struct nvme_fdp_ruh_status {
	uint8_t rsvd0[14];
	uint16_t nruhsd;
	struct nvme_fdp_ruh_status_desc ruhss[];
};

enum nvme_io_opcode {
	nvme_cmd_write = 0x01,
	nvme_cmd_read = 0x02,
	nvme_cmd_io_mgmt_recv = 0x12,
	nvme_cmd_io_mgmt_send = 0x1d,
};

enum nvme_cmd_dword_fields {
	NVME_LOG_CDW10_LID_SHIFT = 0,
	NVME_LOG_CDW10_LSP_SHIFT = 8,
	NVME_LOG_CDW10_RAE_SHIFT = 15,
	NVME_LOG_CDW10_NUMDL_SHIFT = 16,
	NVME_LOG_CDW11_NUMDU_SHIFT = 0,
	NVME_LOG_CDW11_LSI_SHIFT = 16,
	NVME_LOG_CDW14_UUID_SHIFT = 0,
	NVME_LOG_CDW14_CSI_SHIFT = 24,
	NVME_LOG_CDW14_OT_SHIFT = 23,
	NVME_LOG_CDW10_LID_MASK = 0xff,
	NVME_LOG_CDW10_LSP_MASK = 0x7f,
	NVME_LOG_CDW10_RAE_MASK = 0x1,
	NVME_LOG_CDW10_NUMDL_MASK = 0xffff,
	NVME_LOG_CDW11_NUMDU_MASK = 0xffff,
	NVME_LOG_CDW11_LSI_MASK = 0xffff,
	NVME_LOG_CDW14_UUID_MASK = 0x7f,
	NVME_LOG_CDW14_CSI_MASK = 0xff,
	NVME_LOG_CDW14_OT_MASK = 0x1,
};

enum nvme_cmd_get_log_lid {
	NVME_LOG_LID_FDP_CONFIGS = 0x20,
	NVME_LOG_LID_FDP_RUH_USAGE = 0x21,
	NVME_LOG_LID_FDP_STATS = 0x22,
	NVME_LOG_LID_FDP_EVENTS = 0x23,
};

/**
 * struct nvme_get_log_args - Arguments for the NVMe Admin Get Log command
 * @lpo:	Log page offset for partial log transfers
 * @result:	The command completion result from CQE dword0
 * @log:	User space destination address to transfer the data
 * @args_size:	Length of the structure
 * @fd:		File descriptor of nvme device
 * @timeout:	Timeout in ms
 * @lid:	Log page identifier, see &enum nvme_cmd_get_log_lid for known
 *		values
 * @len:	Length of provided user buffer to hold the log data in bytes
 * @nsid:	Namespace identifier, if applicable
 * @csi:	Command set identifier, see &enum nvme_csi for known values
 * @lsi:	Log Specific Identifier
 * @lsp:	Log specific field
 * @uuidx:	UUID selection, if supported
 * @rae:	Retain asynchronous events
 * @ot:		Offset Type; if set @lpo specifies the index into the list
 *		of data structures, otherwise @lpo specifies the byte offset
 *		into the log page.
 */
struct nvme_get_log_args {
	__u64 lpo;
	__u32 *result;
	void *log;
	int args_size;
	int fd;
	__u32 timeout;
	enum nvme_cmd_get_log_lid lid;
	__u32 len;
	__u32 nsid;
	// enum nvme_csi csi;
	__u8 csi;
	__u16 lsi;
	__u8 lsp;
	__u8 uuidx;
	bool rae;
	bool ot;
};

// extern thread_local struct io_uring tls_ring;

// this plays the role of struct file in unix which is the
// physical representation of open files. We should hide this
// from user that should only use the returned filed descriptor.
typedef struct fdp_dev {
	char name[20];
	// device generic name
	char g_name[20];
	// file handle tp the block device
	int bdev_fd;
	// file handle of the generic device
	int g_fd;
	// namespace id to extract from the device
	uint16_t nsid;
	uint16_t lba_size;
	uint32_t max_transfer_size;
	uint16_t nruh;
	/** The sole purpouse of this io_uring instance is to handle
	synchronous fdp_pwrite. If multiple threads are using the
	library we need to protect access to the ring.*/
	struct io_uring ring;
	// callback registered with the fdp_dev, maybe transform this to a list of
	// callbacks
	void (*gc_callback)(void);
	// thrd_t gc_listener_thread;
} fdp_dev_t;

enum nvme_admin_opcode {
	nvme_admin_get_log_page = 0x02,
};

// This is the interface of the library

/** Takes a block device as input will do the same semantic of the open()
syscall and also will open the generic device. On success it will return
the file descriptor of the block device and on failure it WILL returns
the corresponding error code. All interaction with the library will
happen through this handle.  */
int fdp_open(const char *bdev_name, int flags, ... /* mode_t mode */);

// synchronous pwrite with plid (placement id)
ssize_t fdp_pwrite(int fd, void *buf, size_t count, off_t offset,
				   uint16_t plid);

void fdp_close(int fd);

/** On success it return the number of remaining media writes in the open
Reclaim Unit of the Reclaim Unit Handle corresponding to the plid provided.
On failure it returns a failure code.
TODO(mfd4) : define later error code */
ssize_t fdp_get_remaining_bytes_in_ru(int fd, plid_t plid);

// temporarly expose this as an interface for debugging.
int fdp_get_events(int fd, __u8 *log, __u32 log_size);

// This is part of the interface, the callback now does not take any parameter
// mainly because no driver has any meaningful results for the media relocation
// event. We may change this later.
// void fdp_register_gc_callback(int fd, void (*gc_callback)(void));

int open_ru_timer(void *arg);

void fdp_io_uring_prep_write(struct io_uring_sqe *sqe, int fd, const void *buf,
							 unsigned count, __u64 offset, uint16_t plid);
