// FDP reclaim-unit-handle business logic: querying remaining write
// capacity and resetting a handle onto a fresh reclaim unit. Built on top
// of the raw command builders in nvme_ioctl.cpp.
#include "fdp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fdp_internal.h"
#include "nvme_types.h"
#include "util.h"

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
		free(status);
		return -1;
	}

	// TODO(mfd) : check for errors like invalid plid, uninitialized device
	// and return error codes you define in the interface of this libray
	desc = &status->ruhss[plid];
	// For now just assert valid input, I better not misuse a library I am
	// writing
	assert(desc->pid == plid);
	assert(desc->ruhid == plid);
	ssize_t ruamw = desc->ruamw;
	free(status);
	return ruamw;
}

void fdp_reset_free_ru(int fd, plid_t plid) {

	fdp_dev_t *dev = get_fdp_dev(fd);
	assert(plid < dev->nruh);

	__u16 *plidp = &plid;
	int rc =
		nvme_fdp_reclaim_unit_handle_update(dev->g_fd, dev->nsid, 1, plidp);

	assert(rc == 0);

	// TODO(mfd) : Refactor change this to use internal API
	// TODO(mfd) : 3193344 is a magic number for this specific device's RU
	// size; derive the "fresh RU" ruamw value from device config instead so
	// this works on other drives.
	while (fdp_get_remaining_bytes_in_ru(fd, plid) != 3193344) {
		usleep(1000); // 1ms
	}
}
