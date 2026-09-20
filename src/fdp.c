#include "fdp.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <liburing.h>
#include <linux/nvme_ioctl.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <unordered_map>

#include "nvme_types.h"
#include "nvme_util.h"
#include "util.h"

/** map from the file descriptor of the block device to the struct
struct fdp_dev, this is analog to struct file[] inside struct task_struct in
linux.*/
// std::mutex m; put it here to remember to make the library MT-safe.
std::unordered_map<int, fdp_dev_t *> open_fdp_devices;

static fdp_dev_t *get_fdp_dev(int fd) {
	assert(open_fdp_devices.count(fd) == 1);
	return open_fdp_devices[fd];
}

/** TODO(mfd) : change this to issue raw command asyncronously
using the passthrough interface instead of the synchronous ioctl sytem call. */
int nvme_io_mgmt_recv(fdp_dev_t *dev, void *data, uint32_t data_len, uint8_t op,
					  uint16_t op_specific) {
	// Build the I/O management receive command
	// For further details on the CDB format, consult the specification
	// available as "TP4146 Flexible Data Placement 2022.11.30 Ratified"
	// in the following link:
	// https://nvmexpress.org/wp-content/uploads/NVM-Express-2.0-Ratified-TPs_20230111.zip
	uint32_t cdw10 = (op & 0xf) | (op_specific & 0xff << 16);
	uint32_t cdw11 = (data_len >> 2) - 1; // cdw11 is 0 based

	struct nvme_passthru_cmd cmd = {};
	cmd.opcode = nvme_cmd_io_mgmt_recv;
	cmd.nsid = dev->nsid;
	cmd.addr = (uint64_t)data;
	cmd.data_len = data_len;
	cmd.cdw10 = cdw10;
	cmd.cdw11 = cdw11;
	cmd.timeout_ms = NVME_DEFAULT_IOCTL_TIMEOUT;

	return ioctl(dev->g_fd, NVME_IOCTL_IO_CMD, &cmd);
}

// taken as is from libnvme ioctl.c
int nvme_get_log(struct nvme_get_log_args *args) {

	__u32 numd = (args->len >> 2) - 1;
	__u16 numdu = numd >> 16, numdl = numd & 0xffff;

	__u32 cdw10 = NVME_SET(args->lid, LOG_CDW10_LID) |
				  NVME_SET(args->lsp, LOG_CDW10_LSP) |
				  NVME_SET(!!args->rae, LOG_CDW10_RAE) |
				  NVME_SET(numdl, LOG_CDW10_NUMDL);
	__u32 cdw11 =
		NVME_SET(numdu, LOG_CDW11_NUMDU) | NVME_SET(args->lsi, LOG_CDW11_LSI);
	__u32 cdw12 = args->lpo & 0xffffffff;
	__u32 cdw13 = args->lpo >> 32;
	__u32 cdw14 = NVME_SET(args->uuidx, LOG_CDW14_UUID) |
				  NVME_SET(!!args->ot, LOG_CDW14_OT) |
				  NVME_SET(args->csi, LOG_CDW14_CSI);

	struct nvme_passthru_cmd cmd = {
		.opcode = nvme_admin_get_log_page,
		.nsid = args->nsid,
		.addr = (__u64)(uintptr_t)args->log,
		.data_len = args->len,
		.cdw10 = cdw10,
		.cdw11 = cdw11,
		.cdw12 = cdw12,
		.cdw13 = cdw13,
		.cdw14 = cdw14,
		.timeout_ms = args->timeout,
	};

	return ioctl(args->fd, NVME_IOCTL_ADMIN_CMD, &cmd);
}

int fdp_get_events(int fd, __u8 *log, __u32 log_size) {
	fdp_dev_t *dev = get_fdp_dev(fd);
	// construct the corresponding arg type
	struct nvme_get_log_args args = {
		// .lpo = offset,
		.lpo = 0, // use 0 as offset for the moment I don't get much the use of
		// nonzero offset
		.result = NULL,
		.log = log,
		.args_size = sizeof(args),
		.fd = dev->g_fd,
		.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
		.lid = NVME_LOG_LID_FDP_EVENTS,
		.len = log_size,
		.nsid = 0,		 // nsid is not used for this command
		.csi = 0,		 // NVM Command Set Indicator
		.lsi = 1,		 // endurance group id should be 1
		.lsp = (__u8)0U, // controller events by default
		.uuidx = 0U,
	};
	return nvme_get_log(&args);
}

