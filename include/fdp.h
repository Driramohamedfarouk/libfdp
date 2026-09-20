#pragma once
// This is the public interface of libfdp. NVMe/libnvme-derived protocol
// types used internally (nvme_passthru_cmd, the FDP RUH status structs,
// etc.) are not part of this interface and live in src/nvme_types.h instead.
#include <liburing.h>

typedef uint16_t plid_t;

// Opaque handle: the definition of fdp_dev is private to the implementation
// (src/nvme_types.h). Callers only ever interact with a device through the
// fd returned by fdp_open().
typedef struct fdp_dev fdp_dev_t;

// This is the interface of the library

/** Takes a block device as input will do the same semantic of the open()
syscall and also will open the generic device. On success it will return
the file descriptor of the block device and on failure it WILL returns
the corresponding error code. All interaction with the library will
happen through this handle.  */
int fdp_open(const char *bdev_name, int flags, ... /* mode_t mode */);

// synchronous pwrite with plid (placement id)
ssize_t fdp_pwrite(int fd, void *buf, size_t count, off_t offset,
				   uint16_t plid);

// Sets the placement id of an already filled sqe
void fdp_sqe_set_plid(struct io_uring_sqe *sqe, uint16_t plid);

void fdp_close(int fd);

/** On success it return the number of remaining media writes in the open
Reclaim Unit of the Reclaim Unit Handle corresponding to the plid provided.
On failure it returns a failure code.
TODO(mfd4) : define later error code */
ssize_t fdp_get_remaining_bytes_in_ru(int fd, plid_t plid);

// temporarly expose this as an interface for debugging.
int fdp_get_events(int fd, __u8 *log, __u32 log_size);

// This is part of the interface, the callback now does not take any parameter
// mainly because no driver has any meaningful results for the media relocation
// event. We may change this later.
// void fdp_register_gc_callback(int fd, void (*gc_callback)(void));

int open_ru_timer(void *arg);

void fdp_io_uring_prep_write(struct io_uring_sqe *sqe, int fd, const void *buf,
							 unsigned count, __u64 offset, uint16_t plid);

/** Given an placement id, will reset the write pointer of the corresponding RUH
on a new free Reclaim Unit */
void fdp_reset_free_ru(int fd, plid_t plid);
