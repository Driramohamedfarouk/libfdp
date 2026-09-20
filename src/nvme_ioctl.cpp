// Raw NVMe passthrough command builders. These issue ioctls and hand back
// device-reported structures as-is; none of them interpret the result for
// placement/reclaim-unit decisions (that lives in fdp_ruh.cpp).
#include <linux/nvme_ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "fdp_internal.h"
#include "nvme_types.h"
#include "util.h"

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

/**
Reset a number of plids given by the array pids to write on a free RU.
*/
int nvme_fdp_reclaim_unit_handle_update(int fd, __u32 nsid,
										unsigned int npids, __u16 *pids) {
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