// currently assumes a single RUH, easy to generalize later.
// Temporarly use this an interface for testing purpouses.
int open_ru_timer(void *arg) {
	fdp_dev_t *dev = (fdp_dev_t *)arg;
	__u64 last_seen_ruamw = 0;
	__u32 open_ru = -1; // move this to be inside the dev struct
	while (1) {
		// nanosleep(); // 1 ms
		// Temporary use of api here
		ssize_t ruamw = fdp_get_remaining_bytes_in_ru(dev->bdev_fd, 0);
		if (ruamw < 0) {
			XLOGF("ERR", "fdp_get_remaining_bytes returned error");
			break;
		}
		// XXX(mfd) : This is buggy because the results of remaining media
		// writes returned by the device fluctuate
		if (ruamw > last_seen_ruamw) {
			open_ru++;
			XLOGF("INFO", "RUAMW = %ld", ruamw);
			XLOGF("INFO", "Open the new RU #%u", open_ru);
		}
		last_seen_ruamw = ruamw;
		// sleep for 50ms, an optimal strategy is to sleep depending on how many
		// raumw adaptevly reducing sleep until just busy looping without
		// spinning to obtain very exact results. Calculate error as the
		// difference between the RU size and the ruamw when I discovered the
		// new Recalaim unit. usleep(50 * 1000);
		sleep(1); // 1s, high error rate but less fluctuations.
	}
	return 0;
}

// This is the function that will be run by the deamon
/** TODO(mfd) : Usually GC is not triggered until late, adapt sleeping time
so that we don't have unecessary wakeups. */
static int gc_event_listener(void *arg) {
	int err;
	void *log;
	fdp_dev_t *dev = (fdp_dev_t *)arg;
	printf("This is the thread listener for device %s\n", dev->name);
	// posix memalign the log buffer
	err = posix_memalign(&log, 4096, 4096);
	// last seen GC timestamp
	__u64 last_seen_timestamp = 0;
	while (true) {
		// temporary use of external interface.
		err = fdp_get_events(dev->bdev_fd, (__u8 *)log, 4096);
		// Check for new event
		// parse the log and start from the end looking whether there is a new
		// event
		uint32_t nb_gc_events = *(uint32_t *)log;
		// Assume that at most one new GC event will appear here.
		// TODO(mfd) : Assert this assumption later
		// TODO(mfd) : Define the GC event struct and do appropriate casting
		assert(64 * nb_gc_events < 4096);
		__u8 *last_event_struct = ((__u8 *)log) + 64 * nb_gc_events;
		__u8 type = *last_event_struct;
		__u64 timestamp = *(__u64 *)(last_event_struct + 4);
		if (timestamp != last_seen_timestamp) {
			assert(type == 0x80);
			assert(timestamp > last_seen_timestamp);
			last_seen_timestamp = timestamp;
			dev->gc_callback();
			XLOGF("INFO", "SSD GC is triggered!!!");
		}
		err = sleep(1);
		assert(err == 0);
	}
	return 0;
}

/*
void fdp_register_gc_callback(int fd, void (*gc_callback)(void)) {
	// if this is the first registered callback fire up the deamon thread
	// use a mutex here when making the library MT-Safe.
	int err;
	fdp_dev_t *dev = get_fdp_dev(fd);
	if (dev->gc_callback == NULL) {
		dev->gc_callback = gc_callback;
		// create the event listener thread.
		err = thrd_create(&dev->gc_listener_thread, gc_event_listener,
						  (void *)dev);
		assert(err == thrd_success);
		err = thrd_detach(dev->gc_listener_thread);
		assert(err == thrd_success);
		XLOGF("INFO", "Succesfully registered a GC callback");
	}
	XLOGF("WARNING", "There is already a registered callback, for now we "
					 "support only one callback");
}
*/

