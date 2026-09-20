#pragma once
// Declarations shared across libfdp's own translation units (src/*.cpp).
// None of this is part of the public interface; see include/fdp.h for that.
#include "fdp.h" // for the fdp_dev_t typedef
#include "nvme_types.h" // for the full struct fdp_dev definition and friends

// Look up the internal device state for an already-open fd. Defined in
// fdp_device.cpp, which owns the fd -> fdp_dev_t* registry.
fdp_dev_t *get_fdp_dev(int fd);

// Raw NVMe passthrough command builders, defined in nvme_ioctl.cpp.
int nvme_io_mgmt_recv(fdp_dev_t *dev, void *data, uint32_t data_len, uint8_t op,
					  uint16_t op_specific);
int nvme_get_log(struct nvme_get_log_args *args);
int nvme_fdp_reclaim_unit_handle_update(int fd, __u32 nsid, unsigned int npids,
										__u16 *pids);
struct nvme_fdp_ruh_status *nvme_fdp_status(fdp_dev_t *dev);
