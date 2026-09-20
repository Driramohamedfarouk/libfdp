#include "fdp.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unordered_map>

#include "fdp_internal.h"
#include "nvme_types.h"
#include "nvme_util.h"
#include "util.h"

/** map from the file descriptor of the block device to the struct
struct fdp_dev, this is analog to struct file[] inside struct task_struct in
linux.*/
// std::mutex m; put it here to remember to make the library MT-safe.
static std::unordered_map<int, fdp_dev_t *> open_fdp_devices;

fdp_dev_t *get_fdp_dev(int fd) {
	assert(open_fdp_devices.count(fd) == 1);
	return open_fdp_devices[fd];
}

// Main function to read NVMe info
static int read_nvme_info(fdp_dev_t *dev) {
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
	int bdev_fd = -1, g_fd = -1;
	fdp_dev_t *dev = NULL;
	struct nvme_fdp_ruh_status *fdp_status = NULL;
	int rc;
	int saved_errno = 0;

	if (!is_valid_nvme_device(bdev_name)) {
		XLOGF("ERR", "Invalid NVMe device name: %s", bdev_name);
		errno = EINVAL;
		return -1;
	}

	fprintf(stderr, "%s\n", bdev_name);

	// NVMe passthrough ioctls need CAP_SYS_ADMIN, which in practice means root.
	if (geteuid() != 0) {
		XLOGF("ERR", "fdp_open requires root privileges to issue NVMe "
					 "passthrough commands on %s",
			  bdev_name);
		errno = EACCES;
		return -1;
	}

	// open the block device, discard mode it is irrelevant here.
	bdev_fd = open(bdev_name, flags);
	if (bdev_fd < 0) {
		XLOGF("ERR", "Cannot open block device %s: %s", bdev_name,
			  strerror(errno));
		return -1;
	}

	dev = (fdp_dev_t *)malloc(sizeof(fdp_dev_t));
	if (dev == NULL) {
		XLOGF("ERR", "malloc failed");
		saved_errno = ENOMEM;
		goto err_close_bdev;
	}
	memset(dev, 0, sizeof(*dev));

	if (strlen(bdev_name) >= sizeof(dev->name)) {
		XLOGF("ERR", "device name %s is too long (max %zu characters)",
			  bdev_name, sizeof(dev->name) - 1);
		saved_errno = ENAMETOOLONG;
		goto err_free_dev;
	}
	strcpy(dev->name, bdev_name);

	// get the char dev name
	if (get_nvme_char_device(bdev_name, dev->g_name, sizeof(dev->g_name)) !=
		0) {
		XLOGF("ERR", "Failed to get NVMe char device name for %s", bdev_name);
		saved_errno = EINVAL;
		goto err_free_dev;
	}

	/** Check that the given bdev is open in the calling application.
	The information is in /proc/ME/fd I think. */

	g_fd = open(dev->g_name, O_RDONLY);
	if (g_fd < 0) {
		saved_errno = errno;
		XLOGF("ERR", "open() failed for %s: %s", dev->g_name,
			  strerror(saved_errno));
		goto err_free_dev;
	}

	dev->bdev_fd = bdev_fd;
	dev->g_fd = g_fd;
	if (!isdigit((unsigned char)dev->g_name[9])) {
		XLOGF("ERR", "unexpected NVMe char device name format: %s",
			  dev->g_name);
		saved_errno = EINVAL;
		goto err_close_g;
	}
	dev->nsid = (uint16_t)(dev->g_name[9] - '0');
	XLOGF("INFO", "Opening NVMe Char Dev file: %s on fd %d with nsid %d",
		  dev->g_name, dev->g_fd, dev->nsid);

	// XXX(mfd) : assume the list of plid is ordinals to the NRUH.
	if (read_nvme_info(dev) != 0) {
		XLOGF("ERR", "failed to read NVMe device info for %s", bdev_name);
		saved_errno = EIO;
		goto err_close_g;
	}

	// TODO(mfd) : Determine device costant configuratuion, like the size of the
	// RU,
	//  the number of reclaim groups... Determine also the features supported by
	//  the
	//   device in term of statistics reporting... etc

	// Validate that the device/namespace actually supports FDP: the RUH
	// status log can fail to read (unsupported log page, insufficient
	// privileges, ...) or come back with zero reclaim unit handles, neither
	// of which leaves anything usable.
	// TODO(mfd) : this relies on errno surviving the XLOGF call inside
	// nvme_fdp_status() unclobbered; make nvme_fdp_status() report its
	// failure reason directly instead of relying on that.
	fdp_status = nvme_fdp_status(dev);
	if (fdp_status == NULL) {
		saved_errno = (errno == EACCES || errno == EPERM) ? errno : ENOTSUP;
		XLOGF("ERR",
			  "%s does not appear to support FDP (failed to read RUH "
			  "status log)%s",
			  bdev_name, (saved_errno == EACCES || saved_errno == EPERM)
							 ? " -- check for sufficient privileges"
							 : "");
		goto err_close_g;
	}
	if (fdp_status->nruhsd == 0) {
		XLOGF("ERR",
			  "%s reports zero reclaim unit handles; FDP is not usable "
			  "on this namespace",
			  bdev_name);
		free(fdp_status);
		saved_errno = ENOTSUP;
		goto err_close_g;
	}
	dev->nruh = fdp_status->nruhsd;
	XLOGF("INFO", "The number of RUHs in %s is %u", dev->name, dev->nruh);
	free(fdp_status);
	fdp_status = NULL;

	// Initialize the io_uring instance used by this device.
	// TODO(mfd) : read the queue length from the device and place it here,
	// there is the io depth will be bounded by the shortest queue on the path.
	rc = io_uring_queue_init(128, &dev->ring,
							 IORING_SETUP_SQE128 | IORING_SETUP_CQE32);
	if (rc != 0) {
		XLOGF("ERR", "failed to initialize io_uring for %s: %s", bdev_name,
			  strerror(-rc));
		saved_errno = -rc;
		goto err_close_g;
	}

	dev->gc_callback = NULL;

	assert(open_fdp_devices.count(bdev_fd) == 0);
	open_fdp_devices[bdev_fd] = dev;

	return bdev_fd;

err_close_g:
	close(g_fd);
err_free_dev:
	free(dev);
err_close_bdev:
	close(bdev_fd);
	errno = saved_errno;
	return -1;
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
