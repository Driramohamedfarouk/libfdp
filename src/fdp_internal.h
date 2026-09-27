#pragma once
// Declarations shared across libfdp's own translation units (src/*.cpp).
// None of this is part of the public interface; see include/fdp.h for that.
#include "fdp.h"         // for the fdp_dev_t typedef
#include "nvme_types.h"  // for the full struct fdp_dev definition and friends

// Look up the internal device state for an already-open fd. Defined in
// fdp_device.cpp, which owns the fd -> fdp_dev_t* registry.
fdp_dev_t* get_fdp_dev(int fd);

// Look up fd's device state into `dev`, or set errno = EBADF and return
// -1 from the calling function if fd is not a currently-open fdp device.
// Every public function taking a bare `int fd` uses this, so an invalid
// fd is reported the same way pwrite(2)/close(2) report one instead of
// hitting undefined behavior.
#define FDP_GET_DEV_OR_RETURN(dev, fd) \
    fdp_dev_t* dev = get_fdp_dev(fd);  \
    do {                               \
        if (dev == NULL) {             \
            errno = EBADF;             \
            return -1;                 \
        }                              \
    } while (0)

#define CHECK_VALID_PLID_OR_RETURN(dev, plid) \
    do {                                      \
        if (plid >= dev->nruh) {              \
            errno = EINVAL;                   \
            return -1;                        \
        }                                     \
    } while (0)

#define CHECK_DIE(cond)                                                               \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "  %s:%d CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            fprintf(stderr,                                                           \
                    "  You have likely hit a BUG in libfdp, please "                  \
                    "report to authors\n");                                           \
        }                                                                             \
    } while (0)

// Raw NVMe passthrough command builders, defined in nvme_ioctl.cpp.
int nvme_io_mgmt_recv(fdp_dev_t* dev, void* data, uint32_t data_len, uint8_t op, uint16_t op_specific);
int nvme_get_log(struct nvme_get_log_args* args);
int nvme_fdp_reclaim_unit_handle_update(int fd, __u32 nsid, unsigned int npids, __u16* pids);
struct nvme_fdp_ruh_status* nvme_fdp_status(fdp_dev_t* dev);
struct nvme_fdp_config_log* nvme_fdp_config(fdp_dev_t* dev);