/**
Reset a number of plids given by the array pids to write on a free RU.
*/
static int nvme_fdp_reclaim_unit_handle_update(int fd, __u32 nsid,
											   unsigned int npids,
											   __u16 *pids) {
	__u32 cdw10 = 0x01 | ((npids - 1) << 16);

	struct nvme_passthru_cmd cmd = {
		.opcode = nvme_cmd_io_mgmt_send,
		.nsid = nsid,
		.addr = (__u64)(uintptr_t)pids,
		.data_len = (__u32)(npids * sizeof(__u16)),
		.cdw10 = cdw10,
	};

	int err = ioctl(fd, NVME_IOCTL_IO_CMD, &cmd);
	if (err >= 0)
		return cmd.result;
	return err;
}

struct nvme_fdp_ruh_status *nvme_fdp_status(fdp_dev_t *dev) {
	struct nvme_fdp_ruh_status hdr;
	struct nvme_fdp_ruh_status *status;
	int err;

	// Read FDP ruh status header to get Num_RUH Status Descriptors
	err = nvme_io_mgmt_recv(dev, &hdr, sizeof(hdr),
							NVME_IO_MGMT_RECV_RUH_STATUS, 0);
	if (err) {
		XLOGF("ERR", "failed to get ruh status header, fd: %d", dev->g_fd);
		return NULL;
	}

	size_t size = sizeof(struct nvme_fdp_ruh_status) +
				  (hdr.nruhsd * sizeof(struct nvme_fdp_ruh_status_desc));

	status = (struct nvme_fdp_ruh_status *)malloc(size);

