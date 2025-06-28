#include <liburing.h>

typedef uint64_t lba_t;
typedef int rc_t;

// TODO(mfd) : define this flag appropriately in the Makefile
#define CONFIG_NVME_URING_CMD

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

#define NVME_URING_CMD_IO _IOWR('N', 0x80, struct nvme_uring_cmd)
#define NVME_URING_CMD_IO_VEC _IOWR('N', 0x81, struct nvme_uring_cmd)
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

// a single ring will do for now
// extern struct io_uring ring;

struct io_interface {
  struct io_uring ring;
};

typedef struct fdp_dev {
  char name[20];
  // device generic name
  char g_name[20];
  // file handle of the generic device
  int g_fd;
  // namespace id to extract from the device
  uint16_t nsid;
  uint16_t lba_size;
  uint32_t max_transfer_size;
  uint16_t nruh;
  // for now to keep it simple io_request to each device 
  // will go through this ring io_uring. Later we need a more
  // sophisticated way to issue io request like per cpu/thread ring.
  struct io_uring ring;
} fdp_dev_t;

// This is the interface of the library

int nvme_fdp_init(const char *bdev_name, fdp_dev_t *dev);

ssize_t fdp_nvme_write(fdp_dev_t *dev, void *buf, size_t count, off_t offset, uint16_t plid);

void fdp_dev_close(fdp_dev_t *dev);

