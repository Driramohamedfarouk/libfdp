// The write path: encoding an FDP-directed NVMe write and issuing it,
// synchronously through the passthrough ioctl (fdp_pwrite) or into a
// caller-owned io_uring SQE (fdp_io_uring_prep_write).
#include "fdp.h"

#include <assert.h>
#include <errno.h>
#include <liburing.h>
#include <linux/nvme_ioctl.h>
#include <string.h>
#include <sys/ioctl.h>

#include "fdp_internal.h"
#include "nvme_types.h"

void fdp_sqe_set_plid(struct io_uring_sqe* sqe, uint16_t plid)
{
    struct nvme_uring_cmd* cmd = (struct nvme_uring_cmd*)&sqe->cmd;
    assert(sqe->opcode == IORING_OP_URING_CMD);
    cmd->cdw13 = (cmd->cdw13 & 0xFFFF) | ((uint32_t)plid << 16);
}

// Encode an FDP-directed read/write into `cmd`. Works for both
// struct nvme_passthru_cmd (ioctl) and struct nvme_uring_cmd (io_uring),
// which share the same field names for everything set here.
template <typename Cmd>
static void fill_rw_cmd(Cmd* cmd, fdp_dev_t* dev, const void* buf, size_t buf_size, size_t offset, int is_write, uint16_t dspec)
{
    const uint8_t dtype = 0x02;  // 2 specifies FDP directive

    assert(offset % dev->lba_size == 0);
    assert(buf_size % dev->lba_size == 0);
    lba_t slba = offset / dev->lba_size;
    size_t nlba = buf_size / dev->lba_size - 1;

    memset(cmd, 0, sizeof(*cmd));
    cmd->opcode = is_write ? nvme_cmd_write : nvme_cmd_read;
    cmd->nsid = dev->nsid;
    cmd->addr = (uint64_t)buf;
    cmd->data_len = buf_size;
    /* cdw10 and cdw11 represent starting lba */
    cmd->cdw10 = slba & 0xffffffff;
    cmd->cdw11 = slba >> 32;
    /* cdw12 represent number of lba's for read/write */
    cmd->cdw12 = (dtype & 0xFF) << 20 | nlba;
    cmd->cdw13 = (dspec << 16);
}

static void prep_passthrough_cmd(fdp_dev_t* dev, const void* buf, size_t buf_size, size_t offset, int is_write, uint16_t dspec, struct io_uring_sqe* sqe)
{
    // TODO(mfd) : Is there a way to do a sanity check that sqe is from
    // a ring with BIG_SQE ?
    sqe->fd = dev->g_fd;
    sqe->opcode = IORING_OP_URING_CMD;
    sqe->cmd_op = NVME_URING_CMD_IO;
    fill_rw_cmd((struct nvme_uring_cmd*)&sqe->cmd, dev, buf, buf_size, offset, is_write, dspec);
}

/** Issued as a single synchronous NVMe passthrough ioctl. The command lives
on the caller's stack and the kernel (blk-mq) handles concurrent submitters,
so concurrent fdp_pwrite() calls share no mutable library state here.

FIXME(mfd) Currently this is does not offer the exact pwrite
sematics interface. Since this will issue a single command
the number of bytes to write is bounded by how big the
the nvme device can support writes in a single NVMe command.
What we should do here is decompose the write into multiple commands
each of write size does not exceed max_transfer_size. Another
thing is unaligned writes. I guess this is fixed by performing
a read followed by write (Steal code from the kernel to handle this).
I don't need any of this for the moment so I'll just assert. */
ssize_t fdp_pwrite(int fd, void* buf, size_t count, off_t offset, uint16_t plid)
{
    struct nvme_passthru_cmd cmd;
    int rc;
    FDP_GET_DEV_OR_RETURN(dev, fd);
    CHECK_VALID_PLID_OR_RETURN(dev, plid);

    assert(count <= dev->max_transfer_size);
    assert((__u64)buf % dev->lba_size == 0);
    assert((__u64)offset % dev->lba_size == 0);

    fill_rw_cmd(&cmd, dev, buf, count, offset, 1, plid);

    // TODO(mfd) : evaluate the CPU utilisation of this approach against a
    // thread local io_uring, which can also batch large pwrites that don't
    // fit in a single NVMe command into one syscall.
    rc = ioctl(dev->g_fd, NVME_IOCTL_IO_CMD, &cmd);
    if (rc < 0)
        return -1;  // errno set by ioctl
    if (rc > 0) {
        // Positive values are the NVMe completion status.
        // TODO(mfd) : map NVMe status codes to a closer errno.
        errno = EIO;
        return -1;
    }
    return count;
}

// Mirrors the liburing io_uring_prep_*() convention:
// returns void, and never fails synchronously.
void fdp_io_uring_prep_write(struct io_uring_sqe* sqe, int fd, const void* buf, unsigned count, uint64_t offset, uint16_t plid)
{
    fdp_dev_t* dev = get_fdp_dev(fd);
    prep_passthrough_cmd(dev, buf, count, offset, 1, plid, sqe);

    return;
}
