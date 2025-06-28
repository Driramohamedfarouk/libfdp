#include "fdp.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <linux/nvme_ioctl.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <liburing.h>

#include "util.h"
#include "nvme_util.h"

const char nvme_dev_prefix[9] = "/dev/nvme";
const char nvme_generic_dev_prefix[7] = "/dev/ng";

bool is_valid_nvme_device(const char *bdev_name) {
  const char *pattern = "^/dev/nvme[0-9]+n[0-9]+(p[0-9]+)?$";
  regex_t regex;
  int ret;

  ret = regcomp(&regex, pattern, REG_EXTENDED);
  if (ret) {
    fprintf(stderr, "Failed to compile regex\n");
    return false;
  }

  ret = regexec(&regex, bdev_name, 0, NULL, 0);
  regfree(&regex);

  return ret == 0;  // 0 means match
}

int get_nvme_char_device(const char *bdev_name, char *out_buf,
                         size_t buf_size) {
  fprintf(stderr, "get_nvme_char_dev(%s, %lu)\n", bdev_name, buf_size);
  size_t len = strlen(bdev_name);

  if (len < 11 || len > 15) return -1;

  if (strncmp(bdev_name, nvme_dev_prefix, sizeof(nvme_dev_prefix) != 0))
    return -1;

  size_t pos = sizeof(nvme_dev_prefix);

  if (!isdigit(bdev_name[pos++])) return -1;

  if (bdev_name[pos] != 'n') return -1;

  strcpy(out_buf, "/dev/ng");
  strncpy(out_buf + 7, bdev_name + 9, len - pos + 1);
  // The generic device name is always 2 chars shorter
  out_buf[len - 2] = '\0';

  fprintf(stderr, "%s\n", out_buf);

  return 0;
}

// Returns file descriptor or -1 on error
int open_nvme_char_file(const char *bdev_name) {
  if (!is_valid_nvme_device(bdev_name)) {
    fprintf(stderr, "Invalid NVMe device name: %s\n", bdev_name);
    return -1;
  }

  char cdev_name[20];
  if (get_nvme_char_device(bdev_name, cdev_name, sizeof(cdev_name)) != 0) {
    fprintf(stderr, "Failed to get NVMe char device name for %s\n", bdev_name);
    return -1;
  }

  XLOGF("INFO", "Opening NVMe Char Dev file: %s", cdev_name);

  /*
      int fd = open(cdev_name, O_RDONLY);
      if (fd < 0) {
          XLOGF("ERR", "open() failed for %s: %s", cdev_name, strerror(errno));
          return -1;
      }
  */
  return -1;
}

// NVMe IO Mnagement Receive fn for specific config reading
/** TODO(mfd) : change this to issue raw ommand using the passthrough interface
instead of the synchronous ioctl sytem call. Also hide this function internally
and provide function interface it to client to get the remaining space in the RUHs. */
int nvme_io_mgmt_recv(fdp_dev_t *dev, void *data, uint32_t data_len, uint8_t op, uint16_t op_specific) {
  // Build the I/O management receive command
  // For further details on the CDB format, consult the specification
  // available as "TP4146 Flexible Data Placement 2022.11.30 Ratified"
  // in the following link:
  // https://nvmexpress.org/wp-content/uploads/NVM-Express-2.0-Ratified-TPs_20230111.zip
  uint32_t cdw10 = (op & 0xf) | (op_specific & 0xff << 16);
  uint32_t cdw11 = (data_len >> 2) - 1;  // cdw11 is 0 based

  struct nvme_passthru_cmd cmd = {};
  cmd.opcode = nvme_cmd_io_mgmt_recv;
  cmd.nsid = dev->nsid;
  cmd.addr = (uint64_t)(uintptr_t)data;
  cmd.data_len = data_len;
  cmd.cdw10 = cdw10;
  cmd.cdw11 = cdw11;
  cmd.timeout_ms = NVME_DEFAULT_IOCTL_TIMEOUT;

  return ioctl(dev->g_fd, NVME_IOCTL_IO_CMD, &cmd);
}

struct nvme_fdp_ruh_status *nvme_fdp_status(fdp_dev_t *dev) {
  struct nvme_fdp_ruh_status hdr;
  struct nvme_fdp_ruh_status *status;
  int err;

  // Read FDP ruh status header to get Num_RUH Status Descriptors
  err = nvme_io_mgmt_recv(dev, &hdr, sizeof(hdr),
                       NVME_IO_MGMT_RECV_RUH_STATUS, 0);
  if (err) {
  	XLOGF("ERR","failed to get ruh status header, fd: %d", dev->g_fd);
	return NULL;
  }

  size_t size = sizeof(struct nvme_fdp_ruh_status) +
              (hdr.nruhsd * sizeof(struct nvme_fdp_ruh_status_desc));
  
  status = (struct nvme_fdp_ruh_status *)malloc(size);

  // Read FDP RUH Status
  err = nvme_io_mgmt_recv(dev, (char*)status, size,
                       NVME_IO_MGMT_RECV_RUH_STATUS, 0);
  if (err) {
  	XLOGF("ERR","failed to get ruh status header, fd: %d", dev->g_fd);
	return NULL;
  }

  return status;
}