	// Read FDP RUH Status
	err = nvme_io_mgmt_recv(dev, (char *)status, size,
							NVME_IO_MGMT_RECV_RUH_STATUS, 0);
	if (err) {
		XLOGF("ERR", "failed to get ruh status header, fd: %d", dev->g_fd);
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
	return 0; // success
}

// This is part of the library interface
int fdp_open(const char *bdev_name, int flags, ... /* mode_t mode */) {
	int bdev_fd, g_fd;
	fdp_dev_t *dev;

	if (!is_valid_nvme_device(bdev_name)) {
		fprintf(stderr, "Invalid NVMe device name: %s\n", bdev_name);
		return -1;
	}

	fprintf(stderr, "%s\n", bdev_name);

	// open the block device, discard mode it is irrelevant here.
	bdev_fd = open(bdev_name, flags);
	if (bdev_fd < 0) {
		XLOGF("ERR", "Cannot open block device");
		return -1;
	}

	dev = (fdp_dev_t *)malloc(sizeof(fdp_dev_t));
	if (dev == NULL) {
		XLOGF("ERR", "malloc failed");
		return -1;
	}

	strcpy(dev->name, bdev_name);

	// get the char dev name
	if (get_nvme_char_device(bdev_name, dev->g_name, sizeof(dev->g_name)) !=
		0) {
		fprintf(stderr, "Failed to get NVMe char device name for %s\n",
				bdev_name);
		return -1;
	}

	/** Check that the given bdev is open in the calling application.
	The information is in /proc/ME/fd I think. */

	g_fd = open(dev->g_name, O_RDONLY);
	if (g_fd < 0) {
		XLOGF("ERR", "open() failed for %s: %s", dev->g_name, strerror(errno));
		return -1;
	}

	dev->bdev_fd = bdev_fd;
	dev->g_fd = g_fd;
	assert(isdigit(dev->g_name[9]));
	dev->nsid = (uint16_t)(dev->g_name[9] - '0');
	XLOGF("INFO", "Opening NVMe Char Dev file: %s on fd %d with nsid %d",
		  dev->g_name, dev->g_fd, dev->nsid);

	/* XXX(mfd) : assume the list of plid is ordinals to the NRUH,
	validate that the device is fdp capable. */

	// read nvme attributes
	read_nvme_info(dev);

	// TODO(mfd) : Determine device costant configuratuion, like the size of the
	// RU,
	//  the number of reclaim groups... Determine also the features supported by
	//  the
	//   device in term of statistics reporting... etc

	// read fdp attributes
	struct nvme_fdp_ruh_status *fdp_status;
	fdp_status = nvme_fdp_status(dev);
	if (fdp_status == NULL) {
		XLOGF("ERR", "failed to obtain nvme fdp status information");
	}
	dev->nruh = fdp_status->nruhsd;
	// print to stdout the number of RUHS
	XLOGF("INFO", "The number of RUHs in %s is %u", dev->name,
		  fdp_status->nruhsd);

	// Initialize the io_uring instance used by this device.
	// TODO(mfd) : read the queue length from the device and place it here,
	// there is the io depth will be bounded by the shortest queue on the path.
	int rc = io_uring_queue_init(128, &dev->ring,
								 IORING_SETUP_SQE128 | IORING_SETUP_CQE32);
	assert(rc == 0);

	dev->gc_callback = NULL;

	assert(open_fdp_devices.count(bdev_fd) == 0);
	open_fdp_devices[bdev_fd] = dev;

	return bdev_fd;
}

void fdp_sqe_set_plid(struct io_uring_sqe *sqe, uint16_t plid) {
	struct nvme_uring_cmd *cmd = (struct nvme_uring_cmd *)&sqe->cmd;
	assert(sqe->opcode == IORING_OP_URING_CMD);
	cmd->cdw13 = (cmd->cdw13 & 0xFFFF) | ((uint32_t)plid << 16);
}

void fdp_close(int fd) {
	fdp_dev_t *dev;
	dev = get_fdp_dev(fd);
	// if the device has any registered device callbacks
	// remove them.
	close(dev->bdev_fd);
	close(dev->g_fd);
	io_uring_queue_exit(&dev->ring);
	// TODO(mfd) : use atomic flags to signal termination of detached thread
	free(dev);
	return;
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

ssize_t fdp_get_remaining_bytes_in_ru(int fd, plid_t plid) {
	/** XXX(mfd4) : For now we assume plid is the same as ruhid.
	that is the user configured the device to have plid 0,1,...,nruh-1.
	Later we need to maintain a mapping. Also for now we assume there is
	a single application that is using fdp devuce but ruhs are a resource
	that should be managed by the OS.*/
	int err;
	struct nvme_fdp_ruh_status *status;
	struct nvme_fdp_ruh_status_desc *desc;

	fdp_dev_t *dev = get_fdp_dev(fd);

	// assert the fdp_device is initialized
	assert(dev->nruh != 0);
	// assert that plid has a valid value
	assert(plid < dev->nruh);

	size_t size = sizeof(struct nvme_fdp_ruh_status) +
				  (dev->nruh * sizeof(struct nvme_fdp_ruh_status_desc));

	status = (struct nvme_fdp_ruh_status *)malloc(size);

	err = nvme_io_mgmt_recv(dev, (char *)status, size,
							NVME_IO_MGMT_RECV_RUH_STATUS, 0);
	if (err) {
		XLOGF("ERR", "failed to get ruh status header, fd: %d", dev->g_fd);
		return -1;
	}

	// TODO(mfd) : check for errors like invalid plid, uninitialized device
	// and return error codes you define in the interface of this libray
	desc = &status->ruhss[plid];
	// For now just assert valid input, I better not misuse a library I am
	// writing
	assert(desc->pid == plid);
	assert(desc->ruhid == plid);
	return desc->ruamw;
}

void fdp_io_uring_prep_write(struct io_uring_sqe *sqe, int fd, const void *buf,
							 unsigned count, __u64 offset, uint16_t plid) {
	fdp_dev_t *dev = get_fdp_dev(fd);
	assert(plid < dev->nruh);
	sqe = prep_passthrough_cmd(dev, buf, count, offset, 1, plid, sqe);
	assert(sqe != NULL);

	return;
}

void fdp_reset_free_ru(int fd, plid_t plid) {

	fdp_dev_t *dev = get_fdp_dev(fd);
	assert(plid < dev->nruh);

	__u16 *plidp = &plid;
	int rc =
		nvme_fdp_reclaim_unit_handle_update(dev->g_fd, dev->nsid, 1, plidp);

	assert(rc == 0);

	// TODO(mfd) : Refactor change this to use internal API
	while (fdp_get_remaining_bytes_in_ru(fd, plid) != 3193344) {
		usleep(1000); // 1ms
	}
}
