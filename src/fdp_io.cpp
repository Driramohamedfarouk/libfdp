// The io_uring-based write path: building an FDP-directed NVMe uring-cmd
// SQE and issuing it, synchronously (fdp_pwrite) or into a caller-owned
// ring (fdp_io_uring_prep_write).
#include "fdp.h"

#include <assert.h>
#include <liburing.h>
#include <linux/nvme_ioctl.h>
#include <string.h>

#include "fdp_internal.h"
#include "nvme_types.h"

void fdp_sqe_set_plid(struct io_uring_sqe *sqe, uint16_t plid) {
	struct nvme_uring_cmd *cmd = (struct nvme_uring_cmd *)&sqe->cmd;
	assert(sqe->opcode == IORING_OP_URING_CMD);
	cmd->cdw13 = (cmd->cdw13 & 0xFFFF) | ((uint32_t)plid << 16);
}

// first argument is the file descriptor of the generic nvme device
static struct io_uring_sqe *prep_passthrough_cmd(fdp_dev_t *dev,
												 const void *buf,
												 size_t buf_size, size_t offset,
												 int is_write, uint16_t dspec,
												 struct io_uring_sqe *isqe) {
	struct io_uring_sqe *sqe;
	struct nvme_uring_cmd *cmd;
	lba_t slba;
	size_t nlba;

	const uint8_t dtype = 0x02; // 2 specifies FDP directive

	if (isqe == NULL) {
		sqe = io_uring_get_sqe(&dev->ring);
		if (!sqe)
			return sqe;
	} else {
		// TODO(mfd) : Is there a way to do a sanity check that isqe is from
		// a ring with BIG_SQE ?
		sqe = isqe;
	}

	sqe->fd = dev->g_fd;
	sqe->opcode = IORING_OP_URING_CMD;
	sqe->cmd_op = NVME_URING_CMD_IO;
	cmd = (struct nvme_uring_cmd *)&sqe->cmd;
	assert(cmd != NULL);

	// XXX(mfd) I think this step is unnecessary
	memset(cmd, 0, sizeof(struct nvme_uring_cmd));

	cmd->opcode = is_write ? nvme_cmd_write : nvme_cmd_read;

	assert(offset % dev->lba_size == 0);
	slba = offset / dev->lba_size;
	assert(buf_size % dev->lba_size == 0);
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

/** FIXME(mfd) Currently this is does not offer the exact pwrite
sematics interface. Since this will issue a single command
the number of bytes to write is bounded by how big the
the nvme device can support writes in a single NVMe command.
What we should do here is decompose the write into multiple commands
each of write size does not exceed max_transfer_size. Another
thing is unaligned writes. I guess this is fixed by performing
a read followed by write (Steal code from the kernel to handle this).
I don't need any of this for the moment so I'll just assert. */
ssize_t fdp_pwrite(int fd, void *buf, size_t count, off_t offset,
				   uint16_t plid) {
	int rc;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe_ptr = nullptr;
	// transoform the posix like arhument into correponding
	// argument to the device and perform some sanity checks
	fdp_dev_t *dev = get_fdp_dev(fd);

	assert(count <= dev->max_transfer_size);
	assert((__u64)buf % dev->lba_size == 0);
	assert((__u64)offset % dev->lba_size == 0);

	assert(plid < dev->nruh);
	sqe = prep_passthrough_cmd(dev, buf, count, offset, 1, plid, NULL);

	assert(sqe != NULL);
	rc = io_uring_submit(&dev->ring);
	assert(rc == 1);

	rc = io_uring_wait_cqe(&dev->ring, &cqe_ptr);
	assert(rc == 0);

	io_uring_cqe_seen(&dev->ring, cqe_ptr);

	return cqe_ptr->res == 0 ? count : cqe_ptr->res;
}

void fdp_io_uring_prep_write(struct io_uring_sqe *sqe, int fd, const void *buf,
							 unsigned count, __u64 offset, uint16_t plid) {
	fdp_dev_t *dev = get_fdp_dev(fd);
	assert(plid < dev->nruh);
	sqe = prep_passthrough_cmd(dev, buf, count, offset, 1, plid, sqe);
	assert(sqe != NULL);

	return;
}