// Main function to read NVMe info
int read_nvme_info(fdp_dev_t *dev) {
  char ns_name[64] = {0};
  // char part_name[64] = {0};

  if (!dev->name) {
    XLOGF("ERR", "Invalid input to read_nvme_info");
    return -1;
  }

  // Extract ns and partition names
  // get_ns_and_partition(bdev_name, ns_name, part_name);
  strcpy(ns_name, dev->name);

  // Retrieve NVMe properties
  int namespace_id = get_nvme_ns_id(ns_name);
  uint32_t lba_size = get_lba_size(ns_name);
  uint32_t max_transfer_size = get_max_transfer_size(ns_name);

  uint64_t start_lba = 0;
  /*
  if (part_name[0] != '\0') {
    start_lba = get_part_start(ns_name, part_name) >> lba_size;
  }
  */
  XLOGF("INFO",
        "nvme device info, ns_id: %d, lba_size: %u, "
        "max_transfer_size: %u, start_lba: %lu",
        namespace_id, lba_size, max_transfer_size, start_lba);

  // Populate output structure
  dev->nsid = namespace_id;
  dev->lba_size = lba_size;
  dev->max_transfer_size = max_transfer_size;
  // dev->start_lba = start_lba;
  return 0;  // success
}

// This is part of the library interface
int nvme_fdp_init(const char *bdev_name, fdp_dev_t *dev) {
  if (!is_valid_nvme_device(bdev_name)) {
    fprintf(stderr, "Invalid NVMe device name: %s\n", bdev_name);
    return -1;
  }

  fprintf(stderr, "%s\n", bdev_name);
  strcpy(dev->name, bdev_name);

  // get the char dev name

  if (get_nvme_char_device(bdev_name, dev->g_name, sizeof(dev->g_name)) != 0) {
    fprintf(stderr, "Failed to get NVMe char device name for %s\n", bdev_name);
    return -1;
  }

  int fd = open(dev->g_name, O_RDONLY);
  if (fd < 0) {
    XLOGF("ERR", "open() failed for %s: %s", dev->g_name, strerror(errno));
    return -1;
  }

  dev->g_fd = fd;
  assert(isdigit(dev->g_name[9]));
  dev->nsid = (uint16_t)(dev->g_name[9] - '0');
  XLOGF("INFO", "Opening NVMe Char Dev file: %s on fd %d with nsid %d",
        dev->g_name, dev->g_fd, dev->nsid);

  // Now determine the lba_size

  // determine the number of reclaim unit handles

  // assume the list of plid is ordinals to the NRUH
  // validate that the device is fdp capable

  // read nvme attributes
  read_nvme_info(dev);

  // read fdp attributes
  struct nvme_fdp_ruh_status *fdp_status; 
  fdp_status = nvme_fdp_status(dev);
  if (fdp_status == NULL) {
	XLOGF("ERR", "failed to obtain nvme fdp status information");
  }
  dev->nruh = fdp_status->nruhsd;
  // print to stdout the number of RUHS
  XLOGF("INFO", "The number of RUHs in %s is %u", dev->name, fdp_status->nruhsd);

  // Initialize the io_uring instance used by this device.
  int rc = io_uring_queue_init(64, &dev->ring, IORING_SETUP_SQE128 | IORING_SETUP_CQE32); 
  assert(rc == 0);
  return 0;
}

void fdp_dev_close(struct fdp_dev *dev) { close(dev->g_fd); }

// first argument is the file descriptor of the generic nvme device
static struct io_uring_sqe * 
prep_passthrough_cmd(fdp_dev_t *dev, void *buf, size_t buf_size, 
					size_t offset, int is_write, uint16_t dspec) { 
  struct io_uring_sqe *sqe;
  struct nvme_uring_cmd *cmd;
  lba_t slba;
  size_t nlba;
  
  const uint8_t dtype = 0x02; // 2 specifies FDP directive

  sqe = io_uring_get_sqe(&dev->ring);
  if (!sqe) return sqe;

  sqe->fd = dev->g_fd;
  sqe->opcode = IORING_OP_URING_CMD;
  sqe->cmd_op = NVME_URING_CMD_IO;
  cmd = (struct nvme_uring_cmd*)&sqe->cmd;
  assert(cmd != NULL);

  // XXX(mfd) I think this step is unnecessary
  memset(cmd, 0, sizeof(struct nvme_uring_cmd));

  cmd->opcode = is_write ? nvme_cmd_write : nvme_cmd_read;

  assert(offset % dev->lba_size  == 0);
  slba = offset / dev->lba_size;
  assert(buf_size % dev->lba_size  == 0);
  nlba = buf_size / dev->lba_size - 1;

  /* cdw10 and cdw11 represent starting lba */
  cmd->cdw10 = slba & 0xffffffff;
  cmd->cdw11 = slba >> 32;
  /* cdw12 represent number of lba's for read/write */
  cmd->cdw12 = (dtype & 0xFF) << 20 | nlba;
  cmd->cdw13 = (dspec << 16);
  cmd->addr = (uint64_t)buf;
  cmd->data_len = buf_size;
  cmd->nsid = dev->nsid;

  return sqe;
}

// For now the first argument will be a pointer to the fdp_dev_t but since I want 
// POSIX-like api it would rather be a file descriptor and I take care of looking
// up the corresponding fdp_dev_t struct.
ssize_t fdp_nvme_write(fdp_dev_t *dev, void *buf, size_t count, off_t offset, uint16_t plid) {
  int rc;
  struct io_uring_sqe *sqe;
  struct io_uring_cqe cqe;
  struct io_uring_cqe *cqes = &cqe;
  // transoform the posix like arhument into correponding 
  // argument to the device and perform some sanity checks

  sqe = prep_passthrough_cmd(dev, buf, count, offset, 1, plid);
  
  assert(sqe != NULL);
  rc = io_uring_submit(&dev->ring);
  assert(rc == 1);
  
  rc = io_uring_wait_cqe(&dev->ring, &cqes);
  assert(rc == 0);

  // check the cqe returned

  return rc;
}

